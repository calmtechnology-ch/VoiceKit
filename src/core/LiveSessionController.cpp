#include "core/LiveSessionController.h"
#include "core/AudioTraceLog.h"
#include "DisplayTextNormalizer.h"
#include <cstring>
#include <cctype>

const char* audioPacketRejectReasonToString(AudioPacketRejectReason reason) {
    switch (reason) {
        case AudioPacketRejectReason::NONE:               return "AUCUN";
        case AudioPacketRejectReason::TURN_INACTIVE:      return "TOUR_INACTIF";
        case AudioPacketRejectReason::TURN_CANCELLED:     return "TOUR_ANNULE";
        case AudioPacketRejectReason::STATE_CONNECTED:    return "SESSION_CONNECTED";
        case AudioPacketRejectReason::STATE_DISCONNECTED: return "SESSION_DISCONNECTED";
        case AudioPacketRejectReason::STATE_ERROR:        return "SESSION_ERROR";
        case AudioPacketRejectReason::STATE_INTERRUPTING: return "SESSION_INTERRUPTING";
        case AudioPacketRejectReason::STATE_UNEXPECTED:   return "ETAT_INATTENDU";
        case AudioPacketRejectReason::BUFFER_SATURATED:   return "TAMPON_SATURE";
        default:                                          return "INCONNU";
    }
}

LiveSessionController::LiveSessionController() {
    _lastError[0] = '\0';
}

LiveSessionController::~LiveSessionController() {
    if (_client) {
        _client->setOnConnected(nullptr);
        _client->setOnDisconnected(nullptr);
        _client->setOnError(nullptr);
        _client->setOnAudioData(nullptr);
        _client->setOnOutputTranscription(nullptr);
        _client->setOnText(nullptr);
        _client->setOnInputTranscription(nullptr);
        _client->setOnTurnComplete(nullptr);
        _client->setOnInterrupted(nullptr);
        _client->setOnSetupComplete(nullptr);
    }
}

void LiveSessionController::appendTranscriptionFragment(String& target, const String& fragment, size_t maxLength) {
    if (fragment.isEmpty()) return;

    if (target.isEmpty()) {
        size_t start = 0;
        while (start < fragment.length() && fragment[start] == ' ') start++;
        target = fragment.substring(start);
        if (target.length() > maxLength) {
            target = target.substring(0, maxLength);
        }
        return;
    }

    if (fragment.startsWith(target)) {
        target = fragment;
        if (target.length() > maxLength) {
            target = target.substring(0, maxLength);
        }
        return;
    }

    if (target == fragment || target.endsWith(fragment)) {
        return;
    }

    size_t targetLen = target.length();
    size_t fragLen = fragment.length();
    size_t maxOverlap = (targetLen < fragLen) ? targetLen : fragLen;
    size_t bestOverlap = 0;

    for (size_t k = 1; k <= maxOverlap; ++k) {
        String suffix = target.substring(targetLen - k);
        String prefix = fragment.substring(0, k);
        if (suffix == prefix) {
            bestOverlap = k;
        }
    }

    if (bestOverlap > 0) {
        String toAppend = fragment.substring(bestOverlap);
        target += toAppend;
    } else {
        char lastChar = target[targetLen - 1];
        char firstChar = fragment[0];
        bool needSpace = (lastChar != ' ' && firstChar != ' ' && 
                          firstChar != '.' && firstChar != ',' && 
                          firstChar != '!' && firstChar != '?' && 
                          firstChar != ':' && firstChar != ';');
        if (needSpace) {
            target += ' ';
        }
        target += fragment;
    }

    if (target.length() > maxLength) {
        target = target.substring(0, maxLength);
    }
}

