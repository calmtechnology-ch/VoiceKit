#include "VoiceUxLiveBridge.h"

VoiceUxLiveBridge::VoiceUxLiveBridge()
    : _state(VoiceUxBridgeState::READY),
      _stateBeforePause(VoiceUxBridgeState::READY),
      _hasPendingAction(false),
      _activeTurnId(1),
      _turnInProgress(false),
      _captureActive(false),
      _finalReceivedForTurn(false),
      _turnCancelled(false),
      _currentSpeechIsRecall(false),
      _currentSpeechIsFeedback(false),
      _currentSpeechTurnId(0),
      _speechGeneration(0),
      _currentSpeechId(0),
      _speechActive(false) {
}

bool VoiceUxLiveBridge::begin() {
    _state = VoiceUxBridgeState::READY;
    _stateBeforePause = VoiceUxBridgeState::READY;
    _hasPendingAction = false;
    _activeAction = VoiceUxBridgeAction();
    _activeTurnId = 1;
    _turnInProgress = false;
    _captureActive = false;
    _finalReceivedForTurn = false;
    _turnCancelled = false;
    _interimText = "";
    _lastFinalText = "";
    _currentSpokenText = "";
    _currentSpeechIsRecall = false;
    _currentSpeechIsFeedback = false;
    _currentSpeechTurnId = 0;
    _speechGeneration = 0;
    _currentSpeechId = 0;
    _speechActive = false;
    _lastError = "";

    resetBridgeStats();
    bool ok = _controller.begin();
    if (ok) {
        _controller.setEnabled(true);
    }
    return ok;
}

void VoiceUxLiveBridge::setEnabled(bool enabled) {
    _controller.setEnabled(enabled);
}

bool VoiceUxLiveBridge::isEnabled() const {
    return _controller.isEnabled();
}

void VoiceUxLiveBridge::seedTestReplies() {
    // 2 réponses locales déterministes prescrites par le cahier des charges v0.6.4.2
    _controller.rememberReply("La température actuelle est de vingt-deux degrés.");
    _controller.rememberReply("L'humidité relative est de quarante-huit pour cent.");
}

bool VoiceUxLiveBridge::onListeningStarted() {
    if (!canStartListening()) {
        emitDiagnostic("[BRIDGE] Refus de capture microphone : etat incompatible (" +
                       String(voiceUxBridgeStateToString(_state)) + ")");
        return false;
    }

    _activeTurnId++;
    _turnInProgress = true;
    _captureActive = true;
    _finalReceivedForTurn = false;
    _turnCancelled = false;
    _interimText = "";
    _hasPendingAction = false;

    setState(VoiceUxBridgeState::LISTENING);
    return true;
}

void VoiceUxLiveBridge::onListeningFinished() {
    if (_state == VoiceUxBridgeState::LISTENING) {
        _turnInProgress = false;
        setState(VoiceUxBridgeState::TRANSCRIBING);
    }
}

bool VoiceUxLiveBridge::feedTranscription(const String& text, bool isFinal, uint32_t turnId) {
    if (!isFinal) {
        // En cas de tour annulé ou périmé, ne pas écraser ni afficher les transcriptions partielles tardives
        if (_turnCancelled || (turnId != 0 && turnId < _activeTurnId)) {
            return false;
        }
        _interimText = text;
        _stats.partialTranscriptionsIgnored++;
        return false;
    }

    // Transcription finale
    if (turnId != 0) {
        if (turnId < _activeTurnId) {
            _stats.staleTranscriptionsIgnored++;
            emitDiagnostic("[BRIDGE] Transcription tardive ignoree (turnId=" + String(turnId) +
                           " < active=" + String(_activeTurnId) + ")");
            return false;
        }
        if (turnId > _activeTurnId) {
            _stats.staleTranscriptionsIgnored++;
            emitDiagnostic("[BRIDGE] Transcription rejetee : turnId futur ou inconnu (" + String(turnId) + ")");
            return false;
        }
    }

    // Règle 7 & P0 : Rejeter les résultats d'une capture annulée (STOP ou erreur)
    if (_turnCancelled) {
        _stats.staleTranscriptionsIgnored++;
        emitDiagnostic("[BRIDGE] Transcription rejetee car le tour a ete annule");
        return false;
    }

    // Règle 3 & P0 : Rejeter toute deuxième transcription finale appartenant au même tour
    if (_finalReceivedForTurn) {
        _stats.duplicateTranscriptionsIgnored++;
        emitDiagnostic("[BRIDGE] Transcription finale dupliquee ignoree pour ce tour");
        return false;
    }

    // Règle 2 : Aucune restitution TTS ni traitement final si le haut-parleur est actif
    if (_state == VoiceUxBridgeState::SPEAKING) {
        emitDiagnostic("[BRIDGE] Transcription ignoree : haut-parleur actif");
        return false;
    }

    if (turnId == 0) {
        // Lier au tour actif sans jamais créer implicitement un nouveau tour ni réinitialiser une annulation
        turnId = _activeTurnId;
    }

    return processInternal(text, false, turnId);
}

