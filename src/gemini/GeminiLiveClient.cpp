#include "GeminiLiveClient.h"
#include "GoogleRoots.h"
#include "WebSocketReassembler.h"
#include "audio/AudioTraceLog.h"
#include <cstring>
#include <vector>
#include <cJSON.h>

#if defined(ESP32) || defined(ARDUINO)
#include <esp_websocket_client.h>
#include <esp_heap_caps.h>
#endif

// Constantes de connexion Gemini Live WebSocket API
static constexpr const char* GEMINI_LIVE_HOST = "generativelanguage.googleapis.com";
static constexpr const char* GEMINI_LIVE_PATH = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";
static constexpr int GEMINI_LIVE_PORT = 443;
static constexpr uint32_t GEMINI_LIVE_CONNECT_TIMEOUT_MS = 10000;

#if defined(ESP32) || defined(ARDUINO)
/**
 * @brief Transport WebSocket natif ESP-IDF (esp_websocket_client).
 * Tâche dédiée FreeRTOS, buffers sécurisés, réassemblage des trames fragmentées
 * et pile de 14 Ko pour le handshake TLS mbedTLS.
 */
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

    bool connect(const char* uri, const char* certPem) override {
        disconnect();

        if (!_sendMutex) {
            _sendMutex = xSemaphoreCreateMutex();
        }

        _intentionalDisconnect = false;
        _lastCloseCode = 0;
        _lastCloseReason.clear();
        _reassembler.reset();

        esp_websocket_client_config_t cfg = {};
        cfg.uri = uri;
        cfg.cert_pem = certPem;
        cfg.buffer_size = 8192;
        cfg.task_stack = 14336; // Indispensable pour le handshake TLS mbedTLS
        cfg.task_prio = 5;
        cfg.pingpong_timeout_sec = 15;
        cfg.ping_interval_sec = 10;
        cfg.keep_alive_enable = true;
        cfg.keep_alive_idle = 10;
        cfg.user_context = this;

        _client = esp_websocket_client_init(&cfg);
        if (!_client) {
            Serial.println("[VoiceKit:WS] Echec esp_websocket_client_init");
            if (_onError) _onError("Echec init client WebSocket");
            return false;
        }

        esp_websocket_register_events(_client, WEBSOCKET_EVENT_ANY, &EspWebSocketTransport::eventHandler, this);
        esp_err_t err = esp_websocket_client_start(_client);
        if (err != ESP_OK) {
            Serial.printf("[VoiceKit:WS] Echec esp_websocket_client_start: %d\n", err);
            if (_onError) _onError("Echec demarrage client WebSocket");
            disconnect();
            return false;
        }
        return true;
    }

    void disconnect() override {
        _intentionalDisconnect = true;
        if (_client) {
            Serial.println("[VoiceKit:WS] Arret demande du client WebSocket (disconnect)...");
            esp_websocket_client_stop(_client);
            esp_websocket_client_destroy(_client);
            _client = nullptr;
        }
        _connected = false;
        _reassembler.shrink();
    }

    bool isConnected() const override {
        return _connected && _client && esp_websocket_client_is_connected(_client);
    }

    bool send(const char* data, size_t len) override {
        if (!isConnected()) return false;
        if (!_sendMutex) return false;

        if (xSemaphoreTake(_sendMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            Serial.println("[VoiceKit:WS] Echec semaphore send (mutex occupe)");
            return false;
        }

        int written = esp_websocket_client_send_text(_client, data, len, pdMS_TO_TICKS(3000));
        xSemaphoreGive(_sendMutex);

        if (written <= 0) {
            Serial.printf("[VoiceKit:WS] Echec esp_websocket_client_send_text: written=%d\n", written);
            return false;
        }
        return true;
    }

    void setHandlers(ConnectHandler onConnect, DisconnectHandler onDisconnect, 
                            DataHandler onData, ErrorHandler onError) override {
        _onConnect = std::move(onConnect);
        _onDisconnect = std::move(onDisconnect);
        _onData = std::move(onData);
        _onError = std::move(onError);
    }

    void setCloseHandler(CloseHandler onClose) override {
        _onClose = std::move(onClose);
    }

private:
    static void eventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
        auto* self = static_cast<EspWebSocketTransport*>(handler_args);
        auto* data = static_cast<esp_websocket_event_data_t*>(event_data);

        switch (event_id) {
            case WEBSOCKET_EVENT_CONNECTED:
                Serial.println("[VoiceKit:WS] Evenement CONNECTED : Session WebSocket etablie avec succes");
                self->_connected = true;
                self->_lastCloseCode = 0;
                self->_lastCloseReason.clear();
                self->_reassembler.reset();
                if (self->_onConnect) self->_onConnect();
                break;

            case WEBSOCKET_EVENT_DATA:
                if (!data) break;

                // Trame CLOSE (Opcode 0x08)
                if (data->op_code == 0x08) {
                    uint16_t code = 1005;
                    char reasonBuf[256] = {0};
                    if (data->data_len >= 2 && data->data_ptr) {
                        code = (static_cast<uint8_t>(data->data_ptr[0]) << 8) | 
                                static_cast<uint8_t>(data->data_ptr[1]);
                        int reasonLen = data->data_len - 2;
                        if (reasonLen > 0) {
                            size_t copyLen = (reasonLen < (int)sizeof(reasonBuf) - 1) ? 
                                              reasonLen : (sizeof(reasonBuf) - 1);
                            memcpy(reasonBuf, data->data_ptr + 2, copyLen);
                            reasonBuf[copyLen] = '\0';
                        }
                    }
                    self->_lastCloseCode = code;
                    self->_lastCloseReason = reasonBuf;
                    Serial.printf("[VoiceKit:WS] Evenement DATA [CLOSE]: Code de fermeture=%u, Raison=\"%s\"\n",
                                  code, reasonBuf);
                    if (self->_onClose) {
                        self->_onClose(code, reasonBuf);
                    }
                    return;
                }

                // Trames de donnees Texte (0x01) ou Continuation (0x00)
                if ((data->op_code == 0x01 || data->op_code == 0x00) && data->data_ptr && data->data_len > 0) {
                    size_t outLen = 0;
                    const char* assembled = self->_reassembler.processChunk(
                        data->op_code, data->data_ptr, data->data_len,
                        data->payload_len, data->payload_offset, outLen
                    );

                    if (assembled && outLen > 0) {
                        if (self->_onData) {
                            self->_onData(assembled, outLen);
                        }
                        self->_reassembler.reset();
                    }
                }
                break;

            case WEBSOCKET_EVENT_CLOSED:
                Serial.printf("[VoiceKit:WS] Evenement CLOSED : Fermeture normale WebSocket (code=%u, raison=\"%s\")\n",
                              self->_lastCloseCode, self->_lastCloseReason.c_str());
                self->_connected = false;
                self->_reassembler.reset();
                if (self->_onDisconnect) self->_onDisconnect();
                break;

            case WEBSOCKET_EVENT_DISCONNECTED:
                if (self->_intentionalDisconnect) {
                    Serial.println("[VoiceKit:WS] Evenement DISCONNECTED : Deconnexion intentionnelle locale confirmee.");
                } else {
                    Serial.printf("[VoiceKit:WS] Evenement DISCONNECTED : Deconnexion transport WebSocket (code=%u, raison=\"%s\")\n",
                                  self->_lastCloseCode, self->_lastCloseReason.c_str());
                }
                self->_connected = false;
                self->_reassembler.reset();
                if (self->_onDisconnect) self->_onDisconnect();
                break;

            case WEBSOCKET_EVENT_ERROR:
                Serial.println("[VoiceKit:WS] Evenement ERROR : Erreur bas niveau transport WebSocket / TLS");
                if (self->_onError) {
                    if (!self->_lastCloseReason.empty()) {
                        self->_onError(self->_lastCloseReason.c_str());
                    } else {
                        self->_onError("Erreur protocolaire transport WebSocket (TLS ou reseau)");
                    }
                }
                break;

            default:
                break;
        }
    }

    esp_websocket_client_handle_t _client = nullptr;
    SemaphoreHandle_t _sendMutex = nullptr;
    std::atomic<bool> _connected{false};
    WebSocketReassembler _reassembler;
    ConnectHandler _onConnect;
    DisconnectHandler _onDisconnect;
    DataHandler _onData;
    ErrorHandler _onError;
};
#else
#include "MockLiveTransport.h"
#endif