void LiveSessionController::finishTurn() {
    if (!_turnCompletedLogged && _turnActive.load(std::memory_order_acquire) && !_turnCancelled.load(std::memory_order_acquire)) {
        _turnCompletedLogged = true;

        if (!_lastAssistantText.isEmpty()) {
            Serial.printf("[GEMINI] %s\n", _lastAssistantText.c_str());
        }

        uint32_t pcmDecoded = _client ? _client->getPcmSamplesDispatchedTotal() : 0;
        uint32_t pcmRejectedBefore = _audioSamplesRejectedBeforePlayerTotal;
        uint32_t pcmPresented = _audioSamplesPresentedToPlayerTotal;
        uint32_t pcmAccepted = _player ? _player->getSamplesAcceptedBuffer() : 0;
        uint32_t pcmPlayed = _player ? _player->getSamplesPlayedTotal() : 0;
        uint32_t pcmDroppedOverflow = _player ? _player->getOverflowDroppedSamples() : 0;
        uint32_t pcmLost = pcmRejectedBefore + pcmDroppedOverflow;

        Serial.println("--------------------------------------------------------------------------------");
        Serial.printf("[STATUS] Tour #%llu termine : %u ech decodes, %u ech joues (%u rejet, %u perte)\n",
                      (unsigned long long)_currentTurnId, (unsigned)pcmDecoded, (unsigned)pcmPlayed,
                      (unsigned)pcmRejectedBefore, (unsigned)pcmLost);
        Serial.println("--------------------------------------------------------------------------------");

        _turnActive.store(false, std::memory_order_release);

        if (_onTurnFinished) {
            _onTurnFinished(_currentTurnId);
        }
    }
}

void LiveSessionController::printAudioEndCheck(const char* label) {
    uint32_t pcmDecoded = _client ? _client->getPcmSamplesDispatchedTotal() : 0;
    uint32_t pcmPlayed = _player ? _player->getSamplesPlayedTotal() : 0;
    Serial.printf("[AUDIO END CHECK][%s] Decodes: %u | Joues: %u\n", label ? label : "INFO", (unsigned)pcmDecoded, (unsigned)pcmPlayed);
}