bool VoiceUxLiveBridge::feedSimulatedText(const String& text) {
    _activeTurnId++;
    _turnInProgress = false;
    _captureActive = false;
    _finalReceivedForTurn = false;
    _turnCancelled = false;
    return processInternal(text, true, _activeTurnId);
}

bool VoiceUxLiveBridge::processInternal(const String& text, bool isSimulated, uint32_t turnId) {
    _stats.totalTranscriptions++;
    if (isSimulated) {
        _stats.simulatedTranscriptions++;
    } else {
        _stats.realTranscriptions++;
    }

    // Règle 8 : Les événements tardifs d'un ancien tour doivent être ignorés
    if (turnId != 0 && turnId < _activeTurnId) {
        _stats.staleTranscriptionsIgnored++;
        emitDiagnostic("[BRIDGE] Transcription tardive ignoree (turnId=" + String(turnId) +
                       " < active=" + String(_activeTurnId) + ")");
        return false;
    }

    // Règle 7 : Une action annulée ne peut pas être exécutée ultérieurement
    if (_turnCancelled) {
        _stats.staleTranscriptionsIgnored++;
        emitDiagnostic("[BRIDGE] Transcription rejetee car le tour a ete annule");
        return false;
    }

    // Règle 3 : Une transcription finale ne produit qu'une seule action
    if (_finalReceivedForTurn && !isSimulated) {
        _stats.duplicateTranscriptionsIgnored++;
        emitDiagnostic("[BRIDGE] Transcription finale dupliquee ignoree pour ce tour");
        return false;
    }

    // Règle 2 : Aucune restitution TTS ni traitement final si le micro est actif ou haut-parleur actif
    if (_state == VoiceUxBridgeState::SPEAKING) {
        emitDiagnostic("[BRIDGE] Transcription ignoree : haut-parleur actif");
        return false;
    }

    _finalReceivedForTurn = true;
    _lastFinalText = text;
    _interimText = "";

    // Gestion de l'état PAUSED
    if (_state == VoiceUxBridgeState::PAUSED) {
        VoiceUxResult testResult = _controller.processFinalText(text);
        if (testResult.intent == VoiceUxIntent::RESUME) {
            _stats.recognizedCommands++;
            VoiceUxBridgeAction act;
            act.type = VoiceUxBridgeActionType::RESUME;
            act.intent = VoiceUxIntent::RESUME;
            act.text = text;
            act.turnId = _activeTurnId;
            act.isSimulated = isSimulated;
            act.timestampMs = millis();

            _pendingAction = act;
            _hasPendingAction = true;
            setState(VoiceUxBridgeState::ACTION_PENDING);
            if (_actionPendingCb) _actionPendingCb(act);
            return true;
        } else if (testResult.intent == VoiceUxIntent::STOP) {
            _stats.recognizedCommands++;
            _stats.stopRequestsCount++;
            VoiceUxBridgeAction act;
            act.type = VoiceUxBridgeActionType::STOP;
            act.intent = VoiceUxIntent::STOP;
            act.turnId = _activeTurnId;
            act.isSimulated = isSimulated;
            act.timestampMs = millis();

            _pendingAction = act;
            _hasPendingAction = true;
            setState(VoiceUxBridgeState::ACTION_PENDING);
            if (_actionPendingCb) _actionPendingCb(act);
            return true;
        } else {
            // Règle PAUSE : les demandes conversationnelles ordinaires ne sont pas exécutées
            emitDiagnostic("[BRIDGE] Requete ignoree car le pont est en mode PAUSE");
            if (!_captureActive) {
                _finalReceivedForTurn = false;
                _activeTurnId++;
            }
            return false;
        }
    }

    setState(VoiceUxBridgeState::VOICE_UX_PROCESSING);
    VoiceUxResult result = _controller.processFinalText(text);
    _stats.lastProcessingTimeUs = result.processingTimeUs;

    // Règle : Filtrage TextGate IGNORE
    if (result.gateDecision == TextGateDecision::IGNORE) {
        _stats.ignoredCommands++;
        emitDiagnostic("[BRIDGE] TextGate : transcription ignoree (vide ou hesitation)");
        setState(VoiceUxBridgeState::READY);
        if (!_captureActive) {
            _finalReceivedForTurn = false;
            _activeTurnId++;
        }
        return false;
    }

    VoiceUxBridgeAction act;
    act.turnId = _activeTurnId;
    act.intent = result.intent;
    act.isSimulated = isSimulated;
    act.timestampMs = millis();

    switch (result.intent) {
        case VoiceUxIntent::REPEAT: {
            _stats.recognizedCommands++;
            String lastReply = _controller.getReplyMemory().getLastReply();
            act.type = VoiceUxBridgeActionType::TTS_SPEAK;
            if (!lastReply.isEmpty()) {
                act.text = lastReply;
                act.isRecall = true;
            } else {
                act.text = "Aucune réponse à répéter.";
                act.isFeedback = true;
            }
            break;
        }
        case VoiceUxIntent::PREVIOUS: {
            _stats.recognizedCommands++;
            String prevReply;
            act.type = VoiceUxBridgeActionType::TTS_SPEAK;
            if (_controller.getReplyMemory().getPreviousReply(prevReply)) {
                act.text = prevReply;
                act.isRecall = true;
            } else {
                act.text = "Aucune réponse précédente disponible.";
                act.isFeedback = true;
            }
            break;
        }
        case VoiceUxIntent::STOP: {
            _stats.recognizedCommands++;
            _stats.stopRequestsCount++;
            act.type = VoiceUxBridgeActionType::STOP;
            break;
        }
        case VoiceUxIntent::PAUSE: {
            _stats.recognizedCommands++;
            act.type = VoiceUxBridgeActionType::PAUSE;
            break;
        }
        case VoiceUxIntent::RESUME: {
            _stats.recognizedCommands++;
            act.type = VoiceUxBridgeActionType::RESUME;
            break;
        }
        case VoiceUxIntent::SLOWER:
        case VoiceUxIntent::REPHRASE: {
            _stats.recognizedCommands++;
            emitDiagnostic("[BRIDGE] Commande de diagnostic reconnue : " +
                           String(voiceUxIntentToString(result.intent)));
            setState(VoiceUxBridgeState::READY);
            if (!_captureActive) {
                _finalReceivedForTurn = false;
                _activeTurnId++;
            }
            return false;
        }
        case VoiceUxIntent::NONE:
        default: {
            act.type = VoiceUxBridgeActionType::FORWARD_AI;
            act.text = text;
            break;
        }
    }

    if (act.type != VoiceUxBridgeActionType::NONE) {
        _pendingAction = act;
        _hasPendingAction = true;
        setState(VoiceUxBridgeState::ACTION_PENDING);
        if (_actionPendingCb) {
            _actionPendingCb(act);
        }
        if (_actionStatusCb) {
            _actionStatusCb(act, VoiceUxActionStatus::CREATED);
        }
        return true;
    }

    setState(VoiceUxBridgeState::READY);
    return false;
}