// =============================================================================
// GeminiLiveClient Implementation
// =============================================================================

GeminiLiveClient::GeminiLiveClient(ILiveTransport* customTransport) {
    if (customTransport) {
        _transport = customTransport;
    } else {
#if defined(ESP32) || defined(ARDUINO)
        _defaultTransport = std::unique_ptr<ILiveTransport>(new EspWebSocketTransport());
#else
        _defaultTransport = std::unique_ptr<ILiveTransport>(new MockLiveTransport());
#endif
        _transport = _defaultTransport.get();
    }

    _transport->setHandlers(
        [this]() {
            _connected = true;
            _setupComplete = false;
            _lastCloseCode = 0;
            _lastCloseReason.clear();
            sendSetupMessage();
            if (_onConnected) _onConnected();
        },
        [this]() {
            _connected = false;
            _setupComplete = false;
            if (_onDisconnected) _onDisconnected();
        },
        [this](const char* data, size_t len) {
            handleIncomingMessage(data, len);
        },
        [this](const char* err) {
            _lastError = err;
            if (_onError) _onError(_lastError);
        }
    );

    _transport->setCloseHandler(
        [this](uint16_t code, const char* reason) {
            _lastCloseCode = code;
            _lastCloseReason = reason ? reason : "";
            char buf[256];
            snprintf(buf, sizeof(buf), "Session fermee par Gemini (code %u) : %s", code, _lastCloseReason.c_str());
            _lastError = buf;
            Serial.printf("[VoiceKit:Client] ERREUR : %s\n", buf);
            if (_onClose) _onClose(code, _lastCloseReason);
            if (_onError) _onError(_lastError);
        }
    );
}

