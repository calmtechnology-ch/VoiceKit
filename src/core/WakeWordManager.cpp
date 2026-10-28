#include "WakeWordManager.h"
#include "LiveAudioStreamer.h"

#if defined(ESP32) || defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

WakeWordManager::WakeWordManager() {
    _selectedEngineType = WakeWordEngineType::WAKENET;
    _activeEngine = &_wakeNetEngine;
    _wakeNetEngine.setTargetWakeWord(_targetWakeWord);
    _testEngine.setTargetWakeWord(_targetWakeWord);
}

WakeWordManager::~WakeWordManager() {
    _onDetected = nullptr;
    _onEvent = nullptr;
    _onStateChanged = nullptr;
    _onError = nullptr;
    disable();
}

void WakeWordManager::begin(LiveAudioStreamer* streamer) {
    _streamer = streamer;
    if (_streamer) {
        _streamer->setWakeWordAudioCallback([this](const int16_t* samples, size_t count, uint32_t rms) {
            this->onAudioChunkFromTask(samples, count, rms);
        });
    }
}

void WakeWordManager::selectActiveEnginePointer() {
    switch (_selectedEngineType) {
        case WakeWordEngineType::TEST:
            _activeEngine = &_testEngine;
            break;
        case WakeWordEngineType::WAKENET:
            _activeEngine = &_wakeNetEngine;
            break;
        case WakeWordEngineType::CUSTOM:
        case WakeWordEngineType::MICRO_WAKE_WORD:
            _activeEngine = _customEngine;
            break;
        case WakeWordEngineType::NONE:
        default:
            _activeEngine = nullptr;
            break;
    }
    if (_activeEngine) {
        _activeEngine->setTargetWakeWord(_targetWakeWord);
    }
}

void WakeWordManager::setState(WakeWordState newState) {
    WakeWordState oldState = _state.exchange(newState, std::memory_order_acq_rel);
    if (oldState != newState) {
        _metrics.stateTransitionCount++;
        if (_onStateChanged) {
            _onStateChanged(newState);
        }
    }
}

bool WakeWordManager::setEngineType(WakeWordEngineType type) {
    if (_state.load(std::memory_order_acquire) == WakeWordState::ARMED) {
        stopCaptureIfNeeded();
        _bridge.setEnabled(false);
    }
    if (_activeEngine && _activeEngine->isInitialized()) {
        _activeEngine->end();
    }
    _selectedEngineType = type;
    selectActiveEnginePointer();

    // Si déjà activé, réinitialiser avec le nouveau moteur choisi
    if (_enabled.load(std::memory_order_acquire)) {
        return enable(true);
    }
    return true;
}

bool WakeWordManager::setCustomEngine(IWakeWordEngine* customEngine) {
    if (_state.load(std::memory_order_acquire) == WakeWordState::ARMED) {
        stopCaptureIfNeeded();
        _bridge.setEnabled(false);
    }
    if (_activeEngine && _activeEngine->isInitialized()) {
        _activeEngine->end();
    }
    _customEngine = customEngine;
    _selectedEngineType = customEngine ? customEngine->getType() : WakeWordEngineType::NONE;
    _activeEngine = customEngine;
    if (_activeEngine) {
        _activeEngine->setTargetWakeWord(_targetWakeWord);
    }
    if (_enabled.load(std::memory_order_acquire)) {
        return enable(true);
    }
    return true;
}

void WakeWordManager::setTargetWakeWord(const String& word) {
    if (!word.isEmpty()) {
        _targetWakeWord = word;
        _wakeNetEngine.setTargetWakeWord(word);
        _testEngine.setTargetWakeWord(word);
        if (_customEngine) {
            _customEngine->setTargetWakeWord(word);
        }
    }
}

const char* WakeWordManager::getEngineName() const {
    if (_activeEngine) {
        return _activeEngine->getName();
    }
    return wakeWordEngineTypeToString(_selectedEngineType);
}

const char* WakeWordManager::getModelName() const {
    if (_activeEngine) {
        return _activeEngine->getModelName();
    }
    return "";
}

bool WakeWordManager::isEngineAvailable() const {
    return _activeEngine != nullptr && _activeEngine->isAvailable();
}

