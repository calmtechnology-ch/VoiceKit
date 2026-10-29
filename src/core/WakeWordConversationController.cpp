#include "core/WakeWordConversationController.h"
#include "hal/HardwareManager.h"
#include <cctype>
#include <cstring>

#if defined(ESP32) || defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

WakeWordConversationController::WakeWordConversationController() {
    _state = WakeWordConversationState::DISABLED;
}

WakeWordConversationController::~WakeWordConversationController() {
    _onWakeWordDetected = nullptr;
    _onWakeWordDetectedSimple = nullptr;
    _onConnecting = nullptr;
    _onConversationReady = nullptr;
    _onUserListening = nullptr;
    _onResponseSpeaking = nullptr;
    _onConversationEnded = nullptr;
    _onConversationEndedSimple = nullptr;
    _onReturnedToWake = nullptr;
    _onRecoverableError = nullptr;
    _onStateChanged = nullptr;
}

bool WakeWordConversationController::begin(WakeWordManager* wakeWord,
                                           IVoiceProvider* provider,
                                           LiveSessionController* session,
                                           ConversationController* conversation,
                                           LiveAudioStreamer* streamer,
                                           LiveAudioPlayer* player,
                                           const WakeWordConversationConfig& config) {
    if (!wakeWord || !session || !conversation || !streamer || !player) {
        return false;
    }

    _wakeWord = wakeWord;
    _provider = provider;
    _session = session;
    _conversation = conversation;
    _streamer = streamer;
    _player = player;
    setConfig(config);

    // Liaison du flux RMS microphone sans écraser le callback WakeWord ni le callback applicatif
    _streamer->setConversationAudioCallback([this](const int16_t*, size_t, uint32_t rms) {
        uint16_t clamped = (rms > 65535U) ? 65535U : static_cast<uint16_t>(rms);
        this->onAudioChunkCaptured(clamped);
    });

    _lastSeenWakeStopFailures = _wakeWord->getMetrics().captureStopFailures;
    _lastSeenSessionReconnects = _session->getReconnectCount();
    _initialized = true;
    refreshMemoryMetrics();
    return true;
}

void WakeWordConversationController::setProvider(IVoiceProvider* provider, VoiceProviderType providerType) {
    _provider = provider;
    _providerType = providerType;
}

void WakeWordConversationController::setConfig(const WakeWordConversationConfig& config) {
    _config = config;
    _vad.setSilenceDurationMs(_config.vadSilenceDurationMs);
    _vad.setMinSpeechDurationMs(static_cast<uint32_t>(_config.vadMinSpeechFrames) * 32U);
    if (_config.maxTurnDurationMs > 0) {
        _vad.setMaxSegmentDurationMs(_config.maxTurnDurationMs);
    }
}

void WakeWordConversationController::setSilenceTimeoutMs(uint32_t timeoutMs) {
    _config.silenceTimeoutMs = timeoutMs;
}

void WakeWordConversationController::setMaxSessionDurationMs(uint32_t maxDurationMs) {
    _config.maxSessionDurationMs = maxDurationMs;
}

void WakeWordConversationController::addCustomVoiceStopPhrase(const String& phrase) {
    String norm = normalizeStopIntentText(phrase);
    if (!norm.isEmpty()) {
        _customStopPhrases.push_back(norm);
    }
}

void WakeWordConversationController::clearCustomVoiceStopPhrases() {
    _customStopPhrases.clear();
}

bool WakeWordConversationController::enable(bool en) {
    if (!en) {
        disable();
        return true;
    }

    if (!_initialized || !_wakeWord) {
        _lastError = "WakeWordConversationController non initialise";
        return false;
    }

    if (_enabled && _state == WakeWordConversationState::WAKE_ARMED && _wakeWord->isListening()) {
        return true;
    }

    _enabled = true;
    _lastError = "";
    _lastEndReason = WakeWordSessionEndReason::NONE;
    _wakeRequestPending.store(false, std::memory_order_release);
    _pendingFinalTranscription.pending = false;
    _manualStopPending = false;
    _voiceStopConfirmed = false;
    _endSessionAfterTurn = false;
    _immediateStopRequested = false;
    clearRmsQueue();
    _vad.reset();

    // S'assurer que l'auto-reprise interne de WakeWordManager ne court-circuite pas RETURNING_TO_WAKE
    _wakeWord->setAutoResumeAfterVoiceActivity(false);
    _wakeWord->setDeferCaptureStopOnDetection(true);
    _wakeWord->notifyVoiceActivity(false, false);

    uint32_t nowMs = millis();
    if (!_wakeWord->enable(true) || _wakeWord->isError() || _wakeWord->getState() != WakeWordState::ARMED) {
        _lastSeenWakeStopFailures = _wakeWord->getMetrics().captureStopFailures;
        String err = !_wakeWord->getLastError().isEmpty()
            ? _wakeWord->getLastError()
            : String("Echec activation initiale WakeNet");
        enterRecoverableError(err, WakeWordSessionEndReason::MIC_TRANSITION_ERROR, nowMs, true, false);
        return false;
    }

    _lastSeenWakeStopFailures = _wakeWord->getMetrics().captureStopFailures;
    transitionTo(WakeWordConversationState::WAKE_ARMED, nowMs, "Activation mode Wake Word Conversation");
    refreshMemoryMetrics();
    return true;
}

void WakeWordConversationController::disable() {
    if (!_enabled && _state == WakeWordConversationState::DISABLED) {
        return;
    }

    _enabled = false;
    _wakeRequestPending.store(false, std::memory_order_release);
    _pendingFinalTranscription.pending = false;
    _manualStopPending = false;
    clearRmsQueue();

    if (_conversation && _conversation->isTurnActive()) {
        _conversation->requestStop();
    }
    if (_streamer && _streamer->isStreaming()) {
        _streamer->cancel();
    }
    if (_config.disconnectAfterSession && _session && _session->isActive()) {
        _session->deactivate();
    }
    if (_wakeWord) {
        _wakeWord->setDeferCaptureStopOnDetection(false);
        if (_wakeWord->isEnabled()) {
            _wakeWord->disable();
        }
    }

    _lastEndReason = WakeWordSessionEndReason::CONTROLLER_DISABLED;
    transitionTo(WakeWordConversationState::DISABLED, millis(), "Desactivation mode Wake Word Conversation");
}

void WakeWordConversationController::notifyWakeWordDetected(const String& word, float confidence, bool isSimulated) {
    // Règle Section 3 : Le callback onWakeWordDetected() ne doit PAS exécuter d'opérations longues.
    // Il enregistre uniquement une demande de démarrage si l'état est WAKE_ARMED.
    if (!_enabled) {
        return;
    }

    if (_state != WakeWordConversationState::WAKE_ARMED || _wakeRequestPending.load(std::memory_order_acquire)) {
        _metrics.ignoredConcurrentDetections++;
        return;
    }

    _pendingWakeWord = word.isEmpty() ? String("Bonjour ESP") : word;
    _pendingWakeConfidence = confidence;
    _pendingWakeSimulated = isSimulated;
    uint32_t ts = millis();
    _pendingWakeTimestampMs = (ts == 0) ? 1 : ts;
    _wakeRequestPending.store(true, std::memory_order_release);
}

void WakeWordConversationController::notifyWakeWordDetected(const WakeWordEvent& ev) {
    notifyWakeWordDetected(ev.word, ev.confidence, ev.isSimulated);
}

