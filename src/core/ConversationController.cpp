#include "core/ConversationController.h"

ConversationController::ConversationController() {
    _state = VoiceState::READY;
    _isTurnActive = false;
}

ConversationController::~ConversationController() {
    requestStop();
}

bool ConversationController::begin(GeminiLiveClient* client, LiveSessionController* session,
                                     LiveAudioStreamer* streamer, LiveAudioPlayer* player,
                                     const VoiceKitConfig& config) {
    if (!client || !session || !streamer || !player) {
        return false;
    }
    _client = client;
    _session = session;
    _streamer = streamer;
    _player = player;
    _config = config;
    _state = VoiceState::READY;
    _isTurnActive = false;
    return true;
}

void ConversationController::setState(VoiceState newState) {
    if (_state != newState) {
        VoiceState oldState = _state;
        _state = newState;
        Serial.printf("[Conversation] Transition : %s -> %s\n",
                      voiceStateToString(oldState), voiceStateToString(newState));
        if (_onStateChanged) {
            _onStateChanged(_state);
        }
    }
}

bool ConversationController::startTurn() {
    if (!_client || !_client->isConnected() || !_client->isSetupComplete()) {
        Serial.println("[Conversation] Refus startTurn : session Gemini Live non prete");
        if (_onError) _onError("Session Gemini Live non prete");
        return false;
    }

    if (_isTurnActive) {
        Serial.println("[Conversation] Refus startTurn : tour deja actif");
        return false;
    }

    if (_player && _player->isPlaying()) {
        Serial.println("[Conversation] Refus startTurn : haut-parleur en cours de restitution");
        return false;
    }

    // Réinitialisation du tour
    _isTurnActive = true;
    _turnStartTimeMs = millis();
    _lastUserText = "";
    _lastAssistantText = "";
    _metrics = ConversationMetrics();
    _metrics.turnId = _session ? _session->getCurrentTurnId() + 1 : 1;

    // Protection anti-larsen : désactivation du haut-parleur pendant la capture micro
    if (_player) {
        _player->setPlaybackAllowed(false);
    }

    // Activation du tour dans le contrôleur de session
    if (!_session->beginAudioTurn()) {
        Serial.println("[Conversation] ERREUR beginAudioTurn()");
        _isTurnActive = false;
        if (_player) _player->setPlaybackAllowed(true);
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError("Echec initialisation tour session");
        return false;
    }

    // Démarrage de la capture et du streaming audio PCM 16 kHz
    if (!_streamer->start(_client)) {
        Serial.printf("[Conversation] ERREUR streamer start : %s\n", _streamer->getLastError());
        _isTurnActive = false;
        if (_player) _player->setPlaybackAllowed(true);
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_streamer->getLastError());
        return false;
    }

    setState(VoiceState::LISTENING);
    Serial.printf("[Conversation] Tour #%llu demarre (ecoute micro active)\n", (unsigned long long)_metrics.turnId);
    return true;
}

bool ConversationController::finishTurnAudio() {
    if (!_isTurnActive || _state != VoiceState::LISTENING) {
        return false;
    }

    _micStopMs = millis();
    _metrics.micCaptureDurationMs = _micStopMs - _turnStartTimeMs;
    _metrics.audioBlocksSent = _streamer ? _streamer->getChunksSent() : 0;

    Serial.printf("[Conversation] Fin parole : %u ms de capture, %u blocs audio emis\n",
                  (unsigned)_metrics.micCaptureDurationMs, (unsigned)_metrics.audioBlocksSent);

    // Arrêt de la capture micro et envoi de activityEnd
    if (_streamer) {
        _streamer->stop();
    }

    // Signalisation au contrôleur de session qu'on attend la réponse Gemini
    if (_session) {
        _session->finishAudioCapture();
    }

    // Le micro est libéré : réautorisation immédiate du haut-parleur pour la réponse
    if (_player) {
        _player->setPlaybackAllowed(true);
    }

    setState(VoiceState::WAITING_RESPONSE);
    return true;
}

void ConversationController::requestStop() {
    Serial.println("[Conversation] Interruption STOP demandee");
    if (_streamer && _streamer->isStreaming()) {
        _streamer->cancel();
    }
    if (_session) {
        _session->requestStop();
    }
    if (_player) {
        _player->stop(AudioStopReason::USER_STOP);
        _player->setPlaybackAllowed(true);
    }
    _isTurnActive = false;
    setState(VoiceState::STOPPED);
    setState(VoiceState::READY);
}

void ConversationController::tick() {
    if (!_isTurnActive) return;

    // Détection de transition vers la restitution vocale
    if (_state == VoiceState::WAITING_RESPONSE) {
        if (_session && _session->getState() == LiveSessionState::PLAYING) {
            _metrics.firstAudioLatencyMs = _session->getFirstAudioLatencyMs();
            Serial.printf("[Conversation] Reponse audio recue (latence premier son : %u ms)\n",
                          (unsigned)_metrics.firstAudioLatencyMs);
            setState(VoiceState::SPEAKING);
        }
    }

    // Détection de fin de restitution si non gérée par événement
    if (_state == VoiceState::SPEAKING) {
        if (_session && _session->getState() == LiveSessionState::CONNECTED && _player && !_player->isPlaying()) {
            handleTurnFinished(_session->getCurrentTurnId());
        }
    }
}

void ConversationController::handleUserTranscription(const String& text, bool isFinal) {
    _lastUserText = text;
    _metrics.userText = text;
    if (_onUserText) {
        _onUserText(text, isFinal);
    }
}

void ConversationController::handleAssistantTranscription(const String& text) {
    _lastAssistantText = text;
    _metrics.assistantText = text;
    if (_onAssistantText) {
        _onAssistantText(text);
    }
}

void ConversationController::handleTurnFinished(uint64_t turnId) {
    if (!_isTurnActive) return;

    _metrics.turnId = turnId;
    if (_session) {
        _metrics.pcmBlocksReceived = _session->getChunksReceived();
        _metrics.droppedSamples = _session->getDroppedSamplesCount();
    }
    _metrics.totalTurnDurationMs = millis() - _turnStartTimeMs;

    Serial.println("================================================================================");
    Serial.printf("[Conversation] Tour #%llu TERMINE en %u ms | Blocs TX: %u | Blocs RX: %u | Pertes: %u\n",
                  (unsigned long long)_metrics.turnId, (unsigned)_metrics.totalTurnDurationMs,
                  (unsigned)_metrics.audioBlocksSent, (unsigned)_metrics.pcmBlocksReceived,
                  (unsigned)_metrics.droppedSamples);
    if (!_metrics.userText.isEmpty()) {
        Serial.printf("[Conversation] Utilisateur : \"%s\"\n", _metrics.userText.c_str());
    }
    if (!_metrics.assistantText.isEmpty()) {
        Serial.printf("[Conversation] Gemini      : \"%s\"\n", _metrics.assistantText.c_str());
    }
    Serial.println("================================================================================");

    _isTurnActive = false;
    setState(VoiceState::READY);

    if (_onConversationFinished) {
        _onConversationFinished();
    }
    if (_onTurnMetrics) {
        _onTurnMetrics(_metrics);
    }
}
