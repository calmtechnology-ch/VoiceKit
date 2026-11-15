#include "VoiceInteractionController.h"
#include <WiFi.h>

static inline bool isWifiConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

const char* voiceInteractionStateToString(VoiceInteractionState st) {
    switch (st) {
        case VoiceInteractionState::DISABLED:         return "DISABLED";
        case VoiceInteractionState::READY:            return "READY";
        case VoiceInteractionState::STARTING:         return "STARTING";
        case VoiceInteractionState::LISTENING:        return "LISTENING";
        case VoiceInteractionState::STOPPING_CAPTURE: return "STOPPING_CAPTURE";
        case VoiceInteractionState::PROCESSING:       return "PROCESSING";
        case VoiceInteractionState::SPEAKING:         return "SPEAKING";
        case VoiceInteractionState::INTERRUPTING:     return "INTERRUPTING";
        case VoiceInteractionState::RECOVERING:       return "RECOVERING";
        case VoiceInteractionState::ERROR_STATE:      return "ERROR";
        default:                                      return "UNKNOWN";
    }
}

const char* pttStateToString(VoiceInteractionState st) {
    return voiceInteractionStateToString(st);
}

const char* voiceInteractionIntentToString(VoiceInteractionIntent intent) {
    switch (intent) {
        case VoiceInteractionIntent::NONE:            return "NONE";
        case VoiceInteractionIntent::START_CAPTURE:   return "START_CAPTURE";
        case VoiceInteractionIntent::FINISH_CAPTURE:  return "FINISH_CAPTURE";
        case VoiceInteractionIntent::CANCEL_CAPTURE:  return "CANCEL_CAPTURE";
        case VoiceInteractionIntent::RECOVER:         return "RECOVER";
        default:                                      return "UNKNOWN";
    }
}

const char* pttIntentToString(VoiceInteractionIntent intent) {
    return voiceInteractionIntentToString(intent);
}

VoiceInteractionController::VoiceInteractionController(
    VoxCoreVoice& voiceEngine,
    const VoiceInteractionConfig& config
)
    : _voice(voiceEngine),
      _config(config) {
    // Configurer le validateur selon la configuration d'interaction
    VoiceCaptureConfig vadCfg;
    vadCfg.minCaptureDurationMs = _config.minCaptureDurationMs;
    vadCfg.minSpeechDurationMs = _config.minSpeechDurationMs;
    vadCfg.minConsecutiveSpeechMs = _config.minConsecutiveSpeechMs;
    vadCfg.speechThresholdFloorRms = _config.speechThresholdFloorRms;
    vadCfg.noiseMultiplier = _config.noiseMultiplier;
    vadCfg.noiseOffsetRms = _config.noiseOffsetRms;
    vadCfg.initialNoiseFloor = _config.initialNoiseFloor;
    _validator.setConfig(vadCfg);
}

bool VoiceInteractionController::begin() {
    _state = VoiceInteractionState::DISABLED;
    _prevState = VoiceInteractionState::DISABLED;
    _pendingIntent = VoiceInteractionIntent::NONE;

    _startListeningCalled = false;
    _stopListeningCalled = false;
    _isListeningActive = false;
    _lastTurnCancelled = false;
    _lastRejectCause = "OK";

    _lastClickTimeMs = 0;
    _captureStartTimeMs = 0;
    _listeningStartTimeMs = 0;
    _processingStartTimeMs = 0;
    _stoppedStartTimeMs = 0;
    _stopStartTimeMs = 0;
    _errorStartTimeMs = 0;

    resetCallCounters();
    _validator.reset();

    if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
        setState(VoiceInteractionState::READY);
    } else {
        setState(VoiceInteractionState::DISABLED);
    }
    return true;
}

void VoiceInteractionController::resetCallCounters() {
    _startListeningCallsCount = 0;
    _stopListeningCallsCount = 0;
    _cancelListeningCallsCount = 0;
    _stopInterruptsCount = 0;
    _responseRequestsSentCount = 0;
    _silenceRejectionsCount = 0;
}