bool WakeWordManager::isEngineAvailable(WakeWordEngineType type) const {
    switch (type) {
        case WakeWordEngineType::TEST:
            return _testEngine.isAvailable();
        case WakeWordEngineType::WAKENET:
            return _wakeNetEngine.isAvailable();
        case WakeWordEngineType::CUSTOM:
        case WakeWordEngineType::MICRO_WAKE_WORD:
            return _customEngine != nullptr && _customEngine->isAvailable();
        default:
            return false;
    }
}

bool WakeWordManager::isEngineInitialized() const {
    return _activeEngine != nullptr && _activeEngine->isInitialized();
}

bool WakeWordManager::isListening() const {
    return _enabled.load(std::memory_order_acquire) &&
           _state.load(std::memory_order_acquire) == WakeWordState::ARMED &&
           isEngineInitialized() &&
           _bridge.isEnabled();
}

bool WakeWordManager::isSimulated() const {
    return _activeEngine != nullptr && _activeEngine->isSimulatedEngine();
}

bool WakeWordManager::startCaptureIfNeeded() {
    if (!_useLocalMicCapture || !_streamer) {
        return true;
    }
    if (_streamer->isLocalCapture()) {
        _localCaptureStartedByWake = true;
        return true;
    }
    if (_streamer->isStreaming()) {
        // Le streamer est déjà actif pour une écoute STT / Conversation : ne pas réinitialiser le micro
        return true;
    }
    bool ok = _streamer->startLocalCapture();
    if (ok) {
        _localCaptureStartedByWake = true;
    }
    return ok;
}

void WakeWordManager::stopCaptureIfNeeded() {
    if (_localCaptureStartedByWake && _streamer) {
        if (_streamer->isLocalCapture()) {
            _streamer->stopLocalCapture();
        }
        _localCaptureStartedByWake = false;
    }
}

bool WakeWordManager::enable(bool en) {
    if (!en) {
        disable();
        return true;
    }

    _enabled.store(true, std::memory_order_release);
    _lastError = "";
    _voiceActivitySeenSinceSuspend = false;
    _resumePendingAfterBusy = false;

    setState(WakeWordState::INITIALIZING);
    selectActiveEnginePointer();

    // 1. Initialisation du pont audio borné
    if (!_bridge.begin(WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY)) {
        _lastError = "Echec allocation memoire WakeWordAudioBridge";
        _metrics.errorCount++;
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        return false;
    }
    _metrics.queueCapacity = _bridge.getCapacity();

    // 2. Vérification de la disponibilité réelle du moteur
    if (!_activeEngine) {
        _lastError = "Aucun moteur Wake Word configure (NONE)";
        _metrics.errorCount++;
        _bridge.setEnabled(false);
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        return false;
    }

    if (!_activeEngine->isAvailable()) {
        _lastError = _activeEngine->getLastError();
        if (_lastError.isEmpty()) {
            _lastError = "Moteur Wake Word non disponible dans cet environnement";
        }
        _metrics.errorCount++;
        _bridge.setEnabled(false);
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        return false;
    }

    // 3. Initialisation du moteur (PCM 16 000 Hz mono 16 bits)
    if (!_activeEngine->begin(16000, 1)) {
        _lastError = _activeEngine->getLastError();
        if (_lastError.isEmpty()) {
            _lastError = "Echec begin() du moteur Wake Word";
        }
        _metrics.errorCount++;
        _bridge.setEnabled(false);
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        return false;
    }

    // 4. Si une activité vocale (TTS, STT, Conversation) est en cours, démarrer en état SUSPENDED
    bool voiceBusy = (_voiceBusyCheck && _voiceBusyCheck()) || _wasVoiceBusy;
    if (voiceBusy) {
        _bridge.setEnabled(false);
        _bridge.clear();
        _voiceActivitySeenSinceSuspend = true;
        setState(WakeWordState::SUSPENDED);
        refreshMemoryMetrics();
        return true;
    }

    // 5. Démarrage de la capture locale PCM 16 kHz et armement du détecteur
    _bridge.clear();
    _bridge.setEnabled(true);
    if (!startCaptureIfNeeded()) {
        _bridge.setEnabled(false);
        _lastError = _streamer ? _streamer->getLastError() : "Echec demarrage capture locale microphone";
        if (_lastError.isEmpty()) {
            _lastError = "Echec demarrage capture locale microphone ES7210";
        }
        _metrics.errorCount++;
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        return false;
    }

    setState(WakeWordState::ARMED);
    refreshMemoryMetrics();
    return true;
}

