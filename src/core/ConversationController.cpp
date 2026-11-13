#include "core/ConversationController.h"
#include "core/GeminiVoiceProvider.h"

ConversationController::ConversationController() {
    _state = VoiceState::READY;
    _isTurnActive = false;
}

ConversationController::~ConversationController() {
    _onStateChanged = nullptr;
    _onError = nullptr;
    _onUserText = nullptr;
    _onAssistantText = nullptr;
    _onConversationFinished = nullptr;
    _onTurnMetrics = nullptr;
    requestStop();
}

bool ConversationController::begin(IVoiceProvider* provider, LiveSessionController* session,
                                   LiveAudioStreamer* streamer, LiveAudioPlayer* player,
                                   const VoiceKitConfig& config) {
    if (!provider || !session || !streamer || !player) {
        return false;
    }
    _provider = provider;
    _session = session;
    _streamer = streamer;
    _player = player;
    _config = config;
    _state = VoiceState::READY;
    _isTurnActive = false;
    return true;
}

bool ConversationController::begin(GeminiLiveClient* client, LiveSessionController* session,
                                   LiveAudioStreamer* streamer, LiveAudioPlayer* player,
                                   const VoiceKitConfig& config) {
    _legacyClient = client;
    if (!_legacyAdapter) {
        _legacyAdapter = std::unique_ptr<IVoiceProvider>(new GeminiVoiceProvider(client));
    }
    return begin(_legacyAdapter.get(), session, streamer, player, config);
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
    if (!_provider || !_provider->isConnected() || !_provider->isSetupComplete()) {
        Serial.println("[Conversation] Refus startTurn : session vocale non prete");
        if (_onError) _onError("Session vocale non prete");
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
    if (_turnStartTimeMs == 0) _turnStartTimeMs = 1;
    _lastUserText = "";
    _lastAssistantText = "";
    uint32_t prevSpeechEnd = _metrics.latency.speechEndDetectedMs;
    _metrics = ConversationMetrics();
    _metrics.turnId = _session ? _session->getCurrentTurnId() + 1 : 1;
    _metrics.latency.reset();
    _metrics.latency.turnStartedMs = _turnStartTimeMs;
    _metrics.latency.speechEndDetectedMs = prevSpeechEnd;

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
    if (!_streamer->start(_provider)) {
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
    if (_micStopMs == 0) _micStopMs = 1;
    _metrics.micCaptureDurationMs = _micStopMs - _turnStartTimeMs;
    _metrics.latency.micStoppedMs = _micStopMs;
    _metrics.audioBlocksSent = _streamer ? _streamer->getChunksSent() : 0;

    Serial.printf("[Conversation] Fin parole : %u ms de capture, %u blocs audio emis\n",
                  (unsigned)_metrics.micCaptureDurationMs, (unsigned)_metrics.audioBlocksSent);

    // Arrêt de la capture micro et envoi de activityEnd
    if (_streamer) {
        _streamer->stop();
        if (_streamer->getActivityEndSentMs() > 0) {
            _metrics.latency.activityEndSentMs = _streamer->getActivityEndSentMs();
        }
    }
    if (_metrics.latency.activityEndSentMs == 0 && _provider && _provider->getActivityEndSentMs() > 0) {
        _metrics.latency.activityEndSentMs = _provider->getActivityEndSentMs();
    }

    // Signalisation au contrôleur de session qu'on attend la réponse
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

void ConversationController::abortTurnForSessionClose() {
    if (_streamer && _streamer->isStreaming()) {
        _streamer->cancel();
    }
    if (_session) {
        _session->abortTurnLocally();
    }
    if (_player) {
        if (_player->isPlaying() > 0 || _player->getBufferedSamples() > 0) {
            _player->stop(AudioStopReason::USER_STOP);
        }
        _player->setPlaybackAllowed(true);
    }
    _isTurnActive = false;
    setState(VoiceState::STOPPED);
    setState(VoiceState::READY);
}

void ConversationController::tick() {
    if (!_isTurnActive) return;

    // Detection de transition vers la restitution vocale
    if (_state == VoiceState::WAITING_RESPONSE) {
        if (_session && _session->getState() == LiveSessionState::PLAYING) {
            _metrics.firstAudioLatencyMs = _session->getFirstAudioLatencyMs();
            if (_provider) {
                _metrics.latency.firstResponseReceivedMs = _provider->getFirstResponseReceivedMs();
                _metrics.latency.firstAudioReceivedMs = _provider->getFirstAudioReceivedMs();
            }
            if (_player) {
                _metrics.latency.firstAudioPlayedMs = _player->getFirstPlayRawAcceptedTimeMs();
            }
            Serial.printf("[Conversation] Reponse audio recue (latence premier son : %u ms)\n",
                          (unsigned)_metrics.firstAudioLatencyMs);
            setState(VoiceState::SPEAKING);
        } else if (_session && _session->getState() == LiveSessionState::CONNECTED && _player && !_player->isPlaying()) {
            Serial.println("[Conversation] Fin de tour detectee sans restitution audio (transition directe CONNECTED)");
            handleTurnFinished(_session->getCurrentTurnId());
            return;
        }
    }

    // Detection de fin de restitution si non geree par evenement
    if (_state == VoiceState::SPEAKING) {
        if (_session && _session->getState() == LiveSessionState::CONNECTED && _player && !_player->isPlaying()) {
            handleTurnFinished(_session->getCurrentTurnId());
            return;
        }
    }

    // Timeout de securite non bloquant si le tour vocal est anormalement persistant (> 20 s)
    if (_turnStartTimeMs > 0 && (millis() - _turnStartTimeMs >= 20000)) {
        Serial.println("[Conversation] Timeout de securite du tour vocal (20 s) -> reinitialisation propre");
        requestStop();
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
    handleAssistantTranscription(text, false);
}

void ConversationController::handleAssistantTranscription(const String& text, bool isFinal) {
    (void)isFinal;
    if (text.isEmpty()) return;
    _metrics.assistantText = text;
    _lastAssistantText = _metrics.assistantText;
    if (_onAssistantText) {
        _onAssistantText(_metrics.assistantText);
    }
}

void ConversationController::handleTurnFinished(uint64_t turnId) {
    if (!_isTurnActive) return;

    _metrics.turnId = turnId;
    if (_session) {
        _metrics.pcmBlocksReceived = _session->getChunksReceived();
        _metrics.droppedSamples = _session->getDroppedSamplesCount();
        if (!_session->getLastAssistantText().isEmpty()) {
            if (_metrics.assistantText.isEmpty() || _session->getLastAssistantText().length() > _metrics.assistantText.length()) {
                _metrics.assistantText = _session->getLastAssistantText();
                _lastAssistantText = _metrics.assistantText;
            }
        }
    }
    _metrics.totalTurnDurationMs = millis() - _turnStartTimeMs;

    if (_streamer && _metrics.latency.activityEndSentMs == 0) {
        _metrics.latency.activityEndSentMs = _streamer->getActivityEndSentMs();
    }
    if (_provider) {
        if (_metrics.latency.activityEndSentMs == 0) {
            _metrics.latency.activityEndSentMs = _provider->getActivityEndSentMs();
        }
        if (_metrics.latency.firstResponseReceivedMs == 0) {
            _metrics.latency.firstResponseReceivedMs = _provider->getFirstResponseReceivedMs();
        }
        if (_metrics.latency.firstAudioReceivedMs == 0) {
            _metrics.latency.firstAudioReceivedMs = _provider->getFirstAudioReceivedMs();
        }
    }
    if (_player) {
        if (_metrics.latency.firstAudioPlayedMs == 0) {
            _metrics.latency.firstAudioPlayedMs = _player->getFirstPlayRawAcceptedTimeMs();
        }
        _metrics.latency.playbackFinishedMs = _player->getPlaybackFinishedTimeMs();
    }
    if (_metrics.latency.playbackFinishedMs == 0) {
        _metrics.latency.playbackFinishedMs = millis();
        if (_metrics.latency.playbackFinishedMs == 0) _metrics.latency.playbackFinishedMs = 1;
    }

    Serial.println("================================================================================");
    Serial.printf("[Conversation] Tour #%llu TERMINE en %u ms | Blocs TX: %u | Blocs RX: %u | Pertes: %u\n",
                  (unsigned long long)_metrics.turnId, (unsigned)_metrics.totalTurnDurationMs,
                  (unsigned)_metrics.audioBlocksSent, (unsigned)_metrics.pcmBlocksReceived,
                  (unsigned)_metrics.droppedSamples);
    if (!_metrics.userText.isEmpty()) {
        Serial.printf("[Conversation] Utilisateur : \"%s\"\n", _metrics.userText.c_str());
    }
    if (!_metrics.assistantText.isEmpty()) {
        const char* providerName = (_config.provider == VoiceProviderType::OPENAI) ? "OpenAI" : "Gemini";
        Serial.printf("[Conversation] %-10s : \"%s\"\n", providerName, _metrics.assistantText.c_str());
    }
    Serial.println("--------------------------------------------------------------------------------");
    Serial.println("  [LATENCY] Metriques de latence du tour vocal (v0.5.3 / v0.6.0) :               ");
    if (_metrics.latency.hasSpeechEndDetected()) {
        Serial.printf("  - Fin de parole (VAD) -> Fin de capture micro : %d ms\n", _metrics.latency.getSpeechEndToMicStoppedMs());
    }
    Serial.printf("  - Fin de capture micro -> Envoi activityEnd    : %d ms\n", _metrics.latency.getMicStoppedToActivityEndMs());
    Serial.printf("  - Envoi activityEnd -> 1er audio IA            : %d ms\n", _metrics.latency.getActivityEndToFirstAudioMs());
    Serial.printf("  - 1er audio recu -> Debut restitution DMA      : %d ms\n", _metrics.latency.getFirstAudioToPlaybackMs());
    Serial.printf("  - Latence de reponse vocale percue             : %d ms\n", _metrics.latency.getResponseLatencyMs());
    Serial.printf("  - Duree totale du tour vocal                   : %d ms\n", _metrics.latency.getTotalTurnDurationMs());
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