void VoiceInteractionController::setState(VoiceInteractionState newState) {
    if (_state != newState) {
        VoiceInteractionState oldState = _state;
        _prevState = _state;
        _state = newState;
        if (_onStateChanged) {
            _onStateChanged(oldState, newState);
        }
    }
}

void VoiceInteractionController::setConfig(const VoiceInteractionConfig& config) {
    _config = config;
    VoiceCaptureConfig vadCfg = _validator.getConfig();
    vadCfg.minCaptureDurationMs = _config.minCaptureDurationMs;
    vadCfg.minSpeechDurationMs = _config.minSpeechDurationMs;
    vadCfg.minConsecutiveSpeechMs = _config.minConsecutiveSpeechMs;
    vadCfg.speechThresholdFloorRms = _config.speechThresholdFloorRms;
    vadCfg.noiseMultiplier = _config.noiseMultiplier;
    vadCfg.noiseOffsetRms = _config.noiseOffsetRms;
    vadCfg.initialNoiseFloor = _config.initialNoiseFloor;
    _validator.setConfig(vadCfg);
}

bool VoiceInteractionController::isActionAllowed() const {
    if (!isWifiConnected()) return false;
    if (_voice.isConnecting()) return false;
    if (_voice.getSession().getState() != LiveSessionState::CONNECTED) return false;
    if (_voice.getState() == VoiceState::ERROR_STATE) return false;
    return true;
}

const char* VoiceInteractionController::getRejectCause() const {
    if (_isListeningActive) return "ALREADY_IN_LISTENING";
    if (!isWifiConnected()) return "WIFI_NOT_CONNECTED";
    if (_voice.isConnecting()) return "CONNECTING_SERVICE";
    if (_voice.getSession().getState() != LiveSessionState::CONNECTED) {
        switch (_voice.getSession().getState()) {
            case LiveSessionState::CONNECTING:       return "SESSION_CONNECTING";
            case LiveSessionState::RECONNECTING:     return "SESSION_RECONNECTING";
            case LiveSessionState::WAITING_RESPONSE: return "SESSION_WAITING_RESPONSE";
            case LiveSessionState::PLAYING:          return "SESSION_PLAYING";
            case LiveSessionState::INTERRUPTING:     return "SESSION_INTERRUPTING";
            case LiveSessionState::ERROR:            return "SESSION_ERROR";
            case LiveSessionState::DISCONNECTED:     return "SESSION_DISCONNECTED";
            default:                                 return "SESSION_NOT_CONNECTED";
        }
    }
    if (_voice.getConversationController().isTurnActive()) return "TURN_ALREADY_ACTIVE";
    if (_voice.isSpeaking()) return "SPEAKER_BUSY";
    if (_voice.isListening()) return "MIC_ALREADY_LISTENING";
    if (_voice.isTranscribing()) return "TRANSCRIBING";
    if (_voice.getState() == VoiceState::ERROR_STATE) return "VOICE_ERROR_STATE";
    if (_voice.getState() == VoiceState::STOPPED) return "VOICE_STOPPED";
    if (_voice.isBusy()) return "ENGINE_BUSY";
    if (!_voice.isReady()) return "NOT_READY";
    return (_lastRejectCause != nullptr) ? _lastRejectCause : "UNKNOWN";
}

