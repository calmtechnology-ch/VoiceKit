#pragma once
#include "ILiveTransport.h"

#if defined(ESP32) || defined(ARDUINO)
#include <Arduino.h>
#include <string>
#include <esp_websocket_client.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// =============================================================================
// EspWebSocketTransport : Implémentation du transport WebSocket natif ESP32
// Utilise esp_websocket_client et supporte TLS, reconnexion et en-têtes HTTP personnalisés
// =============================================================================
class EspWebSocketTransport : public ILiveTransport {
public:
    EspWebSocketTransport() = default;
    ~EspWebSocketTransport() override {
        disconnect();
        if (_sendMutex) {
            vSemaphoreDelete(_sendMutex);
            _sendMutex = nullptr;
        }
    }

    bool connect(const char* uri, const char* certPem = nullptr, const char* extraHeaders = nullptr) override {
        disconnect();

        if (!_sendMutex) {
            _sendMutex = xSemaphoreCreateMutex();
        }

        esp_websocket_client_config_t cfg = {};
        cfg.uri = uri;
        cfg.cert_pem = certPem;
        cfg.headers = extraHeaders;
        cfg.buffer_size = 8192;
        cfg.task_stack = 14336; // Indispensable pour le handshake TLS mbedTLS (14 Ko)
        cfg.task_prio = 5;
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
        if (_client) {
            esp_websocket_client_stop(_client);
            esp_websocket_client_destroy(_client);
            _client = nullptr;
        }
        _connected = false;
        _rxAccum.clear();
    }

    void tick() override {
        // Le client esp_websocket gère ses événements dans sa tâche FreeRTOS dédiée
    }

    bool isConnected() const override {
        return _client && esp_websocket_client_is_connected(_client);
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

private:
    static void eventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
        auto* self = static_cast<EspWebSocketTransport*>(handler_args);
        auto* data = static_cast<esp_websocket_event_data_t*>(event_data);

        switch (event_id) {
            case WEBSOCKET_EVENT_CONNECTED:
                Serial.println("[WS] WebSocket TLS connecte");
                self->_connected = true;
                if (self->_onConnect) self->_onConnect();
                break;

            case WEBSOCKET_EVENT_DISCONNECTED:
                Serial.println("[WS] WebSocket deconnecte");
                self->_connected = false;
                if (self->_onDisconnect) self->_onDisconnect();
                break;

            case WEBSOCKET_EVENT_DATA:
                if (data->data_ptr && data->data_len > 0) {
                    if (data->payload_offset == 0 && data->data_len == data->payload_len) {
                        // Trame complete en un seul paquet
                        if (self->_onData) self->_onData(data->data_ptr, data->data_len);
                    } else {
                        // Reassemblage de trame multi-paquets
                        if (data->payload_offset == 0) {
                            self->_rxAccum.clear();
                            self->_rxAccum.reserve(data->payload_len);
                        }
                        self->_rxAccum.append(data->data_ptr, data->data_len);
                        if (self->_rxAccum.length() >= (size_t)data->payload_len) {
                            if (self->_onData) self->_onData(self->_rxAccum.c_str(), self->_rxAccum.length());
                            self->_rxAccum.clear();
                        }
                    }
                }
                break;

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
    SemaphoreHandle_t _sendMutex = nullptr;
    std::string _rxAccum;

    ConnectHandler _onConnect;
    DisconnectHandler _onDisconnect;
    DataHandler _onData;
    ErrorHandler _onError;
};
#endif