void WakeWordManager::disable() {
    _enabled.store(false, std::memory_order_release);
    _resumePendingAfterBusy = false;
    _voiceActivitySeenSinceSuspend = false;
    _hasPendingEvent.store(false, std::memory_order_release);

    stopCaptureIfNeeded();
    _bridge.end();

    if (_activeEngine && _activeEngine->isInitialized()) {
        _activeEngine->end();
    }

    setState(WakeWordState::INACTIVE);
}

bool WakeWordManager::suspend() {
    if (!_enabled.load(std::memory_order_acquire)) {
        return false;
    }
    WakeWordState cur = _state.load(std::memory_order_acquire);
    if (cur == WakeWordState::INACTIVE || cur == WakeWordState::ERROR) {
        return false;
    }
    if (cur == WakeWordState::SUSPENDED) {
        return true;
    }

    _bridge.setEnabled(false);
    stopCaptureIfNeeded();
    _bridge.clear();
    if (_activeEngine) {
        _activeEngine->reset();
    }
    _resumePendingAfterBusy = false;
    setState(WakeWordState::SUSPENDED);
    return true;
}

bool WakeWordManager::resume() {
    if (!_enabled.load(std::memory_order_acquire)) {
        return false;
    }
    WakeWordState cur = _state.load(std::memory_order_acquire);
    if (cur == WakeWordState::ARMED) {
        return true;
    }
    if (cur != WakeWordState::SUSPENDED && cur != WakeWordState::DETECTED) {
        return false;
    }
    if (!isEngineInitialized()) {
        return false;
    }

    // Si une opération vocale est encore active, différer la reprise jusqu'à la fin de la restitution
    bool voiceBusy = (_voiceBusyCheck && _voiceBusyCheck()) || _wasVoiceBusy;
    if (voiceBusy) {
        _resumePendingAfterBusy = true;
        _voiceActivitySeenSinceSuspend = true;
        return true;
    }

    _activeEngine->reset();
    _bridge.clear();
    _bridge.setEnabled(true);

    if (!startCaptureIfNeeded()) {
        _bridge.setEnabled(false);
        _lastError = _streamer ? _streamer->getLastError() : "Echec reprise capture locale microphone";
        _metrics.errorCount++;
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        return false;
    }

    _voiceActivitySeenSinceSuspend = false;
    _resumePendingAfterBusy = false;
    setState(WakeWordState::ARMED);
    return true;
}

void WakeWordManager::onAudioChunkFromTask(const int16_t* samples, size_t sampleCount, uint32_t rms) {
    // Exécuté dans la tâche audio FreeRTOS (Core 1) :
    // STRICTEMENT non bloquant, sans allocation, sans Serial.print.
    if (!_enabled.load(std::memory_order_relaxed) ||
        _state.load(std::memory_order_relaxed) != WakeWordState::ARMED) {
        return;
    }
    _bridge.pushAudioChunk(samples, sampleCount, rms);
}

void WakeWordManager::notifyVoiceActivity(bool isVoiceBusy, bool isSpeaking) {
    bool activeNow = isVoiceBusy || isSpeaking;

    if (activeNow) {
        _lastVoiceActivityEndMs = millis();
        if (_lastVoiceActivityEndMs == 0) _lastVoiceActivityEndMs = 1;

        if (_enabled.load(std::memory_order_acquire)) {
            WakeWordState cur = _state.load(std::memory_order_acquire);
            if (cur == WakeWordState::ARMED) {
                // Suspension automatique immédiate pour éviter tout conflit I2S ou faux réveil haut-parleur
                suspend();
                _voiceActivitySeenSinceSuspend = true;
            } else if (cur == WakeWordState::SUSPENDED) {
                _voiceActivitySeenSinceSuspend = true;
            }
        }
    } else if (_wasVoiceBusy && !activeNow) {
        // L'activité vocale vient juste de se terminer : horodater la fin pour le délai de reprise contrôlée
        _lastVoiceActivityEndMs = millis();
        if (_lastVoiceActivityEndMs == 0) _lastVoiceActivityEndMs = 1;
    }

    _wasVoiceBusy = activeNow;
}