bool VoiceUxLiveBridge::hasPendingAction() const {
    return _hasPendingAction;
}

const VoiceUxBridgeAction& VoiceUxLiveBridge::getPendingAction() const {
    return _pendingAction;
}

VoiceUxBridgeAction VoiceUxLiveBridge::consumePendingAction() {
    if (!_hasPendingAction) {
        return VoiceUxBridgeAction();
    }
    VoiceUxBridgeAction act = _pendingAction;
    _activeAction = act;
    _hasPendingAction = false;
    _pendingAction = VoiceUxBridgeAction();

    // Si cette action ne provient pas d'une capture audio PTT (ex: injection directe en test unitaire),
    // réinitialiser le verrou de final pour permettre la prochaine injection séquentielle de test.
    if (!_captureActive) {
        _finalReceivedForTurn = false;
        _activeTurnId++;
    }

    if (_state == VoiceUxBridgeState::ACTION_PENDING) {
        setState((_stateBeforePause == VoiceUxBridgeState::PAUSED) ? VoiceUxBridgeState::PAUSED : VoiceUxBridgeState::READY);
    }

    if (_actionStatusCb) {
        _actionStatusCb(act, VoiceUxActionStatus::DISPATCHED);
    }
    // NOTA : _actionExecutedCb n'est intentionnellement PLUS appelé ici.
    // L'exécution n'est déclarée terminée qu'après achèvement effectif (notifySpeechFinished ou notifyActionCompleted).
    return act;
}

