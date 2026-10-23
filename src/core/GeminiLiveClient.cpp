#include "core/GeminiLiveClient.h"
#include "core/AudioTraceLog.h"
#include "GoogleRoots.h"
#include <cstring>
#include <vector>

#include <cJSON.h>

#if defined(ESP32) || defined(ARDUINO)
#include <esp_websocket_client.h>
#include <esp_heap_caps.h>
#else
// Mock / Native fallback for transport
#include <sstream>
#endif

// =============================================================================
// Implémentation du transport WebSocket natif ESP32 (esp_websocket_client)
// =============================================================================
#if defined(ESP32) || defined(ARDUINO)
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
        cfg.task_stack = 14336; // Indispensable pour le handshake TLS mbedTLS (14 Ko)
        cfg.task_prio = 5;
        cfg.pingpong_timeout_sec = 15;
        cfg.ping_interval_sec = 10;
        cfg.user_context = this;

        _client = esp_websocket_client_init(&cfg);
        if (!_client) {
            Serial.println("[LiveWS] Echec esp_websocket_client_init");
            if (_onError) _onError("Echec init client WebSocket");
            return false;
        }

        esp_websocket_register_events(_client, WEBSOCKET_EVENT_ANY, &EspWebSocketTransport::eventHandler, this);
        esp_err_t err = esp_websocket_client_start(_client);
        if (err != ESP_OK) {
            Serial.printf("[LiveWS] Echec esp_websocket_client_start: %d\n", err);
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
                Serial.println("[LiveWS] WebSocket TLS connecte a Gemini Live");
                self->_connected = true;
                if (self->_onConnect) self->_onConnect();
                break;

            case WEBSOCKET_EVENT_DISCONNECTED:
                Serial.println("[LiveWS] WebSocket deconnecte");
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
                Serial.println("[LiveWS] Erreur WebSocket");
                if (self->_onError) self->_onError("Erreur transport WebSocket");
                break;

            default:
                break;
        }
    }

    esp_websocket_client_handle_t _client = nullptr;
    SemaphoreHandle_t _sendMutex = nullptr;
    bool _connected = false;
    std::string _rxAccum;
    ConnectHandler _onConnect;
    DisconnectHandler _onDisconnect;
    DataHandler _onData;
    ErrorHandler _onError;
};
#endif

// =============================================================================
// GeminiLiveClient : Implémentation
// =============================================================================

GeminiLiveClient::GeminiLiveClient(ILiveTransport* customTransport) {
    if (customTransport) {
        _transport = customTransport;
    } else {
#if defined(ESP32) || defined(ARDUINO)
        _defaultTransport = std::make_unique<EspWebSocketTransport>();
        _transport = _defaultTransport.get();
#endif
    }
}

GeminiLiveClient::~GeminiLiveClient() {
    _onConnected = nullptr;
    _onDisconnected = nullptr;
    _onError = nullptr;
    _onAudioData = nullptr;
    _onOutputTranscription = nullptr;
    _onText = nullptr;
    _onInputTranscription = nullptr;
    _onTurnComplete = nullptr;
    _onInterrupted = nullptr;
    _onToolCallGeneric = nullptr;
    _onToolCallLegacy = nullptr;
    _onSetupComplete = nullptr;
    if (_transport) {
        _transport->setHandlers(nullptr, nullptr, nullptr, nullptr);
    }
    disconnect();
}

void GeminiLiveClient::setTransport(ILiveTransport* transport) {
    disconnect();
    _transport = transport;
}

bool GeminiLiveClient::isDuplicateCall(const String& callId) const {
    if (callId.isEmpty()) return false;
    for (const auto& id : _recentHandledCallIds) {
        if (id == callId) return true;
    }
    return false;
}

void GeminiLiveClient::recordHandledCall(const String& callId) {
    if (callId.isEmpty()) return;
    if (_recentHandledCallIds.size() >= RECENT_CALL_IDS_CAP) {
        _recentHandledCallIds.erase(_recentHandledCallIds.begin());
    }
    _recentHandledCallIds.push_back(callId);
}

bool GeminiLiveClient::begin(const char* apiKey, const char* model, const char* voice) {
    if (!apiKey || apiKey[0] == '\0') {
        _lastError = "Cle API Gemini manquante";
        return false;
    }
    _apiKey = apiKey;
    if (model && model[0] != '\0') _model = model;
    if (voice && voice[0] != '\0') _voice = voice;
    return true;
}