void VoiceInteractionController::onTalkClick() {
    uint32_t now = millis();

    // 1. Filtrage anti-rebond matériel (120 ms par défaut)
    if (_lastClickTimeMs > 0 && (now - _lastClickTimeMs < _config.debounceMs)) {
        return;
    }
    _lastClickTimeMs = now;

    // 2. Interruption prioritaire si l'assistant répond ou traite
    if (_state == VoiceInteractionState::PROCESSING || _state == VoiceInteractionState::SPEAKING) {
        triggerStop();
        return;
    }

    // 3. Récupération suite à un état d'erreur
    if (_state == VoiceInteractionState::ERROR_STATE) {
        _pendingIntent = VoiceInteractionIntent::RECOVER;
        return;
    }

    // 4. Rejet pendant les transitions de nettoyage
    if (_state == VoiceInteractionState::INTERRUPTING ||
        _state == VoiceInteractionState::RECOVERING ||
        _state == VoiceInteractionState::STOPPING_CAPTURE) {
        _lastRejectCause = "TRANSITION_IN_PROGRESS";
        return;
    }

    // 5. Deuxième clic : terminer l'écoute en cours
    if (_state == VoiceInteractionState::LISTENING || _state == VoiceInteractionState::STARTING) {
        _pendingIntent = VoiceInteractionIntent::FINISH_CAPTURE;
        return;
    }

    // 6. Premier clic : engager la capture si READY
    if (_state == VoiceInteractionState::READY) {
        if (!_voice.isReady() || _voice.isBusy() || !isActionAllowed()) {
            _lastRejectCause = getRejectCause();
            return;
        }
        _pendingIntent = VoiceInteractionIntent::START_CAPTURE;
        return;
    }

    // 7. Autre état (ex: DISABLED)
    _lastRejectCause = getRejectCause();
}

void VoiceInteractionController::triggerStop() {
    _pendingIntent = VoiceInteractionIntent::CANCEL_CAPTURE;
}

void VoiceInteractionController::recover() {
    _pendingIntent = VoiceInteractionIntent::RECOVER;
}

void VoiceInteractionController::triggerSerialRelease() {
    if (_state == VoiceInteractionState::LISTENING || _state == VoiceInteractionState::STARTING) {
        _pendingIntent = VoiceInteractionIntent::FINISH_CAPTURE;
    }
}

void VoiceInteractionController::processAudioChunk(const int16_t* samples, size_t count, uint32_t rms) {
    (void)samples;
    (void)count;
    feedChunkRms(rms);
}

void VoiceInteractionController::feedChunkRms(uint32_t rms) {
    if (_isListeningActive || _state == VoiceInteractionState::LISTENING || _state == VoiceInteractionState::STARTING) {
        _validator.feedChunk(rms);
    } else {
        _validator.updateIdleRms(rms);
    }
}

void VoiceInteractionController::attachVoiceCallbacks() {
    _voice.onAudioChunk([this](const int16_t* samples, size_t count, uint32_t rms) {
        this->processAudioChunk(samples, count, rms);
    });

    _voice.onStateChanged([this](VoiceState st) {
        this->notifyVoiceStateChanged(st);
    });

    _voice.onListeningStarted([this]() {
        this->notifyListeningStarted();
    });

    _voice.onListeningFinished([this]() {
        this->notifyListeningFinished();
    });

    _voice.onSpeechStarted([this]() {
        this->notifySpeechStarted();
    });

    _voice.onSpeechFinished([this]() {
        this->notifySpeechFinished();
    });

    _voice.onError([this](const String& err) {
        this->notifyError(err);
    });

    _voice.onReady([this]() {
        this->notifyReady();
    });
}

void VoiceInteractionController::notifyVoiceStateChanged(VoiceState newState) {
    switch (newState) {
        case VoiceState::READY:
            _listeningStartTimeMs = 0;
            _processingStartTimeMs = 0;
            _stoppedStartTimeMs = 0;
            _stopStartTimeMs = 0;
            _errorStartTimeMs = 0;
            _isListeningActive = false;
            _startListeningCalled = false;
            _stopListeningCalled = false;
            if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED) {
                setState(VoiceInteractionState::READY);
            } else {
                setState(VoiceInteractionState::DISABLED);
            }
            break;
        case VoiceState::IDLE:
            if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
                setState(VoiceInteractionState::READY);
            } else {
                setState(VoiceInteractionState::DISABLED);
            }
            break;
        case VoiceState::LISTENING:
            _listeningStartTimeMs = millis();
            _isListeningActive = true;
            if (_state != VoiceInteractionState::LISTENING) {
                setState(VoiceInteractionState::LISTENING);
            }
            break;
        case VoiceState::PROCESSING:
        case VoiceState::TRANSCRIBING:
            _processingStartTimeMs = millis();
            setState(VoiceInteractionState::PROCESSING);
            break;
        case VoiceState::SPEAKING:
            setState(VoiceInteractionState::SPEAKING);
            break;
        case VoiceState::STOPPED:
            _stoppedStartTimeMs = millis();
            if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
                setState(VoiceInteractionState::READY);
            } else {
                setState(VoiceInteractionState::DISABLED);
            }
            break;
        case VoiceState::ERROR_STATE:
            _errorStartTimeMs = millis();
            setState(VoiceInteractionState::ERROR_STATE);
            break;
        case VoiceState::CONNECTING_WIFI:
        case VoiceState::CONNECTING_SERVICE:
        case VoiceState::UNINITIALIZED:
        case VoiceState::DISCONNECTED:
            setState(VoiceInteractionState::DISABLED);
            break;
        default:
            break;
    }
}