void VoiceUxLiveBridge::cancelPendingAction() {
    if (_hasPendingAction) {
        if (_actionStatusCb) {
            _actionStatusCb(_pendingAction, VoiceUxActionStatus::CANCELLED);
        }
        _hasPendingAction = false;
        _pendingAction = VoiceUxBridgeAction();
    }
    if (_activeAction.type != VoiceUxBridgeActionType::NONE &&
        _activeAction.type != VoiceUxBridgeActionType::STOP &&
        _state != VoiceUxBridgeState::SPEAKING) {
        if (_actionStatusCb) {
            _actionStatusCb(_activeAction, VoiceUxActionStatus::CANCELLED);
        }
        _activeAction = VoiceUxBridgeAction();
    }
    if (_state == VoiceUxBridgeState::ACTION_PENDING) {
        setState((_stateBeforePause == VoiceUxBridgeState::PAUSED) ? VoiceUxBridgeState::PAUSED : VoiceUxBridgeState::READY);
    }
}

void VoiceUxLiveBridge::triggerPriorityStop() {
    _stats.stopRequestsCount++;
    _turnCancelled = true;
    _turnInProgress = false;
    _captureActive = false;
    _activeTurnId++;
    cancelPendingAction();

    if (_state == VoiceUxBridgeState::SPEAKING) {
        notifySpeechFinished(false, _currentSpeechId);
    } else {
        setState((_stateBeforePause == VoiceUxBridgeState::PAUSED || _state == VoiceUxBridgeState::PAUSED)
                     ? VoiceUxBridgeState::PAUSED
                     : VoiceUxBridgeState::READY);
    }
    emitDiagnostic("[BRIDGE] STOP prioritaire execute");
}

void VoiceUxLiveBridge::notifySpeechStarted(const String& spokenText) {
    if (!canSpeak()) {
        emitDiagnostic("[BRIDGE] Avertissement : debut restitution alors que micro actif !");
    }
    _stats.ttsDispatchedCount++;
    _currentSpeechId = ++_speechGeneration;
    _speechActive = true;
    _currentSpokenText = (!spokenText.isEmpty()) ? spokenText : _activeAction.text;
    _currentSpeechIsRecall = _activeAction.isRecall;
    _currentSpeechIsFeedback = _activeAction.isFeedback;
    _currentSpeechTurnId = _activeAction.turnId;
    setState(VoiceUxBridgeState::SPEAKING);
    if (_actionStatusCb && _activeAction.type != VoiceUxBridgeActionType::NONE) {
        _actionStatusCb(_activeAction, VoiceUxActionStatus::ACCEPTED);
    }
}