void WakeWordConversationController::notifyUserTranscription(const String& text, bool isFinal) {
    if (!_enabled) {
        return;
    }
    if (_state != WakeWordConversationState::CONVERSATION_LISTENING &&
        _state != WakeWordConversationState::CONVERSATION_SPEAKING) {
        return;
    }

    // Règle Section 5 : Fermeture par commande vocale UNIQUEMENT sur transcription finale fiable
    if (!isFinal || text.isEmpty()) {
        return;
    }

    _pendingFinalTranscription.text = text;
    _pendingFinalTranscription.pending = true;
}

void WakeWordConversationController::notifyTurnFinished(uint64_t) {
    if (!_enabled) {
        return;
    }
    if (_state == WakeWordConversationState::CONVERSATION_LISTENING ||
        _state == WakeWordConversationState::CONVERSATION_SPEAKING) {
        _turnCompletedFlag = true;
    }
}

void WakeWordConversationController::onAudioChunkCaptured(uint16_t rms) {
    // Exécuté depuis la tâche audio FreeRTOS : strictement lock-free et sans allocation
    if (!_enabled || _state != WakeWordConversationState::CONVERSATION_LISTENING || _waitingForResponse) {
        return;
    }
    size_t head = _rmsHead.load(std::memory_order_relaxed);
    size_t next = (head + 1) % RMS_QUEUE_CAPACITY;
    if (next != _rmsTail.load(std::memory_order_acquire)) {
        _rmsQueue[head] = rms;
        _rmsHead.store(next, std::memory_order_release);
    }
}

void WakeWordConversationController::clearRmsQueue() {
    _rmsTail.store(_rmsHead.load(std::memory_order_acquire), std::memory_order_release);
}

bool WakeWordConversationController::stopSession(bool immediate) {
    if (!_enabled || !isSessionInProgress()) {
        return false;
    }

    if (!immediate) {
        return requestEndSessionAfterTurn();
    }

    if (_inUpdate) {
        _manualStopPending = true;
        _manualStopImmediate = true;
        return true;
    }

    uint32_t nowMs = millis();
    _wakeRequestPending.store(false, std::memory_order_release);
    _immediateStopRequested = true;
    _lastEndReason = WakeWordSessionEndReason::MANUAL_STOP;

    if (_conversation && _conversation->isTurnActive()) {
        _conversation->requestStop();
    }
    if (_streamer && _streamer->isStreaming()) {
        _streamer->cancel();
    }
    if (_player && _player->isPlaying() > 0) {
        _player->stop(AudioStopReason::USER_STOP);
    }

    _endingEnterMs = nowMs;
    transitionTo(WakeWordConversationState::ENDING_SESSION, nowMs, "Arret manuel explicite");
    return true;
}

bool WakeWordConversationController::requestEndSessionAfterTurn() {
    if (!_enabled || !isSessionInProgress()) {
        return false;
    }

    _endSessionAfterTurn = true;
    if (_lastEndReason == WakeWordSessionEndReason::NONE) {
        _lastEndReason = WakeWordSessionEndReason::MANUAL_STOP;
    }

    // Si aucun audio n'est en cours de lecture ni attendu, passer directement en ENDING_SESSION
    if (!_inUpdate &&
        _state == WakeWordConversationState::CONVERSATION_LISTENING &&
        !_waitingForResponse &&
        (!_player || _player->isPlaying() == 0)) {
        uint32_t nowMs = millis();
        if (_conversation && _conversation->isTurnActive()) {
            _conversation->requestStop();
        }
        _immediateStopRequested = false;
        _endingEnterMs = nowMs;
        transitionTo(WakeWordConversationState::ENDING_SESSION, nowMs, "Fin de session demandee");
    }
    return true;
}

bool WakeWordConversationController::isSessionActive() const {
    return (_state == WakeWordConversationState::CONVERSATION_LISTENING ||
            _state == WakeWordConversationState::CONVERSATION_SPEAKING);
}

bool WakeWordConversationController::isSessionInProgress() const {
    return (_state == WakeWordConversationState::WAKE_DETECTED ||
            _state == WakeWordConversationState::PREPARING_SESSION ||
            _state == WakeWordConversationState::CONNECTING ||
            _state == WakeWordConversationState::CONVERSATION_LISTENING ||
            _state == WakeWordConversationState::CONVERSATION_SPEAKING ||
            _state == WakeWordConversationState::ENDING_SESSION ||
            _state == WakeWordConversationState::RECOVERABLE_ERROR);
}

const char* WakeWordConversationController::getDisplayStatusText() const {
    switch (_state) {
        case WakeWordConversationState::DISABLED:
            return "DESACTIVE";
        case WakeWordConversationState::WAKE_ARMED:
            return "EN ATTENTE — « Bonjour ESP »";
        case WakeWordConversationState::WAKE_DETECTED:
        case WakeWordConversationState::PREPARING_SESSION:
            return "MOT DE RÉVEIL DÉTECTÉ";
        case WakeWordConversationState::CONNECTING:
            return (_providerType == VoiceProviderType::OPENAI)
                ? "CONNEXION OPENAI"
                : "CONNEXION GEMINI";
        case WakeWordConversationState::CONVERSATION_LISTENING:
            return (_userSpokeInTurn || _waitingForResponse)
                ? "ÉCOUTE"
                : "PRÊT — PARLEZ";
        case WakeWordConversationState::CONVERSATION_SPEAKING:
            return "RÉPONSE VOCALE";
        case WakeWordConversationState::ENDING_SESSION:
            return "FIN DE CONVERSATION";
        case WakeWordConversationState::RETURNING_TO_WAKE:
            return "RETOUR EN ÉCOUTE";
        case WakeWordConversationState::RECOVERABLE_ERROR:
            return "ERREUR / RÉCUPÉRATION";
        default:
            return "INCONNU";
    }
}

const char* WakeWordConversationController::getUiTransitionBannerText() const {
    switch (_state) {
        case WakeWordConversationState::DISABLED:
            return "DESACTIVE";
        case WakeWordConversationState::WAKE_ARMED:
            return "EN ATTENTE — « Bonjour ESP »";
        case WakeWordConversationState::WAKE_DETECTED:
        case WakeWordConversationState::PREPARING_SESSION:
            return "BONJOUR ESP DÉTECTÉ";
        case WakeWordConversationState::CONNECTING:
            return "CONNEXION";
        case WakeWordConversationState::CONVERSATION_LISTENING:
            return (_userSpokeInTurn || _waitingForResponse)
                ? "ÉCOUTE"
                : "PRÊT — PARLEZ";
        case WakeWordConversationState::CONVERSATION_SPEAKING:
            return "RÉPONSE";
        case WakeWordConversationState::ENDING_SESSION:
            return "FIN DE CONVERSATION";
        case WakeWordConversationState::RETURNING_TO_WAKE:
            return "RETOUR EN ÉCOUTE";
        case WakeWordConversationState::RECOVERABLE_ERROR:
            return "ERREUR / RÉCUPÉRATION";
        default:
            return "INCONNU";
    }
}

void WakeWordConversationController::transitionTo(WakeWordConversationState newState, uint32_t nowMs, const char* reason) {
    if (_state == newState) {
        return;
    }

    WakeWordConversationState oldState = _state;
    _state = newState;
    _stateEnterMs = nowMs;

    Serial.printf("[WakeConv] %s -> %s%s%s%s\n",
                  wakeWordConversationStateToString(oldState),
                  wakeWordConversationStateToString(newState),
                  reason ? " (" : "",
                  reason ? reason : "",
                  reason ? ")" : "");

    pushEvent(WakeWordConversationEventType::STATE_CHANGED, nowMs, reason ? String(reason) : String(""));
    if (_onStateChanged) {
        _onStateChanged(_state);
    }
}