GeminiLiveClient::~GeminiLiveClient() {
    disconnect();
}

void GeminiLiveClient::setTransport(ILiveTransport* transport) {
    if (transport) {
        _transport = transport;
        _transport->setHandlers(
            [this]() {
                _connected = true;
                _setupComplete = false;
                _lastCloseCode = 0;
                _lastCloseReason.clear();
                sendSetupMessage();
                if (_onConnected) _onConnected();
            },
            [this]() {
                _connected = false;
                _setupComplete = false;
                if (_onDisconnected) _onDisconnected();
            },
            [this](const char* data, size_t len) {
                handleIncomingMessage(data, len);
            },
            [this](const char* err) {
                _lastError = err;
                if (_onError) _onError(_lastError);
            }
        );
        _transport->setCloseHandler(
            [this](uint16_t code, const char* reason) {
                _lastCloseCode = code;
                _lastCloseReason = reason ? reason : "";
                char buf[256];
                snprintf(buf, sizeof(buf), "Session fermee par Gemini (code %u) : %s", code, _lastCloseReason.c_str());
                _lastError = buf;
                Serial.printf("[VoiceKit:Client] ERREUR : %s\n", buf);
                if (_onClose) _onClose(code, _lastCloseReason);
                if (_onError) _onError(_lastError);
            }
        );
    }
}

String GeminiLiveClient::normalizeModelName(const String& model) {
    if (model.isEmpty()) {
        return VOICEKIT_DEFAULT_MODEL;
    }
    if (model.startsWith("models/")) {
        return model;
    }
    return "models/" + model;
}

bool GeminiLiveClient::begin(const char* apiKey, const char* model, const char* voice) {
    if (!apiKey || strlen(apiKey) == 0) {
        _lastError = "Cle API Gemini manquante";
        return false;
    }
    _apiKey = apiKey;
    if (model && strlen(model) > 0) {
        _model = normalizeModelName(model);
    } else {
        _model = normalizeModelName(VOICEKIT_DEFAULT_MODEL);
    }
    if (voice && strlen(voice) > 0) _voice = voice;
    return true;
}