bool LiveSessionController::begin(GeminiLiveClient* client, void*, LiveAudioPlayer* player) {
    if (!client || !player) return false;
    _client = client;
    _player = player;

    _client->setOnSetupComplete([this]() {
        _lastActivityMs = millis();
        _reconnectAttempts = 0;
        Serial.println("[LiveSession] Setup termine : session Live prete");
        transitionTo(LiveSessionState::CONNECTED, "Setup confirme");
    });

    _client->setOnAudioData([this](const int16_t* pcm, size_t samples) {
        _audioPacketsReceivedCount++;
        _lastAudioPacketTimeMs = millis();
        _lastActivityMs = _lastAudioPacketTimeMs;

        if (_turnCancelled.load(std::memory_order_acquire)) {
            _audioPacketsRejectedBeforePlayerCount++;
            _audioSamplesRejectedBeforePlayerTotal += samples;
            _audioSamplesRejectedTurnCancelled += samples;
            _audioPacketsRejectedCount++;
            _audioSamplesRejectedTotal += samples;
            _lastRejectReason = AudioPacketRejectReason::TURN_CANCELLED;
            AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED, (uint32_t)samples, (uint32_t)_lastRejectReason);
            return;
        }

        if (!_turnActive.load(std::memory_order_acquire)) {
            _audioPacketsRejectedBeforePlayerCount++;
            _audioSamplesRejectedBeforePlayerTotal += samples;
            _audioSamplesRejectedTurnInactive += samples;
            _audioPacketsRejectedCount++;
            _audioSamplesRejectedTotal += samples;
            _lastRejectReason = AudioPacketRejectReason::TURN_INACTIVE;
            AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED, (uint32_t)samples, (uint32_t)_lastRejectReason);
            return;
        }

        if (_state == LiveSessionState::INTERRUPTING) {
            _audioPacketsRejectedBeforePlayerCount++;
            _audioSamplesRejectedBeforePlayerTotal += samples;
            _audioSamplesRejectedTurnCancelled += samples;
            _audioPacketsRejectedCount++;
            _audioSamplesRejectedTotal += samples;
            _lastRejectReason = AudioPacketRejectReason::STATE_INTERRUPTING;
            AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED, (uint32_t)samples, (uint32_t)_lastRejectReason);
            return;
        }

        if (_state == LiveSessionState::DISCONNECTED || _state == LiveSessionState::ERROR) {
            _audioPacketsRejectedBeforePlayerCount++;
            _audioSamplesRejectedBeforePlayerTotal += samples;
            _audioSamplesRejectedStateUnexpected += samples;
            _audioPacketsRejectedCount++;
            _audioSamplesRejectedTotal += samples;
            _lastRejectReason = (_state == LiveSessionState::DISCONNECTED) ? AudioPacketRejectReason::STATE_DISCONNECTED : AudioPacketRejectReason::STATE_ERROR;
            AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED, (uint32_t)samples, (uint32_t)_lastRejectReason);
            return;
        }

        _hasAudioResponse = true;

        if (!_audioStartedForTurn) {
            _audioStartedForTurn = true;
            Serial.println("[AUDIO] Lecture PCM 24 kHz");
            if (_onFirstAudio) {
                _onFirstAudio();
            }
        }

        if (_state == LiveSessionState::WAITING_RESPONSE) {
            _firstAudioLatencyMs = millis() - _waitingStartTimeMs;
            Serial.printf("[LiveSession] Premier bloc audio Gemini Live recu en %u ms !\n", _firstAudioLatencyMs);
            transitionTo(LiveSessionState::PLAYING, "Premier son recu");
        }

        if (_state == LiveSessionState::PLAYING) {
            _audioPacketsPresentedToPlayerCount++;
            _audioSamplesPresentedToPlayerTotal += samples;

            bool accepted = _player->feedPcm(pcm, samples);
            if (accepted) {
                _audioPacketsDispatchedCount++;
            } else {
                _audioPacketsWithSaturationCount++;
                _lastRejectReason = AudioPacketRejectReason::BUFFER_SATURATED;
                _hasBufferOverflowAnomaly = true;
            }
            if (_player->hasHardwareError()) {
                _hasPlaybackHardwareError = true;
            }
        } else {
            _audioPacketsRejectedBeforePlayerCount++;
            _audioSamplesRejectedBeforePlayerTotal += samples;
            _audioSamplesRejectedStateUnexpected += samples;
            _audioPacketsRejectedCount++;
            _audioSamplesRejectedTotal += samples;
            _lastRejectReason = AudioPacketRejectReason::STATE_UNEXPECTED;
            AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED, (uint32_t)samples, (uint32_t)_lastRejectReason);
        }
    });

    _client->setOnOutputTranscription([this](const String& text) {
        if (_turnCancelled.load(std::memory_order_acquire)) return;
        _lastActivityMs = millis();
        _hasOutputTranscription = true;
        appendTranscriptionFragment(_lastAssistantText, text);
        if (_onOutputTranscription) {
            _onOutputTranscription(text);
        }
    });

    _client->setOnText([this](const String& text) {
        if (_turnCancelled.load(std::memory_order_acquire)) return;
        _lastActivityMs = millis();
        _hasModelTurnText = true;
        if (!_hasOutputTranscription) {
            appendTranscriptionFragment(_lastAssistantText, text);
            if (_onOutputTranscription) {
                _onOutputTranscription(text);
            }
        }
    });

    _client->setOnTurnComplete([this]() {
        if (!_turnActive.load(std::memory_order_acquire) || _turnCancelled.load(std::memory_order_acquire)) return;
        _lastActivityMs = millis();
        _turnCompleteReceived = true;
        _turnCompleteTimeMs = millis();
        if (_state == LiveSessionState::PLAYING || _state == LiveSessionState::WAITING_RESPONSE) {
            _player->notifyTurnComplete();
        }
    });

    _client->setOnInterrupted([this]() {
        Serial.println("[LiveSession] Interruption confirmee par le serveur");
        _turnCancelled.store(true, std::memory_order_release);
        _turnActive.store(false, std::memory_order_release);
        if (_state == LiveSessionState::PLAYING || _state == LiveSessionState::WAITING_RESPONSE) {
            _player->stop(AudioStopReason::GEMINI_INTERRUPT);
            printAudioEndCheck("INTERROMPU");
            transitionTo(LiveSessionState::CONNECTED, "Interrompu par serveur");
        }
    });

    _client->setOnDisconnected([this]() {
        _disconnectCount++;
        Serial.printf("[LiveSession] Deconnexion WebSocket Gemini Live (deconnexion #%u)\n", (unsigned)_disconnectCount);
        if (_state != LiveSessionState::DISCONNECTED && _state != LiveSessionState::ERROR) {
            if (_turnActive.load(std::memory_order_acquire)) {
                _turnCancelled.store(true, std::memory_order_release);
                _turnActive.store(false, std::memory_order_release);
                if (_player) _player->stop(AudioStopReason::HARDWARE_ERROR);
                printAudioEndCheck("DECONNECTE");
            }
            _reconnectAttempts = 0;
            _lastReconnectAttemptMs = millis();
            transitionTo(LiveSessionState::RECONNECTING, "Deconnexion reseau");
        }
    });

    _client->setOnError([this](const String& err) {
        strncpy(_lastError, err.c_str(), sizeof(_lastError) - 1);
        _lastError[sizeof(_lastError) - 1] = '\0';
        Serial.printf("[LiveSession] Erreur client : %s\n", _lastError);
        if (_state == LiveSessionState::RECONNECTING || _state == LiveSessionState::CONNECTING) {
            if (_reconnectAttempts > 0 && _reconnectAttempts < LIVE_MAX_RECONNECT_ATTEMPTS) {
                _lastReconnectAttemptMs = millis();
                transitionTo(LiveSessionState::RECONNECTING, "Erreur tentative - retry");
                if (_onError) _onError(err);
                return;
            }
        }
        transitionTo(LiveSessionState::ERROR, "Erreur client");
        if (_onError) {
            _onError(err);
        }
    });

    _player->setOnPlaybackFinished([this]() {
        _lastActivityMs = millis();
        AudioTraceLog::record(AudioTraceEventType::PLAYBACK_FINISHED);
        finishTurn();
        if (_state == LiveSessionState::PLAYING) {
            AudioTraceLog::record(AudioTraceEventType::SESSION_CONNECTED);
            transitionTo(LiveSessionState::CONNECTED, "Fin lecture audio");
        }
        if (_onPlaybackFinished) {
            _onPlaybackFinished();
        }
    });

    return true;
}