void VoiceInteractionController::notifyListeningStarted() {
    if (_lastTurnCancelled) {
        return;
    }
    _listeningStartTimeMs = millis();
    _isListeningActive = true;
    if (_state != VoiceInteractionState::LISTENING) {
        setState(VoiceInteractionState::LISTENING);
    }
}

void VoiceInteractionController::notifyListeningFinished() {
    _isListeningActive = false;
    _startListeningCalled = false;
    _stopListeningCalled = false;
    if (_lastTurnCancelled) {
        return;
    }
    if (_state == VoiceInteractionState::LISTENING || _state == VoiceInteractionState::STOPPING_CAPTURE) {
        setState(VoiceInteractionState::PROCESSING);
    }
}

void VoiceInteractionController::notifySpeechStarted() {
    if (_lastTurnCancelled) {
        return;
    }
    setState(VoiceInteractionState::SPEAKING);
}

void VoiceInteractionController::notifySpeechFinished() {
    if (_state == VoiceInteractionState::SPEAKING || _state == VoiceInteractionState::PROCESSING) {
        if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
            setState(VoiceInteractionState::READY);
        } else {
            setState(VoiceInteractionState::DISABLED);
        }
    }
}

void VoiceInteractionController::notifyError(const String& err) {
    (void)err;
    _errorStartTimeMs = millis();
    _isListeningActive = false;
    _startListeningCalled = false;
    _stopListeningCalled = false;
    _validator.stop();
    setState(VoiceInteractionState::ERROR_STATE);
}

void VoiceInteractionController::notifyReady() {
    _isListeningActive = false;
    _startListeningCalled = false;
    _stopListeningCalled = false;
    _lastTurnCancelled = false;
    _listeningStartTimeMs = 0;
    _processingStartTimeMs = 0;
    _stoppedStartTimeMs = 0;
    _stopStartTimeMs = 0;
    _errorStartTimeMs = 0;
    _validator.reset();
    if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED) {
        setState(VoiceInteractionState::READY);
    } else {
        setState(VoiceInteractionState::DISABLED);
    }
}