void GeminiLiveClient::buildWebSocketUrl(char* buf, size_t bufSize) const {
    // Port 443 implicite par le protocole standard wss:// (evite la pollution SNI sur TLS)
    snprintf(buf, bufSize, "wss://%s%s?key=%s",
             GEMINI_LIVE_HOST, GEMINI_LIVE_PATH, _apiKey.c_str());
}

bool GeminiLiveClient::connect() {
    if (isConnected()) return true;
    if (_apiKey.isEmpty()) {
        _lastError = "Impossible de connecter : Cle API manquante";
        return false;
    }

    char url[512];
    buildWebSocketUrl(url, sizeof(url));

    _connected = false;
    _setupComplete = false;
    _intentionalDisconnect = false;
    _lastCloseCode = 0;
    _lastCloseReason.clear();
    _connectStartTimeMs = millis();

    Serial.println("[VoiceKit:Client] Etablissement session WebSocket Gemini Live...");
    return _transport->connect(url, GOOGLE_ROOTS);
}

void GeminiLiveClient::disconnect() {
    _intentionalDisconnect = true;
    if (_transport) {
        _transport->disconnect();
    }
    _connected = false;
    _setupComplete = false;
}

bool GeminiLiveClient::isIntentionalDisconnect() const {
    if (_intentionalDisconnect.load()) return true;
    if (_transport && _transport->isIntentionalDisconnect()) return true;
    return false;
}

bool GeminiLiveClient::isConnected() const {
    return _connected && _transport && _transport->isConnected();
}

void GeminiLiveClient::tick() {
    if (_transport) {
        _transport->tick();
    }

    if (_connected && !_setupComplete) {
        if (millis() - _connectStartTimeMs > GEMINI_LIVE_CONNECT_TIMEOUT_MS) {
            _lastError = "Timeout attente setupComplete Gemini Live";
            Serial.println("[VoiceKit:Client] ERREUR : Timeout setupComplete");
            disconnect();
            if (_onError) _onError(_lastError);
        }
    }
}

void GeminiLiveClient::sendSetupMessage() {
    String modelName = normalizeModelName(_model);
    String msg = "{\"setup\":{\"model\":\"" + modelName + "\",\"generationConfig\":{\"responseModalities\":[\"AUDIO\"],\"speechConfig\":{\"voiceConfig\":{\"prebuiltVoiceConfig\":{\"voiceName\":\"" + _voice + "\"}}}}";

    if (!_systemInstruction.isEmpty()) {
        msg += ",\"systemInstruction\":{\"parts\":[{\"text\":\"" + escapeJsonString(_systemInstruction) + "\"}]}";
    }

    msg += "}}";

    Serial.printf("[VoiceKit:Client] Envoi setup Gemini Live (model=%s, voice=%s, taille=%u octets)\n",
                  modelName.c_str(), _voice.c_str(), (unsigned)msg.length());
    bool ok = _transport->send(msg.c_str(), msg.length());
    if (!ok) {
        _lastError = "Echec envoi setup vers transport WebSocket";
        Serial.printf("[VoiceKit:Client] ERREUR : %s\n", _lastError.c_str());
        if (_onError) _onError(_lastError);
    }
}

bool GeminiLiveClient::sendTextMessage(const String& text) {
    if (!isConnected() || !_setupComplete) {
        _lastError = "Session Gemini non prete";
        return false;
    }

    String msg = "{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"text\":\"";
    msg += escapeJsonString(text);
    msg += "\"}]}],\"turnComplete\":true}}";

    Serial.printf("[VoiceKit:Client] Envoi texte vers Gemini: \"%s\"\n", text.c_str());
    return _transport->send(msg.c_str(), msg.length());
}

bool GeminiLiveClient::sendInterruption() {
    if (!isConnected()) return false;
    const char* msg = "{\"clientContent\":{\"turns\":[],\"turnComplete\":true}}";
    return _transport->send(msg, strlen(msg));
}