void WakeWordConversationController::enterRecoverableError(const String& errorMsg,
                                                           WakeWordSessionEndReason reason,
                                                           uint32_t nowMs,
                                                           bool isMicFailure,
                                                           bool isNetworkFailure) {
    _lastError = errorMsg;
    _lastEndReason = reason;
    _errorEnterMs = nowMs;
    _wakeRequestPending.store(false, std::memory_order_release);
    _waitingForResponse = false;
    _userSpokeInTurn = false;
    clearRmsQueue();

    _metrics.errorCount++;
    if (isMicFailure) {
        _metrics.micTransitionFailures++;
    }
    if (isNetworkFailure) {
        _metrics.networkFailures++;
        _hadNetworkFailureBeforeConnect = true;
    }

    // Nettoyage sécurisé des ressources actives
    if (_conversation && _conversation->isTurnActive()) {
        _conversation->requestStop();
    }
    if (_streamer && _streamer->isStreaming()) {
        _streamer->cancel();
    }
    if (_player && _player->isPlaying() > 0) {
        _player->stop(AudioStopReason::USER_STOP);
    }
    if (_config.disconnectAfterSession && _session && _session->isActive()) {
        _session->deactivate();
    }

    if (_sessionStartMs > 0) {
        _metrics.lastSessionDurationMs = (nowMs >= _sessionStartMs) ? (nowMs - _sessionStartMs) : 0;
        _metrics.totalSessionDurationMs += _metrics.lastSessionDurationMs;
        _sessionStartMs = 0;
    }

    Serial.printf("[WakeConv] ERREUR RECUPERABLE : %s\n", _lastError.c_str());
    transitionTo(WakeWordConversationState::RECOVERABLE_ERROR, nowMs, _lastError.c_str());
    pushEvent(WakeWordConversationEventType::RECOVERABLE_ERROR, nowMs, _lastError);
    if (_onRecoverableError) {
        _onRecoverableError(_lastError);
    }
}

void WakeWordConversationController::pushEvent(WakeWordConversationEventType type,
                                               uint32_t nowMs,
                                               const String& text,
                                               float confidence,
                                               bool isSimulated) {
    WakeWordConversationEvent ev;
    ev.type = type;
    ev.state = _state;
    ev.endReason = _lastEndReason;
    ev.text = text;
    ev.confidence = confidence;
    ev.isSimulated = isSimulated;
    ev.timestampMs = nowMs;

    if (_eventCount >= EVENT_QUEUE_CAPACITY) {
        // Écraser l'événement le plus ancien pour ne jamais bloquer
        _eventHead = (_eventHead + 1) % EVENT_QUEUE_CAPACITY;
        _eventCount--;
    }
    _eventQueue[_eventTail] = ev;
    _eventTail = (_eventTail + 1) % EVENT_QUEUE_CAPACITY;
    _eventCount++;
}

bool WakeWordConversationController::pollEvent(WakeWordConversationEvent& outEvent) {
    if (_eventCount == 0) {
        return false;
    }
    outEvent = _eventQueue[_eventHead];
    _eventHead = (_eventHead + 1) % EVENT_QUEUE_CAPACITY;
    _eventCount--;
    return true;
}

bool WakeWordConversationController::isNetworkReady() const {
    if (_networkReadyCheck) {
        return _networkReadyCheck();
    }
    if (_session) {
        return _session->isNetworkReady();
    }
    return true;
}

String WakeWordConversationController::normalizeStopIntentText(const String& input) {
    String out = "";
    out.reserve(input.length());
    bool lastWasSpace = true;

    for (size_t i = 0; i < input.length(); ++i) {
        uint8_t c = static_cast<uint8_t>(input[i]);
        // Normalisation élémentaire UTF-8 des accents français courants (é, è, ê, à, ù)
        if (c == 0xC3 && (i + 1) < input.length()) {
            uint8_t next = static_cast<uint8_t>(input[i + 1]);
            char mapped = 0;
            if (next >= 0xA8 && next <= 0xAB) mapped = 'e'; // è, é, ê, ë
            else if (next >= 0x88 && next <= 0x8B) mapped = 'e'; // È, É, Ê, Ë
            else if (next >= 0xA0 && next <= 0xA5) mapped = 'a'; // à, â...
            else if (next >= 0x80 && next <= 0x85) mapped = 'a';
            else if (next >= 0xB9 && next <= 0xBC) mapped = 'u'; // ù, û, ü
            else if (next >= 0xB2 && next <= 0xB6) mapped = 'o'; // ô
            else if (next == 0xA7 || next == 0x87) mapped = 'c'; // ç
            if (mapped != 0) {
                out += mapped;
                lastWasSpace = false;
                i++;
                continue;
            }
        }
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
            lastWasSpace = false;
        } else if (!lastWasSpace) {
            out += ' ';
            lastWasSpace = true;
        }
    }

    out.trim();
    return out;
}

bool WakeWordConversationController::checkVoiceStopIntent(const String& finalText) const {
    String norm = normalizeStopIntentText(finalText);
    if (norm.length() < 4) {
        return false;
    }

    // Commandes exactes courtes
    if (norm == "stop" || norm == "quitter" || norm == "au revoir" || norm == "merci au revoir" || norm == "adieu") {
        return true;
    }

    // Expressions explicites de fin de session
    static const char* const kStopPhrases[] = {
        "au revoir",
        "arrete la conversation",
        "arreter la conversation",
        "stop la conversation",
        "stop conversation",
        "termine la conversation",
        "terminer la conversation",
        "fin de la conversation",
        "fin de conversation",
        "ferme la conversation",
        "fermer la conversation",
        "termine la session",
        "terminer la session",
        "ferme la session",
        "fermer la session",
        "fin de session"
    };

    for (size_t i = 0; i < sizeof(kStopPhrases) / sizeof(kStopPhrases[0]); ++i) {
        if (norm.indexOf(kStopPhrases[i]) >= 0) {
            return true;
        }
    }

    for (const String& custom : _customStopPhrases) {
        if (!custom.isEmpty() && norm.indexOf(custom) >= 0) {
            return true;
        }
    }

    return false;
}

bool WakeWordConversationController::startNextListeningTurn(uint32_t nowMs, bool isFirstTurnOfSession) {
    // Protection contre double réservation microphone ou réactivation prématurée
    if (!_streamer || !_conversation || !_provider) {
        enterRecoverableError("Composants audio/conversation manquants", WakeWordSessionEndReason::MIC_TRANSITION_ERROR, nowMs, true, false);
        return false;
    }

    _streamer->checkQuarantinedTasks();
    if (_streamer->isLocalCapture() || _streamer->isStreaming() || _streamer->isTaskQuarantined() ||
        HardwareManager::getInstance().isMicCaptureReserved()) {
        enterRecoverableError("Conflit de reservation microphone avant tour conversationnel",
                              WakeWordSessionEndReason::MIC_TRANSITION_ERROR, nowMs, true, false);
        return false;
    }

    if (!_provider->isConnected() || !_provider->isSetupComplete()) {
        enterRecoverableError("Session fournisseur non prete au demarrage de l'ecoute",
                              WakeWordSessionEndReason::NETWORK_INTERRUPTION, nowMs, false, true);
        return false;
    }

    clearRmsQueue();
    _vad.reset();
    _userSpokeInTurn = false;
    _waitingForResponse = false;
    _turnCompletedFlag = false;
    _turnListenStartMs = nowMs;
    _lastActivityMs = nowMs;

    if (!_conversation->startTurn()) {
        String err = strlen(_streamer->getLastError()) > 0
            ? String(_streamer->getLastError())
            : String("Echec startTurn() du controleur de conversation");
        enterRecoverableError(err, WakeWordSessionEndReason::MIC_TRANSITION_ERROR, nowMs, true, false);
        return false;
    }

    transitionTo(WakeWordConversationState::CONVERSATION_LISTENING, nowMs,
                 isFirstTurnOfSession ? "Premier tour d'ecoute actif" : "Tour d'ecoute suivant actif");
    pushEvent(WakeWordConversationEventType::USER_LISTENING, nowMs);
    if (_onUserListening) {
        _onUserListening();
    }
    return true;
}

