#include "core/GeminiLiveClient.h"
#include "core/AudioTraceLog.h"
#include "core/Utf8Utils.h"
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
#include "core/EspWebSocketTransport.h"
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

    if (isConnected() && isSetupComplete()) {
        Serial.println("[LiveClient] Connexion deja active et setup valide");
        return true;
    }

    _setupComplete.store(false, std::memory_order_release);
    _connected.store(false, std::memory_order_release);
    resetTurnRxMetrics();
    _connectAttempts++;
    _wsConnectStartTimeMs = millis();

    // Configuration des écouteurs de transport
    _transport->setHandlers(
        [this]() {
            _connected.store(true, std::memory_order_release);
            _wsConnectDurationMs = millis() - _wsConnectStartTimeMs;
            _setupStartTimeMs = millis();
            Serial.printf("[LiveClient] WebSocket TLS etabli en %u ms\n", (unsigned)_wsConnectDurationMs);
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
    uint32_t t0Us = micros();
    if (_transport) {
        _transport->disconnect();
    }
    _connected.store(false, std::memory_order_release);
    _setupComplete.store(false, std::memory_order_release);
    recordWsProcessDurationUs(micros() - t0Us);
}

void GeminiLiveClient::requestDisconnect() {
    uint32_t t0Us = micros();
    if (_transport) {
        _transport->requestDisconnect();
    }
    _connected.store(false, std::memory_order_release);
    _setupComplete.store(false, std::memory_order_release);
    recordWsProcessDurationUs(micros() - t0Us);
}

bool GeminiLiveClient::pollDisconnect() {
    uint32_t t0Us = micros();
    bool done = true;
    if (_transport) {
        done = _transport->pollDisconnect();
    }
    if (done) {
        _connected.store(false, std::memory_order_release);
        _setupComplete.store(false, std::memory_order_release);
    }
    recordWsProcessDurationUs(micros() - t0Us);
    return done;
}

void GeminiLiveClient::tick() {
    uint32_t t0Us = micros();
    if (_transport) {
        _transport->tick();
    }
    recordWsProcessDurationUs(micros() - t0Us);
}

bool GeminiLiveClient::isConnected() const {
    return _connected.load(std::memory_order_acquire) && _transport && _transport->isConnected();
}

void GeminiLiveClient::buildWebSocketUrl(char* buf, size_t bufSize) const {
    snprintf(buf, bufSize, "wss://%s%s?key=%s", 
             GEMINI_LIVE_HOST, GEMINI_LIVE_PATH, _apiKey.c_str());
}

String GeminiLiveClient::escapeJsonString(const String& s) {
    return Utf8Utils::escapeJson(s);
}

bool GeminiLiveClient::isModelSpeechLanguageCodeSupported(const String& /*model*/) {
    // Clarification formelle VoiceKit v0.4.1.1 :
    // Les modèles Gemini Multimodal Live actuellement configurés (ex: gemini-2.0-flash, gemini-3.8-live,
    // modèles bidirectionnels natifs) utilisent principalement les instructions système pour orienter
    // la langue et rejettent le champ speechConfig.languageCode dans le handshake Bidi (INVALID_ARGUMENT).
    // Afin de ne pas annoncer une détection automatique de compatibilité non implémentée,
    // de ne pas introduire de champ speechConfig.languageCode non validé pour le modèle actif,
    // et de conserver une compatibilité absolue avec les modèles déjà utilisés, cette fonction
    // retourne systématiquement false.
    return false;
}

void GeminiLiveClient::sendSetupMessage() {
    // Configuration setup Gemini Live selon protocole BidiGenerateContent
    // VAD manuelle + transcriptions + instructions et outils configurables
    String effectiveModel = _model;
    if (!effectiveModel.isEmpty() && !effectiveModel.startsWith("models/")) {
        effectiveModel = "models/" + effectiveModel;
    }
    String setupJson = "{";
    setupJson += "\"setup\":{";
    setupJson += "\"model\":\"" + escapeJsonString(effectiveModel) + "\",";
    setupJson += "\"generationConfig\":{";
    setupJson += "\"responseModalities\":[\"AUDIO\"],";
    setupJson += "\"speechConfig\":{";
    setupJson += "\"voiceConfig\":{";
    setupJson += "\"prebuiltVoiceConfig\":{\"voiceName\":\"" + escapeJsonString(_voice) + "\"}";
    setupJson += "}";
    if (!_language.isEmpty() && isModelSpeechLanguageCodeSupported(_model)) {
        setupJson += ",\"languageCode\":\"" + escapeJsonString(_language) + "\"";
    }
    setupJson += "}";
    setupJson += "},";

    // Instructions système configurées ou repli par défaut
    String sysInst = _systemInstruction;
    if (sysInst.isEmpty()) {
        sysInst = "Tu es VoxCore, un assistant vocal bienveillant et concis. Reponds toujours en francais direct (1 ou 2 phrases max).";
    }
    String effectiveInst = sysInst;
    if (!_voiceDirectives.isEmpty() && sysInst.indexOf("[Directives vocales]") < 0) {
        effectiveInst += " [Directives vocales] " + _voiceDirectives;
    }
    setupJson += "\"systemInstruction\":{";
    setupJson += "\"parts\":[{";
    setupJson += "\"text\":\"" + escapeJsonString(effectiveInst) + "\"";
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

    Serial.printf("[LiveClient] Envoi setup Gemini Live (modele=%s, voix=%s, VAD manuelle, transcriptions, %u outil(s) declare(s))...\n",
                  effectiveModel.c_str(), _voice.c_str(), (unsigned)_tools.size());
    _transport->send(setupJson.c_str(), setupJson.length());
}

bool GeminiLiveClient::sendActivityStart() {
    if (!isConnected() || !_setupComplete) return false;
    resetTurnRxMetrics();
    _waitingForResponse.store(false, std::memory_order_release);
    const char* msg = "{\"realtimeInput\":{\"activityStart\":{}}}";
    Serial.println("[LiveClient] Signalisation debut d'activite vocale (activityStart)");
    return _transport->send(msg, strlen(msg));
}

bool GeminiLiveClient::sendAudioChunk(const int16_t* samples, size_t sampleCount) {
    if (!isConnected() || !_setupComplete || !samples || sampleCount == 0) {
        _sendRefusedCount++;
        return false;
    }

    const uint8_t* bytePtr = reinterpret_cast<const uint8_t*>(samples);
    const size_t byteCount = sampleCount * sizeof(int16_t);
    const size_t b64Len = ((byteCount + 2) / 3) * 4;

    static const char prefix[] = "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"";
    static const size_t prefixLen = sizeof(prefix) - 1;
    static const char suffix[] = "\"}}}";
    static const size_t suffixLen = sizeof(suffix) - 1;
    const size_t totalLen = prefixLen + b64Len + suffixLen;

    uint32_t t0 = micros();
    bool sent = false;

    // Pour blocs usuels (512 éch. = 1024 octets -> 1444 octets JSON), formater directement
    // sur la pile sans aucune allocation dynamique ni copie intermédiaire (v0.5.3)
    if (totalLen < 2048) {
        char msgBuf[2048];
        memcpy(msgBuf, prefix, prefixLen);
        encodeBase64Direct(bytePtr, byteCount, msgBuf + prefixLen);
        memcpy(msgBuf + prefixLen + b64Len, suffix, suffixLen);
        msgBuf[totalLen] = '\0';
        sent = _transport->send(msgBuf, totalLen);
    } else {
        std::vector<char> msgBuf(totalLen + 1);
        memcpy(msgBuf.data(), prefix, prefixLen);
        encodeBase64Direct(bytePtr, byteCount, msgBuf.data() + prefixLen);
        memcpy(msgBuf.data() + prefixLen + b64Len, suffix, suffixLen);
        msgBuf[totalLen] = '\0';
        sent = _transport->send(msgBuf.data(), totalLen);
    }

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
    uint32_t nowMs = millis();
    if (nowMs == 0) nowMs = 1;
    _lastActivityEndSentMs.store(nowMs, std::memory_order_relaxed);
    _firstResponseReceivedMs.store(0, std::memory_order_relaxed);
    _firstAudioReceivedMs.store(0, std::memory_order_relaxed);
    _waitingForResponse.store(true, std::memory_order_release);
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
    _waitingForResponse.store(false, std::memory_order_release);
    const char* intMsg = "{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[]}],\"turnComplete\":true}}";
    return _transport->send(intMsg, strlen(intMsg), 150);
}

bool GeminiLiveClient::sendTextMessage(const String& text) {
    if (!isConnected() || !_setupComplete) return false;
    resetTurnRxMetrics();
    uint32_t nowMs = millis();
    if (nowMs == 0) nowMs = 1;
    _lastActivityEndSentMs.store(nowMs, std::memory_order_relaxed);
    _firstResponseReceivedMs.store(0, std::memory_order_relaxed);
    _firstAudioReceivedMs.store(0, std::memory_order_relaxed);
    _waitingForResponse.store(true, std::memory_order_release);

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

size_t GeminiLiveClient::encodeBase64Direct(const uint8_t* data, size_t length, char* outBuf) {
    if (!data || length == 0 || !outBuf) return 0;
    size_t i = 0;
    size_t outPos = 0;
    while (i < length) {
        size_t rem = length - i;
        uint32_t octet_a = data[i++];
        uint32_t octet_b = (rem > 1) ? data[i++] : 0;
        uint32_t octet_c = (rem > 2) ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        outBuf[outPos++] = BASE64_CHARS[(triple >> 18) & 0x3F];
        outBuf[outPos++] = BASE64_CHARS[(triple >> 12) & 0x3F];
        outBuf[outPos++] = (rem > 1) ? BASE64_CHARS[(triple >> 6) & 0x3F] : '=';
        outBuf[outPos++] = (rem > 2) ? BASE64_CHARS[triple & 0x3F] : '=';
    }
    outBuf[outPos] = '\0';
    return outPos;
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

    size_t maxOut = (inLen / 4) * 3;
    output.resize(maxOut);
    uint8_t* outPtr = output.data();
    size_t outPos = 0;

    size_t i = 0;
    while (i < inLen) {
        while (i < inLen && (input[i] == ' ' || input[i] == '\r' || input[i] == '\n' || input[i] == '\t')) i++;
        if (i >= inLen) break;

        char c0 = input[i++];
        if (i >= inLen) { output.clear(); return false; }
        char c1 = input[i++];
        char c2 = (i < inLen) ? input[i++] : '=';
        char c3 = (i < inLen) ? input[i++] : '=';

        int v0 = base64CharValue(c0);
        int v1 = base64CharValue(c1);
        int v2 = (c2 == '=') ? 0 : base64CharValue(c2);
        int v3 = (c3 == '=') ? 0 : base64CharValue(c3);

        if (v0 < 0 || v1 < 0 || (c2 != '=' && v2 < 0) || (c3 != '=' && v3 < 0)) {
            output.clear();
            return false;
        }

        uint32_t triple = (v0 << 18) | (v1 << 12) | (v2 << 6) | v3;

        outPtr[outPos++] = static_cast<uint8_t>((triple >> 16) & 0xFF);
        if (c2 != '=') outPtr[outPos++] = static_cast<uint8_t>((triple >> 8) & 0xFF);
        if (c3 != '=') outPtr[outPos++] = static_cast<uint8_t>(triple & 0xFF);
    }
    output.resize(outPos);
    return true;
}

void GeminiLiveClient::resetTurnRxMetrics() {
    _audioWsMessagesCount.store(0, std::memory_order_relaxed);
    _b64BlocksDecodedCount.store(0, std::memory_order_relaxed);
    _b64DecodeFailedCount.store(0, std::memory_order_relaxed);
    _pcmBlocksInvalidCount.store(0, std::memory_order_relaxed);
    _pcmBytesDecodedTotal.store(0, std::memory_order_relaxed);
    _pcmSamplesDispatchedTotal.store(0, std::memory_order_relaxed);
    _lastPcmPacketTimeMs.store(0, std::memory_order_relaxed);
    _turnCompleteTimeMs.store(0, std::memory_order_relaxed);
    _turnCompleteReceived.store(false, std::memory_order_release);
    _turnCompleteBeforeLastPcm.store(false, std::memory_order_release);
    _postTurnCompletePcmSamples.store(0, std::memory_order_relaxed);
    _postTurnCompletePcmBlocks.store(0, std::memory_order_relaxed);
    _turnAudioPartsCount.store(0, std::memory_order_relaxed);
    _firstResponseReceivedMs.store(0, std::memory_order_relaxed);
    _firstAudioReceivedMs.store(0, std::memory_order_relaxed);
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
        _setupCompleteDurationMs = millis() - _setupStartTimeMs;
        Serial.printf("[LiveClient] Session Gemini Live initialisee avec succes (setupComplete en %u ms)\n",
                      (unsigned)_setupCompleteDurationMs);
        _setupComplete.store(true, std::memory_order_release);
        if (_onSetupComplete) _onSetupComplete();
    }

    // 2. Contenu produit par le serveur (serverContent)
    cJSON* serverContent = cJSON_GetObjectItemCaseSensitive(root, "serverContent");
    if (serverContent) {
        if (_waitingForResponse.load(std::memory_order_acquire) && _firstResponseReceivedMs.load(std::memory_order_relaxed) == 0) {
            uint32_t nowResp = millis();
            if (nowResp == 0) nowResp = 1;
            _firstResponseReceivedMs.store(nowResp, std::memory_order_relaxed);
        }

        // Interruption signalee par le serveur
        cJSON* interrupted = cJSON_GetObjectItemCaseSensitive(serverContent, "interrupted");
        if (cJSON_IsTrue(interrupted)) {
            Serial.println("[LiveClient] Intervention interrompue signalee par le serveur");
            _waitingForResponse.store(false, std::memory_order_release);
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
        if (outputTrans && _waitingForResponse.load(std::memory_order_acquire)) {
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

                    // Audio natif inlineData PCM 24 kHz (v0.2.1.3 / v0.6.1 - Correction A)
                    cJSON* inlineData = cJSON_GetObjectItemCaseSensitive(part, "inlineData");
                    if (inlineData) {
                        _turnAudioPartsCount.fetch_add(1, std::memory_order_relaxed);
                        cJSON* b64Data = cJSON_GetObjectItemCaseSensitive(inlineData, "data");
                        if (b64Data && cJSON_IsString(b64Data) && b64Data->valuestring) {
                            const char* b64Str = b64Data->valuestring;
                            const size_t b64Len = strlen(b64Str);
                            std::vector<uint8_t> pcmBytes;
                            bool decodeOk = decodeBase64(b64Str, b64Len, pcmBytes);
                            if (!decodeOk) {
                                _b64DecodeFailedCount.fetch_add(1, std::memory_order_relaxed);
                                AudioTraceLog::record(AudioTraceEventType::PCM_DECODE_FAILED, (uint32_t)b64Len);
                                Serial.printf("[LiveClient] ALERTE: Echec decodage Base64 audio (%u octets)\n", (unsigned)b64Len);
                            } else {
                                _b64BlocksDecodedCount.fetch_add(1, std::memory_order_relaxed);
                                if (pcmBytes.size() < sizeof(int16_t)) {
                                    _pcmBlocksInvalidCount.fetch_add(1, std::memory_order_relaxed);
                                    Serial.printf("[LiveClient] ALERTE: Bloc PCM invalide (< 2 octets: %u)\n", (unsigned)pcmBytes.size());
                                } else {
                                    if (!msgHadAudio) {
                                        msgHadAudio = true;
                                        _audioWsMessagesCount.fetch_add(1, std::memory_order_relaxed);
                                        AudioTraceLog::record(AudioTraceEventType::WS_AUDIO_RECEIVED, (uint32_t)len);
                                    }
                                    const size_t sampleCount = pcmBytes.size() / sizeof(int16_t);
                                    _pcmBytesDecodedTotal.fetch_add((uint32_t)pcmBytes.size(), std::memory_order_relaxed);
                                    _pcmSamplesDispatchedTotal.fetch_add((uint32_t)sampleCount, std::memory_order_relaxed);
                                    uint32_t pktMs = millis();
                                    _lastPcmPacketTimeMs.store(pktMs, std::memory_order_relaxed);
                                    if (_waitingForResponse.load(std::memory_order_acquire) && _firstAudioReceivedMs.load(std::memory_order_relaxed) == 0) {
                                        uint32_t firstAudMs = (pktMs == 0) ? 1 : pktMs;
                                        _firstAudioReceivedMs.store(firstAudMs, std::memory_order_relaxed);
                                    }
                                    AudioTraceLog::record(AudioTraceEventType::PCM_DECODED, (uint32_t)sampleCount);

                                    if (_turnCompleteReceived.load(std::memory_order_acquire)) {
                                        _turnCompleteBeforeLastPcm.store(true, std::memory_order_release);
                                        _postTurnCompletePcmSamples.fetch_add((uint32_t)sampleCount, std::memory_order_relaxed);
                                        uint32_t postBlk = _postTurnCompletePcmBlocks.fetch_add(1, std::memory_order_relaxed) + 1;
                                        Serial.printf("[LiveClient] ALERTE ORDRE: Bloc PCM recu APRES turnComplete (%u echantillons, bloc post-turn #%u)\n",
                                                      (unsigned)sampleCount, (unsigned)postBlk);
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
            _turnCompleteReceived.store(true, std::memory_order_release);
            _turnCompleteTimeMs.store(millis(), std::memory_order_relaxed);
            _waitingForResponse.store(false, std::memory_order_release);
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
