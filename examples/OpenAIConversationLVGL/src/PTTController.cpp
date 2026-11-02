/**
 * @file PTTController.cpp
 * @brief Implementation de la machine a etats Push-to-Talk independante.
 */

#include "PTTController.h"

const char* pttStateToString(PTTState st) {
    switch (st) {
        case PTTState::DISABLED:         return "DISABLED";
        case PTTState::READY:            return "READY";
        case PTTState::STARTING:         return "STARTING";
        case PTTState::LISTENING:        return "LISTENING";
        case PTTState::STOPPING_CAPTURE: return "STOPPING_CAPTURE";
        case PTTState::PROCESSING:       return "PROCESSING";
        case PTTState::SPEAKING:         return "SPEAKING";
        case PTTState::INTERRUPTING:     return "INTERRUPTING";
        case PTTState::RECOVERING:       return "RECOVERING";
        case PTTState::ERROR_STATE:      return "ERROR";
        default:                         return "UNKNOWN";
    }
}

PTTController::PTTController(VoxCoreVoice& voiceEngine, Diagnostics& diag)
    : _voice(voiceEngine), _diag(diag) {}

void PTTController::begin() {
    _state = PTTState::DISABLED;
    _prevState = PTTState::DISABLED;
    _reqPress = false;
    _reqRelease = false;
    _reqStop = false;
    _reqRecover = false;
    _serialListening = false;
    _touchListening = false;
}

void PTTController::setState(PTTState newState) {
    if (_state != newState) {
        _prevState = _state;
        _state = newState;
    }
}

bool PTTController::isActionAllowed() const {
    if (!WiFi.isConnected()) return false;
    if (_voice.isConnecting()) return false;
    if (_voice.getSession().getState() != LiveSessionState::CONNECTED) return false;
    if (_voice.getState() == VoiceState::ERROR_STATE) return false;
    return true;
}