void WakeWordConversationController::update(uint32_t nowMs) {
    if (_inUpdate || !_enabled || !_initialized) {
        return;
    }

    const uint32_t updateStartUs = micros();
    if (nowMs == 0) {
        nowMs = millis();
    }

    struct ScopedUpdateGuard {
        WakeWordConversationController* self;
        uint32_t startUs;
        WakeWordConversationState entryState;
        ScopedUpdateGuard(WakeWordConversationController* s, uint32_t t0)
            : self(s), startUs(t0), entryState(s->_state) {
            self->_inUpdate = true;
        }
        ~ScopedUpdateGuard() {
            uint32_t durUs = micros() - startUs;
            self->_metrics.lastControllerUpdateDurationUs = durUs;
            if (durUs > self->_metrics.maxControllerUpdateDurationUs) {
                self->_metrics.maxControllerUpdateDurationUs = durUs;
            }
            if (self->_state != entryState) {
                self->_metrics.lastStateTransitionDurationUs = durUs;
                if (durUs > self->_metrics.maxStateTransitionDurationUs) {
                    self->_metrics.maxStateTransitionDurationUs = durUs;
                }
            }
            self->syncSubsystemMetrics();
            self->refreshMemoryMetrics();
            self->_inUpdate = false;
        }
    } guard(this, updateStartUs);

    // Synchronisation du compteur de reconnexions du contrôleur de session sous-jacent
    if (_session) {
        uint32_t curSessionReconnects = _session->getReconnectCount();
        if (curSessionReconnects > _lastSeenSessionReconnects) {
            _metrics.reconnectCount += (curSessionReconnects - _lastSeenSessionReconnects);
            _lastSeenSessionReconnects = curSessionReconnects;
        }
    }

    // Traitement d'une demande d'arrêt manuel différée depuis un callback
    if (_manualStopPending) {
        _manualStopPending = false;
        if (_manualStopImmediate) {
            _wakeRequestPending.store(false, std::memory_order_release);
            _immediateStopRequested = true;
            _lastEndReason = WakeWordSessionEndReason::MANUAL_STOP;
            if (_conversation && _conversation->isTurnActive()) {
                _conversation->requestStop();
            }
            if (_streamer && _streamer->isStreaming()) {
                _streamer->cancel();
            }
            if (_player && _player->isPlaying() > 0) {
                _player->stop(AudioStopReason::USER_STOP);
            }
            _endingEnterMs = nowMs;
            transitionTo(WakeWordConversationState::ENDING_SESSION, nowMs, "Arret manuel explicite");
            return;
        }
    }

    // Traitement d'une transcription finale reçue pendant la session
    if (_pendingFinalTranscription.pending) {
        String finalText = _pendingFinalTranscription.text;
        _pendingFinalTranscription.pending = false;
        _pendingFinalTranscription.text = "";
        _lastActivityMs = nowMs;

        if (_config.enableVoiceStopCommand && checkVoiceStopIntent(finalText)) {
            Serial.printf("[WakeConv] Commande vocale de fermeture confirmee : \"%s\"\n", finalText.c_str());
            _voiceStopConfirmed = true;
            _endSessionAfterTurn = true;
            _lastEndReason = WakeWordSessionEndReason::VOICE_COMMAND;

            // Si aucune restitution vocale n'est en cours, clore immédiatement la session
            bool audioPlaying = (_player && (_player->isPlaying() > 0 || _player->getBufferedSamples() > 0)) ||
                                (_session && _session->getState() == LiveSessionState::PLAYING);
            if (!audioPlaying && _state == WakeWordConversationState::CONVERSATION_LISTENING) {
                if (_conversation && _conversation->isTurnActive()) {
                    if (_config.disconnectAfterSession) {
                        _conversation->abortTurnForSessionClose();
                    } else {
                        _conversation->requestStop();
                    }
                }
                _immediateStopRequested = false;
                _endingEnterMs = nowMs;
                transitionTo(WakeWordConversationState::ENDING_SESSION, nowMs, "Commande vocale de fermeture");
                return;
            }
        }
    }

    switch (_state) {
        case WakeWordConversationState::DISABLED:
            break;

        case WakeWordConversationState::WAKE_ARMED: {
            // Surveillance d'une défaillance d'arrêt microphone survenue dans WakeWordManager
            if (_wakeWord && _wakeWord->isError()) {
                uint32_t stopFailures = _wakeWord->getMetrics().captureStopFailures;
                bool isMicErr = (stopFailures > _lastSeenWakeStopFailures) ||
                                (_streamer && (_streamer->isLocalCapture() || _streamer->isTaskQuarantined()));
                _lastSeenWakeStopFailures = stopFailures;
                enterRecoverableError(_wakeWord->getLastError(),
                                      WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
                                      nowMs, isMicErr, false);
                return;
            }

            if (_wakeRequestPending.exchange(false, std::memory_order_acq_rel)) {
                _lastDetectedWord = _pendingWakeWord;
                _lastDetectionSimulated = _pendingWakeSimulated;
                _wakeDetectedMs = (_pendingWakeTimestampMs > 0 && _pendingWakeTimestampMs <= nowMs)
                    ? _pendingWakeTimestampMs
                    : nowMs;
                _metrics.detectionCount++;
                if (_lastDetectionSimulated) {
                    _metrics.simulatedDetectionCount++;
                } else {
                    _metrics.realDetectionCount++;
                }

                _lastEndReason = WakeWordSessionEndReason::NONE;
                _lastError = "";
                _currentSessionTurns = 0;
                _voiceStopConfirmed = false;
                _endSessionAfterTurn = false;
                _immediateStopRequested = false;

                transitionTo(WakeWordConversationState::WAKE_DETECTED, nowMs, _lastDetectedWord.c_str());
                pushEvent(WakeWordConversationEventType::WAKE_WORD_DETECTED, nowMs,
                          _lastDetectedWord, _pendingWakeConfidence, _lastDetectionSimulated);
                if (_onWakeWordDetected) {
                    _onWakeWordDetected(_lastDetectedWord, _pendingWakeConfidence, _lastDetectionSimulated);
                }
                if (_onWakeWordDetectedSimple) {
                    _onWakeWordDetectedSimple(_lastDetectedWord);
                }
            }
            break;
        }

        case WakeWordConversationState::WAKE_DETECTED: {
            // Finalisation de l'arrêt de capture locale demandé de manière asynchrone lors de la détection
            if (_wakeWord && (!_wakeWord->ensureCaptureStopped() || _wakeWord->isError())) {
                _lastSeenWakeStopFailures = _wakeWord->getMetrics().captureStopFailures;
                enterRecoverableError(_wakeWord->getLastError(),
                                      WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
                                      nowMs, true, false);
                return;
            }
            // Transition immédiate non bloquante vers la préparation de session
            transitionTo(WakeWordConversationState::PREPARING_SESSION, nowMs, "Suspension WakeNet et verification micro");
            break;
        }

        case WakeWordConversationState::PREPARING_SESSION: {
            // 1. Suspension de WakeNet et libération contrôlée du microphone
            if (_wakeWord) {
                if (_wakeWord->isError()) {
                    _lastSeenWakeStopFailures = _wakeWord->getMetrics().captureStopFailures;
                    enterRecoverableError(_wakeWord->getLastError(),
                                          WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
                                          nowMs, true, false);
                    return;
                }
                if (_wakeWord->getState() == WakeWordState::ARMED ||
                    _wakeWord->getState() == WakeWordState::DETECTED) {
                    if (!_wakeWord->suspend()) {
                        _lastSeenWakeStopFailures = _wakeWord->getMetrics().captureStopFailures;
                        enterRecoverableError(_wakeWord->getLastError(),
                                              WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
                                              nowMs, true, false);
                        return;
                    }
                }
            }

            // 2. Vérification stricte de la libération du microphone avant tout démarrage de session
            if (_streamer) {
                _streamer->checkQuarantinedTasks();
                if (_streamer->isLocalCapture()) {
                    if (!_streamer->stopLocalCapture()) {
                        enterRecoverableError(_streamer->getLastError(),
                                              WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
                                              nowMs, true, false);
                        return;
                    }
                }
                if (_streamer->isStreaming()) {
                    _streamer->cancel();
                }
                if (_streamer->isLocalCapture() || _streamer->isStreaming() ||
                    _streamer->isTaskQuarantined() || HardwareManager::getInstance().isMicCaptureReserved()) {
                    enterRecoverableError("Echec liberation microphone avant session conversationnelle",
                                          WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
                                          nowMs, true, false);
                    return;
                }
            }

            // 3. Nettoyage de tout tour ou session WebSocket résiduelle
            if (_conversation && _conversation->isTurnActive()) {
                _conversation->requestStop();
            }
            if (_session) {
                LiveSessionState sState = _session->getState();
                if (sState == LiveSessionState::ERROR ||
                    sState == LiveSessionState::RECONNECTING ||
                    sState == LiveSessionState::INTERRUPTING ||
                    (_provider && _provider->isConnected() && !_provider->isSetupComplete())) {
                    _session->deactivate();
                }
            }

            // 4. Activation du mode conversation sur le fournisseur configuré
            if (_enableConversationHook) {
                _enableConversationHook();
            } else if (_provider) {
                _provider->setConversationMode(true);
            }

            _connectStartMs = nowMs;
            _metrics.wakeToConnectStartMs = (nowMs >= _wakeDetectedMs) ? (nowMs - _wakeDetectedMs) : 0;

            transitionTo(WakeWordConversationState::CONNECTING, nowMs,
                         (_providerType == VoiceProviderType::OPENAI) ? "Connexion OpenAI Realtime" : "Connexion Gemini Live");
            pushEvent(WakeWordConversationEventType::CONNECTING_STARTED, nowMs);
            if (_onConnecting) {
                _onConnecting();
            }
            break;
        }

        case WakeWordConversationState::CONNECTING: {
            // 1. Vérification de la disponibilité réseau (Wi-Fi + NTP)
            if (!isNetworkReady()) {
                enterRecoverableError("Perte Wi-Fi ou NTP non synchronise pendant la connexion",
                                      WakeWordSessionEndReason::NETWORK_INTERRUPTION,
                                      nowMs, false, true);
                return;
            }

            if (!_session || !_provider) {
                enterRecoverableError("Fournisseur vocal non configure",
                                      WakeWordSessionEndReason::NETWORK_INTERRUPTION,
                                      nowMs, false, true);
                return;
            }

            // 2. Surveillance d'échec ou de déconnexion du contrôleur de session
            LiveSessionState sState = _session->getState();
            if (sState == LiveSessionState::ERROR || sState == LiveSessionState::RECONNECTING) {
                String err = strlen(_session->getLastError()) > 0
                    ? String(_session->getLastError())
                    : String("Echec connexion WebSocket au fournisseur vocal");
                enterRecoverableError(err, WakeWordSessionEndReason::NETWORK_INTERRUPTION, nowMs, false, true);
                return;
            }

            // 3. Surveillance du timeout de connexion
            if (_config.connectTimeoutMs > 0 && (nowMs - _connectStartMs >= _config.connectTimeoutMs)) {
                enterRecoverableError("Timeout de connexion au fournisseur vocal",
                                      WakeWordSessionEndReason::NETWORK_INTERRUPTION,
                                      nowMs, false, true);
                return;
            }

            // 4. Lancement de la connexion WebSocket si la session est déconnectée
            if (sState == LiveSessionState::DISCONNECTED) {
                if (_hadNetworkFailureBeforeConnect) {
                    _metrics.reconnectCount++;
                    _hadNetworkFailureBeforeConnect = false;
                }
                if (!_session->activate()) {
                    const char* provErr = _provider->getLastError();
                    String err = (provErr && strlen(provErr) > 0)
                        ? String(provErr)
                        : String("Echec activation de la session WebSocket");
                    enterRecoverableError(err, WakeWordSessionEndReason::NETWORK_INTERRUPTION, nowMs, false, true);
                    return;
                }
            }

            // 5. Confirmation de connexion ET de configuration (setupComplete) avant capture micro
            if (_provider->isConnected() && _provider->isSetupComplete() &&
                _session->getState() == LiveSessionState::CONNECTED) {
                uint32_t provWsMs = _provider->getWsConnectMs();
                _metrics.wsConnectMs = (provWsMs > 0)
                    ? provWsMs
                    : ((nowMs >= _connectStartMs) ? (nowMs - _connectStartMs) : 0);
                _metrics.wakeToConversationReadyMs = (nowMs >= _wakeDetectedMs) ? (nowMs - _wakeDetectedMs) : 0;

                _sessionStartMs = (nowMs > 0) ? nowMs : 1;
                _lastActivityMs = nowMs;
                _metrics.sessionCount++;
                _currentSessionTurns = 0;

                pushEvent(WakeWordConversationEventType::CONVERSATION_READY, nowMs);
                if (_onConversationReady) {
                    _onConversationReady();
                }

                // Démarrage automatique de la capture conversationnelle
                startNextListeningTurn(nowMs, true);
            }
            break;
        }

        case WakeWordConversationState::CONVERSATION_LISTENING: {
            // 1. Surveillance des interruptions réseau / WebSocket pendant la conversation
            if (!isNetworkReady() || !_provider || !_provider->isConnected() || !_session ||
                _session->getState() == LiveSessionState::ERROR ||
                _session->getState() == LiveSessionState::RECONNECTING ||
                _session->getState() == LiveSessionState::DISCONNECTED) {
                enterRecoverableError("Deconnexion reseau ou WebSocket pendant l'ecoute",
                                      WakeWordSessionEndReason::NETWORK_INTERRUPTION,
                                      nowMs, false, true);
                return;
            }

            // 2. Surveillance de la durée maximale de session
            if (_config.maxSessionDurationMs > 0 && _sessionStartMs > 0 &&
                (nowMs - _sessionStartMs >= _config.maxSessionDurationMs)) {
                if (_conversation && _conversation->isTurnActive()) {
                    if (_config.disconnectAfterSession) {
                        _conversation->abortTurnForSessionClose();
                    } else {
                        _conversation->requestStop();
                    }
                }
                _lastEndReason = WakeWordSessionEndReason::MAX_DURATION_TIMEOUT;
                _immediateStopRequested = false;
                _endingEnterMs = nowMs;
                transitionTo(WakeWordConversationState::ENDING_SESSION, nowMs, "Duree maximale de session atteinte");
                return;
            }

            // 3. Détection du début de la réponse vocale du fournisseur
            bool speakingStarted = (_conversation && _conversation->getState() == VoiceState::SPEAKING) ||
                                   (_session && _session->getState() == LiveSessionState::PLAYING) ||
                                   (_player && _player->isPlaying() > 0);
            if (speakingStarted) {
                if (_streamer && _streamer->isStreaming()) {
                    _streamer->stop();
                }
                if (_player) {
                    _player->setPlaybackAllowed(true);
                }
                _waitingForResponse = false;
                _turnFinishedMs = 0;
                _currentSessionTurns++;
                _metrics.turnCount++;
                _lastActivityMs = nowMs;

                transitionTo(WakeWordConversationState::CONVERSATION_SPEAKING, nowMs, "Restitution reponse vocale");
                pushEvent(WakeWordConversationEventType::RESPONSE_SPEAKING, nowMs);
                if (_onResponseSpeaking) {
                    _onResponseSpeaking();
                }
                return;
            }

            // 4. Traitement VAD local des blocs RMS microphone pendant la capture active
            if (!_waitingForResponse) {
                size_t tail = _rmsTail.load(std::memory_order_relaxed);
                size_t head = _rmsHead.load(std::memory_order_acquire);
                while (tail != head) {
                    uint16_t rms = _rmsQueue[tail];
                    tail = (tail + 1) % RMS_QUEUE_CAPACITY;
                    _rmsTail.store(tail, std::memory_order_release);

                    bool speechEnded = _vad.processRms(rms, 32);
                    if (_vad.isSpeechConfirmed() || _vad.isSpeechActive()) {
                        bool wasSpeaking = _userSpokeInTurn;
                        _userSpokeInTurn = true;
                        _lastActivityMs = nowMs;
                        if (!wasSpeaking) {
                            pushEvent(WakeWordConversationEventType::USER_LISTENING, nowMs);
                            if (_onUserListening) {
                                _onUserListening();
                            }
                        }
                    }
                    if (speechEnded || _vad.isSpeechEnded()) {
                        _lastActivityMs = nowMs;
                        _conversation->notifySpeechEndDetected(nowMs);
                        if (!_conversation->finishTurnAudio() || (_streamer && _streamer->isTaskQuarantined())) {
                            enterRecoverableError("Echec arret microphone en fin de parole utilisateur",
                                                  WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
                                                  nowMs, true, false);
                            return;
                        }
                        _waitingForResponse = true;
                        _responseWaitStartMs = nowMs;
                        break;
                    }
                }

                // Plafond de durée d'un tour continu si l'utilisateur parle sans interruption
                if (!_waitingForResponse && _userSpokeInTurn && _config.maxTurnDurationMs > 0 &&
                    (nowMs - _turnListenStartMs >= _config.maxTurnDurationMs)) {
                    _conversation->notifySpeechEndDetected(nowMs);
                    if (!_conversation->finishTurnAudio() || (_streamer && _streamer->isTaskQuarantined())) {
                        enterRecoverableError("Echec arret microphone (duree max tour)",
                                              WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
                                              nowMs, true, false);
                        return;
                    }
                    _waitingForResponse = true;
                    _responseWaitStartMs = nowMs;
                    _lastActivityMs = nowMs;
                }

                // 5. Timeout de silence / d'inactivité (par défaut 30 secondes)
                if (!_waitingForResponse && !_vad.isSpeechActive() && !_userSpokeInTurn) {
                    if (_config.silenceTimeoutMs > 0 && (nowMs - _lastActivityMs >= _config.silenceTimeoutMs)) {
                        if (_conversation && _conversation->isTurnActive()) {
                            if (_config.disconnectAfterSession) {
                                _conversation->abortTurnForSessionClose();
                            } else {
                                _conversation->requestStop();
                            }
                        }
                        _lastEndReason = WakeWordSessionEndReason::SILENCE_TIMEOUT;
                        _immediateStopRequested = false;
                        _endingEnterMs = nowMs;
                        transitionTo(WakeWordConversationState::ENDING_SESSION, nowMs, "Timeout de silence / inactivite");
                        return;
                    }
                }
            } else {
                // 6. En attente de la réponse du fournisseur après fin de parole utilisateur
                if (_turnCompletedFlag || (_conversation && !_conversation->isTurnActive() &&
                    (!_player || _player->isPlaying() == 0) &&
                    _session->getState() == LiveSessionState::CONNECTED)) {
                    _turnCompletedFlag = false;
                    _waitingForResponse = false;
                    _currentSessionTurns++;
                    _metrics.turnCount++;

                    if (_voiceStopConfirmed || _endSessionAfterTurn) {
                        _immediateStopRequested = false;
                        _endingEnterMs = nowMs;
                        transitionTo(WakeWordConversationState::ENDING_SESSION, nowMs, "Fin de session apres tour");
                        return;
                    }

                    startNextListeningTurn(nowMs, false);
                    return;
                }

                if (_config.responseTimeoutMs > 0 && (nowMs - _responseWaitStartMs >= _config.responseTimeoutMs)) {
                    enterRecoverableError("Timeout d'attente de reponse vocale du fournisseur",
                                          WakeWordSessionEndReason::NETWORK_INTERRUPTION,
                                          nowMs, false, true);
                    return;
                }
            }
            break;
        }

        case WakeWordConversationState::CONVERSATION_SPEAKING: {
            _lastActivityMs = nowMs;

            // 1. Surveillance des interruptions réseau / WebSocket pendant la réponse
            if (!isNetworkReady() || !_provider || !_provider->isConnected() || !_session ||
                _session->getState() == LiveSessionState::ERROR ||
                _session->getState() == LiveSessionState::RECONNECTING ||
                _session->getState() == LiveSessionState::DISCONNECTED) {
                enterRecoverableError("Deconnexion reseau ou WebSocket pendant la reponse vocale",
                                      WakeWordSessionEndReason::NETWORK_INTERRUPTION,
                                      nowMs, false, true);
                return;
            }

            // 2. Détection de la fin complète de restitution de la réponse TTS du tour en cours
            // Règle Section 5 & 6 :
            // - La fin d'un tour de réponse TTS ne doit JAMAIS être confondue avec la fin de la session.
            // - La réponse vocale doit être restituée intégralement avant toute fermeture normale de session.
            bool playerDrained = (!_player || _player->isPlaying() == 0);
            bool sessionReady = (_session && _session->getState() == LiveSessionState::CONNECTED);
            bool convTurnFinished = (!_conversation || !_conversation->isTurnActive() ||
                                     _conversation->getState() == VoiceState::READY);

            if (playerDrained && sessionReady && convTurnFinished) {
                if (_turnFinishedMs == 0) {
                    _turnFinishedMs = (nowMs > 0) ? nowMs : 1;
                }

                bool maxDurationReached = (_config.maxSessionDurationMs > 0 && _sessionStartMs > 0 &&
                                           (nowMs - _sessionStartMs >= _config.maxSessionDurationMs));

                if (_voiceStopConfirmed || _endSessionAfterTurn || maxDurationReached) {
                    if (_lastEndReason == WakeWordSessionEndReason::NONE) {
                        if (_voiceStopConfirmed) {
                            _lastEndReason = WakeWordSessionEndReason::VOICE_COMMAND;
                        } else if (maxDurationReached) {
                            _lastEndReason = WakeWordSessionEndReason::MAX_DURATION_TIMEOUT;
                        } else {
                            _lastEndReason = WakeWordSessionEndReason::MANUAL_STOP;
                        }
                    }
                    _turnFinishedMs = 0;
                    _immediateStopRequested = false;
                    _endingEnterMs = nowMs;
                    transitionTo(WakeWordConversationState::ENDING_SESSION, nowMs,
                                 "Fermeture normale apres restitution integrale");
                    return;
                }

                // Multi-tours dans la même session sans répéter « Bonjour ESP »
                if (nowMs - _turnFinishedMs >= _config.turnRestartDelayMs) {
                    _turnFinishedMs = 0;
                    startNextListeningTurn(nowMs, false);
                    return;
                }
            } else {
                _turnFinishedMs = 0;
            }
            break;
        }

        case WakeWordConversationState::ENDING_SESSION: {
            // 1. Garantie de restitution intégrale des derniers échantillons lors d'une fermeture normale
            if (!_immediateStopRequested && _player && _player->isPlaying() > 0) {
                if (_config.maxPlaybackDrainMs == 0 || (nowMs - _endingEnterMs < _config.maxPlaybackDrainMs)) {
                    return;
                }
            }

            // 2. Arrêt complet non bloquant de la conversation et libération des ressources audio et réseau
            if (_conversation && _conversation->isTurnActive()) {
                if (_config.disconnectAfterSession) {
                    _conversation->abortTurnForSessionClose();
                } else {
                    _conversation->requestStop();
                }
            }
            if (_streamer && _streamer->isStreaming()) {
                _streamer->cancel();
            }
            if (_streamer && _streamer->isTaskQuarantined()) {
                _streamer->checkQuarantinedTasks();
            }
            if (_config.disconnectAfterSession && _session) {
                if (_session->isActive() && !_session->isDeactivating()) {
                    _session->requestDeactivate();
                }
                if (!_session->pollDeactivate()) {
                    return;
                }
            }

            if (_sessionStartMs > 0) {
                _metrics.lastSessionDurationMs = (nowMs >= _sessionStartMs) ? (nowMs - _sessionStartMs) : 0;
                _metrics.totalSessionDurationMs += _metrics.lastSessionDurationMs;
                _sessionStartMs = 0;
            }
            if (_lastEndReason == WakeWordSessionEndReason::VOICE_COMMAND ||
                _lastEndReason == WakeWordSessionEndReason::SILENCE_TIMEOUT ||
                _lastEndReason == WakeWordSessionEndReason::MAX_DURATION_TIMEOUT ||
                _lastEndReason == WakeWordSessionEndReason::MANUAL_STOP ||
                _lastEndReason == WakeWordSessionEndReason::NONE) {
                _metrics.successfulSessionCount++;
            }

            pushEvent(WakeWordConversationEventType::CONVERSATION_ENDED, nowMs,
                      wakeWordSessionEndReasonToString(_lastEndReason));
            if (_onConversationEnded) {
                _onConversationEnded(_lastEndReason);
            }
            if (_onConversationEndedSimple) {
                _onConversationEndedSimple();
            }

            uint32_t postDeactivateMs = millis();
            if (postDeactivateMs < nowMs) {
                postDeactivateMs = nowMs;
            }
            _returnToWakeStartMs = postDeactivateMs;
            transitionTo(WakeWordConversationState::RETURNING_TO_WAKE, postDeactivateMs, "Preparation reactivation WakeNet");
            break;
        }

        case WakeWordConversationState::RETURNING_TO_WAKE: {
            // 1. Vérification que plus aucune capture, lecture ou session résiduelle n'est en cours
            if (_conversation && _conversation->isTurnActive()) {
                _conversation->abortTurnForSessionClose();
                return;
            }
            if (_player && _player->isPlaying() > 0) {
                return;
            }
            if (_streamer) {
                _streamer->checkQuarantinedTasks();
                if (_streamer->isStreaming() || _streamer->isTaskQuarantined() ||
                    HardwareManager::getInstance().isMicCaptureReserved()) {
                    if (_config.errorRecoveryDelayMs > 0 &&
                        (nowMs - _returnToWakeStartMs >= _config.errorRecoveryDelayMs)) {
                        enterRecoverableError("Microphone non disponible pour reactivation WakeNet",
                                              WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
                                              nowMs, true, false);
                    }
                    return;
                }
            }
            if (_config.disconnectAfterSession && _provider &&
                (_provider->isConnected() || _provider->isDisconnectPending())) {
                if (_session) {
                    if (!_session->isDeactivating()) {
                        _session->requestDeactivate();
                    }
                    _session->pollDeactivate();
                }
                return;
            }

            // 2. Garde temporelle non bloquante avant réactivation du microphone pour WakeNet
            if (nowMs - _returnToWakeStartMs < _config.returnToWakeDelayMs) {
                return;
            }

            // 3. Réactivation de WakeNet et retour à WAKE_ARMED
            if (_wakeWord) {
                _wakeWord->notifyVoiceActivity(false, false);
                bool ok = false;
                if (!_wakeWord->isEnabled() || _wakeWord->isError() ||
                    _wakeWord->getState() == WakeWordState::INACTIVE) {
                    ok = _wakeWord->enable(true);
                } else {
                    ok = _wakeWord->resume();
                }

                if (!ok || _wakeWord->isError() || _wakeWord->getState() != WakeWordState::ARMED) {
                    _lastSeenWakeStopFailures = _wakeWord->getMetrics().captureStopFailures;
                    String err = !_wakeWord->getLastError().isEmpty()
                        ? _wakeWord->getLastError()
                        : String("Echec reactivation de WakeNet");
                    enterRecoverableError(err, WakeWordSessionEndReason::MIC_TRANSITION_ERROR, nowMs, true, false);
                    return;
                }
                _lastSeenWakeStopFailures = _wakeWord->getMetrics().captureStopFailures;
            }

            _metrics.returnToArmedMs = (nowMs >= _returnToWakeStartMs) ? (nowMs - _returnToWakeStartMs) : 0;
            _wakeRequestPending.store(false, std::memory_order_release);

            transitionTo(WakeWordConversationState::WAKE_ARMED, nowMs, "WakeNet actif et arme");
            pushEvent(WakeWordConversationEventType::RETURNED_TO_WAKE, nowMs);
            if (_onReturnedToWake) {
                _onReturnedToWake();
            }
            break;
        }

        case WakeWordConversationState::RECOVERABLE_ERROR: {
            // Maintien dans un état sûr tant qu'une tâche audio précédente est en cours d'arrêt
            if (_conversation && _conversation->isTurnActive()) {
                _conversation->requestStop();
            }
            if (_streamer) {
                _streamer->checkQuarantinedTasks();
                if (_streamer->isTaskQuarantined() || _streamer->isStreaming()) {
                    return;
                }
                if (_streamer->isLocalCapture()) {
                    if (!_streamer->stopLocalCapture()) {
                        return;
                    }
                }
            }

            if (nowMs - _errorEnterMs >= _config.errorRecoveryDelayMs) {
                _metrics.recoveryCount++;
                _returnToWakeStartMs = nowMs;
                transitionTo(WakeWordConversationState::RETURNING_TO_WAKE, nowMs, "Recuperation vers ecoute Wake Word");
            }
            break;
        }
    }
}

