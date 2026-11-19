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
        return this->isBusy() || this->isSpeaking() || _wakeWordConversationController.isSessionInProgress();
    });
    _wakeWordManager.setInternalEventHook([this](const WakeWordEvent& ev) {
        if (_wakeWordConversationController.isEnabled()) {
            _wakeWordConversationController.notifyWakeWordDetected(ev);
        }
    });
    _wakeWordConversationController.begin(&_wakeWordManager, _activeProvider, &_session,
                                          &_conversationController, &_streamer, &_player);
}

VoxCoreVoice::~VoxCoreVoice() {
    _wakeWordConversationController.disable();
    _wakeWordManager.disable();
    _onStateChanged = nullptr;
    _onReady = nullptr;
    _onError = nullptr;
    _onSpeechStarted = nullptr;
    _onSpeechFinished = nullptr;
    _onSpeechFinishedWithId = nullptr;
    _onTranscription = nullptr;
    _onTranscriptionWithTurn = nullptr;
    _onListeningStarted = nullptr;
    _onListeningStartedWithTurn = nullptr;
    _onListeningFinished = nullptr;
    _onUserText = nullptr;
    _onAssistantText = nullptr;
    _onConversationFinished = nullptr;
    _onConversationTurnMetrics = nullptr;
    _onProfileApplied = nullptr;
    _onProfileError = nullptr;
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
    _profileChangePending = false;
    _pendingProviderSwitch = false;
    _modeSwitchPending = false;
    _autoReconnectSuspended = false;

    // Synchronisation bidirectionnelle des clés API dédiées (v0.6.6)
    if (_config.provider == VoiceProviderType::OPENAI) {
        if (_config.openAiApiKey.isEmpty() && !_config.apiKey.isEmpty()) {
            _config.openAiApiKey = _config.apiKey;
        }
        if (_config.apiKey.isEmpty() && !_config.openAiApiKey.isEmpty()) {
            _config.apiKey = _config.openAiApiKey;
        }
    } else {
        if (_config.geminiApiKey.isEmpty() && !_config.apiKey.isEmpty()) {
            _config.geminiApiKey = _config.apiKey;
        }
        if (_config.apiKey.isEmpty() && !_config.geminiApiKey.isEmpty()) {
            _config.apiKey = _config.geminiApiKey;
        }
    }

    // Restauration optionnelle du profil vocal sauvegardé en NVS (v0.6.6 — sans clé API)
    if (_config.restoreSavedProfileOnBoot) {
        VoiceProfile restored;
        bool wasRestored = false;
        _profileManager.loadActiveProfile(restored, _config.provider, &wasRestored, resolveProfileNvsNamespace(nullptr));
        if (wasRestored) {
            VoiceProfileManager::applyToConfig(restored, _config);
            _preferredGender = restored.preferredGender;
            _config.apiKey = _config.getApiKeyForProvider(_config.provider);
            Serial.printf("[VoiceKit] Profil vocal restaure depuis NVS : Fournisseur=%s | Voix=%s | Langue=%s\n",
                          voiceProviderTypeToString(_config.provider), _config.voice.c_str(), _config.language.c_str());
        }
    }

    // Ajustement automatique de la voix par défaut si OpenAI est sélectionné avec la voix Gemini par défaut
    if (_config.provider == VoiceProviderType::OPENAI &&
        (_config.voice.isEmpty() || _config.voice == VOICEKIT_DEFAULT_VOICE)) {
        _config.voice = OPENAI_REALTIME_DEFAULT_VOICE;
    }

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
    Serial.println("  VoxCore VoiceKit v0.6.6.2 — Public Release Readiness  ");
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

    // 4b. Liaison du gestionnaire Wake Word sur le streamer audio partagé (v0.6.2 / v0.6.3.1)
    _wakeWordManager.begin(&_streamer);
    _wakeWordManager.setVoiceBusyCheckCallback([this]() {
        return this->isBusy() || this->isSpeaking() || _wakeWordConversationController.isSessionInProgress();
    });
    if (!_config.wakeWordModelName.isEmpty()) {
        _wakeWordManager.setWakeWordModelName(_config.wakeWordModelName);
    } else {
        _wakeWordManager.setWakeWordModelName("wn10_fr_bonjouresp");
    }
    if (!_config.wakeWordPhrase.isEmpty()) {
        _wakeWordManager.setTargetWakeWord(_config.wakeWordPhrase);
    }
    _wakeWordManager.setAutoResumeAfterVoiceActivity(_config.wakeWordAutoResume);
    _wakeWordManager.setResumeDelayMs(_config.wakeWordResumeDelayMs);
    _wakeWordManager.setMaxFramesPerUpdate(_config.wakeWordMaxFramesPerUpdate);
    _wakeWordManager.setProcessingBudgetUs(_config.wakeWordProcessingBudgetUs);
    _wakeWordManager.setInternalEventHook([this](const WakeWordEvent& ev) {
        if (_wakeWordConversationController.isEnabled()) {
            _wakeWordConversationController.notifyWakeWordDetected(ev);
        }
    });

    // 5 & 6. Initialisation du fournisseur vocal actif et liaison au contrôleur de session
    if (!bindProviderForConfig(_config, &_lastError)) {
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

    // Écouteur de transcription de sortie (Mode conversation et requetes IA v0.6.5)
    _session.setOnOutputTranscription([this](const String& text) {
        if (_conversationEnabled) {
            _conversationController.handleAssistantTranscription(text);
        } else if (_onAssistantText) {
            _onAssistantText(text);
        }
    });

    _session.setOnTurnFinished([this](uint64_t turnId) {
        if (_conversationEnabled) {
            _conversationController.handleTurnFinished(turnId);
        }
        if (_wakeWordConversationController.isEnabled()) {
            _wakeWordConversationController.notifyTurnFinished(turnId);
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
        if (s == LiveSessionState::DISCONNECTED || s == LiveSessionState::CONNECTING) {
            _cancelledCaptureTurnId = 0;
        }

        switch (s) {
            case LiveSessionState::CONNECTED: {
                _cancelledCaptureTurnId = 0;
                bool notifyProfileNow = false;
                // Validation définitive du nouveau profil vocal après setupComplete / session.updated (v0.4.1.1 / v0.6.6)
                if (_profileChangePending) {
                    _config.provider = _pendingProfile.provider;
                    String keyForNewProv = _config.getApiKeyForProvider(_pendingProfile.provider);
                    if (!keyForNewProv.isEmpty()) {
                        _config.apiKey = keyForNewProv;
                    }
                    if (_pendingProviderSwitch) {
                        if (_config.provider == VoiceProviderType::OPENAI &&
                            (_config.model.isEmpty() || _config.model.indexOf("gemini") >= 0)) {
                            _config.model = OPENAI_REALTIME_DEFAULT_MODEL;
                        } else if (_config.provider == VoiceProviderType::GEMINI &&
                                   (_config.model.isEmpty() ||
                                    _config.model.indexOf("gpt-") >= 0 ||
                                    _config.model.indexOf("realtime") >= 0)) {
                            _config.model = VOICEKIT_DEFAULT_MODEL;
                        }
                    }
                    _config.voice = _pendingVoice;
                    _config.language = _pendingLanguage;
                    _config.style = _pendingStyle;
                    _config.tone = _pendingTone;
                    _config.accent = _pendingAccent;
                    _preferredGender = _pendingProfile.preferredGender;
                    _conversationController.setProviderAndConfig(_activeProvider, _config);
                    _profileChangePending = false;
                    _pendingProviderSwitch = false;
                    notifyProfileNow = true;
                    Serial.printf("[VoiceKit] Profil vocal confirme par le fournisseur (%s) : Voix=%s | Langue=%s\n",
                                  voiceProviderTypeToString(_config.provider),
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
                    if (notifyProfileNow && _onProfileApplied) {
                        _onProfileApplied(getVoiceProfile());
                    }
                    if (_onReady) {
                        _onReady();
                    }
                } else if (notifyProfileNow && _onProfileApplied) {
                    _onProfileApplied(getVoiceProfile());
                }
                break;
            }
            case LiveSessionState::RECONNECTING:
                if (_profileChangePending) {
                    // Ne jamais appeler _session.deactivate() depuis websocket_task (risque de blocage/crash FreeRTOS) :
                    // différer le rollback propre sur loopTask dans update().
                    _pendingProfileDropRollback.store(true, std::memory_order_release);
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
            case LiveSessionState::ERROR: {
                String sessErr = _session.getLastError();
                if (_profileChangePending) {
                    rollbackPendingProfileChange(
                        sessErr.isEmpty() ? String("Echec reconnexion profil vocal") : sessErr, true);
                }
                if (_modeSwitchPending && _activeProvider) {
                    _conversationEnabled = _previousConversationMode;
                    _config.enableConversation = _previousConversationMode;
                    _activeProvider->setSystemInstruction(
                        _conversationEnabled ? _config.resolveConversationInstruction() : _config.resolveSystemInstruction());
                    _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
                    _modeSwitchPending = false;
                    Serial.println("[VoiceKit] Echec reconnexion mode : mode precedent restaure");
                }
                _lastError = sessErr;
                setState(VoiceState::ERROR_STATE);
                if (_onError) {
                    _onError(_lastError);
                }
                break;
            }
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

    _session.setOnPlaybackFinishedWithId([this](uint32_t finishedId) {
        if (_state == VoiceState::LISTENING || _state == VoiceState::TRANSCRIBING) {
            return;
        }
        // Si aucun TTS n'est actif (ex: après stop()), la notification est orpheline ou annulée
        if (_activeSpeechId == 0) {
            Serial.printf("[STATE][TTS] Notification fin audio orpheline/annulée ignorée : speechId=%u\n",
                          (unsigned)finishedId);
            return;
        }
        // Validation stricte de corrélation temporelle et d'opération :
        // Si un speechId actif est en cours et que la notification porte un identifiant non nul différent,
        // c'est une ancienne notification résiduelle (drain DMA, tâche différée, fin de tour annulé).
        if (finishedId != 0 && finishedId != _activeSpeechId) {
            Serial.printf("[VoiceKit] Notification fin audio IGNOREE : speechId=%u != actif=%u\n",
                          (unsigned)finishedId, (unsigned)_activeSpeechId);
            return;
        }
        _latencyMetrics.playbackFinishedMs = _player.getPlaybackFinishedTimeMs();
        if (_latencyMetrics.playbackFinishedMs == 0) {
            _latencyMetrics.playbackFinishedMs = millis();
            if (_latencyMetrics.playbackFinishedMs == 0) _latencyMetrics.playbackFinishedMs = 1;
        }
        uint32_t finishedIdToReport = (_activeSpeechId != 0) ? _activeSpeechId : finishedId;
        _activeSpeechId = 0;
        Serial.printf("[STATE][TTS] Restitution terminée (speechId=%u)\n", (unsigned)finishedIdToReport);
        setState(VoiceState::READY);
        if (_onSpeechFinishedWithId && finishedIdToReport > 0) {
            _onSpeechFinishedWithId(finishedIdToReport);
        }
        if (_onSpeechFinished) {
            _onSpeechFinished();
        }

        // Dépilement de la requête TTS / IA suivante en file FIFO
        if (!_textQueue.empty()) {
            SpeechQueueItem nextItem = _textQueue.front();
            _textQueue.pop();
            _activeSpeechId = nextItem.speechId;
            if (nextItem.isAiQuestion) {
                dispatchAiQuestion(nextItem.text);
            } else {
                dispatchSpeech(nextItem.text);
            }
        }
    });

    _session.setOnPlaybackFinished([this]() {
        // Maintenu pour compatibilité
    });

    _session.setOnError([this](const String& err) {
        if (_profileChangePending) {
            rollbackPendingProfileChange(err, true);
        }
        if (_modeSwitchPending && _activeProvider) {
            _conversationEnabled = _previousConversationMode;
            _config.enableConversation = _previousConversationMode;
            _activeProvider->setSystemInstruction(_conversationEnabled ? _config.resolveConversationInstruction() : _config.resolveSystemInstruction());
            _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
            _modeSwitchPending = false;
            Serial.println("[VoiceKit] Erreur session : mode precedent restaure");
        }
        _lastError = err;
        _pendingText = "";
        if (_conversationEnabled) {
            _conversationController.requestStop();
        }
        if (_streamer.isStreaming() || isListening() || isTranscribing()) {
            _streamer.cancel();
            _cancelledCaptureTurnId = (_activeCaptureTurnId != 0) ? _activeCaptureTurnId : _pendingTranscriptionTurnId;
            _activeCaptureTurnId = 0;
            _pendingTranscriptionTurnId = 0;
            _transcriptionPending = false;
            _finalTranscriptionReceived = false;
            _player.setPlaybackAllowed(true);
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

    // 8. Configuration optionnelle du détecteur Wake Word (v0.6.2) et du contrôleur Wake Word Conversation (v0.6.3)
    WakeWordConversationConfig wakeConvCfg;
    wakeConvCfg.silenceTimeoutMs = _config.wakeConversationSilenceTimeoutMs;
    wakeConvCfg.maxSessionDurationMs = _config.wakeConversationMaxSessionDurationMs;
    wakeConvCfg.connectTimeoutMs = _config.wakeConversationConnectTimeoutMs;
    wakeConvCfg.responseTimeoutMs = _config.wakeConversationResponseTimeoutMs;
    wakeConvCfg.maxTurnDurationMs = _config.wakeConversationMaxTurnDurationMs;
    wakeConvCfg.turnRestartDelayMs = _config.wakeConversationTurnRestartDelayMs;
    wakeConvCfg.returnToWakeDelayMs = _config.wakeWordResumeDelayMs;
    wakeConvCfg.errorRecoveryDelayMs = _config.wakeConversationErrorRecoveryDelayMs;
    wakeConvCfg.vadThresholdRms = _config.wakeConversationVadThresholdRms;
    wakeConvCfg.vadSilenceDurationMs = _config.vadSilenceDurationMs;
    wakeConvCfg.enableVoiceStopCommand = _config.wakeConversationVoiceStopEnabled;
    wakeConvCfg.disconnectAfterSession = _config.wakeConversationDisconnectAfterSession;

    _wakeWordConversationController.begin(&_wakeWordManager, _activeProvider, &_session,
                                          &_conversationController, &_streamer, &_player, wakeConvCfg);
    _wakeWordConversationController.setProvider(_activeProvider, _config.provider);
    _wakeWordConversationController.setNetworkReadyCallback([this]() {
        if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
            return _wifi.isConnected() && _wifi.isTimeSynced();
        } else {
            return (WiFi.status() == WL_CONNECTED && isSystemTimeValid());
        }
    });
    _wakeWordConversationController.setEnableConversationCallback([this]() {
        _conversationEnabled = true;
        _config.enableConversation = true;
        if (_activeProvider) {
            _activeProvider->setSystemInstruction(_config.resolveConversationInstruction());
            _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
            _activeProvider->setConversationMode(true);
        }
        return true;
    });
    if (_config.wakeWordEngine != _wakeWordManager.getEngineType()) {
        _wakeWordManager.setEngineType(_config.wakeWordEngine);
    }
    if (_config.enableWakeWordConversation) {
        enableWakeWordConversation(true);
    } else if (_config.enableWakeWord) {
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

    const String activeApiKey = _config.getApiKeyForProvider(_config.provider);

    // 0. Rollback différé sur loopTask si la connexion a chuté pendant un changement de profil
    if (_pendingProfileDropRollback.exchange(false, std::memory_order_acq_rel)) {
        if (_profileChangePending) {
            String dropErr = "Deconnexion reseau pendant le changement de profil vocal";
            _session.deactivate();
            rollbackPendingProfileChange(dropErr, true);
            _lastError = dropErr;
            setState(VoiceState::ERROR_STATE);
            if (_onError) {
                _onError(_lastError);
            }
        }
    }

    // 0b. Progression non bloquante d'une fermeture de session demandée
    if (_session.isDeactivating()) {
        if (_session.pollDeactivate()) {
            _connectingStartTimeMs = 0;
            setState(VoiceState::DISCONNECTED);
            Serial.println("[VoiceKit] Session fermee proprement (DISCONNECTED)");
        }
    }

    // 1. Mise à jour réseau Wi-Fi / SNTP si en mode MANAGED_WIFI
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        _wifi.tick();
        _connectionMetrics.lastWifiTickDurationUs = _wifi.getLastTickDurationUs();
        _connectionMetrics.maxWifiTickDurationUs = _wifi.getMaxTickDurationUs();
        if (!_autoReconnectSuspended && !_session.isDeactivating() && !_config.connectOnDemand && !_wakeWordConversationController.isEnabled() && _wifi.isConnected() && _wifi.isTimeSynced() && !activeApiKey.isEmpty()) {
            if (_state == VoiceState::CONNECTING_WIFI || _state == VoiceState::IDLE || _state == VoiceState::DISCONNECTED || _state == VoiceState::ERROR_STATE) {
                if ((!_activeProvider || !_activeProvider->isConnected()) && (_session.getState() == LiveSessionState::DISCONNECTED || _session.getState() == LiveSessionState::ERROR)) {
                    Serial.println("[VoiceKit] Wi-Fi et NTP valides -> Connexion au service vocal...");
                    setState(VoiceState::CONNECTING_SERVICE);
                    _connectingStartTimeMs = millis();
                    _session.activate();
                }
            }
        } else if (!_wifi.isConnected() && !_config.wifiSSID.isEmpty()) {
            if (_profileChangePending) {
                _session.deactivate();
                rollbackPendingProfileChange("Perte Wi-Fi pendant changement de profil vocal", true);
            }
            if (_state != VoiceState::CONNECTING_WIFI && _state != VoiceState::DISCONNECTED && _state != VoiceState::ERROR_STATE) {
                Serial.println("[VoiceKit] Perte Wi-Fi detectee -> transition CONNECTING_WIFI");
                setState(VoiceState::CONNECTING_WIFI);
            }
        }
    } else {
        // En mode EXTERNAL_WIFI : connexion automatique dès que l'application hôte a établi le réseau (si une clé API est fournie)
        if (_state == VoiceState::IDLE || _state == VoiceState::CONNECTING_WIFI || _state == VoiceState::DISCONNECTED || _state == VoiceState::ERROR_STATE) {
            if (!_autoReconnectSuspended && !_session.isDeactivating() && !_config.connectOnDemand && !_wakeWordConversationController.isEnabled() && !activeApiKey.isEmpty() && WiFi.status() == WL_CONNECTED && isSystemTimeValid()) {
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
            if (_profileChangePending) {
                _session.deactivate();
                rollbackPendingProfileChange("Perte Wi-Fi pendant changement de profil vocal", true);
            }
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

    // 5. Traitement des requêtes TTS / IA en attente
    if (!_pendingText.isEmpty()) {
        if (_session.getState() == LiveSessionState::CONNECTED && !isListening() && !isTranscribing()) {
            String textToSay = _pendingText;
            bool isAi = _pendingSpeech.isAiQuestion;
            _activeSpeechId = _pendingSpeech.speechId;
            _pendingSpeech = SpeechQueueItem();
            _pendingText = "";
            if (isAi) {
                dispatchAiQuestion(textToSay);
            } else {
                dispatchSpeech(textToSay);
            }
        }
    }

    // 6. Surveillance des timeouts de connexion initiale
    if (_state == VoiceState::CONNECTING_SERVICE) {
        VoiceProviderType targetProv = _profileChangePending ? _pendingProfile.provider : _config.provider;
        uint32_t timeoutLimit = (targetProv == VoiceProviderType::OPENAI) ? 15000 : (GEMINI_LIVE_CONNECT_TIMEOUT_MS + 2000);
        if (_connectingStartTimeMs > 0 && (millis() - _connectingStartTimeMs > timeoutLimit)) {
            Serial.println("[VoiceKit] ERREUR : Delai de connexion depasse vers service vocal");
            _session.deactivate();
            if (_profileChangePending) {
                rollbackPendingProfileChange("Timeout connexion service vocal lors du changement de profil", true);
            }
            if (_modeSwitchPending && _activeProvider) {
                _conversationEnabled = _previousConversationMode;
                _config.enableConversation = _previousConversationMode;
                _activeProvider->setSystemInstruction(_conversationEnabled ? _config.resolveConversationInstruction() : _config.resolveSystemInstruction());
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
        bool busyForWake = isBusy() || isSpeaking() || _wakeWordConversationController.isSessionInProgress();
        _wakeWordManager.notifyVoiceActivity(busyForWake, isSpeaking());
        _wakeWordManager.update();
    }
    if (_wakeWordConversationController.isEnabled()) {
        _wakeWordConversationController.update(now);
    }

    const uint32_t updateDurUs = micros() - updateStartUs;
    _connectionMetrics.lastUpdateDurationUs = updateDurUs;
    if (updateDurUs > _connectionMetrics.maxUpdateDurationUs) {
        _connectionMetrics.maxUpdateDurationUs = updateDurUs;
    }
    _wakeWordConversationController.recordVoiceUpdateDurationUs(updateDurUs);
}

// =============================================================================
// Implémentation TTS (Text-to-Speech)
// =============================================================================

bool VoxCoreVoice::say(const String& text) {
    if (text.isEmpty()) {
        _lastError = "Texte vide";
        return false;
    }
    if (_state == VoiceState::UNINITIALIZED) {
        _lastError = "VoiceKit non initialise";
        return false;
    }

    _lastError = "";
    uint32_t speechId = ++_speechGeneration;

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
                _textQueue.push({text, speechId});
                Serial.printf("[VoiceKit] say() mis en file d'attente (position %u, speechId=%u)\n",
                              (unsigned)_textQueue.size(), (unsigned)speechId);
                return true;
        }
    }

    // Vérification de la présence de clé API avant ouverture de session en ligne (v0.6.6)
    if (_session.getState() != LiveSessionState::CONNECTED &&
        _config.getApiKeyForProvider(_config.provider).isEmpty()) {
        _lastError = String("Cle API manquante pour le fournisseur ") + voiceProviderTypeToString(_config.provider);
        if (_onError) _onError(_lastError);
        return false;
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
        _activeSpeechId = speechId;
        return dispatchSpeech(text);
    }

    // Si la session est en cours de connexion ou de reconnexion
    if (_session.getState() == LiveSessionState::CONNECTING || _state == VoiceState::CONNECTING_SERVICE) {
        Serial.printf("[VoiceKit] Connexion en cours -> requete mise en attente (speechId=%u)\n", (unsigned)speechId);
        _pendingSpeech = {text, speechId};
        _pendingText = text;
        return true;
    }

    // Session déconnectée ou en erreur : tentative d'activation
    Serial.printf("[VoiceKit] Session deconnectee -> Activation de session... (speechId=%u)\n", (unsigned)speechId);
    _autoReconnectSuspended = false;
    _pendingSpeech = {text, speechId};
    _pendingText = text;
    _connectingStartTimeMs = millis();
    setState(VoiceState::CONNECTING_SERVICE);
    return _session.activate();
}

bool VoxCoreVoice::dispatchSpeech(const String& text) {
    String prompt = _config.resolvePromptPrefix() + text;
    Serial.printf("[VoiceKit] Prononciation : \"%s\"\n", text.c_str());
    _latencyMetrics.reset();
    _latencyMetrics.turnStartedMs = millis();
    if (_latencyMetrics.turnStartedMs == 0) _latencyMetrics.turnStartedMs = 1;
    bool ok = _session.sendTextMessage(prompt, _activeSpeechId);
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

bool VoxCoreVoice::askAi(const String& question) {
    if (question.isEmpty()) {
        _lastError = "Question vide";
        return false;
    }
    if (_state == VoiceState::UNINITIALIZED) {
        _lastError = "VoiceKit non initialise";
        return false;
    }

    _lastError = "";
    uint32_t speechId = ++_speechGeneration;

    // Si le moteur est occupe
    if (isBusy()) {
        switch (_config.queuePolicy) {
            case QueuePolicy::REJECT_IF_BUSY:
            case QueuePolicy::QUEUE_FIFO:
                _lastError = "Moteur vocal occupe";
                Serial.println("[VoiceKit] askAi() refuse : moteur actuellement occupe");
                return false;

            case QueuePolicy::INTERRUPT_CURRENT:
                Serial.println("[VoiceKit] askAi() interruption demandee de l'activite en cours");
                stop();
                break;
        }
    }

    // Verification de la presence de cle API avant ouverture de session en ligne (v0.6.6)
    if (_session.getState() != LiveSessionState::CONNECTED &&
        _config.getApiKeyForProvider(_config.provider).isEmpty()) {
        _lastError = String("Cle API manquante pour le fournisseur ") + voiceProviderTypeToString(_config.provider);
        if (_onError) _onError(_lastError);
        return false;
    }

    // Verification des prerequis reseau
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

    // Rearmer la restitution haut-parleur (et suspendre Wake Word pour eviter conflit I2S / larsen)
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

    // Si la session est prete, envoi direct
    if (_session.getState() == LiveSessionState::CONNECTED) {
        _activeSpeechId = speechId;
        return dispatchAiQuestion(question);
    }

    // Si la session est en cours de connexion ou de reconnexion
    if (_session.getState() == LiveSessionState::CONNECTING || _state == VoiceState::CONNECTING_SERVICE) {
        Serial.printf("[VoiceKit] Connexion en cours -> requete IA mise en attente (speechId=%u)\n", (unsigned)speechId);
        _pendingSpeech = {question, speechId, true};
        _pendingText = question;
        return true;
    }

    // Session deconnectee ou en erreur : tentative d'activation
    Serial.printf("[VoiceKit] Session deconnectee -> Activation de session pour IA... (speechId=%u)\n", (unsigned)speechId);
    _autoReconnectSuspended = false;
    _pendingSpeech = {question, speechId, true};
    _pendingText = question;
    _connectingStartTimeMs = millis();
    setState(VoiceState::CONNECTING_SERVICE);
    return _session.activate();
}

bool VoxCoreVoice::dispatchAiQuestion(const String& question) {
    String prompt = question;
    String resolvedAiPrefix = _config.resolveAiPromptPrefix();
    if (!_conversationEnabled && resolvedAiPrefix.length() > 0) {
        prompt = resolvedAiPrefix + question;
    }
    Serial.printf("[VoiceKit] Requete IA transmise : \"%s\"\n", question.c_str());
    _latencyMetrics.reset();
    _latencyMetrics.turnStartedMs = millis();
    if (_latencyMetrics.turnStartedMs == 0) _latencyMetrics.turnStartedMs = 1;
    bool ok = _session.sendTextMessage(prompt, _activeSpeechId);
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
    Serial.println("[STATE][STOP] Tour annulé");
    Serial.println("[VoiceKit] Arret demande (stop)");
    if (_wakeWordConversationController.isEnabled() && _wakeWordConversationController.isSessionInProgress()) {
        _wakeWordConversationController.stopSession(true);
    }
    bool sessionAlreadyStopped = false;
    if (_conversationEnabled && _conversationController.isTurnActive()) {
        _conversationController.requestStop();
        sessionAlreadyStopped = true;
    }
    _pendingText = "";
    _pendingSpeech = SpeechQueueItem();
    _activeSpeechId = 0;
    while (!_textQueue.empty()) {
        _textQueue.pop();
    }
    if (isListening() || isTranscribing()) {
        _streamer.cancel();
        _cancelledCaptureTurnId = (_activeCaptureTurnId != 0) ? _activeCaptureTurnId : _pendingTranscriptionTurnId;
        _activeCaptureTurnId = 0;
        _pendingTranscriptionTurnId = 0;
        _transcriptionPending = false;
        _finalTranscriptionReceived = false;
    } else {
        _cancelledCaptureTurnId = 0;
    }
    if (_profileChangePending) {
        rollbackPendingProfileChange("Interruption manuelle (stop)", false);
    }
    if (_modeSwitchPending && _activeProvider) {
        _conversationEnabled = _previousConversationMode;
        _config.enableConversation = _previousConversationMode;
        _activeProvider->setSystemInstruction(_conversationEnabled ? _config.resolveConversationInstruction() : _config.resolveSystemInstruction());
        _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
        _modeSwitchPending = false;
        Serial.println("[VoiceKit] stop() : mode conversationnel precedent restaure");
    }
    if (!sessionAlreadyStopped) {
        _session.requestStop();
    }
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
            _session.isDeactivating() ||
            _profileChangePending ||
            _modeSwitchPending);
}

bool VoxCoreVoice::requestDisconnectSession() {
    Serial.printf("[VoiceKit] Demande fermeture propre de session (Voice=%s, Session=%s, WS=%d)\n",
                  voiceStateToString(_state),
                  liveSessionStateToString(_session.getState()),
                  (_activeProvider && _activeProvider->isConnected()) ? 1 : 0);

    _autoReconnectSuspended = true;
    _connectingStartTimeMs = 0;

    if (_profileChangePending) {
        _config = _rollbackConfig;
        _preferredGender = _rollbackPreferredGender;
        _profileChangePending = false;
        _pendingProviderSwitch = false;
    }
    if (_modeSwitchPending) {
        _conversationEnabled = _previousConversationMode;
        _config.enableConversation = _previousConversationMode;
        _modeSwitchPending = false;
    }

    if (_conversationEnabled && _conversationController.isTurnActive()) {
        _conversationController.abortTurnForSessionClose();
    }
    _pendingText = "";
    _pendingSpeech = SpeechQueueItem();
    _activeSpeechId = 0;
    while (!_textQueue.empty()) {
        _textQueue.pop();
    }
    if (_streamer.isStreaming() || isListening() || isTranscribing()) {
        _streamer.cancel();
        _cancelledCaptureTurnId = (_activeCaptureTurnId != 0) ? _activeCaptureTurnId : _pendingTranscriptionTurnId;
        _activeCaptureTurnId = 0;
        _pendingTranscriptionTurnId = 0;
        _transcriptionPending = false;
        _finalTranscriptionReceived = false;
    }
    _player.setPlaybackAllowed(true);

    if (_session.getState() != LiveSessionState::DISCONNECTED ||
        (_activeProvider && _activeProvider->isConnected()) ||
        _session.isDeactivating()) {
        _session.requestDeactivate();
        if (_session.pollDeactivate()) {
            setState(VoiceState::DISCONNECTED);
        }
    } else {
        setState(VoiceState::DISCONNECTED);
    }
    return true;
}

void VoxCoreVoice::setVolume(uint8_t percent) {
    _config.volume = percent;
    _player.setVolume(percent);
}

uint8_t VoxCoreVoice::getVolume() const {
    return _player.getVolume();
}

// =============================================================================
// Implémentation Configuration Vocale & Profils Universels (v0.4.1 / v0.6.6)
// =============================================================================

const char* VoxCoreVoice::resolveProfileNvsNamespace(const char* customNs) const {
    if (customNs && customNs[0] != '\0') {
        return customNs;
    }
    if (!_config.profileNvsNamespace.isEmpty()) {
        return _config.profileNvsNamespace.c_str();
    }
    return VOICEKIT_PROFILE_NVS_NAMESPACE;
}

bool VoxCoreVoice::bindProviderForConfig(const VoiceKitConfig& targetCfg, String* outError) {
    IVoiceProvider* nextProvider = (targetCfg.provider == VoiceProviderType::OPENAI)
        ? static_cast<IVoiceProvider*>(&_openaiClient)
        : static_cast<IVoiceProvider*>(&_geminiProvider);

    if (_activeProvider && _activeProvider != nextProvider) {
        if (_activeProvider->isConnected()) {
            _activeProvider->disconnect();
        }
        _activeProvider->setOnInputTranscription(nullptr);
    }

    const String effectiveKey = targetCfg.getApiKeyForProvider(targetCfg.provider);
    const bool convActive = _conversationEnabled || targetCfg.enableConversation;

    if (targetCfg.provider == VoiceProviderType::OPENAI) {
        String resolvedModel = targetCfg.model;
        if (resolvedModel.isEmpty() || resolvedModel.indexOf("gemini") >= 0) {
            resolvedModel = OPENAI_REALTIME_DEFAULT_MODEL;
        }
        VoiceKitConfig cfgCopy = targetCfg;
        cfgCopy.apiKey = effectiveKey;
        cfgCopy.model = resolvedModel;
        cfgCopy.enableConversation = convActive;
        if (cfgCopy.voice.isEmpty() || !_profileManager.isValidVoiceForProvider(VoiceProviderType::OPENAI, cfgCopy.voice)) {
            cfgCopy.voice = OPENAI_REALTIME_DEFAULT_VOICE;
        }
        if (!effectiveKey.isEmpty()) {
            if (!_openaiClient.begin(cfgCopy)) {
                if (outError) *outError = _openaiClient.getLastError();
                return false;
            }
        } else {
            _openaiClient.setModel(resolvedModel);
            _openaiClient.setVoice(cfgCopy.voice);
            _openaiClient.setLanguage(cfgCopy.language);
            _openaiClient.setInputTranscriptionLanguage(cfgCopy.inputTranscriptionLanguage);
            _openaiClient.setConversationMode(convActive);
            _openaiClient.setVoiceDirectives(cfgCopy.buildVoiceDirectives());
            _openaiClient.setSystemInstruction(convActive
                ? cfgCopy.resolveConversationInstruction()
                : cfgCopy.resolveSystemInstruction());
        }
    } else {
        String resolvedModel = targetCfg.model;
        if (resolvedModel.isEmpty() ||
            resolvedModel.indexOf("gpt-") >= 0 ||
            resolvedModel.indexOf("realtime") >= 0) {
            resolvedModel = VOICEKIT_DEFAULT_MODEL;
        }
        String voiceToUse = targetCfg.voice;
        if (voiceToUse.isEmpty() || !_profileManager.isValidVoiceForProvider(VoiceProviderType::GEMINI, voiceToUse)) {
            voiceToUse = VOICEKIT_DEFAULT_VOICE;
        }
        if (!effectiveKey.isEmpty()) {
            if (!_client.begin(effectiveKey.c_str(), resolvedModel.c_str(), voiceToUse.c_str())) {
                if (outError) *outError = _client.getLastError();
                return false;
            }
        } else {
            _client.setModel(resolvedModel);
            _client.setVoice(voiceToUse);
        }
        _client.setLanguage(targetCfg.language);
        _client.setVoiceDirectives(targetCfg.buildVoiceDirectives());
        _client.setSystemInstruction(convActive
            ? targetCfg.resolveConversationInstruction()
            : targetCfg.resolveSystemInstruction());
        _geminiProvider.setClient(&_client);
        _geminiProvider.setConversationMode(convActive);
    }

    _activeProvider = nextProvider;
    _activeProvider->setOnInputTranscription([this](const String& text, bool isFinal) {
        if (_conversationEnabled) {
            _conversationController.handleUserTranscription(text, isFinal);
        } else {
            handleIncomingTranscription(text, isFinal);
        }
    });

    _session.begin(_activeProvider, &_player);
    _conversationController.setProviderAndConfig(_activeProvider, targetCfg);
    _wakeWordConversationController.setProvider(_activeProvider, targetCfg.provider);
    return true;
}

void VoxCoreVoice::rollbackPendingProfileChange(const String& reason, bool notifyProfileError) {
    if (!_profileChangePending) {
        return;
    }

    _config = _rollbackConfig;
    _preferredGender = _rollbackPreferredGender;
    _profileChangePending = false;
    _pendingProviderSwitch = false;

    bindProviderForConfig(_config, nullptr);

    Serial.printf("[VoiceKit] Echec changement de profil (%s) -> restauration du profil precedent (%s / %s / %s)\n",
                  reason.c_str(),
                  voiceProviderTypeToString(_config.provider),
                  _config.voice.c_str(),
                  _config.language.c_str());

    if (notifyProfileError && _onProfileError) {
        _onProfileError(reason);
    }
}

VoiceProfile VoxCoreVoice::getVoiceProfile() const {
    return VoiceProfileManager::fromConfig(_config, _preferredGender);
}

bool VoxCoreVoice::applyVoiceProfile(const VoiceProfile& profile, bool connectIfDisconnected) {
    // 1. Validation stricte du profil (fournisseur, voix du catalogue, code langue BCP-47)
    String validationError;
    if (!_profileManager.validateProfile(profile, &validationError)) {
        _lastError = validationError;
        Serial.printf("[VoiceKit] Profil vocal refuse : %s\n", _lastError.c_str());
        if (_onProfileError) _onProfileError(_lastError);
        return false;
    }

    // 2. Refus strict si le moteur est occupé (TTS, STT, conversation ou session Wake Word)
    if (isBusy() || _wakeWordConversationController.isSessionInProgress()) {
        _lastError = "Impossible de modifier le profil vocal pendant une activite vocale active";
        Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
        if (_onProfileError) _onProfileError(_lastError);
        return false;
    }

    // 3. Refus strict si une connexion ou reconnexion est déjà en cours
    if (isConnecting()) {
        _lastError = "Connexion ou reconnexion deja en cours, modification de profil refusee";
        Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
        if (_onProfileError) _onProfileError(_lastError);
        return false;
    }

    const bool providerChanged = (profile.provider != _config.provider);
    const bool currentlyOnline = (_activeProvider && _activeProvider->isConnected() &&
                                  _session.getState() != LiveSessionState::DISCONNECTED &&
                                  _session.getState() != LiveSessionState::ERROR);
    const String targetKey = _config.getApiKeyForProvider(profile.provider);

    // 4. Vérification de clé API si changement de fournisseur, session active ou connexion exigée
    if ((providerChanged || currentlyOnline || connectIfDisconnected) && targetKey.isEmpty()) {
        _lastError = String("Cle API manquante pour le fournisseur ") + voiceProviderTypeToString(profile.provider);
        Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
        if (_onProfileError) _onProfileError(_lastError);
        if (connectIfDisconnected && _onError) _onError(_lastError);
        return false;
    }

    // 5. Cas hors-ligne sans demande d'ouverture de session réseau immédiate
    if (!currentlyOnline && !connectIfDisconnected) {
        VoiceKitConfig nextCfg = _config;
        VoiceProfileManager::applyToConfig(profile, nextCfg);
        if (!targetKey.isEmpty()) {
            nextCfg.apiKey = targetKey;
        }
        String bindErr;
        if (!bindProviderForConfig(nextCfg, &bindErr)) {
            _lastError = bindErr;
            if (_onProfileError) _onProfileError(_lastError);
            return false;
        }
        _config = nextCfg;
        _preferredGender = profile.preferredGender;

        Serial.printf("[VoiceKit] Profil vocal configure (hors ligne) : Fournisseur=%s | Voix=%s | Langue=%s | Style=%s | Ton=%s%s%s\n",
                      voiceProviderTypeToString(_config.provider),
                      _config.voice.c_str(), _config.language.c_str(),
                      voiceStyleToString(_config.style), voiceToneToString(_config.tone),
                      _config.accent.isEmpty() ? "" : " | Accent=",
                      _config.accent.isEmpty() ? "" : _config.accent.c_str());
        if (_onProfileApplied) {
            _onProfileApplied(getVoiceProfile());
        }
        return true;
    }

    // 6. Vérification des prérequis réseau si une connexion immédiate est requise depuis l'état hors-ligne
    if (!currentlyOnline && connectIfDisconnected) {
        bool netOk = false;
        if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
            netOk = (_wifi.isConnected() && _wifi.isTimeSynced());
        } else {
            netOk = (WiFi.status() == WL_CONNECTED && isSystemTimeValid());
        }
        if (!netOk) {
            _lastError = "Reseau Wi-Fi non connecte ou horloge NTP non synchronisee";
            Serial.printf("[VoiceKit] %s\n", _lastError.c_str());
            if (_onProfileError) _onProfileError(_lastError);
            if (_onError) _onError(_lastError);
            return false;
        }
    }

    // 7. Optimisation : si la session est déjà connectée avec exactement la même configuration acoustique,
    //    ne pas ouvrir ni couper inutilement la session WebSocket (règle 4 Section 8).
    if (currentlyOnline && profile.hasSameAcousticConfig(getVoiceProfile())) {
        _preferredGender = profile.preferredGender;
        if (_onProfileApplied) {
            _onProfileApplied(getVoiceProfile());
        }
        return true;
    }

    // 8. Sauvegarde de l'état de repli atomique avant toute mutation de session
    _rollbackConfig = _config;
    _rollbackPreferredGender = _preferredGender;
    _pendingProfile = profile;
    _pendingVoice = profile.voiceId;
    _pendingLanguage = profile.language;
    _pendingStyle = profile.style;
    _pendingTone = profile.tone;
    _pendingAccent = profile.accent;

    VoiceKitConfig tempCfg = _config;
    VoiceProfileManager::applyToConfig(profile, tempCfg);
    tempCfg.apiKey = targetKey;

    // 9. Cas OpenAI déjà connecté sans changement de fournisseur et où session.update en place est autorisé
    //    (aucun audio encore émis sur la session OU voix inchangée : modification langue/style/ton/accent)
    if (currentlyOnline && !providerChanged &&
        profile.provider == VoiceProviderType::OPENAI &&
        _openaiClient.canUpdateVoiceInCurrentSession(profile.voiceId)) {
        _profileChangePending = true;
        _pendingProviderSwitch = false;
        _openaiClient.setVoice(profile.voiceId);
        _openaiClient.setLanguage(profile.language);
        _openaiClient.setInputTranscriptionLanguage(tempCfg.inputTranscriptionLanguage);
        _openaiClient.setVoiceDirectives(tempCfg.buildVoiceDirectives());
        _openaiClient.setSystemInstruction(_conversationEnabled
            ? tempCfg.resolveConversationInstruction()
            : tempCfg.resolveSystemInstruction());

        _session.markPendingSessionUpdate();
        setState(VoiceState::CONNECTING_SERVICE);
        _connectingStartTimeMs = millis();

        if (!_openaiClient.requestSessionUpdate()) {
            String updErr = _openaiClient.getLastError();
            if (updErr.isEmpty()) updErr = "Echec envoi session.update OpenAI";
            rollbackPendingProfileChange(updErr, true);
            _lastError = updErr;
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
            return false;
        }
        return true;
    }

    // 10. Transition complète par fermeture propre de la session active puis reconnexion sécurisée
    //     (garantit zéro double session WebSocket concurrente)
    Serial.printf("[VoiceKit] Demande de nouveau profil vocal (%s) : Voix=%s | Langue=%s | Style=%s | Ton=%s%s%s\n",
                  voiceProviderTypeToString(profile.provider),
                  profile.voiceId.c_str(), profile.language.c_str(),
                  voiceStyleToString(profile.style), voiceToneToString(profile.tone),
                  profile.accent.isEmpty() ? "" : " | Accent=",
                  profile.accent.isEmpty() ? "" : profile.accent.c_str());

    // Désactivation intentionnelle de la session précédente avant d'armer _profileChangePending
    // afin que la fermeture volontaire ne soit pas interprétée comme une coupure réseau accidentelle.
    _autoReconnectSuspended = false;
    _session.deactivate();

    _profileChangePending = true;
    _pendingProviderSwitch = providerChanged;

    String bindErr;
    if (!bindProviderForConfig(tempCfg, &bindErr)) {
        if (bindErr.isEmpty()) bindErr = "Echec preparation du fournisseur vocal";
        rollbackPendingProfileChange(bindErr, true);
        _lastError = bindErr;
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    setState(VoiceState::CONNECTING_SERVICE);
    _connectingStartTimeMs = millis();

    bool ok = _session.activate();
    if (!ok) {
        String actErr = _session.getLastError();
        if (actErr.isEmpty()) actErr = "Echec activation session pour reconnexion profil";
        rollbackPendingProfileChange(actErr, true);
        _lastError = actErr;
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    return true;
}

bool VoxCoreVoice::setVoiceProfile(const String& voice, const String& language,
                                   VoiceStyle style, VoiceTone tone,
                                   const String& accent) {
    VoiceProfile target = getVoiceProfile();
    target.voiceId = voice;
    target.language = language;
    target.style = style;
    target.tone = tone;
    target.accent = accent;
    return applyVoiceProfile(target, false);
}

bool VoxCoreVoice::setProvider(VoiceProviderType provider, const String& voiceId) {
    VoiceProfile target = getVoiceProfile();
    target.provider = provider;
    if (!voiceId.isEmpty()) {
        target.voiceId = voiceId;
    } else if (!_profileManager.isValidVoiceForProvider(provider, target.voiceId)) {
        target.voiceId = _profileManager.getDefaultVoiceForProvider(provider, _preferredGender);
    }
    return applyVoiceProfile(target, false);
}

void VoxCoreVoice::setProviderApiKey(VoiceProviderType provider, const String& apiKey) {
    if (provider == VoiceProviderType::OPENAI) {
        _config.openAiApiKey = apiKey;
    } else {
        _config.geminiApiKey = apiKey;
    }
    if (_config.provider == provider) {
        _config.apiKey = apiKey;
        if (!isBusy() && !isConnecting() && (!_activeProvider || !_activeProvider->isConnected())) {
            bindProviderForConfig(_config, nullptr);
        }
    }
}

bool VoxCoreVoice::saveVoiceProfile(const char* nvsNamespace) {
    return _profileManager.saveActiveProfile(getVoiceProfile(), resolveProfileNvsNamespace(nvsNamespace));
}

bool VoxCoreVoice::loadVoiceProfile(bool applyImmediately, const char* nvsNamespace) {
    VoiceProfile loaded;
    bool wasRestored = false;
    _profileManager.loadActiveProfile(loaded, _config.provider, &wasRestored, resolveProfileNvsNamespace(nvsNamespace));
    if (!wasRestored) {
        return false;
    }
    if (!applyImmediately) {
        return true;
    }
    return applyVoiceProfile(loaded, false);
}

bool VoxCoreVoice::resetVoiceProfile(bool applyDefault, const char* nvsNamespace) {
    bool cleared = _profileManager.resetActiveProfile(resolveProfileNvsNamespace(nvsNamespace));
    if (!applyDefault) {
        return cleared;
    }
    VoiceProfile def = _profileManager.getDefaultProfile(_config.provider);
    return applyVoiceProfile(def, false);
}

bool VoxCoreVoice::hasSavedVoiceProfile(const char* nvsNamespace) const {
    return _profileManager.hasSavedActiveProfile(resolveProfileNvsNamespace(nvsNamespace));
}

bool VoxCoreVoice::saveFavoriteProfile(uint8_t slot, const char* nvsNamespace) {
    return _profileManager.saveFavoriteProfile(slot, getVoiceProfile(), resolveProfileNvsNamespace(nvsNamespace));
}

bool VoxCoreVoice::loadFavoriteProfile(uint8_t slot, bool applyImmediately, const char* nvsNamespace) {
    VoiceProfile fav;
    if (!_profileManager.loadFavoriteProfile(slot, fav, resolveProfileNvsNamespace(nvsNamespace))) {
        return false;
    }
    if (!applyImmediately) {
        return true;
    }
    return applyVoiceProfile(fav, false);
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

bool VoxCoreVoice::setSystemInstruction(const String& instruction) {
    _config.systemInstruction = instruction;
    if (_activeProvider && !_conversationEnabled) {
        _activeProvider->setSystemInstruction(_config.resolveSystemInstruction());
    }
    return true;
}

// =============================================================================
// Implémentation Conversation Bidirectionnelle (v0.4.0 / v0.4.1.1 / v0.6.0 / v0.6.6)
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
            _activeProvider->setSystemInstruction(enable ? _config.resolveConversationInstruction() : _config.resolveSystemInstruction());
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
        _activeProvider->setSystemInstruction(enable ? _config.resolveConversationInstruction() : _config.resolveSystemInstruction());
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
            _activeProvider->setSystemInstruction(_conversationEnabled ? _config.resolveConversationInstruction() : _config.resolveSystemInstruction());
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

    // Refus strict si une capture ou transcription est déjà active
    if (isListening() || isTranscribing()) {
        _lastError = "Capture ou transcription deja en cours";
        Serial.println("[VoiceKit] startListening() refuse : capture ou transcription deja en cours");
        return false;
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

    // 3. Vérification de la clé API et de la disponibilité du fournisseur vocal actif
    if (_config.getApiKeyForProvider(_config.provider).isEmpty()) {
        _lastError = String("Cle API manquante pour le fournisseur ") + voiceProviderTypeToString(_config.provider);
        Serial.printf("[VoiceKit] startListening() refuse : %s\n", _lastError.c_str());
        if (_onError) _onError(_lastError);
        return false;
    }

    if (!_activeProvider || !_activeProvider->isConnected() || !_activeProvider->isSetupComplete()) {
        _lastError = "Session vocale non prete";
        Serial.printf("[VoiceKit] startListening() refuse : %s\n", _lastError.c_str());
        if (_onError) _onError(_lastError);
        return false;
    }

    // 4. Réinitialisation des états de transcription pour ce tour
    _sttTurnId++;
    _activeCaptureTurnId = _sttTurnId;
    _pendingTranscriptionTurnId = 0;
    // Note : conservation de _cancelledCaptureTurnIds pour rejet d'anciennes transcriptions serveur
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
        _activeCaptureTurnId = 0;
        _player.setPlaybackAllowed(true);
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    setState(VoiceState::LISTENING);
    if (_onListeningStartedWithTurn) {
        _onListeningStartedWithTurn(_activeCaptureTurnId);
    }
    if (_onListeningStarted) {
        _onListeningStarted();
    }
    Serial.printf("[VoiceKit] startListening() : Acquisition microphone et streaming actifs (turnId=%u)\n",
                  (unsigned)_activeCaptureTurnId);
    return true;
}

bool VoxCoreVoice::stopListening() {
    if (_conversationEnabled) {
        return _conversationController.finishTurnAudio();
    }

    if (!isListening() && !_streamer.isStreaming()) {
        return false;
    }

    Serial.printf("[VoiceKit] stopListening() : Arret capture micro et envoi de activityEnd (turnId=%u)...\n",
                  (unsigned)_activeCaptureTurnId);
    bool stopped = _streamer.stop();

    // 1. Échec réel d'arrêt matériel du streamer audio
    if (!stopped) {
        _lastError = _streamer.getLastError();
        if (_lastError.isEmpty()) {
            _lastError = "Echec arret capture micro";
        }
        Serial.printf("[VoiceKit] ERREUR stopListening() : %s\n", _lastError.c_str());
        _cancelledCaptureTurnId = (_activeCaptureTurnId != 0) ? _activeCaptureTurnId : _pendingTranscriptionTurnId;
        _activeCaptureTurnId = 0;
        _pendingTranscriptionTurnId = 0;
        _transcriptionPending = false;
        _finalTranscriptionReceived = false;
        _player.setPlaybackAllowed(true);
        setState(VoiceState::ERROR_STATE);
        if (_onError) {
            _onError(_lastError);
        }
        return false;
    }

    // 2. Échec de transmission réseau de activityEnd auprès du fournisseur vocal actif
    if (_activeProvider && _activeProvider->isConnected() && !_streamer.isActivityEndSent()) {
        _lastError = _streamer.getLastError();
        if (_lastError.isEmpty()) {
            _lastError = "Echec transmission activityEnd";
        }
        Serial.printf("[VoiceKit] ERREUR stopListening() : %s\n", _lastError.c_str());
        _cancelledCaptureTurnId = (_activeCaptureTurnId != 0) ? _activeCaptureTurnId : _pendingTranscriptionTurnId;
        _activeCaptureTurnId = 0;
        _pendingTranscriptionTurnId = 0;
        _transcriptionPending = false;
        _finalTranscriptionReceived = false;
        _player.setPlaybackAllowed(true);
        setState(VoiceState::ERROR_STATE);
        if (_onError) {
            _onError(_lastError);
        }
        return false;
    }

    // 3. Terminaison acceptée nominale
    _pendingTranscriptionTurnId = _activeCaptureTurnId;
    _activeCaptureTurnId = 0;
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
    _cancelledCaptureTurnId = (_activeCaptureTurnId != 0) ? _activeCaptureTurnId : _pendingTranscriptionTurnId;
    _activeCaptureTurnId = 0;
    _pendingTranscriptionTurnId = 0;
    _transcriptionPending = false;
    _finalTranscriptionReceived = false;
    _lastInterimText = "";
    _lastFinalText = "";
    _player.setPlaybackAllowed(true);
    setState(VoiceState::READY);
    bool busyForWake = isBusy() || isSpeaking() || _wakeWordConversationController.isSessionInProgress();
    _wakeWordManager.notifyVoiceActivity(busyForWake, isSpeaking());
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

    uint32_t originTurnId = 0;
    bool isCancelledTurn = false;

    if (_pendingTranscriptionTurnId != 0) {
        originTurnId = _pendingTranscriptionTurnId;
    } else if (_cancelledCaptureTurnId != 0 && isFinal) {
        originTurnId = _cancelledCaptureTurnId;
        isCancelledTurn = true;
        _cancelledCaptureTurnId = 0;
    } else if (_activeCaptureTurnId != 0) {
        originTurnId = _activeCaptureTurnId;
    } else if (_cancelledCaptureTurnId != 0) {
        originTurnId = _cancelledCaptureTurnId;
        isCancelledTurn = true;
        if (isFinal) {
            _cancelledCaptureTurnId = 0;
        }
    }

    if (isCancelledTurn) {
        Serial.printf("[STATE][STT] Transcription obsolète ignorée (tour annulé turnId=%u) : \"%s\"\n",
                      (unsigned)originTurnId, text.c_str());
        if (_onTranscriptionWithTurn) {
            _onTranscriptionWithTurn(text, isFinal, originTurnId);
        }
        return;
    }

    if (isFinal) {
        if (_finalTranscriptionReceived && _pendingTranscriptionTurnId == 0 && _activeCaptureTurnId == 0) {
            Serial.printf("[STATE][STT] Transcription obsolète ignorée (finale déjà consommée turnId=%u) : \"%s\"\n",
                          (unsigned)originTurnId, text.c_str());
            return;
        }
        _pendingTranscriptionTurnId = 0;
        _latencyMetrics.playbackFinishedMs = millis();
        if (_latencyMetrics.playbackFinishedMs == 0) _latencyMetrics.playbackFinishedMs = 1;
        Serial.printf("[VoiceKit] Transcription FINALE recue (turnId=%u) : \"%s\"\n",
                      (unsigned)originTurnId, text.c_str());
        _finalTranscriptionReceived = true;
        _transcriptionPending = false;
        _lastFinalText = text;
        _player.setPlaybackAllowed(true);

        if (!_conversationEnabled) {
            if (_state == VoiceState::TRANSCRIBING || _state == VoiceState::LISTENING) {
                setState(VoiceState::READY);
            }
            bool busyForWake = isBusy() || isSpeaking() || _wakeWordConversationController.isSessionInProgress();
            _wakeWordManager.notifyVoiceActivity(busyForWake, isSpeaking());
        }

        if (_onTranscriptionWithTurn) {
            _onTranscriptionWithTurn(text, true, originTurnId);
        }
        if (_onTranscription) {
            _onTranscription(text, true);
        }
    } else {
        _lastInterimText = text;
        if (_onTranscriptionWithTurn) {
            _onTranscriptionWithTurn(text, false, originTurnId);
        }
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
        bool busyForWake = isBusy() || isSpeaking() || _wakeWordConversationController.isSessionInProgress();
        _wakeWordManager.notifyVoiceActivity(busyForWake, isSpeaking());
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
    bool busyForWake = isBusy() || isSpeaking() || _wakeWordConversationController.isSessionInProgress();
    _wakeWordManager.notifyVoiceActivity(busyForWake, isSpeaking());
    return _wakeWordManager.resume();
}

// =============================================================================
// Implémentation Conversation Mains Libres par Mot de Réveil (v0.6.3)
// =============================================================================

bool VoxCoreVoice::enableWakeWordConversation(bool enable) {
    if (!enable) {
        disableWakeWordConversation();
        return true;
    }

    _conversationEnabled = true;
    _config.enableConversation = true;
    if (_activeProvider) {
        _activeProvider->setSystemInstruction(_config.resolveConversationInstruction());
        _activeProvider->setVoiceDirectives(_config.buildVoiceDirectives());
        _activeProvider->setConversationMode(true);
    }

    if (!isBusy() && _wakeWordConversationController.isDisconnectAfterSession() && _session.isActive()) {
        _session.deactivate();
    }

    return _wakeWordConversationController.enable(true);
}

void VoxCoreVoice::disableWakeWordConversation() {
    _wakeWordConversationController.disable();
}