void VoiceUxLiveBridge::notifySpeechFinished(bool completed, uint32_t speechId) {
    if (_state != VoiceUxBridgeState::SPEAKING || !_speechActive) {
        emitDiagnostic("[BRIDGE] Notification TTS ignoree : aucun TTS actif");
        return;
    }

    if (speechId != 0 && speechId != _currentSpeechId) {
        emitDiagnostic("[BRIDGE] Notification TTS ignoree : speechId obsolete (" +
                       String(speechId) + " != " + String(_currentSpeechId) + ")");
        return;
    }

    if (completed) {
        _stats.ttsCompletedCount++;
        // Mémorisation uniquement si restitution complète valide (non recall, non feedback)
        if (!_currentSpeechIsRecall && !_currentSpeechIsFeedback && !_currentSpokenText.isEmpty()) {
            _controller.rememberReply(_currentSpokenText);
        }
        if (_actionExecutedCb && _activeAction.type != VoiceUxBridgeActionType::NONE) {
            _actionExecutedCb(_activeAction);
        }
        if (_actionStatusCb && _activeAction.type != VoiceUxBridgeActionType::NONE) {
            _actionStatusCb(_activeAction, VoiceUxActionStatus::COMPLETED);
        }
    } else {
        _stats.ttsCancelledCount++;
        if (_actionStatusCb && _activeAction.type != VoiceUxBridgeActionType::NONE) {
            _actionStatusCb(_activeAction, VoiceUxActionStatus::CANCELLED);
        }
    }

    _speechActive = false;
    _currentSpeechId = 0;
    _activeAction = VoiceUxBridgeAction();
    _currentSpokenText = "";
    _currentSpeechIsRecall = false;
    _currentSpeechIsFeedback = false;
    _currentSpeechTurnId = 0;

    if (_stateBeforePause == VoiceUxBridgeState::PAUSED) {
        setState(VoiceUxBridgeState::PAUSED);
    } else {
        setState(VoiceUxBridgeState::READY);
    }
}

void VoiceUxLiveBridge::notifySpeechRefused(const String& reason) {
    emitDiagnostic("[BRIDGE] Restitution TTS refusee : " + reason);
    if (_actionStatusCb && _activeAction.type != VoiceUxBridgeActionType::NONE) {
        _actionStatusCb(_activeAction, VoiceUxActionStatus::REJECTED);
    }
    _speechActive = false;
    _currentSpeechId = 0;
    _activeAction = VoiceUxBridgeAction();
    _currentSpokenText = "";
    _currentSpeechIsRecall = false;
    _currentSpeechIsFeedback = false;
    _currentSpeechTurnId = 0;

    if (_stateBeforePause == VoiceUxBridgeState::PAUSED) {
        setState(VoiceUxBridgeState::PAUSED);
    } else {
        setState(VoiceUxBridgeState::READY);
    }
}

void VoiceUxLiveBridge::notifyActionCompleted() {
    if (_activeAction.type != VoiceUxBridgeActionType::NONE) {
        if (_actionExecutedCb) {
            _actionExecutedCb(_activeAction);
        }
        if (_actionStatusCb) {
            _actionStatusCb(_activeAction, VoiceUxActionStatus::COMPLETED);
        }
        _activeAction = VoiceUxBridgeAction();
    }
    if (_state == VoiceUxBridgeState::ACTION_PENDING) {
        setState((_stateBeforePause == VoiceUxBridgeState::PAUSED) ? VoiceUxBridgeState::PAUSED : VoiceUxBridgeState::READY);
    }
}

void VoiceUxLiveBridge::notifyActionRejected(const String& reason) {
    emitDiagnostic("[BRIDGE] Action refusee : " + reason);
    if (_actionStatusCb && _activeAction.type != VoiceUxBridgeActionType::NONE) {
        _actionStatusCb(_activeAction, VoiceUxActionStatus::REJECTED);
    }
    _activeAction = VoiceUxBridgeAction();
    if (_state == VoiceUxBridgeState::ACTION_PENDING) {
        setState((_stateBeforePause == VoiceUxBridgeState::PAUSED) ? VoiceUxBridgeState::PAUSED : VoiceUxBridgeState::READY);
    }
}

void VoiceUxLiveBridge::notifyError(const String& errorMsg) {
    _stats.errorsCount++;
    _lastError = errorMsg;
    _turnCancelled = true;
    _turnInProgress = false;
    _activeTurnId++;
    cancelPendingAction();
    setState(VoiceUxBridgeState::ERROR);
    emitDiagnostic("[BRIDGE][ERREUR] " + errorMsg);
}

void VoiceUxLiveBridge::recoverFromError() {
    _lastError = "";
    _hasPendingAction = false;
    _activeAction = VoiceUxBridgeAction();
    _turnCancelled = false;
    _turnInProgress = false;
    _captureActive = false;
    _speechActive = false;
    _currentSpeechId = 0;
    setState(VoiceUxBridgeState::READY);
    emitDiagnostic("[BRIDGE] Recuperation d'erreur effectuee -> READY");
}