bool GeminiLiveClient::connect() {
    if (_apiKey.isEmpty()) {
        _lastError = "Cle API non configuree";
        if (_onError) _onError(_lastError);
        return false;
    }

#if defined(ESP32) || defined(ARDUINO)
    // Sécurité RAM interne : mbedTLS a besoin d'au moins 30 Ko en interne
    size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (freeInternal < LIVE_MIN_FREE_INTERNAL_HEAP) {
        _lastError = "Memoire interne insuffisante pour WebSocket TLS";
        Serial.printf("[LiveClient] ERREUR : %s (dispo: %u octets, min: %u octets)\n",
                      _lastError.c_str(), (unsigned)freeInternal, (unsigned)LIVE_MIN_FREE_INTERNAL_HEAP);
        if (_onError) _onError(_lastError);
        return false;
    }
#endif

    if (!_transport) {
        _lastError = "Transport WebSocket indisponible";
        if (_onError) _onError(_lastError);
        return false;
    }

    _setupComplete.store(false, std::memory_order_release);
    _connected.store(false, std::memory_order_release);
    _connectStartTimeMs = millis();

    // Configuration des écouteurs de transport
    _transport->setHandlers(
        [this]() {
            _connected.store(true, std::memory_order_release);
            if (_onConnected) _onConnected();
            sendSetupMessage();
        },
        [this]() {
            _connected.store(false, std::memory_order_release);
            _setupComplete.store(false, std::memory_order_release);
            if (_onDisconnected) _onDisconnected();
        },
        [this](const char* data, size_t len) {
            handleIncomingMessage(data, len);
        },
        [this](const char* err) {
            _lastError = err ? err : "Erreur transport";
            if (_onError) _onError(_lastError);
        }
    );

    // Construction de l'URL avec clé masquée dans les logs
    char urlBuf[512];
    buildWebSocketUrl(urlBuf, sizeof(urlBuf));

    Serial.println("[LiveClient] Demarrage connexion WebSocket TLS vers Gemini Live...");
    return _transport->connect(urlBuf, GOOGLE_ROOTS);
}

void GeminiLiveClient::disconnect() {
    if (_transport) {
        _transport->disconnect();
    }
    _connected.store(false, std::memory_order_release);
    _setupComplete.store(false, std::memory_order_release);
}

void GeminiLiveClient::tick() {
    if (_transport) {
        _transport->tick();
    }
}

bool GeminiLiveClient::isConnected() const {
    return _connected.load(std::memory_order_acquire) && _transport && _transport->isConnected();
}

void GeminiLiveClient::buildWebSocketUrl(char* buf, size_t bufSize) const {
    snprintf(buf, bufSize, "wss://%s%s?key=%s", 
             GEMINI_LIVE_HOST, GEMINI_LIVE_PATH, _apiKey.c_str());
}