void VoiceInteractionController::processIntents(uint32_t nowMs) {
    VoiceInteractionIntent intent = _pendingIntent;
    _pendingIntent = VoiceInteractionIntent::NONE;

    if (intent == VoiceInteractionIntent::NONE) {
        return;
    }

    // 1. Commande STOP prioritaire absolue (CANCEL_CAPTURE)
    if (intent == VoiceInteractionIntent::CANCEL_CAPTURE) {
        _stopInterruptsCount++;

        if (_isListeningActive || _state == VoiceInteractionState::LISTENING || _state == VoiceInteractionState::STARTING) {
            _voice.cancelListening();
            _cancelListeningCallsCount++;
        }

        _stopStartTimeMs = nowMs;
        setState(VoiceInteractionState::INTERRUPTING);
        _lastTurnCancelled = true;
        _voice.stop();
        _validator.stop();

        _isListeningActive = false;
        _startListeningCalled = false;
        _stopListeningCalled = false;

        if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
            setState(VoiceInteractionState::READY);
        } else {
            setState(VoiceInteractionState::DISABLED);
        }
        return;
    }

    // 2. Récupération après état d'erreur
    if (intent == VoiceInteractionIntent::RECOVER) {
        setState(VoiceInteractionState::RECOVERING);
        _voice.stop();
        _validator.reset();
        _isListeningActive = false;
        _startListeningCalled = false;
        _stopListeningCalled = false;

        if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED) {
            setState(VoiceInteractionState::READY);
        } else {
            setState(VoiceInteractionState::DISABLED);
        }
        return;
    }

    // 3. Premier clic : démarrer la capture (START_CAPTURE)
    if (intent == VoiceInteractionIntent::START_CAPTURE) {
        if (!_startListeningCalled && _voice.isReady() && !_voice.isBusy() && !_isListeningActive) {
            _startListeningCalled = true;
            _stopListeningCalled = false;
            _lastTurnCancelled = false;
            _captureStartTimeMs = nowMs;
            _validator.start(nowMs);
            setState(VoiceInteractionState::STARTING);

            _startListeningCallsCount++;
            if (_voice.startListening()) {
                _isListeningActive = true;
                _listeningStartTimeMs = nowMs;
            } else {
                _lastRejectCause = "START_LISTENING_FAILED";
                _startListeningCalled = false;
                _isListeningActive = false;
                _validator.stop();
                if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED) {
                    setState(VoiceInteractionState::READY);
                } else {
                    setState(VoiceInteractionState::DISABLED);
                }
            }
        } else {
            _lastRejectCause = getRejectCause();
            _startListeningCalled = false;
            _isListeningActive = false;
        }
        return;
    }

    // 4. Deuxième clic : clôturer la capture et valider la parole (FINISH_CAPTURE)
    if (intent == VoiceInteractionIntent::FINISH_CAPTURE) {
        if (!_stopListeningCalled && (_isListeningActive || _state == VoiceInteractionState::LISTENING || _state == VoiceInteractionState::STARTING)) {
            VoiceCaptureMetrics res = _validator.evaluate(nowMs);
            _validator.stop();
            _isListeningActive = false;

            if (_onCaptureValidated) {
                _onCaptureValidated(res);
            }

            if (res.isValid) {
                // Parole confirmée : arrêt propre et transmission vers l'IA
                _stopListeningCalled = true;
                _lastTurnCancelled = false;
                setState(VoiceInteractionState::STOPPING_CAPTURE);
                _responseRequestsSentCount++;
                _stopListeningCallsCount++;

                if (_voice.isListening()) {
                    if (!_voice.stopListening()) {
                        _lastTurnCancelled = true;
                        _cancelListeningCallsCount++;
                        _voice.cancelListening();
                        _voice.stop();
                        if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED) {
                            setState(VoiceInteractionState::READY);
                        } else {
                            setState(VoiceInteractionState::DISABLED);
                        }
                    }
                } else {
                    _lastTurnCancelled = true;
                    _cancelListeningCallsCount++;
                    _voice.cancelListening();
                    if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED) {
                        setState(VoiceInteractionState::READY);
                    } else {
                        setState(VoiceInteractionState::DISABLED);
                    }
                }
            } else {
                // Absence de parole exploitable (silence, bruit bref, énergie faible, etc.) : annulation stricte
                _silenceRejectionsCount++;
                _lastTurnCancelled = true;
                _cancelListeningCallsCount++;
                _voice.cancelListening();
                _startListeningCalled = false;
                _stopListeningCalled = false;
                _lastRejectCause = res.rejectReason;

                if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
                    setState(VoiceInteractionState::READY);
                } else {
                    setState(VoiceInteractionState::DISABLED);
                }

                if (_onSilence) {
                    _onSilence(res.rejectReason);
                }
            }
        }
        return;
    }
}