void LiveSessionController::transitionTo(LiveSessionState newState, const char* reason) {
    if (_state == newState) return;

    Serial.printf("[LiveSession] Transition : %s -> %s%s%s%s\n",
                  liveSessionStateToString(_state),
                  liveSessionStateToString(newState),
                  reason ? " (" : "",
                  reason ? reason : "",
                  reason ? ")" : "");

    _state = newState;
    _stateEnterTimeMs = millis();

    if (_onStateChanged) {
        _onStateChanged(_state);
    }
}

bool LiveSessionController::activate() {
    if (!_client) return false;

    _lastError[0] = '\0';
    _lastAssistantText = "";
    _disconnectCount = 0;
    _firstAudioLatencyMs = 0;
    _turnActive.store(false, std::memory_order_release);
    _turnCancelled.store(false, std::memory_order_release);
    _hasBufferOverflowAnomaly = false;
    _hasPlaybackHardwareError = false;
    _playbackErrorReported = false;
    _reconnectAttempts = 0;
    _lastReconnectAttemptMs = 0;

    transitionTo(LiveSessionState::CONNECTING, "Activation session LIVE");
    return _client->connect();
}

void LiveSessionController::deactivate() {
    if (_client) {
        _client->disconnect();
    }
    if (_player) {
        _player->stop(AudioStopReason::USER_STOP);
    }
    _turnActive.store(false, std::memory_order_release);
    _turnCancelled.store(false, std::memory_order_release);
    transitionTo(LiveSessionState::DISCONNECTED, "Desactivation LIVE");
}