void WakeWordConversationController::resetMetrics() {
    _metrics = WakeWordConversationMetrics();
    if (_session) {
        _session->resetTimingMetrics();
    }
    if (_streamer) {
        _streamer->resetMicTransitionMetrics();
    }
    HardwareManager::getInstance().resetMicReservationFailureCount();
    syncSubsystemMetrics();
    refreshMemoryMetrics();
}

void WakeWordConversationController::recordVoiceUpdateDurationUs(uint32_t durationUs) {
    _metrics.lastVoiceUpdateDurationUs = durationUs;
    _metrics.voiceUpdateCallCount++;
    _metrics.totalVoiceUpdateDurationUs += durationUs;
    _metrics.avgVoiceUpdateDurationUs = static_cast<uint32_t>(
        _metrics.totalVoiceUpdateDurationUs / _metrics.voiceUpdateCallCount);
    if (durationUs > _metrics.maxVoiceUpdateDurationUs) {
        _metrics.maxVoiceUpdateDurationUs = durationUs;
    }
    syncSubsystemMetrics();
}

void WakeWordConversationController::syncSubsystemMetrics() {
    if (_wakeWord) {
        const WakeWordMetrics& wm = _wakeWord->getMetrics();
        _metrics.lastWakeWordUpdateDurationUs = wm.lastUpdateTotalDurationUs;
        if (wm.maxUpdateTotalDurationUs > _metrics.maxWakeWordUpdateDurationUs) {
            _metrics.maxWakeWordUpdateDurationUs = wm.maxUpdateTotalDurationUs;
        }
        _metrics.lastWakeReactivationDurationUs = wm.lastReactivationDurationUs;
        if (wm.maxReactivationDurationUs > _metrics.maxWakeReactivationDurationUs) {
            _metrics.maxWakeReactivationDurationUs = wm.maxReactivationDurationUs;
        }
        _metrics.wakeQueueSaturations = wm.queueSaturations;
        _metrics.wakeFramesDropped = wm.framesDropped;
    }
    if (_session) {
        _metrics.lastSessionTickDurationUs = _session->getLastTickDurationUs();
        if (_session->getMaxTickDurationUs() > _metrics.maxSessionTickDurationUs) {
            _metrics.maxSessionTickDurationUs = _session->getMaxTickDurationUs();
        }
        _metrics.lastSessionStopDurationUs = _session->getLastSessionStopDurationUs();
        if (_session->getMaxSessionStopDurationUs() > _metrics.maxSessionStopDurationUs) {
            _metrics.maxSessionStopDurationUs = _session->getMaxSessionStopDurationUs();
        }
    }
    if (_provider) {
        _metrics.lastWsProcessDurationUs = _provider->getLastWsProcessDurationUs();
        if (_provider->getMaxWsProcessDurationUs() > _metrics.maxWsProcessDurationUs) {
            _metrics.maxWsProcessDurationUs = _provider->getMaxWsProcessDurationUs();
        }
    }
    if (_streamer) {
        _metrics.lastMicTransitionDurationUs = _streamer->getLastMicTransitionDurationUs();
        if (_streamer->getMaxMicTransitionDurationUs() > _metrics.maxMicTransitionDurationUs) {
            _metrics.maxMicTransitionDurationUs = _streamer->getMaxMicTransitionDurationUs();
        }
    }
    _metrics.micReservationFailures = HardwareManager::getInstance().getMicReservationFailureCount();
}

