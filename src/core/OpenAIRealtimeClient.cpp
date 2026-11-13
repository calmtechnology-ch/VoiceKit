#include "OpenAIRealtimeClient.h"
#include "OpenAIRoots.h"
#include "AudioResampler.h"
#include <cstring>
#include <vector>
#include <cJSON.h>

#if defined(ESP32) || defined(ARDUINO)
#include "core/EspWebSocketTransport.h"
#endif

// Base64 helper chars
static const char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

OpenAIRealtimeClient::OpenAIRealtimeClient(ILiveTransport* customTransport)
    : _transport(customTransport) {
    _resampleBuffer.resize(1024);
}

OpenAIRealtimeClient::~OpenAIRealtimeClient() {
    disconnect();
}

String OpenAIRealtimeClient::escapeJsonString(const String& s) {
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

size_t OpenAIRealtimeClient::encodeBase64Direct(const uint8_t* data, size_t length, char* outBuf) {
    if (!data || length == 0 || !outBuf) return 0;
    size_t i = 0;
    size_t outPos = 0;
    while (i < length) {
        size_t rem = length - i;
        uint32_t octet_a = data[i++];
        uint32_t octet_b = (rem > 1) ? data[i++] : 0;
        uint32_t octet_c = (rem > 2) ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        outBuf[outPos++] = B64_CHARS[(triple >> 18) & 0x3F];
        outBuf[outPos++] = B64_CHARS[(triple >> 12) & 0x3F];
        outBuf[outPos++] = (rem > 1) ? B64_CHARS[(triple >> 6) & 0x3F] : '=';
        outBuf[outPos++] = (rem > 2) ? B64_CHARS[triple & 0x3F] : '=';
    }
    outBuf[outPos] = '\0';
    return outPos;
}

static inline int b64CharVal(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool OpenAIRealtimeClient::decodeBase64(const char* input, size_t inLen, std::vector<uint8_t>& output) {
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

        int v0 = b64CharVal(c0);
        int v1 = b64CharVal(c1);
        int v2 = (c2 == '=') ? 0 : b64CharVal(c2);
        int v3 = (c3 == '=') ? 0 : b64CharVal(c3);

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

bool OpenAIRealtimeClient::begin(const VoiceKitConfig& config) {
    _apiKey = config.getApiKeyForProvider(VoiceProviderType::OPENAI);
    if (_apiKey.isEmpty()) {
        _apiKey = config.apiKey;
        _apiKey.trim();
    }

    if (_apiKey.isEmpty()) {
        _lastError = "Cle API OpenAI manquante (config.apiKey / config.openAiApiKey est vide)";
        Serial.printf("[OpenAIClient] ERREUR : %s\n", _lastError.c_str());
        return false;
    }

    // Sélection du modèle Realtime : si non spécifié ou si valeur par défaut Gemini, repli vers gpt-realtime-2.1-mini
    if (config.model.isEmpty() || config.model.indexOf("gemini") >= 0) {
        _model = OPENAI_REALTIME_DEFAULT_MODEL;
    } else {
        _model = config.model;
    }

    // Sélection de la voix Realtime : si non spécifiée ou voix Gemini par défaut, repli vers alloy
    if (config.voice.isEmpty() || config.voice == VOICEKIT_DEFAULT_VOICE) {
        _voice = OPENAI_REALTIME_DEFAULT_VOICE;
    } else {
        _voice = config.voice;
    }

    _language = config.language;
    _inputTranscriptionLanguage = config.inputTranscriptionLanguage;
    _conversationMode = config.enableConversation;
    _voiceDirectives = config.buildVoiceDirectives();

    String baseInst = _conversationMode ? config.resolveConversationInstruction() : config.resolveSystemInstruction();
    _systemInstruction = config.buildEffectiveInstruction(baseInst);

    Serial.printf("[OpenAIClient] Initialise : Modele=%s | Voix=%s | Langue=%s | Mode=%s (Cle masquee)\n",
                  _model.c_str(), _voice.c_str(), _language.c_str(),
                  _conversationMode ? "CONVERSATION" : "TTS/STT");

    return true;
}

void OpenAIRealtimeClient::buildWebSocketUrl(char* buf, size_t bufSize) const {
    snprintf(buf, bufSize, "wss://api.openai.com/v1/realtime?model=%s", _model.c_str());
}

void OpenAIRealtimeClient::resetTurnRxMetrics() {
    _pcmSamplesDispatchedTotal.store(0, std::memory_order_relaxed);
    _turnCompleteReceived.store(false, std::memory_order_release);
    _turnCompleteBeforeLastPcm.store(false, std::memory_order_release);
    _postTurnCompletePcmSamples.store(0, std::memory_order_relaxed);
    _postTurnCompletePcmBlocks.store(0, std::memory_order_relaxed);
    _firstResponseReceivedMs.store(0, std::memory_order_relaxed);
    _firstAudioReceivedMs.store(0, std::memory_order_relaxed);
    _lastDoneTranscript = "";
}

bool OpenAIRealtimeClient::connect() {
    if (_transport && _transport->isConnected()) {
        // Fermeture silencieuse d'une éventuelle session résiduelle sans déclencher un événement
        // de déconnexion parasite sur la nouvelle tentative en cours d'activation.
        _transport->setHandlers(nullptr, nullptr, nullptr, nullptr);
        _transport->disconnect();
    }
    _connected.store(false, std::memory_order_release);
    _setupComplete.store(false, std::memory_order_release);
    resetTurnRxMetrics();
    _audioEmittedInSession.store(false, std::memory_order_release);
    _confirmedSessionVoice = "";
    _connectAttempts++;
    _lastError = "";
    _wsConnectStartTimeMs = millis();
    _wsConnectDurationMs = 0;
    _setupStartTimeMs = 0;
    _setupCompleteDurationMs = 0;
    _waitingForResponse.store(false, std::memory_order_release);

    if (!_transport) {
#if defined(ESP32) || defined(ARDUINO)
        _defaultTransport = std::unique_ptr<ILiveTransport>(new EspWebSocketTransport());
        _transport = _defaultTransport.get();
#else
        _lastError = "Aucun transport ILiveTransport injecte (environnement natif)";
        Serial.printf("[OpenAIClient] ERREUR : %s\n", _lastError.c_str());
        return false;
#endif
    }

    // Configuration des gestionnaires d'événements de transport
    _transport->setHandlers(
        [this]() {
            _connected.store(true, std::memory_order_release);
            _wsConnectDurationMs = millis() - _wsConnectStartTimeMs;
            Serial.printf("[OpenAIClient] WebSocket TLS connecte en %u ms (attente session.created)\n",
                          (unsigned)_wsConnectDurationMs);
            if (_onConnected) _onConnected();
        },
        [this]() {
            _connected.store(false, std::memory_order_release);
            _setupComplete.store(false, std::memory_order_release);
            Serial.println("[OpenAIClient] WebSocket deconnecte");
            if (_onDisconnected) _onDisconnected();
        },
        [this](const char* data, size_t len) {
            handleIncomingMessage(data, len);
        },
        [this](const char* err) {
            _lastError = err ? err : "Erreur transport WebSocket inconnu";
            Serial.printf("[OpenAIClient] Erreur transport : %s\n", _lastError.c_str());
            if (_onError) _onError(_lastError);
        }
    );

    char urlBuf[192];
    buildWebSocketUrl(urlBuf, sizeof(urlBuf));

    // En-têtes HTTP de négociation WebSocket TLS conformes à la spécification officielle GA OpenAI Realtime
    // NOTE DE SÉCURITÉ : La clé API n'est jamais affichée dans les logs.
    String headers = "Authorization: Bearer " + _apiKey + "\r\n";

    Serial.printf("[OpenAIClient] Tentative connexion TLS a %s (Authorization Bearer masque)...\n", urlBuf);
    bool ok = _transport->connect(urlBuf, OPENAI_ROOTS, headers.c_str());
    if (!ok) {
        _lastError = "Echec demarrage transport WebSocket TLS";
        Serial.printf("[OpenAIClient] ERREUR : %s\n", _lastError.c_str());
        if (_onError) _onError(_lastError);
        return false;
    }
    return true;
}

void OpenAIRealtimeClient::disconnect() {
    uint32_t t0Us = micros();
    if (_transport) {
        _transport->disconnect();
    }
    _connected.store(false, std::memory_order_release);
    _setupComplete.store(false, std::memory_order_release);
    _waitingForResponse.store(false, std::memory_order_release);
    _audioEmittedInSession.store(false, std::memory_order_release);
    _confirmedSessionVoice = "";
    recordWsProcessDurationUs(micros() - t0Us);
}

void OpenAIRealtimeClient::requestDisconnect() {
    uint32_t t0Us = micros();
    if (_transport) {
        _transport->requestDisconnect();
    }
    _connected.store(false, std::memory_order_release);
    _setupComplete.store(false, std::memory_order_release);
    _waitingForResponse.store(false, std::memory_order_release);
    _audioEmittedInSession.store(false, std::memory_order_release);
    _confirmedSessionVoice = "";
    recordWsProcessDurationUs(micros() - t0Us);
}

bool OpenAIRealtimeClient::pollDisconnect() {
    uint32_t t0Us = micros();
    bool done = true;
    if (_transport) {
        done = _transport->pollDisconnect();
    }
    if (done) {
        _connected.store(false, std::memory_order_release);
        _setupComplete.store(false, std::memory_order_release);
        _waitingForResponse.store(false, std::memory_order_release);
        _audioEmittedInSession.store(false, std::memory_order_release);
        _confirmedSessionVoice = "";
    }
    recordWsProcessDurationUs(micros() - t0Us);
    return done;
}

void OpenAIRealtimeClient::tick() {
    uint32_t t0Us = micros();
    if (_transport) {
        _transport->tick();
    }
    recordWsProcessDurationUs(micros() - t0Us);
}

bool OpenAIRealtimeClient::isConnected() const {
    return _connected.load(std::memory_order_acquire);
}

void OpenAIRealtimeClient::setTransport(ILiveTransport* transport) {
    if (_transport != transport) {
        disconnect();
        _defaultTransport.reset();
        _transport = transport;
    }
}

bool OpenAIRealtimeClient::requestSessionUpdate() {
    if (!isConnected()) {
        _lastError = "Session OpenAI non connectee pour session.update";
        return false;
    }
    if (!canUpdateVoiceInCurrentSession(_voice)) {
        _lastError = "Modification de voix interdite sur une session OpenAI ayant deja emis de l'audio (reconnexion requise)";
        Serial.printf("[OpenAIClient] %s\n", _lastError.c_str());
        return false;
    }
    _setupComplete.store(false, std::memory_order_release);
    sendSessionUpdate();
    return true;
}

void OpenAIRealtimeClient::sendSessionUpdate() {
    if (!isConnected()) return;

    _setupStartTimeMs = millis();

    // 1. Construction cohérente de l'instruction système effective + directives vocales
    String effectiveInst = _systemInstruction;
    effectiveInst.trim();
    if (!_voiceDirectives.isEmpty() && effectiveInst.indexOf("[Directives vocales]") < 0) {
        if (!effectiveInst.isEmpty()) {
            effectiveInst += " [Directives vocales] " + _voiceDirectives;
        } else {
            effectiveInst = _voiceDirectives;
        }
    }

    // 2. Séparation stricte : langue de transcription d'entrée (ISO-639-1 whisper-1) vs langue de synthèse
    String langHint = "";
    String customInputLang = _inputTranscriptionLanguage;
    customInputLang.trim();
    if (customInputLang.equalsIgnoreCase("auto")) {
        langHint = ""; // Autodétection Whisper explicite
    } else if (!customInputLang.isEmpty()) {
        langHint = VoiceKitConfig::extractPrimaryIso2(customInputLang);
    } else {
        langHint = VoiceKitConfig::extractPrimaryIso2(_language);
    }

    // 3. Protection protocole OpenAI Realtime : ne jamais transmettre une nouvelle voix
    //    dans session.update si de l'audio a déjà été produit sur la session active.
    String voiceForPayload = _voice;
    if (_audioEmittedInSession.load(std::memory_order_acquire) && !_confirmedSessionVoice.isEmpty()) {
        voiceForPayload = _confirmedSessionVoice;
    }

    String payload = "{";
    payload += "\"type\":\"session.update\",";
    payload += "\"session\":{";
    payload += "\"type\":\"realtime\",";
    payload += "\"instructions\":\"" + escapeJsonString(effectiveInst) + "\",";
    payload += "\"audio\":{";
    payload += "\"input\":{";
    payload += "\"turn_detection\":null,";
    payload += "\"transcription\":{";
    payload += "\"model\":\"whisper-1\"";
    if (!langHint.isEmpty()) {
        payload += ",\"language\":\"" + escapeJsonString(langHint) + "\"";
    }
    payload += "}";
    payload += "},";
    payload += "\"output\":{";
    payload += "\"voice\":\"" + escapeJsonString(voiceForPayload) + "\"";
    payload += "}";
    payload += "}";
    payload += "}";
    payload += "}";

    Serial.printf("[OpenAIClient] Envoi session.update GA (Voix=%s, TranscrISO=%s, turn_detection=null)...\n",
                  voiceForPayload.c_str(), langHint.isEmpty() ? "auto" : langHint.c_str());
    _transport->send(payload.c_str(), payload.length());
}

bool OpenAIRealtimeClient::sendTextMessage(const String& text) {
    if (!isConnected() || !isSetupComplete()) {
        _lastError = "Client OpenAI non pret pour sendTextMessage";
        return false;
    }

    resetTurnRxMetrics();
    uint32_t nowMs = millis();
    if (nowMs == 0) nowMs = 1;
    _lastActivityEndSentMs.store(nowMs, std::memory_order_relaxed);
    _firstResponseReceivedMs.store(0, std::memory_order_relaxed);
    _firstAudioReceivedMs.store(0, std::memory_order_relaxed);
    _waitingForResponse.store(true, std::memory_order_release);

    // 1. Injection du message textuel utilisateur dans l'historique conversationnel
    String itemMsg = "{";
    itemMsg += "\"type\":\"conversation.item.create\",";
    itemMsg += "\"item\":{";
    itemMsg += "\"type\":\"message\",";
    itemMsg += "\"role\":\"user\",";
    itemMsg += "\"content\":[{";
    itemMsg += "\"type\":\"input_text\",";
    itemMsg += "\"text\":\"" + escapeJsonString(text) + "\"";
    itemMsg += "}]";
    itemMsg += "}";
    itemMsg += "}";

    _transport->send(itemMsg.c_str(), itemMsg.length());

    // 2. Déclenchement explicite de la génération de réponse
    const char* respCreate = "{\"type\":\"response.create\"}";
    Serial.printf("[OpenAIClient] Envoi message texte et response.create: \"%s\"\n", text.c_str());
    return _transport->send(respCreate, strlen(respCreate));
}

bool OpenAIRealtimeClient::sendActivityStart() {
    if (!isConnected() || !isSetupComplete()) return false;
    // Réinitialise les métriques RX du tour et l'état d'attente
    resetTurnRxMetrics();
    _waitingForResponse.store(false, std::memory_order_release);
    return true;
}

bool OpenAIRealtimeClient::sendAudioChunk(const int16_t* samples, size_t sampleCount) {
    if (!isConnected() || !isSetupComplete() || !samples || sampleCount == 0) {
        return false;
    }

    // Rééchantillonnage 16 kHz (micro CoreS3) vers 24 kHz (OpenAI Realtime PCM16)
    // 512 échantillons 16 kHz -> 768 échantillons 24 kHz (1536 octets)
    const size_t outSamples = (sampleCount * 3) / 2;
    if (_resampleBuffer.size() < outSamples) {
        _resampleBuffer.resize(outSamples + 128);
    }

    size_t converted = AudioResampler::resample16to24(samples, sampleCount,
                                                      _resampleBuffer.data(), _resampleBuffer.size());
    if (converted == 0) {
        return false;
    }

    const uint8_t* bytePtr = reinterpret_cast<const uint8_t*>(_resampleBuffer.data());
    const size_t byteCount = converted * sizeof(int16_t);
    const size_t b64Len = ((byteCount + 2) / 3) * 4;

    static const char prefix[] = "{\"type\":\"input_audio_buffer.append\",\"audio\":\"";
    static const size_t prefixLen = sizeof(prefix) - 1;
    static const char suffix[] = "\"}";
    static const size_t suffixLen = sizeof(suffix) - 1;
    const size_t totalLen = prefixLen + b64Len + suffixLen;

    // Buffer d'émission sur pile ou dynamique borné
    if (totalLen < 3072) {
        char msgBuf[3072];
        memcpy(msgBuf, prefix, prefixLen);
        encodeBase64Direct(bytePtr, byteCount, msgBuf + prefixLen);
        memcpy(msgBuf + prefixLen + b64Len, suffix, suffixLen);
        msgBuf[totalLen] = '\0';
        return _transport->send(msgBuf, totalLen);
    } else {
        std::vector<char> msgBuf(totalLen + 1);
        memcpy(msgBuf.data(), prefix, prefixLen);
        encodeBase64Direct(bytePtr, byteCount, msgBuf.data() + prefixLen);
        memcpy(msgBuf.data() + prefixLen + b64Len, suffix, suffixLen);
        msgBuf[totalLen] = '\0';
        return _transport->send(msgBuf.data(), totalLen);
    }
}

bool OpenAIRealtimeClient::sendActivityEnd() {
    if (!isConnected() || !isSetupComplete()) return false;

    uint32_t nowMs = millis();
    if (nowMs == 0) nowMs = 1;
    _lastActivityEndSentMs.store(nowMs, std::memory_order_relaxed);

    // 1. Validation de la fin du tampon audio utilisateur
    const char* commitMsg = "{\"type\":\"input_audio_buffer.commit\"}";
    _transport->send(commitMsg, strlen(commitMsg));

    // 2. Si le mode conversationnel est actif, demander au modèle de répondre oralement
    if (_conversationMode) {
        _firstResponseReceivedMs.store(0, std::memory_order_relaxed);
        _firstAudioReceivedMs.store(0, std::memory_order_relaxed);
        _waitingForResponse.store(true, std::memory_order_release);
        const char* respMsg = "{\"type\":\"response.create\"}";
        Serial.println("[OpenAIClient] Fin audio detectee -> input_audio_buffer.commit + response.create");
        return _transport->send(respMsg, strlen(respMsg));
    } else {
        Serial.println("[OpenAIClient] Fin audio detectee -> input_audio_buffer.commit (Mode STT pur)");
        return true;
    }
}

bool OpenAIRealtimeClient::sendTurnComplete() {
    if (!isConnected() || !isSetupComplete()) return false;
    if (_conversationMode && !_waitingForResponse.load(std::memory_order_acquire)) {
        _waitingForResponse.store(true, std::memory_order_release);
        const char* respMsg = "{\"type\":\"response.create\"}";
        return _transport->send(respMsg, strlen(respMsg));
    }
    return true;
}

bool OpenAIRealtimeClient::isResponseCancelled(const char* respId) const {
    if (!respId || strlen(respId) == 0) return false;
    if (_cancelledResponseId.length() > 0 && _cancelledResponseId == respId) {
        return true;
    }
    for (const auto& id : _cancelledResponseIds) {
        if (id.length() > 0 && id == respId) {
            return true;
        }
    }
    return false;
}

void OpenAIRealtimeClient::addCancelledResponseId(const String& respId) {
    if (respId.length() == 0) return;
    _cancelledResponseId = respId;
    for (const auto& id : _cancelledResponseIds) {
        if (id == respId) return;
    }
    _cancelledResponseIds.push_back(respId);
    if (_cancelledResponseIds.size() > 16) {
        _cancelledResponseIds.erase(_cancelledResponseIds.begin());
    }
}

bool OpenAIRealtimeClient::sendInterruption() {
    if (!isConnected()) return false;

    _waitingForResponse.store(false, std::memory_order_release);
    _lastDoneTranscript = "";
    if (_currentResponseId.length() > 0) {
        addCancelledResponseId(_currentResponseId);
        _currentResponseId = "";
    }
    Serial.println("[OpenAIClient] Interruption STOP -> response.cancel et input_audio_buffer.clear");

    // Annule la réponse en cours de génération / streaming
    const char* cancelMsg = "{\"type\":\"response.cancel\"}";
    _transport->send(cancelMsg, strlen(cancelMsg));

    // Vide le buffer audio d'entrée
    const char* clearMsg = "{\"type\":\"input_audio_buffer.clear\"}";
    _transport->send(clearMsg, strlen(clearMsg));

    if (_onInterrupted) {
        _onInterrupted();
    }

    return true;
}

void OpenAIRealtimeClient::handleIncomingMessage(const char* data, size_t len) {
    if (!data || len == 0) {
        _lastParseErrorCategory = "EMPTY_DATA";
        return;
    }

    // Ignorer silencieusement les trames vides ou constituees exclusivement d'espaces / CRLF
    size_t firstNonWs = 0;
    while (firstNonWs < len && (data[firstNonWs] == ' ' || data[firstNonWs] == '\t' || data[firstNonWs] == '\r' || data[firstNonWs] == '\n')) {
        firstNonWs++;
    }
    if (firstNonWs == len) {
        return;
    }

    cJSON* root = cJSON_ParseWithLength(data, len);
    if (!root) {
        std::string safeCopy(data, len);
        root = cJSON_Parse(safeCopy.c_str());
    }
    if (!root) {
        uint32_t errIdx = _jsonErrorCount.fetch_add(1, std::memory_order_relaxed) + 1;
        _lastParseErrorCategory = "JSON_SYNTAX_ERROR";
        _lastError = "Erreur parsing JSON recu";
        Serial.printf("[OpenAIClient][PARSE ERR #%u] Categorie: %s | Taille: %u octets | Connecte: %s | Setup: %s\n",
                      (unsigned)errIdx, _lastParseErrorCategory.c_str(), (unsigned)len,
                      isConnected() ? "OUI" : "NON",
                      isSetupComplete() ? "OUI" : "NON");
        return;
    }

    cJSON* typeItem = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!typeItem || !cJSON_IsString(typeItem) || !typeItem->valuestring) {
        uint32_t errIdx = _jsonErrorCount.fetch_add(1, std::memory_order_relaxed) + 1;
        _lastParseErrorCategory = "MISSING_TYPE_FIELD";
        _lastError = "Erreur parsing JSON recu";
        Serial.printf("[OpenAIClient][PARSE ERR #%u] Categorie: %s | Taille: %u octets\n",
                      (unsigned)errIdx, _lastParseErrorCategory.c_str(), (unsigned)len);
        cJSON_Delete(root);
        return;
    }

    const char* evType = typeItem->valuestring;

    // 1. Session initialisée par le serveur distant
    if (strcmp(evType, "session.created") == 0) {
        Serial.println("[OpenAIClient] Evenement session.created recu -> envoi session.update");
        sendSessionUpdate();
    }
    // 2. Confirmation de mise à jour des paramètres de session
    else if (strcmp(evType, "session.updated") == 0) {
        _setupCompleteDurationMs = millis() - _setupStartTimeMs;
        if (!_audioEmittedInSession.load(std::memory_order_acquire) || _confirmedSessionVoice.isEmpty()) {
            _confirmedSessionVoice = _voice;
        }
        Serial.printf("[OpenAIClient] Session OpenAI Realtime prete (setupComplete en %u ms, Voix=%s)\n",
                      (unsigned)_setupCompleteDurationMs, _confirmedSessionVoice.c_str());
        _setupComplete.store(true, std::memory_order_release);
        if (_onSetupComplete) _onSetupComplete();
    }
    // 3. Début de réponse du modèle
    else if (strcmp(evType, "response.created") == 0) {
        cJSON* respObj = cJSON_GetObjectItemCaseSensitive(root, "response");
        const char* respId = nullptr;
        const char* respStatus = nullptr;
        if (respObj) {
            cJSON* idItem = cJSON_GetObjectItemCaseSensitive(respObj, "id");
            if (idItem && cJSON_IsString(idItem) && idItem->valuestring) {
                respId = idItem->valuestring;
            }
            cJSON* stItem = cJSON_GetObjectItemCaseSensitive(respObj, "status");
            if (stItem && cJSON_IsString(stItem) && stItem->valuestring) respStatus = stItem->valuestring;
        }

        if (respId) {
            _lastDoneTranscript = "";
            if (!_waitingForResponse.load(std::memory_order_acquire)) {
                // Reçu alors qu'aucune réponse n'est attendue (annulée avant réception de response.created)
                addCancelledResponseId(respId);
                return;
            }
            if (isResponseCancelled(respId)) {
                // Identifiant explicitement obsolète : ne pas écraser l'identifiant de la réponse courante
                return;
            }
            _currentResponseId = String(respId);
        }

        if (_waitingForResponse.load(std::memory_order_acquire) && _firstResponseReceivedMs.load(std::memory_order_relaxed) == 0) {
            uint32_t nowMs = millis();
            if (nowMs == 0) nowMs = 1;
            _firstResponseReceivedMs.store(nowMs, std::memory_order_relaxed);
        }
        Serial.printf("[OpenAIClient] response.created : id=%s, status=%s\n",
                      respId ? respId : "none", respStatus ? respStatus : "in_progress");
    }
    // 3b. Ajout d'élément de sortie (message audio ou texte)
    else if (strcmp(evType, "response.output_item.added") == 0) {
        cJSON* itemObj = cJSON_GetObjectItemCaseSensitive(root, "item");
        const char* itemType = nullptr;
        const char* itemRole = nullptr;
        if (itemObj) {
            cJSON* t = cJSON_GetObjectItemCaseSensitive(itemObj, "type");
            if (t && cJSON_IsString(t) && t->valuestring) itemType = t->valuestring;
            cJSON* r = cJSON_GetObjectItemCaseSensitive(itemObj, "role");
            if (r && cJSON_IsString(r) && r->valuestring) itemRole = r->valuestring;
        }
        Serial.printf("[OpenAIClient] response.output_item.added : type=%s, role=%s\n",
                      itemType ? itemType : "unknown", itemRole ? itemRole : "unknown");
    }
    // 4. Paquet audio streaming PCM16 24 kHz (GA: response.output_audio.delta, Beta: response.audio.delta)
    else if (strcmp(evType, "response.output_audio.delta") == 0 || strcmp(evType, "response.audio.delta") == 0) {
        _audioEmittedInSession.store(true, std::memory_order_release);
        cJSON* respIdItem = cJSON_GetObjectItemCaseSensitive(root, "response_id");
        if (respIdItem && cJSON_IsString(respIdItem) && respIdItem->valuestring) {
            if (isResponseCancelled(respIdItem->valuestring)) {
                return;
            }
        }

        if (_waitingForResponse.load(std::memory_order_acquire)) {
            uint32_t nowMs = millis();
            if (nowMs == 0) nowMs = 1;
            if (_firstResponseReceivedMs.load(std::memory_order_relaxed) == 0) _firstResponseReceivedMs.store(nowMs, std::memory_order_relaxed);
            if (_firstAudioReceivedMs.load(std::memory_order_relaxed) == 0) _firstAudioReceivedMs.store(nowMs, std::memory_order_relaxed);
        }

        cJSON* deltaItem = cJSON_GetObjectItemCaseSensitive(root, "delta");
        if (deltaItem && cJSON_IsString(deltaItem) && deltaItem->valuestring) {
            std::vector<uint8_t> pcmBytes;
            if (decodeBase64(deltaItem->valuestring, strlen(deltaItem->valuestring), pcmBytes)) {
                if (pcmBytes.size() >= sizeof(int16_t)) {
                    const int16_t* pcmSamples = reinterpret_cast<const int16_t*>(pcmBytes.data());
                    size_t sampleCount = pcmBytes.size() / sizeof(int16_t);
                    uint32_t prevDispatched = _pcmSamplesDispatchedTotal.fetch_add((uint32_t)sampleCount, std::memory_order_relaxed);
                    if (prevDispatched == 0) {
                        Serial.printf("[OpenAIClient] response.audio.delta : premier bloc audio recu (%u ech)\n",
                                      (unsigned)sampleCount);
                    }
                    if (_turnCompleteReceived.load(std::memory_order_acquire)) {
                        _turnCompleteBeforeLastPcm.store(true, std::memory_order_release);
                        _postTurnCompletePcmSamples.fetch_add((uint32_t)sampleCount, std::memory_order_relaxed);
                        uint32_t postBlk = _postTurnCompletePcmBlocks.fetch_add(1, std::memory_order_relaxed) + 1;
                        Serial.printf("[OpenAIClient] ALERTE ORDRE: Bloc PCM recu APRES turnComplete (%u echantillons, bloc post-turn #%u)\n",
                                      (unsigned)sampleCount, (unsigned)postBlk);
                    }
                    if (_onAudioData) {
                        _onAudioData(pcmSamples, sampleCount);
                    }
                }
            }
        }
    }
    // 5. Transcription textuelle de la réponse assistant (GA: response.output_audio_transcript.delta, Beta: response.audio_transcript.delta)
    else if (strcmp(evType, "response.output_audio_transcript.delta") == 0 || strcmp(evType, "response.audio_transcript.delta") == 0) {
        cJSON* respIdItem = cJSON_GetObjectItemCaseSensitive(root, "response_id");
        if (respIdItem && cJSON_IsString(respIdItem) && respIdItem->valuestring) {
            if (isResponseCancelled(respIdItem->valuestring)) {
                return;
            }
        }
        cJSON* deltaItem = cJSON_GetObjectItemCaseSensitive(root, "delta");
        if (deltaItem && cJSON_IsString(deltaItem) && deltaItem->valuestring) {
            String textFragment(deltaItem->valuestring);
            if (_onOutputTranscriptionWithFinal) _onOutputTranscriptionWithFinal(textFragment, false);
            else if (_onOutputTranscription) _onOutputTranscription(textFragment);
            if (_onText) _onText(textFragment);
        }
    }
    // 5b. Transcription finale complète de l'assistant (GA: response.output_audio_transcript.done, Beta: response.audio_transcript.done)
    else if (strcmp(evType, "response.output_audio_transcript.done") == 0 || strcmp(evType, "response.audio_transcript.done") == 0) {
        cJSON* respIdItem = cJSON_GetObjectItemCaseSensitive(root, "response_id");
        if (respIdItem && cJSON_IsString(respIdItem) && respIdItem->valuestring) {
            if (isResponseCancelled(respIdItem->valuestring)) {
                return;
            }
        }
        cJSON* transItem = cJSON_GetObjectItemCaseSensitive(root, "transcript");
        if (transItem && cJSON_IsString(transItem) && transItem->valuestring) {
            String fullText(transItem->valuestring);
            if (fullText != _lastDoneTranscript) {
                _lastDoneTranscript = fullText;
                if (_onOutputTranscriptionWithFinal) _onOutputTranscriptionWithFinal(fullText, true);
                else if (_onOutputTranscription) _onOutputTranscription(fullText);
                if (_onText) _onText(fullText);
            }
        }
    }
    // 6. Transcription de l'entrée microphone utilisateur (whisper-1)
    else if (strcmp(evType, "conversation.item.input_audio_transcription.completed") == 0) {
        cJSON* transItem = cJSON_GetObjectItemCaseSensitive(root, "transcript");
        if (transItem && cJSON_IsString(transItem) && transItem->valuestring) {
            String userText(transItem->valuestring);
            Serial.printf("[OpenAIClient] Transcription utilisateur recue : \"%s\"\n", userText.c_str());
            if (_onInputTranscription) {
                _onInputTranscription(userText, true);
            }
        }
    }
    // 7. Fin de réponse du modèle (signal équivalent à turnComplete)
    else if (strcmp(evType, "response.done") == 0) {
        cJSON* respObj = cJSON_GetObjectItemCaseSensitive(root, "response");
        const char* respId = nullptr;
        const char* statusStr = nullptr;
        const char* statusDetailsType = nullptr;
        const char* statusDetailsReason = nullptr;
        const char* statusDetailsErrMsg = nullptr;
        const char* statusDetailsErrCode = nullptr;

        if (respObj) {
            cJSON* idItem = cJSON_GetObjectItemCaseSensitive(respObj, "id");
            if (idItem && cJSON_IsString(idItem) && idItem->valuestring) {
                respId = idItem->valuestring;
                if (isResponseCancelled(respId)) {
                    Serial.printf("[OpenAIClient] response.done ignore : response_id=%s annule\n", respId);
                    return;
                }
            }
            cJSON* stItem = cJSON_GetObjectItemCaseSensitive(respObj, "status");
            if (stItem && cJSON_IsString(stItem) && stItem->valuestring) {
                statusStr = stItem->valuestring;
            }
            cJSON* detObj = cJSON_GetObjectItemCaseSensitive(respObj, "status_details");
            if (detObj && cJSON_IsObject(detObj)) {
                cJSON* dt = cJSON_GetObjectItemCaseSensitive(detObj, "type");
                if (dt && cJSON_IsString(dt) && dt->valuestring) statusDetailsType = dt->valuestring;
                cJSON* dr = cJSON_GetObjectItemCaseSensitive(detObj, "reason");
                if (dr && cJSON_IsString(dr) && dr->valuestring) statusDetailsReason = dr->valuestring;
                cJSON* de = cJSON_GetObjectItemCaseSensitive(detObj, "error");
                if (de && cJSON_IsObject(de)) {
                    cJSON* dem = cJSON_GetObjectItemCaseSensitive(de, "message");
                    if (dem && cJSON_IsString(dem) && dem->valuestring) statusDetailsErrMsg = dem->valuestring;
                    cJSON* dec = cJSON_GetObjectItemCaseSensitive(de, "code");
                    if (dec && cJSON_IsString(dec) && dec->valuestring) statusDetailsErrCode = dec->valuestring;
                }
            }
            // Extraction de la transcription finale dans response.output si présente
            cJSON* outputArr = cJSON_GetObjectItemCaseSensitive(respObj, "output");
            if (outputArr && cJSON_IsArray(outputArr)) {
                cJSON* outItem = nullptr;
                cJSON_ArrayForEach(outItem, outputArr) {
                    cJSON* contentArr = cJSON_GetObjectItemCaseSensitive(outItem, "content");
                    if (contentArr && cJSON_IsArray(contentArr)) {
                        cJSON* contentItem = nullptr;
                        cJSON_ArrayForEach(contentItem, contentArr) {
                            cJSON* trItem = cJSON_GetObjectItemCaseSensitive(contentItem, "transcript");
                            if (trItem && cJSON_IsString(trItem) && trItem->valuestring && strlen(trItem->valuestring) > 0) {
                                String fullText(trItem->valuestring);
                                if (fullText != _lastDoneTranscript) {
                                    _lastDoneTranscript = fullText;
                                    if (_onOutputTranscriptionWithFinal) _onOutputTranscriptionWithFinal(fullText, true);
                                    else if (_onOutputTranscription) _onOutputTranscription(fullText);
                                    if (_onText) _onText(fullText);
                                }
                            }
                        }
                    }
                }
            }
        }

        _waitingForResponse.store(false, std::memory_order_release);

        uint32_t decodedSamples = _pcmSamplesDispatchedTotal.load(std::memory_order_relaxed);
        bool hasAudio = (decodedSamples > 0);

        Serial.printf("[OpenAIClient] response.done recu : status=%s", statusStr ? statusStr : "unknown");
        if (statusDetailsType) {
            Serial.printf(" | details_type=%s", statusDetailsType);
        }
        if (statusDetailsReason) {
            Serial.printf(" | reason=%s", statusDetailsReason);
        }
        if (statusDetailsErrMsg) {
            Serial.printf(" | err_code=%s, err_msg=%s",
                          statusDetailsErrCode ? statusDetailsErrCode : "",
                          statusDetailsErrMsg);
        }
        Serial.printf(" | audio_ech=%u (%s)\n", (unsigned)decodedSamples, hasAudio ? "AVEC_AUDIO" : "SANS_AUDIO");

        if (statusStr && strcmp(statusStr, "cancelled") == 0) {
            if (respId) {
                addCancelledResponseId(respId);
            }
            Serial.println("[OpenAIClient] Reponse annulee par le serveur (status: cancelled)");
            if (_onInterrupted) {
                _onInterrupted();
            }
        } else if (statusStr && strcmp(statusStr, "failed") == 0) {
            _lastError = statusDetailsErrMsg ? statusDetailsErrMsg : "Reponse OpenAI echouee (status: failed)";
            Serial.printf("[OpenAIClient] ERREUR SERVEUR : Reponse echouee (status: failed) : %s\n", _lastError.c_str());
            if (_onError) {
                _onError(_lastError);
            }
        } else {
            _turnCompleteReceived.store(true, std::memory_order_release);
            if (hasAudio) {
                Serial.printf("[OpenAIClient] Reponse modele terminee avec audio (%u ech) (response.done)\n", (unsigned)decodedSamples);
            } else {
                Serial.println("[OpenAIClient] Reponse modele terminee SANS audio (text-only/silence) (response.done)");
            }
            if (_onTurnComplete) {
                _onTurnComplete();
            }
        }
    }
    // 8. Annulation de réponse (suite à response.cancel / interruption STOP)
    else if (strcmp(evType, "response.cancelled") == 0) {
        cJSON* respObj = cJSON_GetObjectItemCaseSensitive(root, "response");
        if (respObj) {
            cJSON* idItem = cJSON_GetObjectItemCaseSensitive(respObj, "id");
            if (idItem && cJSON_IsString(idItem) && idItem->valuestring) {
                addCancelledResponseId(idItem->valuestring);
            }
        }
        _waitingForResponse.store(false, std::memory_order_release);
        Serial.println("[OpenAIClient] Reponse annulee par le serveur (response.cancelled)");
        if (_onInterrupted) {
            _onInterrupted();
        }
    }
    // 9. Gestion des erreurs serveur
    else if (strcmp(evType, "error") == 0) {
        cJSON* errObj = cJSON_GetObjectItemCaseSensitive(root, "error");
        if (errObj) {
            cJSON* msgItem = cJSON_GetObjectItemCaseSensitive(errObj, "message");
            cJSON* codeItem = cJSON_GetObjectItemCaseSensitive(errObj, "code");
            const char* msgStr = (msgItem && cJSON_IsString(msgItem) && msgItem->valuestring) ? msgItem->valuestring : "Erreur OpenAI inconnue";
            const char* codeStr = (codeItem && cJSON_IsString(codeItem) && codeItem->valuestring) ? codeItem->valuestring : "";

            // Ignorer l'erreur bénigne d'annulation si aucune réponse n'était active au moment du STOP
            if (strstr(msgStr, "no active response") != nullptr ||
                strcmp(codeStr, "response_cancel_failed") == 0 ||
                strcmp(codeStr, "input_audio_buffer_commit_empty") == 0 ||
                strstr(msgStr, "buffer too small") != nullptr) {
                Serial.printf("[OpenAIClient] Info annulation / appui rapide (benigne) : %s\n", msgStr);
                _waitingForResponse.store(false, std::memory_order_release);
            } else {
                _lastError = msgStr;
                Serial.printf("[OpenAIClient] ERREUR SERVEUR (%s) : %s\n", codeStr, msgStr);
                if (_onError) {
                    _onError(_lastError);
                }
            }
        }
    }

    cJSON_Delete(root);
}
