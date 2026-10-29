#include "VoxCoreVoice.h"
#include <WiFi.h>
#include <time.h>

#if defined(ESP32) || defined(ARDUINO)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

VoxCoreVoice::VoxCoreVoice() {
    _geminiProvider.setClient(&_client);
    _activeProvider = &_geminiProvider;
    _wakeWordManager.begin(&_streamer);
    _wakeWordManager.setVoiceBusyCheckCallback([this]() {
        return this->isBusy() || this->isSpeaking();
    });
}

VoxCoreVoice::~VoxCoreVoice() {
    _wakeWordManager.disable();
    _onStateChanged = nullptr;
    _onReady = nullptr;
    _onError = nullptr;
    _onSpeechStarted = nullptr;
    _onSpeechFinished = nullptr;
    _onTranscription = nullptr;
    _onListeningStarted = nullptr;
    _onListeningFinished = nullptr;
    _onUserText = nullptr;
    _onAssistantText = nullptr;
    _onConversationFinished = nullptr;
    _onConversationTurnMetrics = nullptr;
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

void VoxCoreVoice::updateConnectionMetrics() {
    _connectionMetrics.wifiConnectMs = _wifi.getWifiConnectMs();
    _connectionMetrics.sntpSyncMs = _wifi.getSntpSyncMs();
    _connectionMetrics.wsConnectMs = _activeProvider ? _activeProvider->getWsConnectMs() : 0;
    _connectionMetrics.setupCompleteMs = _activeProvider ? _activeProvider->getSetupCompleteMs() : 0;
    _connectionMetrics.wifiAttempts = _wifi.getWifiAttempts();
    _connectionMetrics.wifiReconnects = _wifi.getWifiReconnects();
    _connectionMetrics.geminiAttempts = _activeProvider ? _activeProvider->getConnectAttempts() : 0;
    _connectionMetrics.geminiReconnects = _session.getReconnectCount();
    _connectionMetrics.fastConnectEnabled = _config.enableFastConnect;
    _connectionMetrics.fastConnectAttempted = _wifi.isFastConnectAttempted();
    _connectionMetrics.fastConnectSuccess = _wifi.isFastConnectSuccess();
    _connectionMetrics.sntpSkipped = _wifi.isSntpSkipped();
    _connectionMetrics.channel = _wifi.getChannel();
    strncpy(_connectionMetrics.bssidStr, _wifi.getBSSID(), sizeof(_connectionMetrics.bssidStr) - 1);
    _connectionMetrics.bssidStr[sizeof(_connectionMetrics.bssidStr) - 1] = '\0';

    // Diagnostics FreeRTOS, TLS et mémoire interne (v0.6.1 — Corrections B & C)
    if (_activeProvider) {
        _connectionMetrics.wsTaskCore = _activeProvider->getWsTaskCore();
        _connectionMetrics.wsTaskPriority = _activeProvider->getWsTaskPriority();
        _connectionMetrics.wsHandshakeTaskPriority = _activeProvider->getWsHandshakeTaskPriority();
        _connectionMetrics.wsSteadyTaskPriority = _activeProvider->getWsSteadyTaskPriority();
        uint32_t provFreeInt = _activeProvider->getFreeInternalHeapDuringTls();
        uint32_t provMinInt = _activeProvider->getMinFreeInternalHeapDuringTls();
        uint32_t provLargestInt = _activeProvider->getLargestInternalBlockDuringTls();
        if (provFreeInt > 0 && (_connectionMetrics.freeInternalHeapDuringTls == 0 || provFreeInt < _connectionMetrics.freeInternalHeapDuringTls)) {
            _connectionMetrics.freeInternalHeapDuringTls = provFreeInt;
        }
        if (provMinInt > 0) {
            _connectionMetrics.minFreeInternalHeapDuringTls = provMinInt;
        }
        if (provLargestInt > 0 && (_connectionMetrics.largestInternalBlockDuringTls == 0 || provLargestInt < _connectionMetrics.largestInternalBlockDuringTls)) {
            _connectionMetrics.largestInternalBlockDuringTls = provLargestInt;
        }
    }
    _connectionMetrics.lastWifiTickDurationUs = _wifi.getLastTickDurationUs();
    _connectionMetrics.maxWifiTickDurationUs = _wifi.getMaxTickDurationUs();

    if (_totalReadyMs == 0 && _initStartTimeMs > 0) {
        _totalReadyMs = millis() - _initStartTimeMs;
        _connectionMetrics.totalReadyMs = _totalReadyMs;
        Serial.printf("[VoiceKit] Demarrage complet READY en %u ms (WiFi: %u ms | SNTP: %u ms | WS: %u ms | Setup: %u ms)\n",
                      (unsigned)_totalReadyMs,
                      (unsigned)_connectionMetrics.wifiConnectMs,
                      (unsigned)_connectionMetrics.sntpSyncMs,
                      (unsigned)_connectionMetrics.wsConnectMs,
                      (unsigned)_connectionMetrics.setupCompleteMs);
    }
}

void VoxCoreVoice::setState(VoiceState newState) {
    if (_state != newState) {
        if (newState == VoiceState::ERROR_STATE &&
            (_state == VoiceState::CONNECTING_WIFI || _state == VoiceState::CONNECTING_SERVICE)) {
            _connectionMetrics.connectionFailures++;
        }
        _state = newState;
        if (_state == VoiceState::READY) {
            updateConnectionMetrics();
        }
        if (_onStateChanged) {
            _onStateChanged(_state);
        }
    }
}

bool VoxCoreVoice::begin(const VoiceKitConfig& config) {
    _config = config;
    _lastError = "";
    _initStartTimeMs = millis();
    _totalReadyMs = 0;
    _lastUpdateCallMs = 0;
    _connectionMetrics = ConnectionMetrics();
    _connectionMetrics.fastConnectEnabled = _config.enableFastConnect;
#if defined(ESP32) || defined(ARDUINO)
    _connectionMetrics.loopTaskCore = (int8_t)xPortGetCoreID();
    _connectionMetrics.loopTaskPriority = (uint8_t)uxTaskPriorityGet(NULL);
#else
    _connectionMetrics.loopTaskCore = 1;
    _connectionMetrics.loopTaskPriority = 1;
#endif

    Serial.println("\n========================================================");
    Serial.println("  VoxCore VoiceKit v0.6.2.1 — Multi-Fournisseurs & Wake ");
    Serial.printf("  Fournisseur : %s | Mode Audio : %s | FastConnect : %s\n",
                  (_config.provider == VoiceProviderType::OPENAI) ? "OPENAI REALTIME" : "GEMINI LIVE",
                  (LiveAudioPlayer::getCompiledAudioMode() == 1) ? "MODE 1 (REACTIVE_CALLBACK)" : "MODE 2 (DEDICATED_AUDIO_TASK)",
                  _config.enableFastConnect ? "ACTIF" : "DESACTIVE");
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

    // 4b. Liaison du gestionnaire Wake Word sur le streamer audio partagé (v0.6.2 / v0.6.2.1)
    _wakeWordManager.begin(&_streamer);
    _wakeWordManager.setVoiceBusyCheckCallback([this]() {
        return this->isBusy() || this->isSpeaking();
    });
    if (!_config.wakeWordPhrase.isEmpty()) {
        _wakeWordManager.setTargetWakeWord(_config.wakeWordPhrase);
    }
    _wakeWordManager.setAutoResumeAfterVoiceActivity(_config.wakeWordAutoResume);
    _wakeWordManager.setResumeDelayMs(_config.wakeWordResumeDelayMs);
    _wakeWordManager.setMaxFramesPerUpdate(_config.wakeWordMaxFramesPerUpdate);
    _wakeWordManager.setProcessingBudgetUs(_config.wakeWordProcessingBudgetUs);

    // 5. Initialisation du fournisseur vocal actif (Gemini Live ou OpenAI Realtime, ou mode hors ligne sans clé API)
    if (_config.provider == VoiceProviderType::OPENAI) {
        _activeProvider = &_openaiClient;
        if (!_config.apiKey.isEmpty()) {
            if (!_openaiClient.begin(_config)) {
                _lastError = _openaiClient.getLastError();
                Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
                setState(VoiceState::ERROR_STATE);
                if (_onError) _onError(_lastError);
                return false;
            }
        } else {
            Serial.println("[VoiceKit] Mode hors ligne (aucune cle API OpenAI) : capture audio locale active.");
        }
    } else {
        _geminiProvider.setClient(&_client);
        _activeProvider = &_geminiProvider;
        if (!_config.apiKey.isEmpty()) {
            if (!_client.begin(_config.apiKey.c_str(), _config.model.c_str(), _config.voice.c_str())) {
                _lastError = _client.getLastError();
                Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
                setState(VoiceState::ERROR_STATE);
                if (_onError) _onError(_lastError);
                return false;
            }
        } else {
            Serial.println("[VoiceKit] Mode hors ligne (aucune cle API Gemini) : capture audio locale active.");
        }
        _client.setLanguage(_config.language);
        _client.setVoiceDirectives(_config.buildVoiceDirectives());
        _client.setSystemInstruction(_config.enableConversation ? _config.conversationInstruction : _config.systemInstruction);
    }

    // Enregistrement de l'écouteur de transcriptions entrantes utilisateur
    _activeProvider->setOnInputTranscription([this](const String& text, bool isFinal) {
        if (_conversationEnabled) {
            _conversationController.handleUserTranscription(text, isFinal);
        }
        handleIncomingTranscription(text, isFinal);
    });

    // 6. Initialisation du contrôleur de session LiveSessionController
    if (!_session.begin(_activeProvider, &_player)) {
        _lastError = "Echec initialisation LiveSessionController";
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    _session.setNetworkCheck([this]() {
        if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
            return _wifi.isConnected() && _wifi.isTimeSynced();
        } else {
            return (WiFi.status() == WL_CONNECTED && isSystemTimeValid());
        }
    });

    // Écouteur de transcription de sortie (Mode conversation)
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
    _conversationController.begin(_activeProvider, &_session, &_streamer, &_player, _config);

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
                if (_profileChangePending && _activeProvider) {
                    _activeProvider->setVoice(_config.voice);
                    _activeProvider->setLanguage(_config.language);
                    _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
                    _profileChangePending = false;
                    Serial.println("[VoiceKit] Echec reconnexion profil : profil precedent restaure");
                }
                if (_modeSwitchPending && _activeProvider) {
                    _conversationEnabled = _previousConversationMode;
                    _config.enableConversation = _previousConversationMode;
                    _activeProvider->setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
                    _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
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
        if (_activeProvider) {
            _latencyMetrics.firstResponseReceivedMs = _activeProvider->getFirstResponseReceivedMs();
            _latencyMetrics.firstAudioReceivedMs = _activeProvider->getFirstAudioReceivedMs();
        }
        _latencyMetrics.firstAudioPlayedMs = _player.getFirstPlayRawAcceptedTimeMs();
        setState(VoiceState::SPEAKING);
        if (_onSpeechStarted) {
            _onSpeechStarted();
        }
    });

    _session.setOnPlaybackFinished([this]() {
        if (_state == VoiceState::LISTENING || _state == VoiceState::TRANSCRIBING) {
            return;
        }
        _latencyMetrics.playbackFinishedMs = _player.getPlaybackFinishedTimeMs();
        if (_latencyMetrics.playbackFinishedMs == 0) {
            _latencyMetrics.playbackFinishedMs = millis();
            if (_latencyMetrics.playbackFinishedMs == 0) _latencyMetrics.playbackFinishedMs = 1;
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
        if (_profileChangePending && _activeProvider) {
            _activeProvider->setVoice(_config.voice);
            _activeProvider->setLanguage(_config.language);
            _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
            _profileChangePending = false;
            Serial.println("[VoiceKit] Erreur session : profil vocal precedent restaure");
        }
        if (_modeSwitchPending && _activeProvider) {
            _conversationEnabled = _previousConversationMode;
            _config.enableConversation = _previousConversationMode;
            _activeProvider->setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
            _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
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
            Serial.printf("[VoiceKit] Demarrage WiFiManager gere (SSID: %s, FastConnect: %s)...\n",
                          _config.wifiSSID.c_str(), _config.enableFastConnect ? "OUI" : "NON");
            _wifi.begin(_config.wifiSSID.c_str(), _config.wifiPassword.c_str(), _config.enableFastConnect, _config.fastConnectTimeoutMs);
            setState(VoiceState::CONNECTING_WIFI);
        } else {
            Serial.println("[VoiceKit] AVERTISSEMENT : Mode MANAGED_WIFI sans SSID");
            setState(VoiceState::IDLE);
        }
    } else {
        Serial.println("[VoiceKit] Mode EXTERNAL_WIFI : Wi-Fi gere par l'application hote.");
        setState(VoiceState::IDLE);
    }

    // 8. Configuration optionnelle du détecteur Wake Word (v0.6.2)
    if (_config.wakeWordEngine != _wakeWordManager.getEngineType()) {
        _wakeWordManager.setEngineType(_config.wakeWordEngine);
    }
    if (_config.enableWakeWord) {
        _wakeWordManager.enable(true);
    }

    Serial.println("[VoiceKit] Initialisation TTS + STT terminee avec succes.");
    return true;
}

void VoxCoreVoice::update() {
    const uint32_t updateStartUs = micros();
    const uint32_t now = millis();

#if defined(ESP32) || defined(ARDUINO)
    _connectionMetrics.loopTaskCore = (int8_t)xPortGetCoreID();
    _connectionMetrics.loopTaskPriority = (uint8_t)uxTaskPriorityGet(NULL);
#endif

    // Instrumentation des intervalles entre appels applicatifs et pendant le handshake TLS (v0.6.1 — Correction B)
    if (_lastUpdateCallMs > 0 && now >= _lastUpdateCallMs) {
        uint32_t gapMs = now - _lastUpdateCallMs;
        _connectionMetrics.lastLoopGapMs = gapMs;
        if (gapMs > _connectionMetrics.maxLoopGapMs) {
            _connectionMetrics.maxLoopGapMs = gapMs;
        }
        if (_state == VoiceState::CONNECTING_SERVICE ||
            _session.getState() == LiveSessionState::CONNECTING ||
            _session.getState() == LiveSessionState::RECONNECTING) {
            if (gapMs > _connectionMetrics.maxLoopGapDuringTlsMs) {
                _connectionMetrics.maxLoopGapDuringTlsMs = gapMs;
            }
#if defined(ESP32) || defined(ARDUINO)
            uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            uint32_t minFreeInt = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (_connectionMetrics.freeInternalHeapDuringTls == 0 || freeInt < _connectionMetrics.freeInternalHeapDuringTls) {
                _connectionMetrics.freeInternalHeapDuringTls = freeInt;
            }
            _connectionMetrics.minFreeInternalHeapDuringTls = minFreeInt;
            if (_connectionMetrics.largestInternalBlockDuringTls == 0 || largestInt < _connectionMetrics.largestInternalBlockDuringTls) {
                _connectionMetrics.largestInternalBlockDuringTls = largestInt;
            }
#endif
        }
    }
    _lastUpdateCallMs = now;

    // 1. Mise à jour réseau Wi-Fi / SNTP si en mode MANAGED_WIFI
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        _wifi.tick();
        _connectionMetrics.lastWifiTickDurationUs = _wifi.getLastTickDurationUs();
        _connectionMetrics.maxWifiTickDurationUs = _wifi.getMaxTickDurationUs();
        if (_wifi.isConnected() && _wifi.isTimeSynced() && !_config.apiKey.isEmpty()) {
            if (_state == VoiceState::CONNECTING_WIFI || _state == VoiceState::IDLE || _state == VoiceState::DISCONNECTED || _state == VoiceState::ERROR_STATE) {
                if ((!_activeProvider || !_activeProvider->isConnected()) && (_session.getState() == LiveSessionState::DISCONNECTED || _session.getState() == LiveSessionState::ERROR)) {
                    Serial.println("[VoiceKit] Wi-Fi et NTP valides -> Connexion au service vocal...");
                    setState(VoiceState::CONNECTING_SERVICE);
                    _connectingStartTimeMs = millis();
                    _session.activate();
                }
            }
        } else if (!_wifi.isConnected() && !_config.wifiSSID.isEmpty()) {
            if (_state != VoiceState::CONNECTING_WIFI && _state != VoiceState::DISCONNECTED && _state != VoiceState::ERROR_STATE) {
                Serial.println("[VoiceKit] Perte Wi-Fi detectee -> transition CONNECTING_WIFI");
                setState(VoiceState::CONNECTING_WIFI);
            }
        }
    } else {
        // En mode EXTERNAL_WIFI : connexion automatique dès que l'application hôte a établi le réseau (si une clé API est fournie)
        if (_state == VoiceState::IDLE || _state == VoiceState::CONNECTING_WIFI || _state == VoiceState::DISCONNECTED || _state == VoiceState::ERROR_STATE) {
            if (!_config.apiKey.isEmpty() && WiFi.status() == WL_CONNECTED && isSystemTimeValid()) {
                if ((!_activeProvider || !_activeProvider->isConnected()) && (_session.getState() == LiveSessionState::DISCONNECTED || _session.getState() == LiveSessionState::ERROR)) {
                    Serial.println("[VoiceKit] Reseau hote & NTP valides -> Connexion au service vocal...");
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
            setState(VoiceState::CONNECTING_WIFI);
            if (_onError) _onError(_lastError);
        }
    }

    // 2. Traitement régulier des moteurs VoxCore
    if (_activeProvider) {
        _activeProvider->tick();
    }
    _player.tick();
    _session.tick();

    if (_conversationEnabled) {
        _conversationController.tick();
    }

    // 3. Traitement régulier du streamer audio STT
    if (_streamer.isStreaming()) {
        if (!_activeProvider || !_activeProvider->isConnected()) {
            _lastError = "Perte connexion service vocal pendant ecoute micro";
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
            Serial.println("[VoiceKit] Avertissement : Timeout attente transcription finale");
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
        uint32_t timeoutLimit = (_config.provider == VoiceProviderType::OPENAI) ? 15000 : (GEMINI_LIVE_CONNECT_TIMEOUT_MS + 2000);
        if (_connectingStartTimeMs > 0 && (millis() - _connectingStartTimeMs > timeoutLimit)) {
            Serial.println("[VoiceKit] ERREUR : Delai de connexion depasse vers service vocal");
            if (_profileChangePending && _activeProvider) {
                _activeProvider->setVoice(_config.voice);
                _activeProvider->setLanguage(_config.language);
                _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
                _profileChangePending = false;
                Serial.println("[VoiceKit] Timeout reconnexion profil : profil vocal precedent restaure");
            }
            if (_modeSwitchPending && _activeProvider) {
                _conversationEnabled = _previousConversationMode;
                _config.enableConversation = _previousConversationMode;
                _activeProvider->setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
                _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
                _modeSwitchPending = false;
                Serial.println("[VoiceKit] Timeout reconnexion mode : mode precedent restaure");
            }
            _lastError = "Timeout connexion service vocal";
            _connectingStartTimeMs = 0;
            _pendingText = "";
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
        }
    }

    // 7. Mise à jour du gestionnaire de mot de réveil (suspension/reprise et traitement hors callback audio)
    if (_wakeWordManager.isEnabled()) {
        _wakeWordManager.notifyVoiceActivity(isBusy(), isSpeaking());
        _wakeWordManager.update();
    }

    const uint32_t updateDurUs = micros() - updateStartUs;
    _connectionMetrics.lastUpdateDurationUs = updateDurUs;
    if (updateDurUs > _connectionMetrics.maxUpdateDurationUs) {
        _connectionMetrics.maxUpdateDurationUs = updateDurUs;
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

    // Réarmer la restitution haut-parleur (et suspendre Wake Word pour éviter conflit I2S / larsen)
    if (_wakeWordManager.isEnabled() &&
        (_wakeWordManager.getState() == WakeWordState::ARMED ||
         _wakeWordManager.getState() == WakeWordState::DETECTED ||
         _streamer.isLocalCapture())) {
        _wakeWordManager.notifyVoiceActivity(true, true);
        if (_wakeWordManager.isError()) {
            _lastError = _wakeWordManager.getLastError();
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
            return false;
        }
    } else if (_streamer.isLocalCapture()) {
        if (!_streamer.stopLocalCapture()) {
            _lastError = _streamer.getLastError();
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
            return false;
        }
    }
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
    _latencyMetrics.reset();
    _latencyMetrics.turnStartedMs = millis();
    if (_latencyMetrics.turnStartedMs == 0) _latencyMetrics.turnStartedMs = 1;
    bool ok = _session.sendTextMessage(prompt);
    if (!ok) {
        _lastError = "Echec envoi message texte vers service vocal";
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }
    _latencyMetrics.activityEndSentMs = _activeProvider ? _activeProvider->getActivityEndSentMs() : 0;
    if (_latencyMetrics.activityEndSentMs == 0) {
        _latencyMetrics.activityEndSentMs = millis();
        if (_latencyMetrics.activityEndSentMs == 0) _latencyMetrics.activityEndSentMs = 1;
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
    if (_profileChangePending && _activeProvider) {
        _activeProvider->setVoice(_config.voice);
        _activeProvider->setLanguage(_config.language);
        _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
        _profileChangePending = false;
        Serial.println("[VoiceKit] stop() : profil vocal precedent restaure");
    }
    if (_modeSwitchPending && _activeProvider) {
        _conversationEnabled = _previousConversationMode;
        _config.enableConversation = _previousConversationMode;
        _activeProvider->setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
        _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
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
    if (!_activeProvider || !_activeProvider->isConnected() || _session.getState() == LiveSessionState::DISCONNECTED) {
        _config.voice = voice;
        _config.language = language;
        _config.style = style;
        _config.tone = tone;
        _config.accent = accent;

        if (_activeProvider) {
            _activeProvider->setVoice(voice);
            _activeProvider->setLanguage(language);
            _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
            _activeProvider->setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
        }

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

    // Prépare le fournisseur pour la nouvelle trame de configuration
    VoiceKitConfig tempCfg = _config;
    tempCfg.voice = voice;
    tempCfg.language = language;
    tempCfg.style = style;
    tempCfg.tone = tone;
    tempCfg.accent = accent;

    if (_activeProvider) {
        _activeProvider->setVoice(voice);
        _activeProvider->setLanguage(language);
        _activeProvider->setVoiceDirectives(tempCfg.buildVoiceDirectives());
        _activeProvider->setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
    }

    Serial.printf("[VoiceKit] Demande de nouveau profil vocal : Voix=%s | Langue=%s | Style=%s | Ton=%s%s%s\n",
                  voice.c_str(), language.c_str(), voiceStyleToString(style), voiceToneToString(tone),
                  accent.isEmpty() ? "" : " | Accent=", accent.isEmpty() ? "" : accent.c_str());
    Serial.println("[VoiceKit] Reconnexion securisee pour prise en compte du nouveau profil vocal...");

    _session.deactivate();
    setState(VoiceState::CONNECTING_SERVICE);
    _connectingStartTimeMs = millis();

    bool ok = _session.activate();
    if (!ok) {
        // En cas d'échec immédiat d'activation, restaurer immédiatement le profil actif
        if (_activeProvider) {
            _activeProvider->setVoice(_config.voice);
            _activeProvider->setLanguage(_config.language);
            _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
        }
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
// Implémentation Conversation Bidirectionnelle (v0.4.0 / v0.4.1.1 / v0.6.0)
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
    if (!_activeProvider || !_activeProvider->isConnected() || _session.getState() == LiveSessionState::DISCONNECTED) {
        _conversationEnabled = enable;
        _config.enableConversation = enable;
        if (_activeProvider) {
            _activeProvider->setSystemInstruction(enable ? _config.conversationInstruction : _config.systemInstruction);
            _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
            _activeProvider->setConversationMode(enable);
        }
        Serial.printf("[VoiceKit] Mode conversation %s (hors ligne)\n", enable ? "ACTIVE" : "DESACTIVE");
        return true;
    }

    // 4. Session connectée et au repos : transition maîtrisée via reconnexion
    _modeSwitchPending = true;
    _previousConversationMode = _conversationEnabled;

    _conversationEnabled = enable;
    _config.enableConversation = enable;
    if (_activeProvider) {
        _activeProvider->setSystemInstruction(enable ? _config.conversationInstruction : _config.systemInstruction);
        _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
        _activeProvider->setConversationMode(enable);
    }

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
        if (_activeProvider) {
            _activeProvider->setSystemInstruction(_conversationEnabled ? _config.conversationInstruction : _config.systemInstruction);
            _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
            _activeProvider->setConversationMode(_conversationEnabled);
        }
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

    // Suspension automatique du détecteur Wake Word pendant tout tour de conversation ou écoute STT
    if (_wakeWordManager.isEnabled() &&
        (_wakeWordManager.getState() == WakeWordState::ARMED ||
         _wakeWordManager.getState() == WakeWordState::DETECTED ||
         _streamer.isLocalCapture())) {
        _wakeWordManager.notifyVoiceActivity(true, false);
        if (_wakeWordManager.isError()) {
            _lastError = _wakeWordManager.getLastError();
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
            return false;
        }
    } else if (_wakeWordManager.isEnabled() && _wakeWordManager.isError() &&
               (_streamer.isLocalCapture() || _streamer.isTaskQuarantined())) {
        _streamer.checkQuarantinedTasks();
        if (_streamer.isLocalCapture() || _streamer.isTaskQuarantined()) {
            _lastError = _wakeWordManager.getLastError();
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
            return false;
        }
    } else if (_streamer.isLocalCapture()) {
        if (!_streamer.stopLocalCapture()) {
            _lastError = _streamer.getLastError();
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
            return false;
        }
    }

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

    // 3. Vérification de la disponibilité du fournisseur vocal actif
    if (!_activeProvider || !_activeProvider->isConnected() || !_activeProvider->isSetupComplete()) {
        _lastError = "Session vocale non prete";
        Serial.printf("[VoiceKit] startListening() refuse : %s\n", _lastError.c_str());
        if (_onError) _onError(_lastError);
        return false;
    }

    // 4. Réinitialisation des états de transcription pour ce tour
    _finalTranscriptionReceived = false;
    _transcriptionPending = false;
    _lastInterimText = "";
    _lastFinalText = "";
    uint32_t prevSpeechEnd = _latencyMetrics.speechEndDetectedMs;
    _latencyMetrics.reset();
    _latencyMetrics.turnStartedMs = millis();
    if (_latencyMetrics.turnStartedMs == 0) _latencyMetrics.turnStartedMs = 1;
    _latencyMetrics.speechEndDetectedMs = prevSpeechEnd;

    // 5. Démarrage effectif du streamer (envoie activityStart et active double-buffering)
    if (!_streamer.start(_activeProvider)) {
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
    if (_stopListeningTimeMs == 0) _stopListeningTimeMs = 1;
    _latencyMetrics.micStoppedMs = _stopListeningTimeMs;
    if (_streamer.getActivityEndSentMs() > 0) {
        _latencyMetrics.activityEndSentMs = _streamer.getActivityEndSentMs();
    } else if (_activeProvider && _activeProvider->getActivityEndSentMs() > 0) {
        _latencyMetrics.activityEndSentMs = _activeProvider->getActivityEndSentMs();
    }

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
    _wakeWordManager.notifyVoiceActivity(isBusy(), isSpeaking());
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
    if (_latencyMetrics.firstResponseReceivedMs == 0 && _activeProvider) {
        _latencyMetrics.firstResponseReceivedMs = _activeProvider->getFirstResponseReceivedMs();
        if (_latencyMetrics.firstResponseReceivedMs == 0) {
            _latencyMetrics.firstResponseReceivedMs = millis();
            if (_latencyMetrics.firstResponseReceivedMs == 0) _latencyMetrics.firstResponseReceivedMs = 1;
        }
    }
    if (isFinal) {
        _latencyMetrics.playbackFinishedMs = millis();
        if (_latencyMetrics.playbackFinishedMs == 0) _latencyMetrics.playbackFinishedMs = 1;
        Serial.printf("[VoiceKit] Transcription FINALE recue : \"%s\"\n", text.c_str());
        _finalTranscriptionReceived = true;
        _transcriptionPending = false;
        _lastFinalText = text;
        _player.setPlaybackAllowed(true);

        if (!_conversationEnabled) {
            if (_state == VoiceState::TRANSCRIBING || _state == VoiceState::LISTENING) {
                setState(VoiceState::READY);
            }
            _wakeWordManager.notifyVoiceActivity(isBusy(), isSpeaking());
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

const VoiceLatencyMetrics& VoxCoreVoice::getLatencyMetrics() const {
    if (_conversationEnabled) {
        return _conversationController.getLatencyMetrics();
    }
    return _latencyMetrics;
}

void VoxCoreVoice::notifySpeechEndDetected(uint32_t ms) {
    _latencyMetrics.speechEndDetectedMs = ms;
    if (_conversationEnabled) {
        _conversationController.notifySpeechEndDetected(ms);
    }
}

// =============================================================================
// Implémentation Détection Locale de Mot de Réveil (Wake Word v0.6.2)
// =============================================================================

bool VoxCoreVoice::enableWakeWord(bool enable) {
    if (enable) {
        _wakeWordManager.notifyVoiceActivity(isBusy(), isSpeaking());
    }
    return _wakeWordManager.enable(enable);
}

void VoxCoreVoice::disableWakeWord() {
    _wakeWordManager.disable();
}

bool VoxCoreVoice::suspendWakeWord() {
    return _wakeWordManager.suspend();
}

bool VoxCoreVoice::resumeWakeWord() {
    _wakeWordManager.notifyVoiceActivity(isBusy(), isSpeaking());
    return _wakeWordManager.resume();
}