void WakeWordConversationController::refreshMemoryMetrics() {
#if defined(ESP32) || defined(ARDUINO)
    _metrics.freeInternalHeap = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    _metrics.minFreeInternalHeap = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    _metrics.freePsram = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    _metrics.minFreePsram = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
#else
    _metrics.freeInternalHeap = 200000;
    _metrics.minFreeInternalHeap = 180000;
    _metrics.freePsram = 7500000;
    _metrics.minFreePsram = 7200000;
#endif
}

void WakeWordConversationController::printDiagnostics() const {
    // Règle Section 4 & 9 : Ne jamais afficher les clés API ou mots de passe Wi-Fi dans les diagnostics.
    Serial.println("================================================================================");
    Serial.println("  [DIAGNOSTICS] WakeWordConversationController (VoiceKit v0.6.3.1)              ");
    Serial.println("--------------------------------------------------------------------------------");
    Serial.printf("  - Etat controleur          : %s (%s | UI: %s)\n",
                  wakeWordConversationStateToString(_state), getDisplayStatusText(), getUiTransitionBannerText());
    Serial.printf("  - Etat WakeWordManager     : %s | Moteur : %s (%s) | Modele : %s\n",
                  _wakeWord ? wakeWordStateToString(_wakeWord->getState()) : "N/A",
                  _wakeWord ? _wakeWord->getEngineName() : "N/A",
                  (_wakeWord && _wakeWord->isSimulated()) ? "SIMULE" : "ACOUSTIQUE REEL",
                  _wakeWord ? _wakeWord->getResolvedModelName() : "N/A");
    Serial.printf("  - Fournisseur vocal        : %s\n",
                  (_providerType == VoiceProviderType::OPENAI) ? "OpenAI Realtime" : "Gemini Live");
    Serial.printf("  - Sessions / Detections    : %u ouvertes (%u reussies) | %u detections (%u reelles, %u simulees, %u ignorees)\n",
                  (unsigned)_metrics.sessionCount,
                  (unsigned)_metrics.successfulSessionCount,
                  (unsigned)_metrics.detectionCount,
                  (unsigned)_metrics.realDetectionCount,
                  (unsigned)_metrics.simulatedDetectionCount,
                  (unsigned)_metrics.ignoredConcurrentDetections);
    Serial.printf("  - Tours / Reconnexions     : %u tours | %u reconnexions | %u erreurs | %u recuperations\n",
                  (unsigned)_metrics.turnCount,
                  (unsigned)_metrics.reconnectCount,
                  (unsigned)_metrics.errorCount,
                  (unsigned)_metrics.recoveryCount);
    Serial.printf("  - Latences de transition   : Wake->Connect=%u ms | Wake->Ready=%u ms | WS=%u ms | Retour ARMED=%u ms\n",
                  (unsigned)_metrics.wakeToConnectStartMs,
                  (unsigned)_metrics.wakeToConversationReadyMs,
                  (unsigned)_metrics.wsConnectMs,
                  (unsigned)_metrics.returnToArmedMs);
    Serial.printf("  - Duree de session         : Derniere=%u ms | Cumulee=%u ms | Fin=%s\n",
                  (unsigned)_metrics.lastSessionDurationMs,
                  (unsigned)_metrics.totalSessionDurationMs,
                  wakeWordSessionEndReasonToString(_lastEndReason));
    Serial.printf("  - Echecs Micro / Reseau    : Transition=%u | Reservation=%u | Reseau=%u\n",
                  (unsigned)_metrics.micTransitionFailures,
                  (unsigned)_metrics.micReservationFailures,
                  (unsigned)_metrics.networkFailures);
    Serial.printf("  - Trames Wake Word         : Saturations=%u | Pertes=%u\n",
                  (unsigned)_metrics.wakeQueueSaturations,
                  (unsigned)_metrics.wakeFramesDropped);
    Serial.printf("  - Temps voice.update()     : moy=%u us | max=%u us | dernier=%u us (%u appels)\n",
                  (unsigned)_metrics.avgVoiceUpdateDurationUs,
                  (unsigned)_metrics.maxVoiceUpdateDurationUs,
                  (unsigned)_metrics.lastVoiceUpdateDurationUs,
                  (unsigned)_metrics.voiceUpdateCallCount);
    Serial.printf("  - Temps sous-systemes max  : Ctrl=%u us | Transition=%u us | Wake=%u us | Session=%u us\n",
                  (unsigned)_metrics.maxControllerUpdateDurationUs,
                  (unsigned)_metrics.maxStateTransitionDurationUs,
                  (unsigned)_metrics.maxWakeWordUpdateDurationUs,
                  (unsigned)_metrics.maxSessionTickDurationUs);
    Serial.printf("  - Temps I/O max            : WS=%u us | Mic=%u us | StopSess=%u us | ReactWake=%u us\n",
                  (unsigned)_metrics.maxWsProcessDurationUs,
                  (unsigned)_metrics.maxMicTransitionDurationUs,
                  (unsigned)_metrics.maxSessionStopDurationUs,
                  (unsigned)_metrics.maxWakeReactivationDurationUs);
    Serial.printf("  - Memoire Interne / PSRAM  : Int=%u oct (min %u) | PSRAM=%u oct (min %u)\n",
                  (unsigned)_metrics.freeInternalHeap,
                  (unsigned)_metrics.minFreeInternalHeap,
                  (unsigned)_metrics.freePsram,
                  (unsigned)_metrics.minFreePsram);
    Serial.println("================================================================================");
}