void WakeWordManager::update() {
    if (!_enabled.load(std::memory_order_acquire)) {
        return;
    }

    WakeWordState curState = _state.load(std::memory_order_acquire);

    // 1. Gestion de la reprise contrôlée après fin de conversation / réponse vocale
    if (curState == WakeWordState::SUSPENDED) {
        bool voiceBusy = (_voiceBusyCheck && _voiceBusyCheck()) || _wasVoiceBusy;
        if (!voiceBusy && (_resumePendingAfterBusy || (_autoResumeAfterVoice && _voiceActivitySeenSinceSuspend))) {
            uint32_t now = millis();
            if (_lastVoiceActivityEndMs == 0 || (now - _lastVoiceActivityEndMs >= _resumeDelayMs)) {
                resume();
            }
        }
        return;
    }

    if (curState != WakeWordState::ARMED || !_activeEngine || !_activeEngine->isInitialized()) {
        return;
    }

    // 2. Dépilage et traitement des trames PCM 16 kHz hors du callback audio critique
    size_t maxFramesPerUpdate = _bridge.getCapacity() > 0 ? _bridge.getCapacity() : 8;
    size_t processedThisTick = 0;

    while (processedThisTick < maxFramesPerUpdate && _state.load(std::memory_order_acquire) == WakeWordState::ARMED) {
        const WakeWordAudioBridge::FrameSlot* slot = _bridge.peekFrame();
        if (!slot) {
            break;
        }

        const uint32_t procStartUs = micros();
        WakeWordResult res = _activeEngine->process(slot->samples, slot->sampleCount, slot->rms);
        const uint32_t procDurUs = micros() - procStartUs;

        // Libération immédiate du slot dans la file bornée (zéro copie supplémentaire)
        _bridge.consumeFrame();
        processedThisTick++;

        _metrics.framesProcessed++;
        _metrics.lastWakeProcessDurationUs = procDurUs;
        if (procDurUs > _metrics.maxWakeProcessDurationUs) {
            _metrics.maxWakeProcessDurationUs = procDurUs;
        }

        // Vérification d'erreur d'exécution du moteur
        if (!res.detected && _activeEngine->getType() == WakeWordEngineType::TEST && _testEngine.isSimulatingRuntimeError()) {
            _lastError = _activeEngine->getLastError();
            _metrics.errorCount++;
            _bridge.setEnabled(false);
            stopCaptureIfNeeded();
            _bridge.clear();
            setState(WakeWordState::ERROR);
            if (_onError) {
                _onError(_lastError);
            }
            break;
        }

        // 3. Traitement d'une détection (ARMED -> DETECTED -> SUSPENDED)
        if (res.detected) {
            // Libérer immédiatement le microphone et couper le pont audio avant d'invoquer les callbacks
            _bridge.setEnabled(false);
            stopCaptureIfNeeded();
            _bridge.clear();
            _activeEngine->reset();

            String detectedWord = res.word.isEmpty() ? _targetWakeWord : res.word;
            _lastDetectedWord = detectedWord;
            uint32_t detTimeMs = millis();
            if (detTimeMs == 0) detTimeMs = 1;

            _metrics.detectionCount++;
            if (res.isSimulated) {
                _metrics.simulatedDetectionCount++;
            }
            _metrics.lastDetectionTimestampMs = detTimeMs;

            // Préparation de l'événement pour la boucle LVGL (pollEvent)
            _pendingEvent.valid = true;
            _pendingEvent.word = detectedWord;
            _pendingEvent.confidence = res.confidence;
            _pendingEvent.isSimulated = res.isSimulated;
            _pendingEvent.timestampMs = detTimeMs;
            _pendingEvent.detectionIndex = _metrics.detectionCount;
            _pendingEvent.engineType = _activeEngine->getType();
            _hasPendingEvent.store(true, std::memory_order_release);

            // Transition 1 : ARMED -> DETECTED
            setState(WakeWordState::DETECTED);

            // Notification de l'application (dans le contexte sûr de update())
            if (_onDetected) {
                _onDetected(detectedWord);
            }
            if (_onEvent) {
                _onEvent(_pendingEvent);
            }

            // Transition 2 : DETECTED -> SUSPENDED (si le callback n'a pas déjà changé l'état vers DISABLED ou ARMED)
            if (_state.load(std::memory_order_acquire) == WakeWordState::DETECTED) {
                _voiceActivitySeenSinceSuspend = _wasVoiceBusy || (_voiceBusyCheck && _voiceBusyCheck());
                setState(WakeWordState::SUSPENDED);
            }
            break;
        }
    }

    refreshMemoryMetrics();
}

