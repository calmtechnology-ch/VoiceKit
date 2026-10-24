#include "VoxCoreVoice.h"
#include <WiFi.h>
#include <time.h>

VoxCoreVoice::VoxCoreVoice() = default;

VoxCoreVoice::~VoxCoreVoice() {
    stop();
}

bool VoxCoreVoice::isSystemTimeValid() {
    time_t now = time(nullptr);
    return (now >= NTP_MIN_VALID_TIMESTAMP);
}

void VoxCoreVoice::setState(VoiceState newState) {
    if (_state != newState) {
        _state = newState;
        if (_onStateChanged) {
            _onStateChanged(_state);
        }
    }
}

bool VoxCoreVoice::begin(const VoiceKitConfig& config) {
    _config = config;
    _lastError = "";

    Serial.println("\n========================================================");
    Serial.println("  VoxCore VoiceKit v0.3.0 — TTS + STT (Gemini Live)    ");
    Serial.printf("  Mode Audio : %s\n",
                  (LiveAudioPlayer::getCompiledAudioMode() == 1) ? "MODE 1 (REACTIVE_CALLBACK)" : "MODE 2 (DEDICATED_AUDIO_TASK)");
    Serial.println("========================================================");

    // 1. Initialisation HAL audio matérielle (Sortie : Haut-parleur M5Unified)
    if (_config.audioOutput) {
        _audioOutput = _config.audioOutput;
    } else {
        _internalAudioOutput = std::unique_ptr<M5AudioOutput>(new M5AudioOutput());
        _audioOutput = _internalAudioOutput.get();
    }

    if (!_audioOutput->begin()) {
        _lastError = "Echec initialisation materielle AudioOutput";
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    // 2. Initialisation HAL audio matérielle (Entrée : Microphone M5Unified ES7210)
    if (_config.audioInput) {
        _audioInput = _config.audioInput;
    } else {
        _internalAudioInput = std::unique_ptr<M5AudioInput>(new M5AudioInput());
        _audioInput = _internalAudioInput.get();
    }

    // Arbitrage centralisé I2S
    HardwareManager::getInstance().setAudioOutput(_audioOutput);
    HardwareManager::getInstance().setAudioInput(_audioInput);

    // 3. Initialisation du lecteur LiveAudioPlayer (tampon PSRAM, tâche audio FreeRTOS)
    _player.setAudioOutput(_audioOutput);
    _player.setVolume(_config.volume);
    _player.setPlaybackAllowed(true);

    if (!_player.begin()) {
        _lastError = "Echec initialisation memoire LiveAudioPlayer";
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    // 4. Initialisation du streamer audio LiveAudioStreamer (capture PCM 16 kHz et double-buffering)
    _streamer.setAudioInput(_audioInput);
    _streamer.setAudioOutput(_audioOutput);
    if (!_streamer.begin()) {
        _lastError = "Echec initialisation memoire LiveAudioStreamer";
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    // 5. Initialisation du client Gemini Live (protocole BidiGenerateContent)
    if (!_client.begin(_config.apiKey.c_str(), _config.model.c_str(), _config.voice.c_str())) {
        _lastError = _client.getLastError();
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    _client.setSystemInstruction(_config.systemInstruction);

    // Enregistrement de l'écouteur de transcriptions entrantes utilisateur
    _client.setOnInputTranscription([this](const String& text, bool isFinal) {
        if (_conversationEnabled) {
            _conversationController.handleUserTranscription(text, isFinal);
        }
        handleIncomingTranscription(text, isFinal);
    });

    // 6. Initialisation du contrôleur de session LiveSessionController
    if (!_session.begin(&_client, &_player)) {
        _lastError = "Echec initialisation LiveSessionController";
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    // Écouteur de transcription de sortie Gemini (Mode conversation)
    _session.setOnOutputTranscription([this](const String& text) {
        if (_conversationEnabled) {
            _conversationController.handleAssistantTranscription(text);
        }
    });

    _session.setOnTurnFinished([this](uint64_t turnId) {
        if (_conversationEnabled) {
            _conversationController.handleTurnFinished(turnId);
        }
    });

    // Initialisation du contrôleur de conversation bidirectionnelle
    _conversationController.begin(&_client, &_session, &_streamer, &_player, _config);

    _conversationController.setOnStateChanged([this](VoiceState s) {
        setState(s);
    });

    _conversationController.setOnUserText([this](const String& text, bool isFinal) {
        if (_onUserText) _onUserText(text, isFinal);
    });

    _conversationController.setOnAssistantText([this](const String& text) {
        if (_onAssistantText) _onAssistantText(text);
    });

    _conversationController.setOnConversationFinished([this]() {
        if (_onConversationFinished) _onConversationFinished();
    });

    _conversationController.setOnTurnMetrics([this](const ConversationMetrics& m) {
        if (_onConversationTurnMetrics) _onConversationTurnMetrics(m);
    });

    _conversationController.setOnError([this](const String& err) {
        _lastError = err;
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(err);
    });

    if (_config.enableConversation) {
        enableConversation(true);
    }

    // Callbacks du contrôleur de session
    _session.setOnStateChanged([this](LiveSessionState s) {

        switch (s) {
            case LiveSessionState::CONNECTED:
                // Si on n'est pas déjà en train d'écouter ou de transcrire
                if (_state != VoiceState::LISTENING && _state != VoiceState::TRANSCRIBING) {
                    setState(VoiceState::READY);
                    if (_onReady) {
                        _onReady();
                    }
                }
                break;
            case LiveSessionState::WAITING_RESPONSE:
                if (_state != VoiceState::LISTENING && _state != VoiceState::TRANSCRIBING) {
                    setState(VoiceState::WAITING_RESPONSE);
                }
                break;
            case LiveSessionState::PLAYING:
                if (_state != VoiceState::LISTENING && _state != VoiceState::TRANSCRIBING) {
                    setState(VoiceState::SPEAKING);
                }
                break;
            case LiveSessionState::ERROR:
                _lastError = _session.getLastError();
                setState(VoiceState::ERROR_STATE);
                if (_onError) {
                    _onError(_lastError);
                }
                break;
            default:
                break;
        }
    });

    _session.setOnFirstAudio([this]() {
        // En mode STT pur ou pendant l'écoute, le haut-parleur est silencieux
        if (_state == VoiceState::LISTENING || _state == VoiceState::TRANSCRIBING) {
            return;
        }
        setState(VoiceState::SPEAKING);
        if (_onSpeechStarted) {
            _onSpeechStarted();
        }
    });

    _session.setOnPlaybackFinished([this]() {
        if (_state == VoiceState::LISTENING || _state == VoiceState::TRANSCRIBING) {
            return;
        }
        setState(VoiceState::READY);
        if (_onSpeechFinished) {
            _onSpeechFinished();
        }

        // Dépilement de la requête TTS suivante en file FIFO
        if (!_textQueue.empty()) {
            String nextText = _textQueue.front();
            _textQueue.pop();
            dispatchSpeech(nextText);
        }
    });

    _session.setOnError([this](const String& err) {
        _lastError = err;
        _pendingText = "";
        if (_conversationEnabled) {
            _conversationController.requestStop();
        }
        setState(VoiceState::ERROR_STATE);
        if (_onError) {
            _onError(err);
        }
    });

    // 7. Connectivité Wi-Fi
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        if (!_config.wifiSSID.isEmpty()) {
            Serial.printf("[VoiceKit] Demarrage WiFiManager gere (SSID: %s)...\n", _config.wifiSSID.c_str());
            _wifi.begin(_config.wifiSSID.c_str(), _config.wifiPassword.c_str());
            setState(VoiceState::CONNECTING_WIFI);
        } else {
            Serial.println("[VoiceKit] AVERTISSEMENT : Mode MANAGED_WIFI sans SSID");
            setState(VoiceState::IDLE);
        }
    } else {
        Serial.println("[VoiceKit] Mode EXTERNAL_WIFI : Wi-Fi gere par l'application hote.");
        setState(VoiceState::IDLE);
    }

    Serial.println("[VoiceKit] Initialisation TTS + STT terminee avec succes.");
    return true;
}

void VoxCoreVoice::update() {
    const uint32_t now = millis();

    // 1. Mise à jour réseau Wi-Fi / SNTP si en mode MANAGED_WIFI
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        _wifi.tick();
        if (_wifi.isConnected() && _wifi.isTimeSynced()) {
            if (_state == VoiceState::CONNECTING_WIFI || _state == VoiceState::IDLE) {
                if (!_client.isConnected() && _session.getState() == LiveSessionState::DISCONNECTED) {
                    Serial.println("[VoiceKit] Wi-Fi et NTP valides -> Connexion a Gemini Live...");
                    setState(VoiceState::CONNECTING_SERVICE);
                    _connectingStartTimeMs = millis();
                    _session.activate();
                }
            }
        }
    } else {
        // En mode EXTERNAL_WIFI : connexion automatique dès que l'application hôte a établi le réseau
        if (_state == VoiceState::IDLE || _state == VoiceState::CONNECTING_WIFI) {
            if (WiFi.status() == WL_CONNECTED && isSystemTimeValid()) {
                if (!_client.isConnected() && _session.getState() == LiveSessionState::DISCONNECTED) {
                    Serial.println("[VoiceKit] Reseau hote & NTP valides -> Connexion a Gemini Live...");
                    setState(VoiceState::CONNECTING_SERVICE);
                    _connectingStartTimeMs = millis();
                    _session.activate();
                }
            }
        }
        if (_state == VoiceState::CONNECTING_SERVICE && WiFi.status() != WL_CONNECTED) {
            _lastError = "Perte Wi-Fi pendant connexion";
            _pendingText = "";
            _connectingStartTimeMs = 0;
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
        }
    }

    // 2. Traitement régulier des moteurs VoxCore
    _client.tick();
    _player.tick();
    _session.tick();

    if (_conversationEnabled) {
        _conversationController.tick();
    }


    // 3. Traitement régulier du streamer audio STT
    if (_streamer.isStreaming()) {
        if (!_client.isConnected()) {
            _lastError = "Perte connexion Gemini Live pendant ecoute micro";
            Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
            _streamer.cancel();
            if (_conversationEnabled) {
                _conversationController.requestStop();
            }
            _player.setPlaybackAllowed(true);
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
        } else if (!_streamer.tick()) {
            if (_streamer.isQueueSaturated()) {
                _lastError = "File emission saturee (reseau lent)";
            } else {
                _lastError = _streamer.getLastError();
            }
            Serial.printf("[VoiceKit] Erreur streaming micro : %s\n", _lastError.c_str());
            _streamer.cancel();
            if (_conversationEnabled) {
                _conversationController.requestStop();
            }
            _player.setPlaybackAllowed(true);
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
        }
    }

    // 4. Surveillance du timeout de transcription finale (STT)
    if (_state == VoiceState::TRANSCRIBING && _transcriptionPending) {
        if (_stopListeningTimeMs > 0 && (now - _stopListeningTimeMs > _config.sttFinalTimeoutMs)) {
            Serial.println("[VoiceKit] Avertissement : Timeout attente transcription finale de Gemini Live");
            _transcriptionPending = false;
            _player.setPlaybackAllowed(true);
            setState(VoiceState::READY);
            // Règle formelle v0.3.0 : NE PAS inventer de transcription finale à partir de l'intérim.
        }
    }

    // 5. Traitement des requêtes TTS en attente
    if (!_pendingText.isEmpty()) {
        if (_session.getState() == LiveSessionState::CONNECTED && !isListening() && !isTranscribing()) {
            String textToSay = _pendingText;
            _pendingText = "";
            dispatchSpeech(textToSay);
        }
    }

    // 6. Surveillance des timeouts de connexion initiale
    if (_state == VoiceState::CONNECTING_SERVICE) {
        if (_connectingStartTimeMs > 0 && (now - _connectingStartTimeMs > GEMINI_LIVE_CONNECT_TIMEOUT_MS + 2000)) {
            Serial.println("[VoiceKit] ERREUR : Delai de connexion depasse vers Gemini Live");
            _lastError = "Timeout connexion Gemini Live";
            _connectingStartTimeMs = 0;
            _pendingText = "";
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
        }
    }
}

// =============================================================================
// Implémentation TTS (Text-to-Speech)
// =============================================================================

bool VoxCoreVoice::say(const String& text) {
    if (text.isEmpty()) {
        _lastError = "Texte vide";
        return false;
    }

    _lastError = "";

    // Si le moteur est occupé
    if (isBusy()) {
        switch (_config.queuePolicy) {
            case QueuePolicy::REJECT_IF_BUSY:
                _lastError = "Moteur vocal occupe";
                Serial.println("[VoiceKit] say() refuse : moteur actuellement occupe");
                return false;

            case QueuePolicy::INTERRUPT_CURRENT:
                Serial.println("[VoiceKit] say() interruption demandee de l'activite en cours");
                stop();
                break;

            case QueuePolicy::QUEUE_FIFO:
                if (_textQueue.size() >= _config.maxQueueSize) {
                    _lastError = "File d'attente vocale saturee";
                    Serial.println("[VoiceKit] say() refuse : file d'attente saturee");
                    return false;
                }
                _textQueue.push(text);
                Serial.printf("[VoiceKit] say() mis en file d'attente (position %u)\n", (unsigned)_textQueue.size());
                return true;
        }
    }

    // Vérification des prérequis réseau
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        if (!_wifi.isConnected()) {
            _lastError = "Wi-Fi non connecte";
            if (_onError) _onError(_lastError);
            return false;
        }
        if (!_wifi.isTimeSynced()) {
            _lastError = "Horloge NTP non synchronisee (requise pour TLS)";
            if (_onError) _onError(_lastError);
            return false;
        }
    } else {
        if (WiFi.status() != WL_CONNECTED) {
            _lastError = "Wi-Fi hote non connecte";
            if (_onError) _onError(_lastError);
            return false;
        }
        if (!isSystemTimeValid()) {
            _lastError = "Heure systeme non synchronisee (requise pour TLS)";
            if (_onError) _onError(_lastError);
            return false;
        }
    }

    // Réarmer la restitution haut-parleur
    _player.setPlaybackAllowed(true);

    // Si la session est prête, envoi direct
    if (_session.getState() == LiveSessionState::CONNECTED) {
        return dispatchSpeech(text);
    }

    // Si la session est en cours de connexion
    if (_session.getState() == LiveSessionState::CONNECTING) {
        Serial.println("[VoiceKit] Connexion en cours -> requete mise en attente");
        _pendingText = text;
        return true;
    }

    // Session déconnectée ou en erreur : tentative d'activation
    Serial.println("[VoiceKit] Session deconnectee -> Activation de session...");
    _pendingText = text;
    _connectingStartTimeMs = millis();
    setState(VoiceState::CONNECTING_SERVICE);
    return _session.activate();
}

bool VoxCoreVoice::dispatchSpeech(const String& text) {
    String prompt = _config.promptPrefix + text;
    Serial.printf("[VoiceKit] Prononciation : \"%s\"\n", text.c_str());
    bool ok = _session.sendTextMessage(prompt);
    if (!ok) {
        _lastError = "Echec envoi message texte vers Gemini Live";
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }
    return true;
}

void VoxCoreVoice::stop() {
    Serial.println("[VoiceKit] Arret demande (stop)");
    if (_conversationEnabled && _conversationController.isTurnActive()) {
        _conversationController.requestStop();
    }
    _pendingText = "";
    while (!_textQueue.empty()) {
        _textQueue.pop();
    }
    if (isListening() || isTranscribing()) {
        _streamer.cancel();
        _transcriptionPending = false;
        _finalTranscriptionReceived = false;
    }
    _session.requestStop();
    _player.setPlaybackAllowed(true);
    setState(VoiceState::READY);
}

bool VoxCoreVoice::isReady() const {
    return (_session.getState() == LiveSessionState::CONNECTED && !isBusy());
}

bool VoxCoreVoice::isSpeaking() const {
    return (_player.isPlaying() > 0 || _session.getState() == LiveSessionState::PLAYING);
}

bool VoxCoreVoice::isBusy() const {
    if (_conversationEnabled && _conversationController.isTurnActive()) {
        return true;
    }
    return (isSpeaking() || 
            isListening() ||
            isTranscribing() ||
            _session.getState() == LiveSessionState::WAITING_RESPONSE || 
            !_pendingText.isEmpty() || 
            !_textQueue.empty());
}

void VoxCoreVoice::setVolume(uint8_t percent) {
    _config.volume = percent;
    _player.setVolume(percent);
}

uint8_t VoxCoreVoice::getVolume() const {
    return _player.getVolume();
}

// =============================================================================
// Implémentation Conversation Bidirectionnelle (v0.4.0)
// =============================================================================

void VoxCoreVoice::enableConversation(bool enable) {
    _conversationEnabled = enable;
    _config.enableConversation = enable;
    if (enable) {
        _client.setSystemInstruction(_config.conversationInstruction);
        Serial.println("[VoiceKit] Mode conversation bidirectionnelle native (Mode B) ACTIVE");
    } else {
        _client.setSystemInstruction(_config.systemInstruction);
        Serial.println("[VoiceKit] Mode conversation DESACTIVE (retour mode unitaire)");
    }
}

// =============================================================================
// Implémentation STT (Speech-to-Text) & Déclenchement Conversation
// =============================================================================

bool VoxCoreVoice::startListening() {
    _lastError = "";

    // 0. En mode conversationnel bidirectionnel (Mode B)
    if (_conversationEnabled) {
        return _conversationController.startTurn();
    }

    // 1. Si une restitution sonore TTS ou attente de réponse est en cours, l'interrompre prioritairement
    if (_session.getState() == LiveSessionState::WAITING_RESPONSE || isSpeaking()) {
        _session.requestStop();
    }
    if (isSpeaking()) {
        _player.stop(AudioStopReason::NEW_TURN);
    }
    if (_config.muteSpeakerDuringSTT) {
        _player.setPlaybackAllowed(false);
    }

    // 2. Vérification réseau
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        if (!_wifi.isConnected() || !_wifi.isTimeSynced()) {
            _lastError = "Wi-Fi non connecte ou horloge non synchronisee";
            if (_onError) _onError(_lastError);
            return false;
        }
    } else {
        if (WiFi.status() != WL_CONNECTED || !isSystemTimeValid()) {
            _lastError = "Wi-Fi hote non connecte ou horloge non synchronisee";
            if (_onError) _onError(_lastError);
            return false;
        }
    }

    // 3. Vérification de la disponibilité du client Gemini Live
    if (!_client.isConnected() || !_client.isSetupComplete()) {
        _lastError = "Session Gemini Live non prete";
        Serial.printf("[VoiceKit] startListening() refuse : %s\n", _lastError.c_str());
        if (_onError) _onError(_lastError);
        return false;
    }

    // 4. Réinitialisation des états de transcription pour ce tour
    _finalTranscriptionReceived = false;
    _transcriptionPending = false;
    _lastInterimText = "";
    _lastFinalText = "";

    // 5. Démarrage effectif du streamer (envoie activityStart et active double-buffering)
    if (!_streamer.start(&_client)) {
        _lastError = _streamer.getLastError();
        Serial.printf("[VoiceKit] ERREUR startListening() : %s\n", _lastError.c_str());
        _player.setPlaybackAllowed(true);
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    setState(VoiceState::LISTENING);
    if (_onListeningStarted) {
        _onListeningStarted();
    }
    Serial.println("[VoiceKit] startListening() : Acquisition microphone et streaming actifs");
    return true;
}

bool VoxCoreVoice::stopListening() {
    if (_conversationEnabled) {
        return _conversationController.finishTurnAudio();
    }

    if (!isListening() && !_streamer.isStreaming()) {
        return false;
    }

    Serial.println("[VoiceKit] stopListening() : Arret capture micro et envoi de activityEnd...");
    bool stopped = _streamer.stop();
    _transcriptionPending = true;
    _stopListeningTimeMs = millis();

    if (_onListeningFinished) {
        _onListeningFinished();
    }

    if (!stopped) {
        _lastError = _streamer.getLastError();
        Serial.printf("[VoiceKit] Avertissement stopListening() : %s\n", _lastError.c_str());
    }

    setState(VoiceState::TRANSCRIBING);
    return true;
}

void VoxCoreVoice::cancelListening() {
    if (_conversationEnabled) {
        _conversationController.requestStop();
        return;
    }

    Serial.println("[VoiceKit] cancelListening() : Annulation ecoute sans transcription");
    _streamer.cancel();
    _transcriptionPending = false;
    _finalTranscriptionReceived = false;
    _lastInterimText = "";
    _lastFinalText = "";
    _player.setPlaybackAllowed(true);
    setState(VoiceState::READY);
}

bool VoxCoreVoice::isListening() const {
    if (_conversationEnabled) {
        return _conversationController.getState() == VoiceState::LISTENING;
    }
    return (_state == VoiceState::LISTENING || _streamer.isStreaming());
}

bool VoxCoreVoice::isTranscribing() const {
    if (_conversationEnabled) {
        return _conversationController.getState() == VoiceState::WAITING_RESPONSE;
    }
    return (_state == VoiceState::TRANSCRIBING || _transcriptionPending);
}

void VoxCoreVoice::handleIncomingTranscription(const String& text, bool isFinal) {
    if (isFinal) {
        Serial.printf("[VoiceKit] Transcription FINALE recue : \"%s\"\n", text.c_str());
        _finalTranscriptionReceived = true;
        _transcriptionPending = false;
        _lastFinalText = text;
        _player.setPlaybackAllowed(true);

        if (!_conversationEnabled) {
            if (_state == VoiceState::TRANSCRIBING || _state == VoiceState::LISTENING) {
                setState(VoiceState::READY);
            }
        }

        if (_onTranscription) {
            _onTranscription(text, true);
        }
    } else {
        _lastInterimText = text;
        if (_onTranscription) {
            _onTranscription(text, false);
        }
    }
}

