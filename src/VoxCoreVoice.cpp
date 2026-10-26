#include "VoxCoreVoice.h"
#include <WiFi.h>
#include <time.h>

VoxCoreVoice::VoxCoreVoice() = default;

VoxCoreVoice::~VoxCoreVoice() {
    stop();
    if (HardwareManager::getInstance().getAudioOutput() == _audioOutput) {
        HardwareManager::getInstance().setAudioOutput(nullptr);
    }
    if (HardwareManager::getInstance().getAudioInput() == _audioInput) {
        HardwareManager::getInstance().setAudioInput(nullptr);
    }
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
    Serial.println("  VoxCore VoiceKit v0.5.0.1 — TTS + STT + Conversation  ");
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

    _client.setLanguage(_config.language);
    _client.setVoiceDirectives(_config.buildVoiceDirectives());
    _client.setSystemInstruction(_config.enableConversation ? _config.conversationInstruction : _config.systemInstruction);

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
                // Validation définitive du nouveau profil vocal après setupComplete (v0.4.1.1)
                if (_profileChangePending) {
                    _config.voice = _pendingVoice;
                    _config.language = _pendingLanguage;
                    _config.style = _pendingStyle;
                    _config.tone = _pendingTone;
                    _config.accent = _pendingAccent;
                    _profileChangePending = false;
                    Serial.printf("[VoiceKit] Profil vocal confirme par setupComplete : Voix=%s | Langue=%s\n",
                                  _config.voice.c_str(), _config.language.c_str());
                }
                // Confirmation de la transition de mode après setupComplete (v0.4.1.1)
                if (_modeSwitchPending) {
                    _modeSwitchPending = false;
                    Serial.printf("[VoiceKit] Transition de mode confirmee par setupComplete : Mode=%s\n",
                                  _conversationEnabled ? "CONVERSATION" : "TTS/STT");
                }
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
                // Restauration cohérente en cas d'erreur de reconnexion (v0.4.1.1)
                if (_profileChangePending) {
                    _client.setVoice(_config.voice);
                    _client.setLanguage(_config.language);
                    _client.setVoiceDirectives(_config.buildVoiceDirectives());
                    _profileChangePending = false;
                    Serial.println("[VoiceKit] Echec reconnexion profil : profil precedent restaure");
                }
                if (_modeSwitchPending) {
                    _conversationEnabled = _previousConversationMode;
                    _config.enableConversation = _previousConversationMode;
                    _client.setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
                    _client.setVoiceDirectives(_config.buildVoiceDirectives());
                    _modeSwitchPending = false;
                    Serial.println("[VoiceKit] Echec reconnexion mode : mode precedent restaure");
                }
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
        if (_profileChangePending) {
            _client.setVoice(_config.voice);
            _client.setLanguage(_config.language);
            _client.setVoiceDirectives(_config.buildVoiceDirectives());
            _profileChangePending = false;
            Serial.println("[VoiceKit] Erreur session : profil vocal precedent restaure");
        }
        if (_modeSwitchPending) {
            _conversationEnabled = _previousConversationMode;
            _config.enableConversation = _previousConversationMode;
            _client.setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
            _client.setVoiceDirectives(_config.buildVoiceDirectives());
            _modeSwitchPending = false;
            Serial.println("[VoiceKit] Erreur session : mode precedent restaure");
        }
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
    if (!_config.wifiSSID.isEmpty() && _config.wifiMode == WiFiMode::EXTERNAL_WIFI) {
        Serial.println("[VoiceKit] SSID configure : activation automatique du mode MANAGED_WIFI");
        _config.wifiMode = WiFiMode::MANAGED_WIFI;
    }

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
        // Connection can start during this update, after the initial now snapshot.
        // Read the clock again to avoid unsigned underflow and an immediate timeout.
        if (_connectingStartTimeMs > 0 && (millis() - _connectingStartTimeMs > GEMINI_LIVE_CONNECT_TIMEOUT_MS + 2000)) {
            Serial.println("[VoiceKit] ERREUR : Delai de connexion depasse vers Gemini Live");
            if (_profileChangePending) {
                _client.setVoice(_config.voice);
                _client.setLanguage(_config.language);
                _client.setVoiceDirectives(_config.buildVoiceDirectives());
                _profileChangePending = false;
                Serial.println("[VoiceKit] Timeout reconnexion profil : profil vocal precedent restaure");
            }
            if (_modeSwitchPending) {
                _conversationEnabled = _previousConversationMode;
                _config.enableConversation = _previousConversationMode;
                _client.setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
                _client.setVoiceDirectives(_config.buildVoiceDirectives());
                _modeSwitchPending = false;
                Serial.println("[VoiceKit] Timeout reconnexion mode : mode precedent restaure");
            }
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

    // Si la session est en cours de connexion ou de reconnexion
    if (_session.getState() == LiveSessionState::CONNECTING || _state == VoiceState::CONNECTING_SERVICE) {
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
    if (_profileChangePending) {
        _client.setVoice(_config.voice);
        _client.setLanguage(_config.language);
        _client.setVoiceDirectives(_config.buildVoiceDirectives());
        _profileChangePending = false;
        Serial.println("[VoiceKit] stop() : profil vocal precedent restaure");
    }
    if (_modeSwitchPending) {
        _conversationEnabled = _previousConversationMode;
        _config.enableConversation = _previousConversationMode;
        _client.setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
        _client.setVoiceDirectives(_config.buildVoiceDirectives());
        _modeSwitchPending = false;
        Serial.println("[VoiceKit] stop() : mode conversationnel precedent restaure");
    }
    _session.requestStop();
    _player.setPlaybackAllowed(true);
    if (_session.getState() == LiveSessionState::CONNECTED) {
        setState(VoiceState::READY);
    } else if (_state != VoiceState::CONNECTING_WIFI && _state != VoiceState::CONNECTING_SERVICE) {
        setState(VoiceState::IDLE);
    }
}

bool VoxCoreVoice::isReady() const {
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        if (!_wifi.isConnected()) {
            return false;
        }
    } else {
        if (WiFi.status() != WL_CONNECTED) {
            return false;
        }
    }
    return (_session.getState() == LiveSessionState::CONNECTED && !isBusy() && !isConnecting());
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

bool VoxCoreVoice::isConnecting() const {
    return (_state == VoiceState::CONNECTING_SERVICE ||
            _session.getState() == LiveSessionState::CONNECTING ||
            _session.getState() == LiveSessionState::RECONNECTING ||
            _profileChangePending ||
            _modeSwitchPending);
}

void VoxCoreVoice::setVolume(uint8_t percent) {
    _config.volume = percent;
    _player.setVolume(percent);
}

uint8_t VoxCoreVoice::getVolume() const {
    return _player.getVolume();
}

// =============================================================================
// Implémentation Configuration Vocale & Profils (v0.4.1 / v0.4.1.1)
// =============================================================================

bool VoxCoreVoice::setVoiceProfile(const String& voice, const String& language,
                                   VoiceStyle style, VoiceTone tone,
                                   const String& accent) {
    if (voice.isEmpty()) {
        _lastError = "Nom de voix invalide (vide)";
        Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
        return false;
    }

    // 1. Refus strict si le moteur est occupé
    if (isBusy()) {
        _lastError = "Impossible de modifier le profil vocal pendant une activite vocale active";
        Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
        return false;
    }

    // 2. Refus strict si une connexion ou reconnexion est déjà en cours
    if (isConnecting()) {
        _lastError = "Connexion ou reconnexion deja en cours, modification de profil refusee";
        Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
        return false;
    }

    // 3. Cas hors-ligne / session non-connectée : application directe pour la future session
    if (!_client.isConnected() || _session.getState() == LiveSessionState::DISCONNECTED) {
        _config.voice = voice;
        _config.language = language;
        _config.style = style;
        _config.tone = tone;
        _config.accent = accent;

        _client.setVoice(voice);
        _client.setLanguage(language);
        _client.setVoiceDirectives(_config.buildVoiceDirectives());
        _client.setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);

        Serial.printf("[VoiceKit] Profil vocal configure (hors ligne) : Voix=%s | Langue=%s | Style=%s | Ton=%s%s%s\n",
                      voice.c_str(), language.c_str(), voiceStyleToString(style), voiceToneToString(tone),
                      accent.isEmpty() ? "" : " | Accent=", accent.isEmpty() ? "" : accent.c_str());
        return true;
    }

    // 4. Session connectée et au repos : reconnexion sécurisée maîtrisée
    // Conserver le profil précédent intact dans _config jusqu'à validation setupComplete
    _profileChangePending = true;
    _pendingVoice = voice;
    _pendingLanguage = language;
    _pendingStyle = style;
    _pendingTone = tone;
    _pendingAccent = accent;

    // Prépare le client Gemini pour la nouvelle trame de setup
    VoiceKitConfig tempCfg = _config;
    tempCfg.voice = voice;
    tempCfg.language = language;
    tempCfg.style = style;
    tempCfg.tone = tone;
    tempCfg.accent = accent;

    _client.setVoice(voice);
    _client.setLanguage(language);
    _client.setVoiceDirectives(tempCfg.buildVoiceDirectives());
    _client.setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);

    Serial.printf("[VoiceKit] Demande de nouveau profil vocal : Voix=%s | Langue=%s | Style=%s | Ton=%s%s%s\n",
                  voice.c_str(), language.c_str(), voiceStyleToString(style), voiceToneToString(tone),
                  accent.isEmpty() ? "" : " | Accent=", accent.isEmpty() ? "" : accent.c_str());
    Serial.println("[VoiceKit] Reconnexion securisee pour prise en compte du nouveau profil vocal...");

    _session.deactivate();
    setState(VoiceState::CONNECTING_SERVICE);
    _connectingStartTimeMs = millis();

    bool ok = _session.activate();
    if (!ok) {
        // En cas d'échec immédiat d'activation, restaurer immédiatement le profil actif dans le client
        _client.setVoice(_config.voice);
        _client.setLanguage(_config.language);
        _client.setVoiceDirectives(_config.buildVoiceDirectives());
        _profileChangePending = false;
        _lastError = "Echec activation session pour reconnexion profil";
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    return true;
}

bool VoxCoreVoice::setVoice(const String& voice) {
    return setVoiceProfile(voice, _config.language, _config.style, _config.tone, _config.accent);
}

bool VoxCoreVoice::setLanguage(const String& language) {
    return setVoiceProfile(_config.voice, language, _config.style, _config.tone, _config.accent);
}

bool VoxCoreVoice::setStyle(VoiceStyle style) {
    return setVoiceProfile(_config.voice, _config.language, style, _config.tone, _config.accent);
}

bool VoxCoreVoice::setTone(VoiceTone tone) {
    return setVoiceProfile(_config.voice, _config.language, _config.style, tone, _config.accent);
}

bool VoxCoreVoice::setAccent(const String& accent) {
    return setVoiceProfile(_config.voice, _config.language, _config.style, _config.tone, accent);
}

// =============================================================================
// Implémentation Conversation Bidirectionnelle (v0.4.0 / v0.4.1.1)
// =============================================================================

bool VoxCoreVoice::enableConversation(bool enable) {
    // Si déjà dans le mode demandé et aucune transition en cours
    if (enable == _conversationEnabled && !_modeSwitchPending) {
        return true;
    }

    // 1. Refus strict si le moteur est actif ou en cours de tour
    if (isBusy()) {
        _lastError = "Impossible de changer de mode pendant un tour actif ou une activite vocale";
        Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
        return false;
    }

    // 2. Refus strict si une connexion ou reconnexion est déjà en cours
    if (isConnecting()) {
        _lastError = "Connexion ou reconnexion en cours, basculement de mode refuse";
        Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
        return false;
    }

    // 3. Cas hors-ligne / session non-connectée : application directe sans reconnexion
    if (!_client.isConnected() || _session.getState() == LiveSessionState::DISCONNECTED) {
        _conversationEnabled = enable;
        _config.enableConversation = enable;
        _client.setSystemInstruction(enable ? _config.conversationInstruction : _config.systemInstruction);
        _client.setVoiceDirectives(_config.buildVoiceDirectives());
        Serial.printf("[VoiceKit] Mode conversation %s (hors ligne)\n", enable ? "ACTIVE" : "DESACTIVE");
        return true;
    }

    // 4. Session connectée et au repos : transition maîtrisée via reconnexion
    _modeSwitchPending = true;
    _previousConversationMode = _conversationEnabled;

    _conversationEnabled = enable;
    _config.enableConversation = enable;
    _client.setSystemInstruction(enable ? _config.conversationInstruction : _config.systemInstruction);
    _client.setVoiceDirectives(_config.buildVoiceDirectives());

    Serial.printf("[VoiceKit] Transition vers mode %s -> reconnexion securisee pour injection instructions...\n",
                  enable ? "CONVERSATION" : "TTS/STT");

    _session.deactivate();
    setState(VoiceState::CONNECTING_SERVICE);
    _connectingStartTimeMs = millis();

    bool ok = _session.activate();
    if (!ok) {
        // En cas d'échec immédiat d'activation, restaurer le mode précédent
        _conversationEnabled = _previousConversationMode;
        _config.enableConversation = _previousConversationMode;
        _client.setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
        _client.setVoiceDirectives(_config.buildVoiceDirectives());
        _modeSwitchPending = false;
        _lastError = "Echec activation session pour transition de mode";
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    return true;
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