bool WakeWordManager::triggerTestDetection(const String& customWord) {
    // Sécurité stricte (Section 9) : Ne JAMAIS autoriser un déclenchement de test
    // si le moteur actif n'est pas explicitement TestWakeWordEngine ou si le détecteur n'est pas ARMED.
    if (!_enabled.load(std::memory_order_acquire) ||
        _state.load(std::memory_order_acquire) != WakeWordState::ARMED ||
        !_activeEngine ||
        _activeEngine->getType() != WakeWordEngineType::TEST) {
        return false;
    }

    bool ok = _testEngine.requestTrigger(customWord.isEmpty() ? _targetWakeWord : customWord);
    if (!ok) {
        return false;
    }

    // Si aucune trame n'est actuellement dans le pont (ex: test unitaire sans tâche DMA ou commande série),
    // pousser une trame silence de validation pour que update() exécute le cycle complet Bridge -> Engine -> State.
    if (_bridge.isEmpty()) {
        int16_t silentFrame[WakeWordAudioBridge::MAX_FRAME_SAMPLES] = {0};
        _bridge.pushAudioChunk(silentFrame, WakeWordAudioBridge::MAX_FRAME_SAMPLES, 0);
    }
    return true;
}

bool WakeWordManager::handleSerialCommand(const String& cmd) {
    if (!_enabled.load(std::memory_order_acquire) ||
        _state.load(std::memory_order_acquire) != WakeWordState::ARMED ||
        !_activeEngine ||
        _activeEngine->getType() != WakeWordEngineType::TEST) {
        return false;
    }
    if (_testEngine.handleSerialCommand(cmd)) {
        if (_bridge.isEmpty()) {
            int16_t silentFrame[WakeWordAudioBridge::MAX_FRAME_SAMPLES] = {0};
            _bridge.pushAudioChunk(silentFrame, WakeWordAudioBridge::MAX_FRAME_SAMPLES, 0);
        }
        return true;
    }
    return false;
}

bool WakeWordManager::pollEvent(WakeWordEvent& outEvent) {
    if (!_hasPendingEvent.exchange(false, std::memory_order_acq_rel)) {
        outEvent = WakeWordEvent();
        return false;
    }
    outEvent = _pendingEvent;
    return true;
}

void WakeWordManager::refreshMemoryMetrics() const {
#if defined(ESP32) || defined(ARDUINO)
    _metrics.freeInternalHeap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    _metrics.minFreeInternalHeap = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    _metrics.freePsram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    _metrics.minFreePsram = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
#else
    _metrics.freeInternalHeap = 200000;
    _metrics.minFreeInternalHeap = 180000;
    _metrics.freePsram = 8000000;
    _metrics.minFreePsram = 7500000;
#endif
    _metrics.lastAudioPushDurationUs = _bridge.getLastPushDurationUs();
    _metrics.maxAudioPushDurationUs = _bridge.getMaxPushDurationUs();
    _metrics.framesReceived = _bridge.getFramesReceived();
    _metrics.framesDropped = _bridge.getFramesDropped();
    _metrics.queueSaturations = _bridge.getQueueSaturationCount();
    _metrics.currentQueueOccupancy = _bridge.getOccupancy();
    _metrics.maxQueueOccupancy = _bridge.getMaxOccupancy();
    _metrics.queueCapacity = _bridge.getCapacity();
}

const WakeWordMetrics& WakeWordManager::getMetrics() const {
    refreshMemoryMetrics();
    return _metrics;
}

void WakeWordManager::resetMetrics() {
    _bridge.resetMetrics();
    _metrics.reset();
    refreshMemoryMetrics();
}
