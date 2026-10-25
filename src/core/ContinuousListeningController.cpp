#include "ContinuousListeningController.h"
#include "../VoxCoreVoice.h"

ContinuousListeningController::ContinuousListeningController() {
    // Configuration par défaut conforme au cahier des charges :
    // - Début de parole : 200 ms
    // - Fin de parole : 1 200 ms de silence
    // - Durée maximale : 30 secondes
    VadConfig cfg;
    cfg.minSpeechDurationMs = 200;
    cfg.silenceDurationMs = 1200;
    cfg.maxSegmentDurationMs = 30000;
    cfg.initialNoiseFloor = 200;
    _vad.setConfig(cfg);

    _vad.onSpeechStarted([this]() {
        if (_onSpeechStarted) {
            _onSpeechStarted();
        }
    });

    _vad.onSpeechEnded([this](VadEndReason reason) {
        if (_onSpeechEnded) {
            _onSpeechEnded(reason);
        }
    });
}

ContinuousListeningController::~ContinuousListeningController() {
    stop();
}

void ContinuousListeningController::begin(VoxCoreVoice* voice) {
    _voice = voice;
}

void ContinuousListeningController::setState(ContinuousState newState) {
    if (_state != newState) {
        _state = newState;
        if (_onStateChanged) {
            _onStateChanged(_state);
        }
    }
}

bool ContinuousListeningController::start() {
    _isActive = true;
    _isPaused = false;
    _speechEndRequested = false;

    if (_voice && _voice->isReady() && !_voice->isListening() && !_voice->isTranscribing()) {
        _vad.reset();
        bool ok = _voice->startListening();
        if (ok) {
            setState(ContinuousState::LISTENING);
            return true;
        }
    }

    setState(ContinuousState::IDLE);
    return true;
}

bool ContinuousListeningController::stop() {
    _isActive = false;
    _isPaused = false;
    _speechEndRequested = false;

    if (_voice && _voice->isListening()) {
        _voice->cancelListening();
    }

    setState(ContinuousState::INACTIVE);
    return true;
}

void ContinuousListeningController::pause() {
    if (!_isActive || _isPaused) return;

    _isPaused = true;
    _speechEndRequested = false;

    if (_voice && _voice->isListening()) {
        _voice->cancelListening();
    }

    setState(ContinuousState::PAUSED);
}

bool ContinuousListeningController::resume() {
    if (!_isActive || !_isPaused) return false;

    _isPaused = false;
    _speechEndRequested = false;

    if (_voice && _voice->isReady() && !_voice->isListening() && !_voice->isTranscribing()) {
        _vad.reset();
        bool ok = _voice->startListening();
        if (ok) {
            setState(ContinuousState::LISTENING);
            return true;
        }
    }

    setState(ContinuousState::IDLE);
    return true;
}

void ContinuousListeningController::processAudioChunk(const int16_t* samples, size_t count, uint32_t rms) {
    (void)samples;
    (void)count;
    _lastChunkRms = rms;

    if (_isActive && !_isPaused && (_state == ContinuousState::LISTENING || _state == ContinuousState::SPEECH_DETECTED)) {
        bool ended = _vad.processRms(rms, 32);
        if (ended) {
            _speechEndRequested = true;
        }
    }
}

void ContinuousListeningController::handleTranscription(const String& text, bool isFinal) {
    if (!_isActive || _isPaused) return;

    if (isFinal) {
        _lastFinalText = text;
        _lastFinalConfirmed = true;
        _segmentCount++;
        _lastCompletedTimeMs = millis();
        setState(ContinuousState::SEGMENT_DONE);

        if (_onSegmentComplete) {
            _onSegmentComplete(text, true);
        }
    }
}

void ContinuousListeningController::update() {
    if (!_isActive || _isPaused || !_voice) {
        return;
    }

    uint32_t now = millis();

    // 1. État IDLE : Attente que le moteur vocal et Gemini Live soient prêts
    if (_state == ContinuousState::IDLE) {
        if (_voice->isReady() && !_voice->isListening() && !_voice->isTranscribing()) {
            _vad.reset();
            _speechEndRequested = false;
            bool ok = _voice->startListening();
            if (ok) {
                setState(ContinuousState::LISTENING);
            }
        }
        return;
    }

    // 2. Prise en compte de la demande de fin de parole issue du VAD (chunk audio)
    if (_speechEndRequested) {
        _speechEndRequested = false;
        if (_voice->isListening()) {
            _voice->stopListening();
            _stopListeningTimeMs = now;
            setState(ContinuousState::WAITING_FINAL);
            return;
        }
    }

    // 3. Suivi de l'activité vocale pendant l'écoute
    if (_state == ContinuousState::LISTENING || _state == ContinuousState::SPEECH_DETECTED) {
        if (!_voice->isListening()) {
            // Le microphone a été fermé
            if (_voice->isTranscribing()) {
                setState(ContinuousState::WAITING_FINAL);
            } else {
                setState(ContinuousState::IDLE);
            }
            return;
        }

        if (_vad.getState() == VadState::SPEECH_ACTIVE) {
            if (_state != ContinuousState::SPEECH_DETECTED) {
                setState(ContinuousState::SPEECH_DETECTED);
            }
        } else if (_vad.getState() == VadState::SILENCE) {
            if (_state != ContinuousState::LISTENING) {
                setState(ContinuousState::LISTENING);
            }
        }
    }

    // 4. Détection du timeout de transcription finale (STT)
    if (_state == ContinuousState::WAITING_FINAL) {
        if (_voice->isReady() && !_voice->isTranscribing()) {
            // VoiceKit est revenu à READY sans confirmation finale : timeout dépassé !
            // Règle absolue : zéro hallucination ni fabrication de faux texte
            _lastFinalText = "";
            _lastFinalConfirmed = false;
            _lastCompletedTimeMs = now;
            setState(ContinuousState::SEGMENT_DONE);

            if (_onSegmentComplete) {
                _onSegmentComplete("", false);
            }
        }
    }

    // 5. Reprise automatique mains libres après finalisation du segment
    if (_state == ContinuousState::SEGMENT_DONE) {
        if (now - _lastCompletedTimeMs >= _autoResumeDelayMs) {
            if (!_isPaused && _voice->isReady() && !_voice->isListening() && !_voice->isTranscribing()) {
                _vad.reset();
                _speechEndRequested = false;
                bool ok = _voice->startListening();
                if (ok) {
                    setState(ContinuousState::LISTENING);
                } else {
                    setState(ContinuousState::IDLE);
                }
            }
        }
    }
}

bool ContinuousListeningController::isSpeechActive() const {
    return (_state == ContinuousState::SPEECH_DETECTED || _vad.isSpeechActive());
}

bool ContinuousListeningController::isBusy() const {
    if (!_isActive || _isPaused) return false;
    return (_state == ContinuousState::SPEECH_DETECTED ||
            _state == ContinuousState::WAITING_FINAL ||
            _vad.isSpeechActive());
}