void GeminiLiveClient::resetTurnRxMetrics() {
    _audioWsMessagesCount = 0;
    _b64BlocksDecodedCount = 0;
    _b64DecodeFailedCount = 0;
    _pcmBlocksInvalidCount = 0;
    _pcmBytesDecodedTotal = 0;
    _pcmSamplesDispatchedTotal = 0;
    _lastPcmPacketTimeMs = 0;
    _turnCompleteTimeMs = 0;
    _turnCompleteReceived = false;
}

void GeminiLiveClient::handleIncomingMessage(const char* data, size_t len) {
    if (!data || len == 0) return;

    cJSON* root = cJSON_ParseWithLength(data, len);
    if (!root) {
        Serial.printf("[VoiceKit:Client] Avertissement : Echec parsing JSON (len=%u)\n", (unsigned)len);
        return;
    }

    // 0. Détection d'une erreur JSON renvoyée par le serveur Gemini
    cJSON* errorItem = cJSON_GetObjectItem(root, "error");
    if (errorItem) {
        int errCode = 0;
        const char* errMsg = "Erreur Gemini inconnue";
        const char* errStatus = "";

        cJSON* codeObj = cJSON_GetObjectItem(errorItem, "code");
        if (codeObj && cJSON_IsNumber(codeObj)) errCode = codeObj->valueint;

        cJSON* msgObj = cJSON_GetObjectItem(errorItem, "message");
        if (msgObj && cJSON_IsString(msgObj) && msgObj->valuestring) errMsg = msgObj->valuestring;

        cJSON* statusObj = cJSON_GetObjectItem(errorItem, "status");
        if (statusObj && cJSON_IsString(statusObj) && statusObj->valuestring) errStatus = statusObj->valuestring;

        char buf[256];
        if (strlen(errStatus) > 0) {
            snprintf(buf, sizeof(buf), "Gemini [%s / %d] : %s", errStatus, errCode, errMsg);
        } else {
            snprintf(buf, sizeof(buf), "Gemini [%d] : %s", errCode, errMsg);
        }

        _lastError = buf;
        Serial.printf("[VoiceKit:Client] ERREUR JSON SERVEUR GEMINI : %s\n", buf);
        cJSON_Delete(root);
        if (_onError) _onError(_lastError);
        return;
    }

    cJSON* errMsgRoot = cJSON_GetObjectItem(root, "errorMessage");
    if (errMsgRoot && cJSON_IsString(errMsgRoot) && errMsgRoot->valuestring) {
        _lastError = String("Gemini : ") + errMsgRoot->valuestring;
        Serial.printf("[VoiceKit:Client] ERREUR SERVEUR GEMINI : %s\n", _lastError.c_str());
        cJSON_Delete(root);
        if (_onError) _onError(_lastError);
        return;
    }

    // 1. Détection setupComplete
    if (!_setupComplete) {
        cJSON* setupComp = cJSON_GetObjectItem(root, "setupComplete");
        if (setupComp) {
            _setupComplete = true;
            Serial.println("[VoiceKit:Client] Session Gemini Live configuree et prete (setupComplete)");
            if (_onSetupComplete) _onSetupComplete();
        }
    }

    cJSON* serverContent = cJSON_GetObjectItem(root, "serverContent");
    if (serverContent) {
        // 2. Détection interruption
        cJSON* interrupted = cJSON_GetObjectItem(serverContent, "interrupted");
        if (interrupted && cJSON_IsTrue(interrupted)) {
            Serial.println("[VoiceKit:Client] Interruption signalee par Gemini");
            if (_onInterrupted) _onInterrupted();
        }

        // 3. Détection transcription de sortie
        cJSON* outputTx = cJSON_GetObjectItem(serverContent, "outputTranscription");
        if (outputTx) {
            cJSON* textItem = cJSON_GetObjectItem(outputTx, "text");
            if (textItem && textItem->valuestring && _onOutputTranscription) {
                _onOutputTranscription(String(textItem->valuestring));
            }
        }

        // 4. Extraction des blocs audio PCM 24 kHz
        cJSON* modelTurn = cJSON_GetObjectItem(serverContent, "modelTurn");
        if (modelTurn) {
            cJSON* parts = cJSON_GetObjectItem(modelTurn, "parts");
            if (parts && cJSON_IsArray(parts)) {
                int partCount = cJSON_GetArrayItem(parts, 0) ? cJSON_GetArraySize(parts) : 0;
                for (int i = 0; i < partCount; ++i) {
                    cJSON* part = cJSON_GetArrayItem(parts, i);
                    if (!part) continue;

                    cJSON* inlineData = cJSON_GetObjectItem(part, "inlineData");
                    if (inlineData) {
                        cJSON* mimeType = cJSON_GetObjectItem(inlineData, "mimeType");
                        cJSON* b64Data = cJSON_GetObjectItem(inlineData, "data");

                        if (mimeType && mimeType->valuestring && b64Data && b64Data->valuestring) {
                            if (strstr(mimeType->valuestring, "audio/pcm") != nullptr) {
                                _audioWsMessagesCount++;
                                std::vector<uint8_t> pcmBytes;
                                if (decodeBase64(b64Data->valuestring, strlen(b64Data->valuestring), pcmBytes)) {
                                    _b64BlocksDecodedCount++;
                                    size_t byteCount = pcmBytes.size();
                                    if (byteCount >= 2 && (byteCount % 2 == 0)) {
                                        size_t sampleCount = byteCount / 2;
                                        const int16_t* samples = reinterpret_cast<const int16_t*>(pcmBytes.data());
                                        _pcmBytesDecodedTotal += byteCount;
                                        _pcmSamplesDispatchedTotal += sampleCount;
                                        _lastPcmPacketTimeMs = millis();

                                        AudioTraceLog::record(AudioTraceEventType::PCM_DECODED, sampleCount, byteCount);

                                        if (_onAudioData) {
                                            _onAudioData(samples, sampleCount);
                                        }
                                    } else {
                                        _pcmBlocksInvalidCount++;
                                    }
                                } else {
                                    _b64DecodeFailedCount++;
                                    AudioTraceLog::record(AudioTraceEventType::PCM_DECODE_FAILED);
                                }
                            }
                        }
                    }
                }
            }
        }

        // 5. Détection turnComplete
        cJSON* turnComp = cJSON_GetObjectItem(serverContent, "turnComplete");
        if (turnComp && cJSON_IsTrue(turnComp)) {
            _turnCompleteReceived = true;
            _turnCompleteTimeMs = millis();
            AudioTraceLog::record(AudioTraceEventType::TURN_COMPLETE_RECEIVED);
            if (_onTurnComplete) {
                _onTurnComplete();
            }
        }
    }

    cJSON_Delete(root);
}

