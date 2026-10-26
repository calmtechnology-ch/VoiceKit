#include "ContinuousListeningController.h"
#include "../VoxCoreVoice.h"

ContinuousListeningController::ContinuousListeningController() {
    _state.store(ContinuousState::INACTIVE, std::memory_order_relaxed);
    _isActive.store(false, std::memory_order_relaxed);
    _isPaused.store(false, std::memory_order_relaxed);
    _speechEndRequested.store(false, std::memory_order_relaxed);
    _lastChunkRms.store(0, std::memory_order_relaxed);
    _rmsHead.store(0, std::memory_order_relaxed);
    _rmsTail.store(0, std::memory_order_relaxed);
    for (size_t i = 0; i < RMS_QUEUE_CAPACITY; ++i) {
        _rmsQueue[i].store(0, std::memory_order_relaxed);
    }

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

    // Callbacks VAD déclenchés exclusivement dans le contexte de update() (tâche principale)
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

void ContinuousListeningController::clearRmsQueue() {
    _rmsTail.store(_rmsHead.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

bool ContinuousListeningController::pushRms(uint32_t rms) {
    size_t head = _rmsHead.load(std::memory_order_relaxed);
    size_t tail = _rmsTail.load(std::memory_order_acquire);
    if (head - tail < RMS_QUEUE_CAPACITY) {
        _rmsQueue[head % RMS_QUEUE_CAPACITY].store(rms, std::memory_order_relaxed);
        _rmsHead.store(head + 1, std::memory_order_release);
        return true;
    }
    return false; // Queue saturée : abandon non bloquant sans jamais freiner l'audio
}

bool ContinuousListeningController::popRms(uint32_t& rms) {
    size_t head = _rmsHead.load(std::memory_order_acquire);
    size_t tail = _rmsTail.load(std::memory_order_relaxed);
    if (tail < head) {
        rms = _rmsQueue[tail % RMS_QUEUE_CAPACITY].load(std::memory_order_relaxed);
        _rmsTail.store(tail + 1, std::memory_order_release);
        return true;
    }
    return false;
}

void ContinuousListeningController::setState(ContinuousState newState) {
    ContinuousState oldState = _state.exchange(newState, std::memory_order_acq_rel);
    if (oldState != newState) {
        if (_onStateChanged) {
            _onStateChanged(newState);
        }
    }
}

bool ContinuousListeningController::start() {
    _isActive.store(true, std::memory_order_release);
    _isPaused.store(false, std::memory_order_release);
    _speechEndRequested.store(false, std::memory_order_relaxed);
    _speechEndDetectedMs = 0;
    _segmentFinalized = false;
    clearRmsQueue();

    if (_voice && _voice->isReady() && !_voice->isListening() && !_voice->isTranscribing()) {
        _vad.reset();
        bool ok = _voice->startListening();
        if (ok) {
            _currentSegmentId++;
            setState(ContinuousState::LISTENING);
            return true;
        }
    }

    setState(ContinuousState::IDLE);
    return true;
}

bool ContinuousListeningController::stop() {
    _isActive.store(false, std::memory_order_release);
    _isPaused.store(false, std::memory_order_release);
    _speechEndRequested.store(false, std::memory_order_relaxed);
    _speechEndDetectedMs = 0;
    _segmentFinalized = false;
    clearRmsQueue();

    if (_voice && _voice->isListening()) {
        _voice->cancelListening();
    }

    setState(ContinuousState::INACTIVE);
    return true;
}

void ContinuousListeningController::pause() {
    if (!_isActive.load(std::memory_order_acquire) || _isPaused.load(std::memory_order_acquire)) return;

    _isPaused.store(true, std::memory_order_release);
    _speechEndRequested.store(false, std::memory_order_relaxed);
    clearRmsQueue();

    if (_voice && _voice->isListening()) {
        _voice->cancelListening();
    }

    setState(ContinuousState::PAUSED);
}

bool ContinuousListeningController::resume() {
    if (!_isActive.load(std::memory_order_acquire) || !_isPaused.load(std::memory_order_acquire)) return false;

    _isPaused.store(false, std::memory_order_release);
    _speechEndRequested.store(false, std::memory_order_relaxed);
    _segmentFinalized = false;
    clearRmsQueue();

    if (_voice && _voice->isReady() && !_voice->isListening() && !_voice->isTranscribing()) {
        _vad.reset();
        bool ok = _voice->startListening();
        if (ok) {
            _currentSegmentId++;
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
    _lastChunkRms.store(rms, std::memory_order_relaxed);

    // Contexte tâche audio FreeRTOS (Core 1) :
    // STRICTEMENT non bloquant, sans mutex, sans allocation, sans modification de String, sans callbacks.
    // Transfert léger de la donnée RMS via la file lock-free SPSC vers update().
    if (_isActive.load(std::memory_order_acquire) && !_isPaused.load(std::memory_order_acquire)) {
        ContinuousState cur = _state.load(std::memory_order_relaxed);
        if (cur == ContinuousState::LISTENING || cur == ContinuousState::SPEECH_DETECTED) {
            pushRms(rms);
        }
    }
}

void ContinuousListeningController::handleTranscription(const String& text, bool isFinal) {
    if (!_isActive.load(std::memory_order_acquire) || _isPaused.load(std::memory_order_acquire)) return;

    // Règle 1 : Ne jamais convertir une transcription partielle en transcription finale
    if (!isFinal) {
        return;
    }

    // Règle 2 : Autoriser au maximum une finalisation par segment (ignorer doublons)
    if (_segmentFinalized) {
        return;
    }

    // Règle 3 : Empêcher les callbacks tardifs d'un segment terminé de modifier le suivant
    if (_state.load(std::memory_order_relaxed) != ContinuousState::WAITING_FINAL) {
        return;
    }

    _segmentFinalized = true;
    _lastFinalText = text;
    _lastFinalConfirmed = true;
    _segmentCount++;
    _lastCompletedTimeMs = millis();
    setState(ContinuousState::SEGMENT_DONE);

    if (_onSegmentComplete) {
        _onSegmentComplete(text, true);
    }
}

void ContinuousListeningController::update() {
    if (!_isActive.load(std::memory_order_acquire) || _isPaused.load(std::memory_order_acquire) || !_voice) {
        return;
    }

    uint32_t now = millis();
    ContinuousState curState = _state.load(std::memory_order_relaxed);

    // 1. État IDLE : Attente que le moteur vocal et Gemini Live soient prêts
    if (curState == ContinuousState::IDLE) {
        if (_voice->isReady() && !_voice->isListening() && !_voice->isTranscribing()) {
            _vad.reset();
            _speechEndRequested.store(false, std::memory_order_relaxed);
            _segmentFinalized = false;
            clearRmsQueue();
            bool ok = _voice->startListening();
            if (ok) {
                _currentSegmentId++;
                setState(ContinuousState::LISTENING);
            }
        }
        return;
    }

    // 2. Traitement des blocs RMS accumulés dans le contexte unique de update() (tâche principale)
    if (curState == ContinuousState::LISTENING || curState == ContinuousState::SPEECH_DETECTED) {
        uint32_t queuedRms = 0;
        while (popRms(queuedRms)) {
            bool ended = _vad.processRms(queuedRms, 32);
            if (ended) {
                _speechEndDetectedMs = millis();
                if (_speechEndDetectedMs == 0) _speechEndDetectedMs = 1;
                _speechEndRequested.store(true, std::memory_order_release);
                clearRmsQueue();
                break;
            }
        }
    } else {
        clearRmsQueue();
    }

    // 3. Prise en compte de la demande de fin de parole issue du VAD
    if (_speechEndRequested.load(std::memory_order_acquire)) {
        _speechEndRequested.store(false, std::memory_order_relaxed);
        if (_voice && _voice->isListening()) {
            _voice->notifySpeechEndDetected(_speechEndDetectedMs);
            _voice->stopListening();
            _stopListeningTimeMs = now;
            _segmentFinalized = false;
            setState(ContinuousState::WAITING_FINAL);
            return;
        }
    }

    // 4. Suivi de l'activité vocale pendant l'écoute
    curState = _state.load(std::memory_order_relaxed);
    if (curState == ContinuousState::LISTENING || curState == ContinuousState::SPEECH_DETECTED) {
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
            if (curState != ContinuousState::SPEECH_DETECTED) {
                setState(ContinuousState::SPEECH_DETECTED);
            }
        } else if (_vad.getState() == VadState::SILENCE) {
            if (curState != ContinuousState::LISTENING) {
                setState(ContinuousState::LISTENING);
            }
        }
    }

    // 5. Détection du timeout de transcription finale (STT)
    if (curState == ContinuousState::WAITING_FINAL) {
        if (_voice->isReady() && !_voice->isTranscribing()) {
            // VoiceKit est revenu à READY sans confirmation finale : timeout dépassé !
            // Règle absolue : zéro hallucination ni fabrication de faux texte
            if (!_segmentFinalized) {
                _segmentFinalized = true;
                _lastFinalText = "";
                _lastFinalConfirmed = false;
                _lastCompletedTimeMs = now;
                setState(ContinuousState::SEGMENT_DONE);

                if (_onSegmentComplete) {
                    _onSegmentComplete("", false);
                }
            }
        }
    }

    // 6. Reprise automatique mains libres après finalisation du segment (400 ms par défaut)
    curState = _state.load(std::memory_order_relaxed);
    if (curState == ContinuousState::SEGMENT_DONE) {
        if (now - _lastCompletedTimeMs >= _autoResumeDelayMs) {
            if (!_isPaused.load(std::memory_order_acquire) && _voice->isReady() && !_voice->isListening() && !_voice->isTranscribing()) {
                _vad.reset();
                _speechEndRequested.store(false, std::memory_order_relaxed);
                _segmentFinalized = false;
                clearRmsQueue();
                bool ok = _voice->startListening();
                if (ok) {
                    _currentSegmentId++;
                    setState(ContinuousState::LISTENING);
                } else {
                    setState(ContinuousState::IDLE);
                }
            }
        }
    }
}

bool ContinuousListeningController::isSpeechActive() const {
    ContinuousState s = _state.load(std::memory_order_relaxed);
    return (s == ContinuousState::SPEECH_DETECTED || _vad.isSpeechActive());
}

bool ContinuousListeningController::isBusy() const {
    if (!_isActive.load(std::memory_order_relaxed) || _isPaused.load(std::memory_order_relaxed)) return false;
    ContinuousState s = _state.load(std::memory_order_relaxed);
    return (s == ContinuousState::SPEECH_DETECTED ||
            s == ContinuousState::WAITING_FINAL ||
            _vad.isSpeechActive());
}