const char* PTTController::getRejectCause() const {
    if (_touchListening || _serialListening) return "ALREADY_IN_LISTENING";
    if (!WiFi.isConnected()) return "WIFI_NOT_CONNECTED";
    if (_voice.isConnecting()) return "CONNECTING_SERVICE";
    if (_voice.getSession().getState() != LiveSessionState::CONNECTED) {
        switch (_voice.getSession().getState()) {
            case LiveSessionState::CONNECTING:       return "SESSION_CONNECTING";
            case LiveSessionState::RECONNECTING:     return "SESSION_RECONNECTING";
            case LiveSessionState::WAITING_RESPONSE: return "SESSION_WAITING_RESPONSE";
            case LiveSessionState::PLAYING:          return "SESSION_PLAYING";
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
    return "UNKNOWN";
}

void PTTController::onButtonPress(uint32_t contactId, int32_t x, int32_t y) {
    _curContactId = contactId;
    _curTouchX = x;
    _curTouchY = y;
    _diag.logPttPress(contactId, x, y, pttStateToString(_state), voiceStateToString(_voice.getState()));

    if (_state == PTTState::PROCESSING || _state == PTTState::SPEAKING) {
        _reqStop = true;
    } else if (_state == PTTState::INTERRUPTING) {
        // Interruption prioritaire deja en cours : ne pas reenclencher stop() a repetition
        Serial.printf("[PTT] Interruption deja en cours (contact #%u), appui ignore\n", (unsigned)contactId);
    } else if (_state == PTTState::ERROR_STATE) {
        _reqRecover = true;
    } else if (_state == PTTState::READY) {
        if (_voice.isReady() && !_voice.isBusy()) {
            _reqPress = true;
        } else {
            const char* cause = getRejectCause();
            _diag.logPttReject("BUTTON_PRESS", cause, voiceStateToString(_voice.getState()), 0,
                               liveSessionStateToString(_voice.getSession().getState()),
                               _voice.isBusy(), _voice.getConversationController().isTurnActive(),
                               _voice.isSpeaking(), _voice.isListening(), _voice.isReady(), _touchListening);
        }
    } else {
        const char* cause = getRejectCause();
        _diag.logPttReject("BUTTON_PRESS", cause, voiceStateToString(_voice.getState()), 0,
                           liveSessionStateToString(_voice.getSession().getState()),
                           _voice.isBusy(), _voice.getConversationController().isTurnActive(),
                           _voice.isSpeaking(), _voice.isListening(), _voice.isReady(), _touchListening);
    }
}

void PTTController::onButtonRelease(uint32_t contactId, int32_t x, int32_t y) {
    _curContactId = contactId;
    _curTouchX = x;
    _curTouchY = y;
    uint32_t dur = (millis() >= _pressStartTimeMs) ? (millis() - _pressStartTimeMs) : 0;
    _diag.logPttRelease(contactId, x, y, dur, _touchListening);

    if (_state == PTTState::LISTENING || _state == PTTState::STARTING || _touchListening) {
        _reqRelease = true;
    }
}

void PTTController::onButtonPressLost(uint32_t contactId, int32_t x, int32_t y) {
    onButtonRelease(contactId, x, y);
}

void PTTController::triggerStop() {
    _reqStop = true;
}

void PTTController::triggerSerialPress() {
    if (_state == PTTState::PROCESSING || _state == PTTState::SPEAKING) {
        _reqStop = true;
        return;
    }
    if (_state == PTTState::ERROR_STATE) {
        _reqRecover = true;
        return;
    }
    if (_voice.isReady() && !_voice.isBusy() && !_touchListening && !_serialListening) {
        Serial.println("[PTT][SERIE] Commande PARLER validee");
        _serialListening = true;
        _pressStartTimeMs = millis();
        setState(PTTState::STARTING);
        if (_voice.startListening()) {
            _listeningStartTimeMs = millis();
        } else {
            _diag.logPttReject("SERIE_PRESS", "START_LISTENING_FAILED", voiceStateToString(_voice.getState()), 0,
                               liveSessionStateToString(_voice.getSession().getState()),
                               _voice.isBusy(), _voice.getConversationController().isTurnActive(),
                               _voice.isSpeaking(), _voice.isListening(), _voice.isReady(), _touchListening);
            _serialListening = false;
            setState(PTTState::READY);
        }
    } else {
        const char* cause = getRejectCause();
        _diag.logPttReject("SERIE_PRESS", cause, voiceStateToString(_voice.getState()), 0,
                           liveSessionStateToString(_voice.getSession().getState()),
                           _voice.isBusy(), _voice.getConversationController().isTurnActive(),
                           _voice.isSpeaking(), _voice.isListening(), _voice.isReady(), _touchListening);
    }
}

void PTTController::triggerSerialRelease() {
    _serialListening = false;
    _touchListening = false;
    if (_voice.isListening()) {
        Serial.println("[PTT][SERIE] Commande RELACHER validee -> stopListening()");
        setState(PTTState::STOPPING_CAPTURE);
        if (!_voice.stopListening()) {
            _voice.cancelListening();
            _voice.stop();
        }
    }
}

void PTTController::notifyVoiceStateChanged(VoiceState newState) {
    switch (newState) {
        case VoiceState::READY:
            _listeningStartTimeMs = 0;
            _processingStartTimeMs = 0;
            _stoppedStartTimeMs = 0;
            _stopStartTimeMs = 0;
            _errorStartTimeMs = 0;
            _touchListening = false;
            _serialListening = false;
            setState(PTTState::READY);
            break;
        case VoiceState::IDLE:
            if (_voice.isReady() && !_voice.isBusy()) {
                setState(PTTState::READY);
            } else {
                setState(PTTState::DISABLED);
            }
            break;
        case VoiceState::LISTENING:
            _listeningStartTimeMs = millis();
            setState(PTTState::LISTENING);
            break;
        case VoiceState::PROCESSING:
        case VoiceState::TRANSCRIBING:
            _processingStartTimeMs = millis();
            setState(PTTState::PROCESSING);
            break;
        case VoiceState::SPEAKING:
            setState(PTTState::SPEAKING);
            break;
        case VoiceState::STOPPED:
            _stoppedStartTimeMs = millis();
            // L'etat READY doit etre autorise UNIQUEMENT lorsque VoiceKit confirme reellement sa disponibilite
            if (_voice.isReady() && !_voice.isBusy()) {
                setState(PTTState::READY);
            }
            break;
        case VoiceState::ERROR_STATE:
            _errorStartTimeMs = millis();
            setState(PTTState::ERROR_STATE);
            break;
        case VoiceState::CONNECTING_WIFI:
        case VoiceState::CONNECTING_SERVICE:
        case VoiceState::UNINITIALIZED:
        case VoiceState::DISCONNECTED:
            setState(PTTState::DISABLED);
            break;
        default:
            break;
    }
}

void PTTController::notifyListeningStarted() {
    _listeningStartTimeMs = millis();
    if (!_serialListening) {
        _touchListening = true;
    }
    setState(PTTState::LISTENING);
}

void PTTController::notifyListeningFinished() {
    _touchListening = false;
    _serialListening = false;
    setState(PTTState::PROCESSING);
}

void PTTController::notifySpeechStarted() {
    setState(PTTState::SPEAKING);
}

void PTTController::notifySpeechFinished() {
    if (_state == PTTState::SPEAKING || _state == PTTState::PROCESSING) {
        setState(PTTState::READY);
    }
}

void PTTController::notifyError(const String& err) {
    (void)err;
    _errorStartTimeMs = millis();
    _touchListening = false;
    _serialListening = false;
    setState(PTTState::ERROR_STATE);
}

void PTTController::notifyReady() {
    _touchListening = false;
    _serialListening = false;
    _listeningStartTimeMs = 0;
    _processingStartTimeMs = 0;
    _stoppedStartTimeMs = 0;
    _stopStartTimeMs = 0;
    _errorStartTimeMs = 0;
    setState(PTTState::READY);
}

void PTTController::processIntents(uint32_t nowMs) {
    // 1. Traitement STOP prioritaire
    if (_reqStop) {
        _reqStop = false;
        _diag.recordStopInterrupt();
        Serial.printf("[PTT][STOP] Interruption prioritaire STOP executee (contact #%u)\n", (unsigned)_curContactId);
        _touchListening = false;
        _serialListening = false;
        _stopStartTimeMs = nowMs;
        setState(PTTState::INTERRUPTING);
        _voice.stop();
        // L'etat READY est autorise uniquement si VoiceKit est immediatement disponible et non occupe
        if (_voice.isReady() && !_voice.isBusy()) {
            setState(PTTState::READY);
        }
        return;
    }

    // 2. Traitement Recuperation apres erreur
    if (_reqRecover) {
        _reqRecover = false;
        uint32_t errDur = (_errorStartTimeMs > 0 && nowMs >= _errorStartTimeMs) ? (nowMs - _errorStartTimeMs) : 0;
        _diag.logPttRecovery("Reinitialisation utilisateur suite a erreur", errDur);
        _touchListening = false;
        _serialListening = false;
        setState(PTTState::RECOVERING);
        _voice.stop();
        if (_voice.isReady()) {
            setState(PTTState::READY);
        } else if (!WiFi.isConnected()) {
            setState(PTTState::DISABLED);
        }
        return;
    }

    // 3. Traitement Press (Demande de parole)
    if (_reqPress) {
        _reqPress = false;
        if (_voice.isReady() && !_voice.isBusy() && !_touchListening && !_serialListening) {
            _pressStartTimeMs = nowMs;
            setState(PTTState::STARTING);
            if (_voice.startListening()) {
                _touchListening = true;
                _listeningStartTimeMs = nowMs;
            } else {
                _diag.logPttReject("INTENT_PRESS", "START_LISTENING_FAILED", voiceStateToString(_voice.getState()), 0,
                                   liveSessionStateToString(_voice.getSession().getState()),
                                   _voice.isBusy(), _voice.getConversationController().isTurnActive(),
                                   _voice.isSpeaking(), _voice.isListening(), _voice.isReady(), _touchListening);
                _touchListening = false;
                setState(PTTState::READY);
            }
        } else {
            const char* cause = getRejectCause();
            _diag.logPttReject("INTENT_PRESS", cause, voiceStateToString(_voice.getState()), 0,
                               liveSessionStateToString(_voice.getSession().getState()),
                               _voice.isBusy(), _voice.getConversationController().isTurnActive(),
                               _voice.isSpeaking(), _voice.isListening(), _voice.isReady(), _touchListening);

            // Verifier s'il s'agit d'un etat persistant necessitant une recuperation
            if ((_voice.getState() == VoiceState::STOPPED) ||
                (_voice.getConversationController().isTurnActive() && !_voice.isSpeaking() && !_voice.isListening())) {
                _diag.logPttRecovery(cause, 0);
                _voice.stop();
                if (_voice.isReady()) {
                    setState(PTTState::READY);
                }
            }
        }
    }

    // 4. Traitement Release (Fin de prise de parole)
    if (_reqRelease) {
        _reqRelease = false;
        _touchListening = false;
        setState(PTTState::STOPPING_CAPTURE);
        if (_voice.isListening()) {
            if (!_voice.stopListening()) {
                Serial.println("[PTT][WARN] stopListening() a echoue -> cancelListening()");
                _voice.cancelListening();
                _voice.stop();
            }
        }
    }
}

void PTTController::checkWatchdogs(uint32_t nowMs, bool isPhysicalTouchActive) {
    // Verification de synchronisation INTERRUPTING : retour a READY des que VoiceKit est disponible
    if (_state == PTTState::INTERRUPTING) {
        if (_voice.isReady() && !_voice.isBusy()) {
            _stopStartTimeMs = 0;
            setState(PTTState::READY);
        } else if (_stopStartTimeMs > 0 && nowMs >= _stopStartTimeMs && (nowMs - _stopStartTimeMs >= 1500)) {
            _stopStartTimeMs = 0;
            if (_voice.isReady()) {
                _diag.logPttRecovery("Interruption stabilisee (1.5 s) -> retour READY", 1500);
                setState(PTTState::READY);
            }
        }
    }

    // Synchronisation non bloquante si PTT est en PROCESSING mais que VoiceKit a termine
    if (_state == PTTState::PROCESSING) {
        if (_voice.isReady() && !_voice.isBusy() && !_voice.isSpeaking() && !_voice.isListening()) {
            uint32_t procDur = (_processingStartTimeMs > 0 && nowMs >= _processingStartTimeMs) ? (nowMs - _processingStartTimeMs) : 0;
            _diag.logPttRecovery("Synchronisation PTT -> READY : reponse achevee sans restitution audio", procDur);
            _processingStartTimeMs = 0;
            _touchListening = false;
            _serialListening = false;
            setState(PTTState::READY);
        }
    }
    // Securite de coherence physique : si le doigt n'est plus sur l'ecran mais _touchListening est actif
    if (!isPhysicalTouchActive && _touchListening && !_serialListening) {
        if (nowMs - _pressStartTimeMs > 200) {
            _diag.logPttRecovery("Incoherence detectee : touchListening=true sans appui physique", 0);
            _touchListening = false;
            if (_voice.isListening()) {
                _voice.stopListening();
            }
        }
    }

    // Timeout ecoute micro (15 s)
    if (_voice.isListening() && _listeningStartTimeMs > 0 && nowMs >= _listeningStartTimeMs && (nowMs - _listeningStartTimeMs >= AppConfig::MIC_TIMEOUT_MS)) {
        _diag.logPttRecovery("Timeout ecoute microphone (15 s)", nowMs - _listeningStartTimeMs);
        _touchListening = false;
        _serialListening = false;
        _listeningStartTimeMs = 0;
        _voice.stopListening();
    }

    // Timeout processing serveur (15 s)
    if (_state == PTTState::PROCESSING && _processingStartTimeMs > 0 && nowMs >= _processingStartTimeMs && (nowMs - _processingStartTimeMs >= AppConfig::PROCESSING_TIMEOUT_MS)) {
        _diag.logPttRecovery("Timeout attente reponse serveur (15 s)", nowMs - _processingStartTimeMs);
        _touchListening = false;
        _serialListening = false;
        _processingStartTimeMs = 0;
        _voice.stop();
        if (_voice.isReady()) {
            setState(PTTState::READY);
        }
    }

    // Reprise automatique apres STOPPED (> 1 s)
    if (_voice.getState() == VoiceState::STOPPED && _stoppedStartTimeMs > 0 && nowMs >= _stoppedStartTimeMs && (nowMs - _stoppedStartTimeMs >= AppConfig::STOPPED_RECOVERY_MS)) {
        _stoppedStartTimeMs = 0;
        if (_voice.isReady()) {
            _diag.logPttRecovery("Transition automatique STOPPED -> READY (apres 1 s)", AppConfig::STOPPED_RECOVERY_MS);
            setState(PTTState::READY);
        }
    }

    // Reprise automatique apres erreur persistante (> 15 s) si Wi-Fi connecte
    if (_state == PTTState::ERROR_STATE && _errorStartTimeMs > 0 && nowMs >= _errorStartTimeMs && (nowMs - _errorStartTimeMs >= AppConfig::ERROR_RECOVERY_MS)) {
        _errorStartTimeMs = 0;
        if (WiFi.isConnected()) {
            _diag.logPttRecovery("Tentative automatique de reprise apres erreur persistante (15 s)", AppConfig::ERROR_RECOVERY_MS);
            _touchListening = false;
            _serialListening = false;
            _voice.stop();
            if (_voice.isReady()) {
                setState(PTTState::READY);
            }
        }
    }
}

void PTTController::update(uint32_t nowMs, bool isPhysicalTouchActive) {
    processIntents(nowMs);
    checkWatchdogs(nowMs, isPhysicalTouchActive);
}