// =============================================================================
// Base64 & JSON Utilities
// =============================================================================

static const char B64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String GeminiLiveClient::encodeBase64(const uint8_t* data, size_t length) {
    String out = "";
    out.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        uint32_t b = (data[i] << 16);
        if (i + 1 < length) b |= (data[i + 1] << 8);
        if (i + 2 < length) b |= data[i + 2];

        out += B64_TABLE[(b >> 18) & 0x3F];
        out += B64_TABLE[(b >> 12) & 0x3F];
        out += (i + 1 < length) ? B64_TABLE[(b >> 6) & 0x3F] : '=';
        out += (i + 2 < length) ? B64_TABLE[b & 0x3F] : '=';
    }
    return out;
}

static inline int b64CharValue(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool GeminiLiveClient::decodeBase64(const char* input, size_t inLen, std::vector<uint8_t>& output) {
    output.clear();
    if (!input || inLen == 0) return true;

    output.reserve((inLen / 4) * 3);
    uint32_t buf = 0;
    int bits = 0;

    for (size_t i = 0; i < inLen; ++i) {
        char c = input[i];
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;

        int val = b64CharValue(c);
        if (val < 0) return false;

        buf = (buf << 6) | val;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return true;
}

String GeminiLiveClient::escapeJsonString(const String& s) {
    String out = "";
    out.reserve(s.length() + 16);

    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<uint8_t>(c) < 0x20) {
                    char hexBuf[8];
                    snprintf(hexBuf, sizeof(hexBuf), "\\u%04x", c);
                    out += hexBuf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}
