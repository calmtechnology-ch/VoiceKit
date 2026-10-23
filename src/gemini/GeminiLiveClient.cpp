#include "GeminiLiveClient.h"
#include "GoogleRoots.h"
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
 * Tâche dédiée FreeRTOS, buffers sécurisés et pile de 14 Ko pour le handshake TLS mbedTLS.
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

        esp_websocket_client_config_t cfg = {};
        cfg.uri = uri;
        cfg.cert_pem = certPem;
        cfg.buffer_size = 8192;
        cfg.task_stack = 14336; // Indispensable pour le handshake TLS mbedTLS
        cfg.task_prio = 5;
        cfg.pingpong_timeout_sec = 15;
        cfg.ping_interval_sec = 10;
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
        if (_client) {
            esp_websocket_client_stop(_client);
            esp_websocket_client_destroy(_client);
            _client = nullptr;
        }
        _connected = false;
    }

    bool isConnected() const override {
        return _connected && _client && esp_websocket_client_is_connected(_client);
    }

    bool send(const char* data, size_t len) override {
        if (!isConnected()) return false;
        if (!_sendMutex) return false;

        if (xSemaphoreTake(_sendMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return false;
        }

        int written = esp_websocket_client_send_text(_client, data, len, pdMS_TO_TICKS(3000));
        xSemaphoreGive(_sendMutex);

        return (written > 0);
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
                self->_connected = true;
                if (self->_onConnect) self->_onConnect();
                break;
            case WEBSOCKET_EVENT_DISCONNECTED:
                self->_connected = false;
                if (self->_onDisconnect) self->_onDisconnect();
                break;
            case WEBSOCKET_EVENT_DATA:
                if (data->op_code == 0x01 && data->data_ptr && data->data_len > 0) {
                    if (self->_onData) {
                        self->_onData(data->data_ptr, data->data_len);
                    }
                }
                break;
            case WEBSOCKET_EVENT_ERROR:
                if (self->_onError) self->_onError("Erreur protocolaire transport WebSocket");
                break;
            default:
                break;
        }
    }

    esp_websocket_client_handle_t _client = nullptr;
    SemaphoreHandle_t _sendMutex = nullptr;
    std::atomic<bool> _connected{false};
    ConnectHandler _onConnect;
    DisconnectHandler _onDisconnect;
    DataHandler _onData;
    ErrorHandler _onError;
};
#else
class MockLiveTransport : public ILiveTransport {
public:
    bool connect(const char*, const char*) override {
        _connected = true;
        if (_onConnect) _onConnect();
        return true;
    }
    void disconnect() override {
        _connected = false;
        if (_onDisconnect) _onDisconnect();
    }
    bool isConnected() const override { return _connected; }
    bool send(const char* data, size_t) override {
        _lastSent = data;
        return true;
    }
    void setHandlers(ConnectHandler onConnect, DisconnectHandler onDisconnect, 
                            DataHandler onData, ErrorHandler onError) override {
        _onConnect = std::move(onConnect);
        _onDisconnect = std::move(onDisconnect);
        _onData = std::move(onData);
        _onError = std::move(onError);
    }
    std::string getLastSent() const { return _lastSent; }

private:
    bool _connected = false;
    std::string _lastSent;
    ConnectHandler _onConnect;
    DisconnectHandler _onDisconnect;
    DataHandler _onData;
    ErrorHandler _onError;
};
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
    }
}

bool GeminiLiveClient::begin(const char* apiKey, const char* model, const char* voice) {
    if (!apiKey || strlen(apiKey) == 0) {
        _lastError = "Cle API Gemini manquante";
        return false;
    }
    _apiKey = apiKey;
    if (model && strlen(model) > 0) _model = model;
    if (voice && strlen(voice) > 0) _voice = voice;
    return true;
}

void GeminiLiveClient::buildWebSocketUrl(char* buf, size_t bufSize) const {
    snprintf(buf, bufSize, "wss://%s:%d%s?key=%s",
             GEMINI_LIVE_HOST, GEMINI_LIVE_PORT, GEMINI_LIVE_PATH, _apiKey.c_str());
}

bool GeminiLiveClient::connect() {
    if (isConnected()) return true;
    if (_apiKey.isEmpty()) {
        _lastError = "Impossible de connecter: Cle API manquante";
        return false;
    }

    char url[512];
    buildWebSocketUrl(url, sizeof(url));

    _connected = false;
    _setupComplete = false;
    _connectStartTimeMs = millis();

    Serial.println("[VoiceKit:Client] Etablissement session WebSocket Gemini Live...");
    return _transport->connect(url, GOOGLE_ROOTS);
}

void GeminiLiveClient::disconnect() {
    if (_transport) {
        _transport->disconnect();
    }
    _connected = false;
    _setupComplete = false;
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
            Serial.println("[VoiceKit:Client] ERREUR: Timeout setupComplete");
            disconnect();
            if (_onError) _onError(_lastError);
        }
    }
}

void GeminiLiveClient::sendSetupMessage() {
    String msg = "{\"setup\":{\"model\":\"" + _model + "\",\"generationConfig\":{\"responseModalities\":[\"AUDIO\"],\"speechConfig\":{\"voiceConfig\":{\"prebuiltVoiceConfig\":{\"voiceName\":\"" + _voice + "\"}}}}";

    if (!_systemInstruction.isEmpty()) {
        msg += ",\"systemInstruction\":{\"parts\":[{\"text\":\"" + escapeJsonString(_systemInstruction) + "\"}]}";
    }

    msg += "}}";

    Serial.printf("[VoiceKit:Client] Envoi setup Gemini Live (model=%s, voice=%s)\n",
                  _model.c_str(), _voice.c_str());
    _transport->send(msg.c_str(), msg.length());
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
    if (!root) return;

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