void VoiceUxLiveBridge::setPaused(bool paused) {
    if (paused) {
        if (_state != VoiceUxBridgeState::PAUSED) {
            _stateBeforePause = _state;
            setState(VoiceUxBridgeState::PAUSED);
            emitDiagnostic("[BRIDGE] Session mise en PAUSE");
        }
    } else {
        if (_state == VoiceUxBridgeState::PAUSED) {
            setState(VoiceUxBridgeState::READY);
            emitDiagnostic("[BRIDGE] Sortie de pause -> READY");
        }
    }
}

bool VoiceUxLiveBridge::isPaused() const {
    return _state == VoiceUxBridgeState::PAUSED;
}

bool VoiceUxLiveBridge::canStartListening() const {
    return (_state == VoiceUxBridgeState::READY || _state == VoiceUxBridgeState::PAUSED);
}

bool VoiceUxLiveBridge::canSpeak() const {
    return (_state != VoiceUxBridgeState::LISTENING && _state != VoiceUxBridgeState::TRANSCRIBING);
}

VoiceUxBridgeState VoiceUxLiveBridge::getState() const {
    return _state;
}

uint32_t VoiceUxLiveBridge::getActiveTurnId() const {
    return _activeTurnId;
}

const VoiceUxBridgeAction& VoiceUxLiveBridge::getActiveAction() const {
    return _activeAction;
}

bool VoiceUxLiveBridge::isCurrentSpeechRecall() const {
    return _currentSpeechIsRecall;
}

bool VoiceUxLiveBridge::isCurrentSpeechFeedback() const {
    return _currentSpeechIsFeedback;
}

uint32_t VoiceUxLiveBridge::getCurrentSpeechTurnId() const {
    return _currentSpeechTurnId;
}

uint32_t VoiceUxLiveBridge::getCurrentSpeechId() const {
    return _currentSpeechId;
}

const String& VoiceUxLiveBridge::getCurrentSpokenText() const {
    return _currentSpokenText;
}

const String& VoiceUxLiveBridge::getInterimText() const {
    return _interimText;
}

const String& VoiceUxLiveBridge::getLastFinalText() const {
    return _lastFinalText;
}

const String& VoiceUxLiveBridge::getLastError() const {
    return _lastError;
}

VoiceUxController& VoiceUxLiveBridge::getController() {
    return _controller;
}

const VoiceUxController& VoiceUxLiveBridge::getController() const {
    return _controller;
}

ReplyMemory& VoiceUxLiveBridge::getReplyMemory() {
    return _controller.getReplyMemory();
}

const ReplyMemory& VoiceUxLiveBridge::getReplyMemory() const {
    return _controller.getReplyMemory();
}

bool VoiceUxLiveBridge::rememberReply(const String& reply) {
    return _controller.rememberReply(reply);
}

void VoiceUxLiveBridge::clearMemory() {
    _controller.getReplyMemory().clear();
}

const VoiceUxBridgeStats& VoiceUxLiveBridge::getBridgeStats() const {
    return _stats;
}

void VoiceUxLiveBridge::resetBridgeStats() {
    _stats = VoiceUxBridgeStats();
}

void VoiceUxLiveBridge::onStateChanged(StateChangeCallback cb) {
    _stateCb = cb;
}

void VoiceUxLiveBridge::onActionPending(ActionPendingCallback cb) {
    _actionPendingCb = cb;
}

void VoiceUxLiveBridge::onActionExecuted(ActionExecutedCallback cb) {
    _actionExecutedCb = cb;
}

void VoiceUxLiveBridge::onActionStatus(ActionStatusCallback cb) {
    _actionStatusCb = cb;
}

void VoiceUxLiveBridge::onDiagnostic(DiagnosticCallback cb) {
    _diagCb = cb;
}

void VoiceUxLiveBridge::setState(VoiceUxBridgeState newState) {
    if (_state != newState) {
        VoiceUxBridgeState oldState = _state;
        _state = newState;
        if (_stateCb) {
            _stateCb(oldState, newState);
        }
    }
}

void VoiceUxLiveBridge::emitDiagnostic(const String& msg) {
    if (_diagCb) {
        _diagCb(msg);
    }
}