String GeminiLiveClient::escapeJsonString(const String& s) {
    String out;
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
                if (static_cast<uint8_t>(c) < 32) {
                    char hexBuf[8];
                    snprintf(hexBuf, sizeof(hexBuf), "\\u%04x", static_cast<uint8_t>(c));
                    out += hexBuf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

void GeminiLiveClient::sendSetupMessage() {
    // Configuration setup Gemini Live selon protocole BidiGenerateContent
    // VAD manuelle + transcriptions + instructions et outils configurables
    String setupJson = "{";
    setupJson += "\"setup\":{";
    setupJson += "\"model\":\"" + _model + "\",";
    setupJson += "\"generationConfig\":{";
    setupJson += "\"responseModalities\":[\"AUDIO\"],";
    setupJson += "\"speechConfig\":{";
    setupJson += "\"voiceConfig\":{";
    setupJson += "\"prebuiltVoiceConfig\":{\"voiceName\":\"" + _voice + "\"}";
    setupJson += "}";
    setupJson += "}";
    setupJson += "},";

    // Instructions système configurées ou repli par défaut
    String sysInst = _systemInstruction;
    if (sysInst.isEmpty()) {
        sysInst = "Tu es VoxCore, un assistant vocal bienveillant et concis. Reponds toujours en francais direct (1 ou 2 phrases max).";
    }
    setupJson += "\"systemInstruction\":{";
    setupJson += "\"parts\":[{";
    setupJson += "\"text\":\"" + escapeJsonString(sysInst) + "\"";
    setupJson += "}]";
    setupJson += "},";

    // Outils configurables (omission complète du bloc tools si aucun outil n'est déclaré)
    if (!_tools.empty()) {
        setupJson += "\"tools\":[{";
        setupJson += "\"functionDeclarations\":[";
        for (size_t i = 0; i < _tools.size(); ++i) {
            if (i > 0) setupJson += ",";
            setupJson += "{";
            setupJson += "\"name\":\"" + escapeJsonString(_tools[i].name) + "\",";
            setupJson += "\"description\":\"" + escapeJsonString(_tools[i].description) + "\"";
            if (!_tools[i].parametersSchemaJson.isEmpty()) {
                setupJson += ",\"parameters\":" + _tools[i].parametersSchemaJson;
            } else {
                setupJson += ",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}";
            }
            setupJson += "}";
        }
        setupJson += "]";
        setupJson += "}],";
    }

    setupJson += "\"realtimeInputConfig\":{";
    setupJson += "\"automaticActivityDetection\":{\"disabled\":true}";
    setupJson += "},";
    setupJson += "\"inputAudioTranscription\":{},";
    setupJson += "\"outputAudioTranscription\":{}";
    setupJson += "}";
    setupJson += "}";

    Serial.printf("[LiveClient] Envoi setup Gemini Live (VAD manuelle, transcriptions, %u outil(s) declare(s))...\n",
                  (unsigned)_tools.size());
    _transport->send(setupJson.c_str(), setupJson.length());
}

bool GeminiLiveClient::sendActivityStart() {
    if (!isConnected() || !_setupComplete) return false;
    const char* msg = "{\"realtimeInput\":{\"activityStart\":{}}}";
    Serial.println("[LiveClient] Signalisation debut d'activite vocale (activityStart)");
    return _transport->send(msg, strlen(msg));
}

bool GeminiLiveClient::sendAudioChunk(const int16_t* samples, size_t sampleCount) {
    if (!isConnected() || !_setupComplete || !samples || sampleCount == 0) {
        _sendRefusedCount++;
        return false;
    }

    // Conversion en octets et encodage Base64 conforme RFC 4648
    const uint8_t* bytePtr = reinterpret_cast<const uint8_t*>(samples);
    const size_t byteCount = sampleCount * sizeof(int16_t);
    String b64 = encodeBase64(bytePtr, byteCount);

    String msg;
    msg.reserve(b64.length() + 80);
    msg = "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"";
    msg += b64;
    msg += "\"}}}";

    uint32_t t0 = micros();
    bool sent = _transport->send(msg.c_str(), msg.length());
    uint32_t elapsedUs = micros() - t0;

    _lastSendDurationUs = elapsedUs;
    if (elapsedUs > _maxSendDurationUs) {
        _maxSendDurationUs = elapsedUs;
    }
    if (elapsedUs > 32000) {
        _sendsOver32msCount++;
    }
    if (elapsedUs > 100000) {
        _sendsOver100msCount++;
    }

    if (sent) {
        _audioChunksSentCount++;
    } else {
        _sendRefusedCount++;
    }

    return sent;
}

bool GeminiLiveClient::sendActivityEnd() {
    if (!isConnected() || !_setupComplete) return false;
    const char* msg = "{\"realtimeInput\":{\"activityEnd\":{}}}";
    Serial.println("[LiveClient] Signalisation fin d'activite vocale (activityEnd)");
    return _transport->send(msg, strlen(msg));
}

bool GeminiLiveClient::sendTurnComplete() {
    if (!isConnected() || !_setupComplete) return false;

    const char* turnCompleteMsg = "{\"clientContent\":{\"turnComplete\":true}}";
    Serial.println("[LiveClient] Signalisation fin de tour (clientContent.turnComplete)");
    return _transport->send(turnCompleteMsg, strlen(turnCompleteMsg));
}

bool GeminiLiveClient::sendInterruption() {
    if (!isConnected()) return false;
    const char* intMsg = "{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[]}],\"turnComplete\":true}}";
    return _transport->send(intMsg, strlen(intMsg));
}

bool GeminiLiveClient::sendTextMessage(const String& text) {
    if (!isConnected() || !_setupComplete) return false;

    String msg = "{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"text\":\"";
    msg += escapeJsonString(text);
    msg += "\"}]}],\"turnComplete\":true}}";

    Serial.printf("[LiveClient] Envoi message texte clientContent: \"%s\"\n", text.c_str());
    return _transport->send(msg.c_str(), msg.length());
}

bool GeminiLiveClient::sendToolResponse(const String& callId, const String& funcName, const String& resultJson) {
    if (!isConnected()) return false;

    // Assurer la validité stricte du JSON produit
    String validJson = resultJson;
    validJson.trim();
    if (validJson.isEmpty()) {
        validJson = "{\"status\":\"ok\"}";
    } else {
        cJSON* testParsed = cJSON_Parse(validJson.c_str());
        if (!testParsed) {
            Serial.printf("[LiveClient] ERREUR : sendToolResponse recoit un JSON invalide: %s\n", validJson.c_str());
            return false;
        }
        cJSON_Delete(testParsed);
    }

    String resp = "{\"toolResponse\":{\"functionResponses\":[{\"name\":\"" + escapeJsonString(funcName) + "\",\"response\":{\"output\":";
    resp += validJson;
    resp += "},\"id\":\"" + escapeJsonString(callId) + "\"}]}}";
    Serial.printf("[LiveClient] Envoi toolResponse (id=%s, func=%s)\n", callId.c_str(), funcName.c_str());
    return _transport->send(resp.c_str(), resp.length());
}

bool GeminiLiveClient::sendToolError(const String& callId, const String& funcName, const String& errorMessage) {
    String errJson = "{\"error\":\"" + escapeJsonString(errorMessage) + "\"}";
    return sendToolResponse(callId, funcName, errJson);
}

// =============================================================================
// Base64 Encode / Decode — Conforme RFC 4648 (préservation exacte des octets)
// =============================================================================
static const char BASE64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String GeminiLiveClient::encodeBase64(const uint8_t* data, size_t length) {
    String ret;
    if (!data || length == 0) return ret;
    ret.reserve(((length + 2) / 3) * 4);

    size_t i = 0;
    while (i < length) {
        size_t rem = length - i;
        uint32_t octet_a = data[i++];
        uint32_t octet_b = (rem > 1) ? data[i++] : 0;
        uint32_t octet_c = (rem > 2) ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        ret += BASE64_CHARS[(triple >> 18) & 0x3F];
        ret += BASE64_CHARS[(triple >> 12) & 0x3F];
        ret += (rem > 1) ? BASE64_CHARS[(triple >> 6) & 0x3F] : '=';
        ret += (rem > 2) ? BASE64_CHARS[triple & 0x3F] : '=';
    }
    return ret;
}

static inline int base64CharValue(char c) {
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

    size_t i = 0;
    while (i < inLen) {
        while (i < inLen && (input[i] == ' ' || input[i] == '\r' || input[i] == '\n' || input[i] == '\t')) i++;
        if (i >= inLen) break;

        char c0 = input[i++];
        if (i >= inLen) return false;
        char c1 = input[i++];
        char c2 = (i < inLen) ? input[i++] : '=';
        char c3 = (i < inLen) ? input[i++] : '=';

        int v0 = base64CharValue(c0);
        int v1 = base64CharValue(c1);
        int v2 = (c2 == '=') ? 0 : base64CharValue(c2);
        int v3 = (c3 == '=') ? 0 : base64CharValue(c3);

        if (v0 < 0 || v1 < 0 || (c2 != '=' && v2 < 0) || (c3 != '=' && v3 < 0)) {
            return false;
        }

        uint32_t triple = (v0 << 18) | (v1 << 12) | (v2 << 6) | v3;

        output.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
        if (c2 != '=') output.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
        if (c3 != '=') output.push_back(static_cast<uint8_t>(triple & 0xFF));
    }
    return true;
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
    _turnCompleteBeforeLastPcm = false;
    _postTurnCompletePcmSamples = 0;
    _postTurnCompletePcmBlocks = 0;
    _turnAudioPartsCount = 0;
}

// =============================================================================
void GeminiLiveClient::handleIncomingMessage(const char* data, size_t len) {
    if (!data || len == 0) return;

    cJSON* root = cJSON_ParseWithLength(data, len);
    if (!root) {
        root = cJSON_Parse(data);
    }
    if (!root) {
        Serial.println("[LiveClient] Erreur parsing JSON recu");
        return;
    }

    // 1. Confirmation de configuration (setupComplete)
    cJSON* setupComplete = cJSON_GetObjectItemCaseSensitive(root, "setupComplete");
    if (setupComplete) {
        Serial.println("[LiveClient] Session Gemini Live initialisee avec succes (setupComplete)");
        _setupComplete.store(true, std::memory_order_release);
        if (_onSetupComplete) _onSetupComplete();
    }

    // 2. Contenu produit par le serveur (serverContent)
    cJSON* serverContent = cJSON_GetObjectItemCaseSensitive(root, "serverContent");
    if (serverContent) {
        // Interruption signalee par le serveur
        cJSON* interrupted = cJSON_GetObjectItemCaseSensitive(serverContent, "interrupted");
        if (cJSON_IsTrue(interrupted)) {
            Serial.println("[LiveClient] Intervention interrompue signalee par le serveur");
            if (_onInterrupted) _onInterrupted();
        }

        // Transcription vocale utilisateur finale (inputTranscription)
        cJSON* inputTrans = cJSON_GetObjectItemCaseSensitive(serverContent, "inputTranscription");
        if (inputTrans) {
            cJSON* textItem = cJSON_GetObjectItemCaseSensitive(inputTrans, "text");
            if (textItem && cJSON_IsString(textItem) && textItem->valuestring) {
                String text(textItem->valuestring);
                Serial.printf("[LiveClient] Transcription utilisateur finale : \"%s\"\n", text.c_str());
                if (_onInputTranscription) _onInputTranscription(text, true);
            }
        }

        // Transcription vocale utilisateur intérimaire (interimInputTranscription)
        cJSON* interimTrans = cJSON_GetObjectItemCaseSensitive(serverContent, "interimInputTranscription");
        if (interimTrans) {
            cJSON* textItem = cJSON_GetObjectItemCaseSensitive(interimTrans, "text");
            if (textItem && cJSON_IsString(textItem) && textItem->valuestring) {
                String text(textItem->valuestring);
                Serial.printf("[LiveClient] Transcription utilisateur interim : \"%s\"\n", text.c_str());
                if (_onInputTranscription) _onInputTranscription(text, false);
            }
        }

        // Transcription vocale de sortie produite par Gemini (outputTranscription)
        bool hasOutputTrans = false;
        cJSON* outputTrans = cJSON_GetObjectItemCaseSensitive(serverContent, "outputTranscription");
        if (outputTrans) {
            cJSON* textItem = cJSON_GetObjectItemCaseSensitive(outputTrans, "text");
            if (textItem && cJSON_IsString(textItem) && textItem->valuestring) {
                hasOutputTrans = true;
                String text(textItem->valuestring);
                if (_onOutputTranscription) _onOutputTranscription(text);
            }
        }

        // Tour du modèle (modelTurn) : audio natif & texte éventuel
        cJSON* modelTurn = cJSON_GetObjectItemCaseSensitive(serverContent, "modelTurn");
        if (modelTurn) {
            cJSON* parts = cJSON_GetObjectItemCaseSensitive(modelTurn, "parts");
            if (parts && cJSON_IsArray(parts)) {
                bool msgHadAudio = false;
                cJSON* part = nullptr;
                cJSON_ArrayForEach(part, parts) {
                    cJSON* text = cJSON_GetObjectItemCaseSensitive(part, "text");
                    if (text && cJSON_IsString(text) && text->valuestring) {
                        String txtVal(text->valuestring);
                        if (_onText) _onText(txtVal);
                        if (!hasOutputTrans && _onOutputTranscription) {
                            _onOutputTranscription(txtVal);
                        }
                    }

                    // Audio natif inlineData PCM 24 kHz (v0.2.1.3 - Correction A)
                    cJSON* inlineData = cJSON_GetObjectItemCaseSensitive(part, "inlineData");
                    if (inlineData) {
                        _turnAudioPartsCount++;
                        cJSON* b64Data = cJSON_GetObjectItemCaseSensitive(inlineData, "data");
                        if (b64Data && cJSON_IsString(b64Data) && b64Data->valuestring) {
                            const char* b64Str = b64Data->valuestring;
                            const size_t b64Len = strlen(b64Str);
                            std::vector<uint8_t> pcmBytes;
                            bool decodeOk = decodeBase64(b64Str, b64Len, pcmBytes);
                            if (!decodeOk) {
                                _b64DecodeFailedCount++;
                                AudioTraceLog::record(AudioTraceEventType::PCM_DECODE_FAILED, (uint32_t)b64Len);
                                Serial.printf("[LiveClient] ALERTE: Echec decodage Base64 audio (%u octets)\n", (unsigned)b64Len);
                            } else {
                                _b64BlocksDecodedCount++;
                                if (pcmBytes.size() < sizeof(int16_t)) {
                                    _pcmBlocksInvalidCount++;
                                    Serial.printf("[LiveClient] ALERTE: Bloc PCM invalide (< 2 octets: %u)\n", (unsigned)pcmBytes.size());
                                } else {
                                    if (!msgHadAudio) {
                                        msgHadAudio = true;
                                        _audioWsMessagesCount++;
                                        AudioTraceLog::record(AudioTraceEventType::WS_AUDIO_RECEIVED, (uint32_t)len);
                                    }
                                    const size_t sampleCount = pcmBytes.size() / sizeof(int16_t);
                                    _pcmBytesDecodedTotal += (uint32_t)pcmBytes.size();
                                    _pcmSamplesDispatchedTotal += (uint32_t)sampleCount;
                                    _lastPcmPacketTimeMs = millis();
                                    AudioTraceLog::record(AudioTraceEventType::PCM_DECODED, (uint32_t)sampleCount);

                                    if (_turnCompleteReceived) {
                                        _turnCompleteBeforeLastPcm = true;
                                        _postTurnCompletePcmSamples += (uint32_t)sampleCount;
                                        _postTurnCompletePcmBlocks++;
                                        Serial.printf("[LiveClient] ALERTE ORDRE: Bloc PCM recu APRES turnComplete (%u echantillons, bloc post-turn #%u)\n",
                                                      (unsigned)sampleCount, (unsigned)_postTurnCompletePcmBlocks);
                                    }

                                    const int16_t* pcm16 = reinterpret_cast<const int16_t*>(pcmBytes.data());
                                    if (_onAudioData) {
                                        _onAudioData(pcm16, sampleCount);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Fin de génération du tour
        cJSON* turnComplete = cJSON_GetObjectItemCaseSensitive(serverContent, "turnComplete");
        if (cJSON_IsTrue(turnComplete)) {
            _turnCompleteReceived = true;
            _turnCompleteTimeMs = millis();
            AudioTraceLog::record(AudioTraceEventType::TURN_COMPLETE_RECEIVED);
            Serial.println("[LiveClient] Fin de generation audio (turnComplete)");
            if (_onTurnComplete) _onTurnComplete();
        }
    }

    // 3. Appel de fonction structuré générique (toolCall / functionCalls)
    cJSON* toolCall = cJSON_GetObjectItemCaseSensitive(root, "toolCall");
    if (toolCall) {
        cJSON* functionCalls = cJSON_GetObjectItemCaseSensitive(toolCall, "functionCalls");
        if (functionCalls && cJSON_IsArray(functionCalls)) {
            cJSON* call = nullptr;
            cJSON_ArrayForEach(call, functionCalls) {
                cJSON* nameItem = cJSON_GetObjectItemCaseSensitive(call, "name");
                cJSON* idItem = cJSON_GetObjectItemCaseSensitive(call, "id");
                cJSON* argsItem = cJSON_GetObjectItemCaseSensitive(call, "args");
                if (nameItem && cJSON_IsString(nameItem) && idItem && cJSON_IsString(idItem)) {
                    String funcName = nameItem->valuestring;
                    String callId = idItem->valuestring;

                    // Déduplication bornée des identifiants récents (v0.2.0.1)
                    if (isDuplicateCall(callId)) {
                        Serial.printf("[LiveClient] Tool call duplique ignore (id=%s)\n", callId.c_str());
                        continue;
                    }
                    recordHandledCall(callId);

                    String argsJson = "{}";
                    String projectArg = "";
                    if (argsItem) {
                        char* printed = cJSON_PrintUnformatted(argsItem);
                        if (printed) {
                            argsJson = printed;
                            cJSON_free(printed);
                        }
                        cJSON* proj = cJSON_GetObjectItemCaseSensitive(argsItem, "project");
                        if (proj && cJSON_IsString(proj) && proj->valuestring) {
                            projectArg = proj->valuestring;
                        }
                    }
                    Serial.printf("[LiveClient] ToolCall reçu : %s (id=%s, args=%s)\n",
                                  funcName.c_str(), callId.c_str(), argsJson.c_str());

                    if (_onToolCallGeneric) {
                        ToolCall tc;
                        tc.callId = callId;
                        tc.functionName = funcName;
                        tc.argumentsJson = argsJson;
                        _onToolCallGeneric(tc);
                    }
                    if (_onToolCallLegacy) {
                        _onToolCallLegacy(callId, funcName, projectArg);
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}