void LiveSessionController::tick() {
    const uint32_t now = millis();

    if (_state == LiveSessionState::RECONNECTING) {
        if (now - _lastReconnectAttemptMs >= LIVE_RECONNECT_INTERVAL_MS) {
            _lastReconnectAttemptMs = now;
            if (_reconnectAttempts < LIVE_MAX_RECONNECT_ATTEMPTS) {
                _reconnectAttempts++;
                Serial.printf("[LiveSession] Tentative de reconnexion auto (%u/%u)...\n",
                              _reconnectAttempts, (unsigned)LIVE_MAX_RECONNECT_ATTEMPTS);
                if (_client && _client->connect()) {
                    transitionTo(LiveSessionState::CONNECTING, "Reconnexion initiee");
                }
            } else {
                Serial.println("[LiveSession] Echec de toutes les tentatives de reconnexion.");
                transitionTo(LiveSessionState::ERROR, "Reconnexion impossible");
            }
        }
    }
}

bool LiveSessionController::sendTextMessage(const String& text) {
    if (!_client || !_client->isConnected() || !_client->isSetupComplete()) {
        return false;
    }

    _turnActive.store(true, std::memory_order_release);
    _turnCancelled.store(false, std::memory_order_release);
    _currentTurnId++;
    _turnCompleteReceived = false;
    _turnCompletedLogged = false;
    _audioStartedForTurn = false;
    _hasAudioResponse = false;
    _hasOutputTranscription = false;
    _hasModelTurnText = false;
    _lastAssistantText = "";
    _waitingStartTimeMs = millis();
    _lastActivityMs = _waitingStartTimeMs;

    if (_player) {
        _player->reset();
        if (_player->getAudioOutput()) {
            _player->getAudioOutput()->resetTurnMetrics();
        }
    }
    if (_client) {
        _client->resetTurnRxMetrics();
    }

    transitionTo(LiveSessionState::WAITING_RESPONSE, "Envoi message texte TTS");
    return _client->sendTextMessage(text);
}

bool LiveSessionController::beginAudioTurn() {
    if (!_client || !_client->isConnected() || !_client->isSetupComplete()) {
        return false;
    }

    _turnActive.store(true, std::memory_order_release);
    _turnCancelled.store(false, std::memory_order_release);
    _currentTurnId++;
    _turnCompleteReceived = false;
    _turnCompletedLogged = false;
    _audioStartedForTurn = false;
    _hasAudioResponse = false;
    _hasOutputTranscription = false;
    _hasModelTurnText = false;
    _lastAssistantText = "";
    _waitingStartTimeMs = millis();
    _lastActivityMs = _waitingStartTimeMs;

    if (_player) {
        _player->reset();
        if (_player->getAudioOutput()) {
            _player->getAudioOutput()->resetTurnMetrics();
        }
    }
    if (_client) {
        _client->resetTurnRxMetrics();
    }

    transitionTo(LiveSessionState::LISTENING, "Debut tour vocal audio conversation");
    return true;
}

void LiveSessionController::finishAudioCapture() {
    _waitingStartTimeMs = millis();
    _lastActivityMs = _waitingStartTimeMs;
    transitionTo(LiveSessionState::WAITING_RESPONSE, "Attente reponse serveur (fin parole)");
}


void LiveSessionController::requestStop() {
    Serial.println("\n[LiveSession] >>> DEMANDE INTERRUPTION STOP PRIORITAIRE <<<");
    _turnCancelled.store(true, std::memory_order_release);
    _turnActive.store(false, std::memory_order_release);
    _turnCompletedLogged = true;

    if (_client && _client->isConnected()) {
        _client->sendInterruption();
    }
    if (_player) {
        _player->stop(AudioStopReason::USER_STOP);
    }
    printAudioEndCheck("STOP");
    transitionTo(LiveSessionState::CONNECTED, "Interruption STOP demandee");
}

void LiveSessionController::setVolume(unsigned volumePercent) {
    if (_player) {
        _player->setVolume(volumePercent);
    }
}

uint32_t LiveSessionController::getChunksReceived() const {
    return _player ? _player->getChunksReceived() : 0;
}
