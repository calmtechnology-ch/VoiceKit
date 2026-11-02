#pragma once
#include "ILiveTransport.h"

#if defined(ESP32) || defined(ARDUINO)
#include <Arduino.h>
#include <string>
#include <atomic>
#include <esp_websocket_client.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

// =============================================================================
// EspWebSocketTransport : Implémentation du transport WebSocket natif ESP32
// Utilise esp_websocket_client et supporte TLS, reconnexion et en-têtes HTTP personnalisés
//
// Correction B (v0.6.1) — Ordonnancement FreeRTOS pendant le handshake TLS :
// Dans ESP-IDF v4.4 (framework-arduinoespressif32), esp_websocket_client_config_t
// ne dispose pas de champ task_core (websocket_task est créée avec tskNO_AFFINITY).
// Avec task_prio = 5, websocket_task préemptait loopTask (Cœur 1, prio 1) pendant
// tout le handshake cryptographique mbedTLS (~1,0 à 1,55 s sans rendre la main).
// Solution :
// 1. Démarrer websocket_task à la priorité DEFAULT_HANDSHAKE_TASK_PRIO (1), égale
//    à loopTask (1) : l'ordonnanceur FreeRTOS exécute le handshake TLS sur le Cœur 0
//    dès qu'il est libre et partage le Cœur 1 en Round-Robin (1 ms) avec loopTask.
// 2. Une fois le handshake TLS achevé (WEBSOCKET_EVENT_CONNECTED), relever la
//    priorité à DEFAULT_STEADY_TASK_PRIO (2) pour garantir une réception réactive
//    des trames PCM 24 kHz (< AudioPlayerTask prio 4), et la redescendre à 1 lors
//    d'une déconnexion (WEBSOCKET_EVENT_DISCONNECTED) avant toute reconnexion TLS.
// =============================================================================
class EspWebSocketTransport : public ILiveTransport {
public:
    static constexpr uint8_t DEFAULT_HANDSHAKE_TASK_PRIO = 1;
    static constexpr uint8_t DEFAULT_STEADY_TASK_PRIO    = 2;

    EspWebSocketTransport() = default;
    ~EspWebSocketTransport() override {
        disconnect();
        if (_sendMutex) {
            vSemaphoreDelete(_sendMutex);
            _sendMutex = nullptr;
        }
    }

    void setTaskPriorities(uint8_t handshakePrio, uint8_t steadyPrio) {
        _handshakeTaskPrio = (handshakePrio > 0) ? handshakePrio : DEFAULT_HANDSHAKE_TASK_PRIO;
        _steadyTaskPrio = (steadyPrio > 0) ? steadyPrio : DEFAULT_STEADY_TASK_PRIO;
    }