void VoiceInteractionController::checkWatchdogs(uint32_t nowMs) {
    // 1. Stabilisation INTERRUPTING -> retour READY dès que VoiceKit est au repos
    if (_state == VoiceInteractionState::INTERRUPTING) {
        if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
            _stopStartTimeMs = 0;
            setState(VoiceInteractionState::READY);
        } else if (_stopStartTimeMs > 0 && nowMs >= _stopStartTimeMs && (nowMs - _stopStartTimeMs >= _config.interruptingRecoveryMs)) {
            _stopStartTimeMs = 0;
            if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
                setState(VoiceInteractionState::READY);
            } else {
                setState(VoiceInteractionState::DISABLED);
            }
        }
    }

    // 2. Synchronisation non bloquante PROCESSING -> READY si la session a terminé
    if (_state == VoiceInteractionState::PROCESSING) {
        if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED &&
            !_voice.isBusy() && !_voice.isSpeaking() && !_voice.isListening()) {
            _processingStartTimeMs = 0;
            _isListeningActive = false;
            setState(VoiceInteractionState::READY);
        }
    }

    // 3. Timeout d'écoute micro (15 s par défaut) avec évaluation VAD de secours
    if ((_isListeningActive || _state == VoiceInteractionState::LISTENING || _voice.isListening()) && _listeningStartTimeMs > 0 && nowMs >= _listeningStartTimeMs && (nowMs - _listeningStartTimeMs >= _config.micTimeoutMs)) {
        VoiceCaptureMetrics res = _validator.evaluate(nowMs);
        _validator.stop();
        _isListeningActive = false;
        _listeningStartTimeMs = 0;

        if (_onCaptureValidated) {
            _onCaptureValidated(res);
        }

        if (res.isValid) {
            _lastTurnCancelled = false;
            setState(VoiceInteractionState::STOPPING_CAPTURE);
            _responseRequestsSentCount++;
            _stopListeningCallsCount++;
            if (!_voice.stopListening()) {
                _lastTurnCancelled = true;
                _cancelListeningCallsCount++;
                _voice.cancelListening();
                _voice.stop();
                if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
                    setState(VoiceInteractionState::READY);
                } else {
                    setState(VoiceInteractionState::DISABLED);
                }
            }
        } else {
            _silenceRejectionsCount++;
            _lastTurnCancelled = true;
            _cancelListeningCallsCount++;
            _voice.cancelListening();
            _voice.stop();
            if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
                setState(VoiceInteractionState::READY);
            } else {
                setState(VoiceInteractionState::DISABLED);
            }
            if (_onSilence) {
                _onSilence(res.rejectReason);
            }
        }
    }

    // 4. Timeout attente réponse serveur (15 s par défaut)
    if (_state == VoiceInteractionState::PROCESSING && _processingStartTimeMs > 0 && nowMs >= _processingStartTimeMs && (nowMs - _processingStartTimeMs >= _config.serverResponseTimeoutMs)) {
        _isListeningActive = false;
        _processingStartTimeMs = 0;
        _lastTurnCancelled = true;
        _voice.stop();
        if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
            setState(VoiceInteractionState::READY);
        } else {
            setState(VoiceInteractionState::DISABLED);
        }
    }

    // 5. Reprise automatique après STOPPED (> 1 s)
    if (_voice.getState() == VoiceState::STOPPED && _stoppedStartTimeMs > 0 && nowMs >= _stoppedStartTimeMs && (nowMs - _stoppedStartTimeMs >= _config.stoppedRecoveryMs)) {
        _stoppedStartTimeMs = 0;
        if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
            setState(VoiceInteractionState::READY);
        } else {
            setState(VoiceInteractionState::DISABLED);
        }
    }

    // 6. Reprise automatique après erreur persistante (> 15 s) si Wi-Fi connecté
    if (_state == VoiceInteractionState::ERROR_STATE && _errorStartTimeMs > 0 && nowMs >= _errorStartTimeMs && (nowMs - _errorStartTimeMs >= _config.errorRecoveryMs)) {
        _errorStartTimeMs = 0;
        if (isWifiConnected()) {
            _isListeningActive = false;
            _voice.stop();
            if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
                setState(VoiceInteractionState::READY);
            } else {
                setState(VoiceInteractionState::DISABLED);
            }
        }
    }
}

void VoiceInteractionController::update(uint32_t nowMs) {
    if (nowMs == 0) {
        nowMs = millis();
    }
    processIntents(nowMs);
    checkWatchdogs(nowMs);
}