    bool connect(const char* uri, const char* certPem = nullptr, const char* extraHeaders = nullptr) override {
        disconnect();

        if (!_sendMutex) {
            _sendMutex = xSemaphoreCreateMutex();
        }

        _wsTaskPriority.store(_handshakeTaskPrio, std::memory_order_relaxed);
        _freeInternalHeapAtConnect.store(
            (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            std::memory_order_relaxed);
        _minFreeInternalHeapAtConnect.store(
            (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            std::memory_order_relaxed);
        _largestInternalBlockAtConnect.store(
            (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            std::memory_order_relaxed);

        esp_websocket_client_config_t cfg = {};
        cfg.uri = uri;
        cfg.cert_pem = certPem;
        cfg.headers = extraHeaders;
        cfg.buffer_size = 8192;
        cfg.task_stack = 14336; // Indispensable pour le handshake TLS mbedTLS (14 Ko)
        cfg.task_prio = _handshakeTaskPrio; // Priorité 1 pendant le handshake TLS (évite de bloquer loopTask)
        cfg.pingpong_timeout_sec = 15;
        cfg.ping_interval_sec = 10;
        cfg.user_context = this;

        _client = esp_websocket_client_init(&cfg);
        if (!_client) {
            Serial.println("[WS] Echec esp_websocket_client_init");
            if (_onError) _onError("Echec init client WebSocket");
            return false;
        }

        esp_websocket_register_events(_client, WEBSOCKET_EVENT_ANY, &EspWebSocketTransport::eventHandler, this);
        esp_err_t err = esp_websocket_client_start(_client);
        if (err != ESP_OK) {
            Serial.printf("[WS] Echec esp_websocket_client_start: %d\n", err);
            if (_onError) _onError("Echec demarrage client WebSocket");
            disconnect();
            return false;
        }
        return true;
    }

    void disconnect() override {
        uint32_t t0Us = micros();
        if (_client) {
            uint8_t* raw = reinterpret_cast<uint8_t*>(_client);
            EventGroupHandle_t statusBits = *reinterpret_cast<EventGroupHandle_t*>(raw + 68);
            bool alreadyStopped = false;
            if (_disconnectPending && statusBits != nullptr) {
                EventBits_t bits = xEventGroupWaitBits(statusBits, 1, pdFALSE, pdTRUE, 0);
                if ((bits & 1) != 0) {
                    alreadyStopped = true;
                    *reinterpret_cast<int32_t*>(raw + 20) = 0; // WEBSOCKET_STATE_UNKNOW
                }
            }
            if (!alreadyStopped) {
                esp_websocket_client_stop(_client);
            }
            esp_websocket_client_destroy(_client);
            _client = nullptr;
        }
        _disconnectPending = false;
        _connected = false;
        _rxAccum.clear();
        recordDisconnectStepUs(micros() - t0Us);
    }

    // Déconnexion non bloquante v0.6.3.1 :
    // Dans ESP-IDF v4.4, esp_websocket_client_stop() met client->run = false (offset 64)
    // puis bloque sur xEventGroupWaitBits(client->status_bits [offset 68], STOPPED_BIT [1], portMAX_DELAY)
    // pendant que websocket_task est bloquée dans esp_transport_poll_read(1000 ms), ce qui gelait
    // loopTask jusqu'à ~582 ms lors de ENDING_SESSION.
    // requestDisconnect() abaisse client->run = false immédiatement (< 2 µs) et pollDisconnect()
    // vérifie STOPPED_BIT sans attente (0 tick) à chaque cycle update(), puis détruit le client
    // dès que websocket_task a quitté sa boucle (~40 µs), sans aucune nouvelle tâche FreeRTOS.
    void requestDisconnect() override {
        uint32_t t0Us = micros();
        _connected = false;
        _rxAccum.clear();
        if (!_client) {
            _disconnectPending = false;
            recordDisconnectStepUs(micros() - t0Us);
            return;
        }
        if (!_disconnectPending) {
            _disconnectPending = true;
            _disconnectReqStartMs = millis();
            uint8_t* raw = reinterpret_cast<uint8_t*>(_client);
            // Offset 64 dans struct esp_websocket_client (ESP-IDF v4.4 esp32s3) : bool run
            if (raw[64] != 0) {
                raw[64] = 0;
            }
        }
        recordDisconnectStepUs(micros() - t0Us);
    }

    bool pollDisconnect() override {
        if (!_client) {
            _disconnectPending = false;
            return true;
        }
        uint32_t t0Us = micros();
        if (!_disconnectPending) {
            requestDisconnect();
        }
        uint8_t* raw = reinterpret_cast<uint8_t*>(_client);
        // Offset 68 dans struct esp_websocket_client (ESP-IDF v4.4 esp32s3) : EventGroupHandle_t status_bits
        EventGroupHandle_t statusBits = *reinterpret_cast<EventGroupHandle_t*>(raw + 68);
        if (statusBits != nullptr) {
            EventBits_t bits = xEventGroupWaitBits(statusBits, 1, pdFALSE, pdTRUE, 0);
            if ((bits & 1) == 0) {
                if (millis() - _disconnectReqStartMs < 1500) {
                    recordDisconnectStepUs(micros() - t0Us);
                    return false;
                }
                // Sécurité : si le délai maximal de poll (1000 ms + marge) est dépassé, arrêt complet
                esp_websocket_client_stop(_client);
                esp_websocket_client_destroy(_client);
                _client = nullptr;
                _disconnectPending = false;
                recordDisconnectStepUs(micros() - t0Us);
                return true;
            }
            // websocket_task est formellement arrêtée (STOPPED_BIT = 1 et client->run = 0).
            // Offset 20 : state = WEBSOCKET_STATE_UNKNOW (0).
            *reinterpret_cast<int32_t*>(raw + 20) = 0;
        } else {
            esp_websocket_client_stop(_client);
        }
        // Comme client->run == 0, esp_websocket_client_destroy() ne rappelle pas esp_websocket_client_stop()
        // et libère immédiatement le transport TLS, les tampons et la mémoire sans aucun blocage.
        esp_websocket_client_destroy(_client);
        _client = nullptr;
        _disconnectPending = false;
        recordDisconnectStepUs(micros() - t0Us);
        return true;
    }

    bool isDisconnectPending() const override { return _disconnectPending; }
    uint32_t getLastDisconnectDurationUs() const override { return _lastDisconnectStepUs; }
    uint32_t getMaxDisconnectDurationUs() const override { return _maxDisconnectStepUs; }

    void tick() override {
        // Si une déconnexion asynchrone est en cours, la faire progresser sans bloquer
        if (_disconnectPending) {
            pollDisconnect();
        }
    }

    bool isConnected() const override {
        return !_disconnectPending && _client && esp_websocket_client_is_connected(_client);
    }

    bool send(const char* data, size_t len) override {
        if (!isConnected()) return false;
        if (_sendMutex) {
            if (xSemaphoreTake(_sendMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
                return false;
            }
        }
        int sent = esp_websocket_client_send_text(_client, data, len, pdMS_TO_TICKS(1000));
        if (_sendMutex) {
            xSemaphoreGive(_sendMutex);
        }
        return sent == (int)len;
    }

    void setHandlers(ConnectHandler onConnect, DisconnectHandler onDisconnect,
                     DataHandler onData, ErrorHandler onError) override {
        _onConnect = std::move(onConnect);
        _onDisconnect = std::move(onDisconnect);
        _onData = std::move(onData);
        _onError = std::move(onError);
    }

    // Diagnostics FreeRTOS & mémoire interne TLS (v0.6.1 — Correction B)
    int8_t getTaskCore() const override { return _wsTaskCore.load(std::memory_order_relaxed); }
    uint8_t getTaskPriority() const override { return _wsTaskPriority.load(std::memory_order_relaxed); }
    uint8_t getHandshakeTaskPriority() const override { return _handshakeTaskPrio; }
    uint8_t getSteadyTaskPriority() const override { return _steadyTaskPrio; }
    uint32_t getFreeInternalHeapAtConnect() const override { return _freeInternalHeapAtConnect.load(std::memory_order_relaxed); }
    uint32_t getMinFreeInternalHeapAtConnect() const override { return _minFreeInternalHeapAtConnect.load(std::memory_order_relaxed); }
    uint32_t getLargestInternalBlockAtConnect() const override { return _largestInternalBlockAtConnect.load(std::memory_order_relaxed); }

    // Instrumentation WebSocket & contrôle de trames (v0.6.3.2.1 — Correction A)
    uint32_t getControlFrameCount() const { return _controlFrameCount.load(std::memory_order_relaxed); }
    uint8_t getLastDataOpcode() const { return _lastDataOpcode.load(std::memory_order_relaxed); }
    uint32_t getLastDataLen() const { return _lastDataLen.load(std::memory_order_relaxed); }
    uint32_t getLastPayloadLen() const { return _lastPayloadLen.load(std::memory_order_relaxed); }
    uint32_t getReassemblyCompleteCount() const { return _reassemblyCompleteCount.load(std::memory_order_relaxed); }

private:
    static void eventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
        auto* self = static_cast<EspWebSocketTransport*>(handler_args);
        auto* data = static_cast<esp_websocket_event_data_t*>(event_data);

        switch (event_id) {
            case WEBSOCKET_EVENT_CONNECTED: {
                uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                uint32_t minFreeInt = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                self->_freeInternalHeapAtConnect.store(freeInt, std::memory_order_relaxed);
                self->_minFreeInternalHeapAtConnect.store(minFreeInt, std::memory_order_relaxed);
                self->_largestInternalBlockAtConnect.store(largestInt, std::memory_order_relaxed);

                if (self->_steadyTaskPrio != self->_handshakeTaskPrio) {
                    vTaskPrioritySet(NULL, self->_steadyTaskPrio);
                }
                int8_t coreId = (int8_t)xPortGetCoreID();
                uint8_t curPrio = self->_steadyTaskPrio;
                self->_wsTaskCore.store(coreId, std::memory_order_relaxed);
                self->_wsTaskPriority.store(curPrio, std::memory_order_relaxed);

                Serial.printf("[WS] WebSocket TLS connecte (core=%d, prio=%u->%u, ramInt=%u, maxBlk=%u)\n",
                              (int)coreId, (unsigned)self->_handshakeTaskPrio, (unsigned)curPrio,
                              (unsigned)freeInt, (unsigned)largestInt);
                self->_connected = true;
                if (self->_onConnect) self->_onConnect();
                break;
            }

            case WEBSOCKET_EVENT_DISCONNECTED:
                if (self->_steadyTaskPrio != self->_handshakeTaskPrio) {
                    vTaskPrioritySet(NULL, self->_handshakeTaskPrio);
                }
                self->_wsTaskPriority.store(self->_handshakeTaskPrio, std::memory_order_relaxed);
                Serial.println("[WS] WebSocket deconnecte");
                self->_connected = false;
                if (self->_onDisconnect) self->_onDisconnect();
                break;

            case WEBSOCKET_EVENT_DATA: {
                if (self->_wsTaskCore.load(std::memory_order_relaxed) < 0) {
                    self->_wsTaskCore.store((int8_t)xPortGetCoreID(), std::memory_order_relaxed);
                }
                // Filtrage strict des trames de controle WebSocket (RFC 6455)
                // 0x08 = CLOSE, 0x09 = PING, 0x0A = PONG
                // Ces trames sont gerees par esp_websocket_client et ne contiennent pas de JSON valide.
                // Les filtrer empeche egalement qu'un Ping/Pong n'efface prematurement le tampon _rxAccum en cours de reassemblage.
                uint8_t op = data->op_code & 0x0F;
                if (op >= 0x08) {
                    self->_controlFrameCount.fetch_add(1, std::memory_order_relaxed);
                    break;
                }

                self->_lastDataOpcode.store(op, std::memory_order_relaxed);
                self->_lastDataLen.store((uint32_t)data->data_len, std::memory_order_relaxed);
                self->_lastPayloadLen.store((uint32_t)data->payload_len, std::memory_order_relaxed);
                self->_lastPayloadOffset.store((uint32_t)data->payload_offset, std::memory_order_relaxed);

                if (data->data_ptr && data->data_len > 0) {
                    if (data->payload_offset == 0 && data->data_len == data->payload_len && op != 0x00) {
                        // Trame complete en un seul paquet (non fragmentee)
                        if (self->_onData) self->_onData(data->data_ptr, data->data_len);
                    } else {
                        // Reassemblage de trame multi-paquets
                        if (data->payload_offset == 0 && op != 0x00) {
                            self->_rxAccum.clear();
                            self->_rxAccum.reserve(data->payload_len);
                        }
                        self->_rxAccum.append(data->data_ptr, data->data_len);
                        if (self->_rxAccum.length() >= (size_t)data->payload_len) {
                            if (self->_onData) self->_onData(self->_rxAccum.c_str(), self->_rxAccum.length());
                            self->_rxAccum.clear();
                            self->_reassemblyCompleteCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
                break;
            }

            case WEBSOCKET_EVENT_ERROR:
                Serial.println("[WS] Erreur WebSocket signalee");
                if (self->_onError) self->_onError("Erreur transport WebSocket");
                break;

            default:
                break;
        }
    }

    esp_websocket_client_handle_t _client = nullptr;
    bool _connected = false;
    bool _disconnectPending = false;
    uint32_t _disconnectReqStartMs = 0;
    uint32_t _lastDisconnectStepUs = 0;
    uint32_t _maxDisconnectStepUs = 0;
    SemaphoreHandle_t _sendMutex = nullptr;
    std::string _rxAccum;

    void recordDisconnectStepUs(uint32_t us) {
        _lastDisconnectStepUs = us;
        if (us > _maxDisconnectStepUs) {
            _maxDisconnectStepUs = us;
        }
    }

    uint8_t _handshakeTaskPrio = DEFAULT_HANDSHAKE_TASK_PRIO;
    uint8_t _steadyTaskPrio = DEFAULT_STEADY_TASK_PRIO;
    std::atomic<int8_t> _wsTaskCore{-1};
    std::atomic<uint8_t> _wsTaskPriority{DEFAULT_HANDSHAKE_TASK_PRIO};
    std::atomic<uint32_t> _freeInternalHeapAtConnect{0};
    std::atomic<uint32_t> _minFreeInternalHeapAtConnect{0};
    std::atomic<uint32_t> _largestInternalBlockAtConnect{0};

    // Instrumentation WebSocket v0.6.3.2.1
    std::atomic<uint32_t> _controlFrameCount{0};
    std::atomic<uint8_t> _lastDataOpcode{0};
    std::atomic<uint32_t> _lastDataLen{0};
    std::atomic<uint32_t> _lastPayloadLen{0};
    std::atomic<uint32_t> _lastPayloadOffset{0};
    std::atomic<uint32_t> _reassemblyCompleteCount{0};

    ConnectHandler _onConnect;
    DisconnectHandler _onDisconnect;
    DataHandler _onData;
    ErrorHandler _onError;
};
#endif
