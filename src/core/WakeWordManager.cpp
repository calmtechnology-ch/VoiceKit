#include "WakeWordManager.h"
#include "LiveAudioStreamer.h"
#include "../hal/HardwareManager.h"
#include <algorithm>

#if defined(ESP32) || defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

WakeWordManager::WakeWordManager() {
    _selectedEngineType = WakeWordEngineType::WAKENET;
    _activeEngine = &_wakeNetEngine;
    _wakeNetEngine.setModelName(_configuredModelName);
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
        if (newState == WakeWordState::ARMED) {
            if (_hasArmedOnce) {
                _metrics.rearmCount++;
            } else {
                _hasArmedOnce = true;
            }
        }
        if (_onStateChanged) {
            _onStateChanged(newState);
        }
    }
}

bool WakeWordManager::setEngineType(WakeWordEngineType type) {
    if (_state.load(std::memory_order_acquire) == WakeWordState::ARMED || _localCaptureStartedByWake) {
        _bridge.setEnabled(false);
        if (!stopCaptureIfNeeded()) {
            _lastError = (_streamer && strlen(_streamer->getLastError()) > 0)
                ? String(_streamer->getLastError())
                : String("Echec arret capture locale microphone lors du changement de moteur");
            _metrics.errorCount++;
            setState(WakeWordState::ERROR);
            if (_onError) _onError(_lastError);
            return false;
        }
    }
    if (_activeEngine && _activeEngine->isInitialized()) {
        _activeEngine->end();
    }
    _selectedEngineType = type;
    selectActiveEnginePointer();

    // Si déjà activé, réinitialiser avec le nouveau moteur choisi
    if (_enabled.load(std::memory_order_acquire)) {
        setState(WakeWordState::INACTIVE);
        return enable(true);
    }
    return true;
}

bool WakeWordManager::setCustomEngine(IWakeWordEngine* customEngine) {
    if (_state.load(std::memory_order_acquire) == WakeWordState::ARMED || _localCaptureStartedByWake) {
        _bridge.setEnabled(false);
        if (!stopCaptureIfNeeded()) {
            _lastError = (_streamer && strlen(_streamer->getLastError()) > 0)
                ? String(_streamer->getLastError())
                : String("Echec arret capture locale microphone lors de l'injection moteur");
            _metrics.errorCount++;
            setState(WakeWordState::ERROR);
            if (_onError) _onError(_lastError);
            return false;
        }
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
        setState(WakeWordState::INACTIVE);
        return enable(true);
    }
    return true;
}

void WakeWordManager::setMaxFramesPerUpdate(size_t maxFrames) {
    if (maxFrames == 0) {
        maxFrames = 1;
    }
    if (maxFrames > WakeWordAudioBridge::MAX_QUEUE_CAPACITY) {
        maxFrames = WakeWordAudioBridge::MAX_QUEUE_CAPACITY;
    }
    _maxFramesPerUpdate = maxFrames;
    _metrics.maxFramesPerUpdate = maxFrames;
}

void WakeWordManager::setProcessingBudgetUs(uint32_t budgetUs) {
    _processingBudgetUs = budgetUs;
    _metrics.processingBudgetUs = budgetUs;
}

void WakeWordManager::setTargetWakeWord(const String& word) {
    if (!word.isEmpty()) {
        _targetWakeWord = word;
        _wakeNetEngine.setTargetWakeWord(word);
        _testEngine.setTargetWakeWord(word);
        if (_customEngine) {
            _customEngine->setTargetWakeWord(word);
        }
        if (_selectedEngineType == WakeWordEngineType::WAKENET && !_wakeNetEngine.isPhraseConsistentWithModel()) {
            Serial.printf("[WakeWord] AVERTISSEMENT : wakeWordPhrase modifiee ('%s') alors que le modele acoustique actif reste '%s' (« Bonjour ESP »). Modifier wakeWordPhrase n'entraine pas l'apprentissage d'une nouvelle phrase.\n",
                          _targetWakeWord.c_str(), _wakeNetEngine.getModelName());
        }
    }
}

bool WakeWordManager::isWakeWordPhraseConsistentWithModel() const {
    if (_selectedEngineType == WakeWordEngineType::WAKENET) {
        return _wakeNetEngine.isPhraseConsistentWithModel();
    }
    return true;
}

bool WakeWordManager::setWakeWordModelName(const String& modelName) {
    String clean = modelName;
    clean.trim();
    if (clean.isEmpty()) {
        clean = WakeNetEngine::DEFAULT_MODEL_NAME;
    }

    if (_state.load(std::memory_order_acquire) == WakeWordState::ARMED || _localCaptureStartedByWake) {
        _bridge.setEnabled(false);
        if (!stopCaptureIfNeeded()) {
            _lastError = (_streamer && strlen(_streamer->getLastError()) > 0)
                ? String(_streamer->getLastError())
                : String("Echec arret capture locale microphone lors du changement de modele WakeNet");
            _metrics.errorCount++;
            setState(WakeWordState::ERROR);
            if (_onError) _onError(_lastError);
            return false;
        }
    }

    if (_activeEngine && _activeEngine->isInitialized()) {
        _activeEngine->end();
    }

    _configuredModelName = clean;
    _wakeNetEngine.setModelName(_configuredModelName);
    if (_customEngine) {
        _customEngine->setModelName(_configuredModelName);
    }

    if (_enabled.load(std::memory_order_acquire)) {
        setState(WakeWordState::INACTIVE);
        return enable(true);
    }
    return true;
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
    return _configuredModelName.c_str();
}

const char* WakeWordManager::getResolvedModelName() const {
    if (_selectedEngineType == WakeWordEngineType::WAKENET) {
        return _wakeNetEngine.getResolvedModelName();
    }
    return getModelName();
}

bool WakeWordManager::isEngineCompiledSupportAvailable() const {
    return _activeEngine != nullptr && _activeEngine->isCompiledSupportAvailable();
}

bool WakeWordManager::isEngineModelAvailable() const {
    return _activeEngine != nullptr && _activeEngine->isModelAvailable();
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

bool WakeWordManager::stopCaptureIfNeeded() {
    _deferredCaptureStopPending = false;
    if (!_useLocalMicCapture || !_streamer) {
        _localCaptureStartedByWake = false;
        return true;
    }
    _streamer->checkQuarantinedTasks();
    if (_localCaptureStartedByWake || _streamer->isLocalCapture()) {
        if (_streamer->isLocalCapture() || _streamer->isTaskQuarantined() ||
            (!_streamer->isStreaming() && HardwareManager::getInstance().isMicCaptureReserved())) {
            bool ok = _streamer->stopLocalCapture();
            if (!ok) {
                _metrics.captureStopFailures++;
                return false;
            }
        }
        _localCaptureStartedByWake = false;
    }
    return true;
}

bool WakeWordManager::ensureCaptureStopped() {
    if (!stopCaptureIfNeeded()) {
        _lastError = (_streamer && strlen(_streamer->getLastError()) > 0)
            ? String(_streamer->getLastError())
            : String("Echec arret capture locale microphone apres detection Wake Word");
        _metrics.errorCount++;
        setState(WakeWordState::ERROR);
        if (_onError) {
            _onError(_lastError);
        }
        return false;
    }
    return true;
}

bool WakeWordManager::enable(bool en) {
    if (!en) {
        disable();
        return !isError();
    }

    uint32_t t0Us = micros();
    WakeWordState curState = _state.load(std::memory_order_acquire);

    // Idempotence sur appels répétés à enable(true) lorsque déjà armé et opérationnel (Section 5 — v0.6.2.1)
    if (_enabled.load(std::memory_order_acquire) && isEngineInitialized()) {
        if (curState == WakeWordState::ARMED && _bridge.isEnabled()) {
            recordReactivationDurationUs(micros() - t0Us);
            return true;
        }
        if (curState == WakeWordState::SUSPENDED) {
            bool voiceBusy = (_voiceBusyCheck && _voiceBusyCheck()) || _wasVoiceBusy;
            if (!voiceBusy) {
                return resume();
            }
            recordReactivationDurationUs(micros() - t0Us);
            return true;
        }
    }

    // Si en état ERROR, passer d'abord proprement par INACTIVE après vérification de l'arrêt du producteur audio
    if (curState == WakeWordState::ERROR) {
        _bridge.setEnabled(false);
        if (!stopCaptureIfNeeded()) {
            _lastError = (_streamer && strlen(_streamer->getLastError()) > 0)
                ? String(_streamer->getLastError())
                : String("Impossible de reinitialiser : producteur audio precedent non arrete");
            _metrics.errorCount++;
            if (_onError) _onError(_lastError);
            recordReactivationDurationUs(micros() - t0Us);
            return false;
        }
        if (_activeEngine && _activeEngine->isInitialized()) {
            _activeEngine->end();
        }
        setState(WakeWordState::INACTIVE);
    }

    _enabled.store(true, std::memory_order_release);
    _lastError = "";
    _voiceActivitySeenSinceSuspend = false;
    _resumePendingAfterBusy = false;

    setState(WakeWordState::INITIALIZING);
    selectActiveEnginePointer();

    // Initialisation du pont audio borné
    if (!_bridge.begin(WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY)) {
        _lastError = "Echec allocation memoire WakeWordAudioBridge";
        _metrics.errorCount++;
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        recordReactivationDurationUs(micros() - t0Us);
        return false;
    }
    _metrics.queueCapacity = _bridge.getCapacity();

    if (!_activeEngine) {
        _lastError = "Aucun moteur Wake Word configure (NONE)";
        _metrics.errorCount++;
        _bridge.setEnabled(false);
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        recordReactivationDurationUs(micros() - t0Us);
        return false;
    }

    // Vérification stricte en 4 étapes avant utilisation du modèle (Section 3 — v0.6.3.1) :
    // Étape 1 : Vérifier le support WakeNet / moteur compilé
    if (!_activeEngine->isCompiledSupportAvailable()) {
        _lastError = _activeEngine->getLastError();
        if (_lastError.isEmpty()) {
            _lastError = "Support WakeNet non compile dans cet environnement";
        }
        _metrics.errorCount++;
        _bridge.setEnabled(false);
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        recordReactivationDurationUs(micros() - t0Us);
        return false;
    }

    // Étape 2 : Vérifier la présence du modèle dans la partition Flash (sans jamais remplacer par TEST)
    if (!_activeEngine->isModelAvailable() || !_activeEngine->isAvailable()) {
        _lastError = _activeEngine->getLastError();
        if (_lastError.isEmpty()) {
            _lastError = "Modele WakeNet absent de la partition Flash";
        }
        _metrics.errorCount++;
        _bridge.setEnabled(false);
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        recordReactivationDurationUs(micros() - t0Us);
        return false;
    }

    // Étape 3 : Vérifier son initialisation (PCM 16 000 Hz mono 16 bits)
    if (_activeEngine->isInitialized()) {
        _activeEngine->end();
    }
    if (!_activeEngine->begin(16000, 1) || !_activeEngine->isInitialized()) {
        _lastError = _activeEngine->getLastError();
        if (_lastError.isEmpty()) {
            _lastError = "Echec initialisation begin() du moteur Wake Word";
        }
        _metrics.errorCount++;
        _bridge.setEnabled(false);
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        recordReactivationDurationUs(micros() - t0Us);
        return false;
    }

    // Si une activité vocale (TTS, STT, Conversation) est en cours, démarrer en état SUSPENDED
    bool voiceBusy = (_voiceBusyCheck && _voiceBusyCheck()) || _wasVoiceBusy;
    if (voiceBusy) {
        _bridge.setEnabled(false);
        _bridge.clear();
        _voiceActivitySeenSinceSuspend = true;
        setState(WakeWordState::SUSPENDED);
        refreshMemoryMetrics();
        recordReactivationDurationUs(micros() - t0Us);
        return true;
    }

    // Démarrage de la capture locale PCM 16 kHz et armement du détecteur
    _bridge.clear();
    _bridge.setEnabled(true);
    if (!startCaptureIfNeeded()) {
        _bridge.setEnabled(false);
        _activeEngine->end();
        _lastError = _streamer ? _streamer->getLastError() : "Echec demarrage capture locale microphone";
        if (_lastError.isEmpty()) {
            _lastError = "Echec demarrage capture locale microphone ES7210";
        }
        _metrics.errorCount++;
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        recordReactivationDurationUs(micros() - t0Us);
        return false;
    }

    setState(WakeWordState::ARMED);

    // Étape 4 : Vérifier que la reconnaissance acoustique (ou écoute moteur) est réellement active
    if (!isListening() || (!_activeEngine->isSimulatedEngine() && !isRealRecognitionActive())) {
        _bridge.setEnabled(false);
        stopCaptureIfNeeded();
        _activeEngine->end();
        _lastError = "La reconnaissance acoustique Wake Word n'est pas active apres armement";
        _metrics.errorCount++;
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        recordReactivationDurationUs(micros() - t0Us);
        return false;
    }

    refreshMemoryMetrics();
    recordReactivationDurationUs(micros() - t0Us);
    return true;
}

void WakeWordManager::disable() {
    WakeWordState prevState = _state.load(std::memory_order_acquire);
    bool wasEnabled = _enabled.exchange(false, std::memory_order_acq_rel);
    _resumePendingAfterBusy = false;
    _voiceActivitySeenSinceSuspend = false;
    _hasPendingEvent.store(false, std::memory_order_release);

    _bridge.setEnabled(false);
    if (!stopCaptureIfNeeded()) {
        // Règle Section 5 : Si l'arrêt du producteur audio échoue, ne JAMAIS détruire la mémoire
        // (_bridge.end() / _activeEngine->end()) encore susceptible d'être utilisée par la tâche audio.
        _lastError = (_streamer && strlen(_streamer->getLastError()) > 0)
            ? String(_streamer->getLastError())
            : String("Echec arret capture locale microphone lors de la desactivation");
        _metrics.errorCount++;
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        return;
    }

    _bridge.end();

    if (_activeEngine && _activeEngine->isInitialized()) {
        _activeEngine->end();
    }

    if (prevState != WakeWordState::INACTIVE || wasEnabled) {
        setState(WakeWordState::INACTIVE);
    }
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
        if (_deferredCaptureStopPending) {
            return ensureCaptureStopped();
        }
        return true;
    }

    _bridge.setEnabled(false);
    if (!stopCaptureIfNeeded()) {
        _lastError = (_streamer && strlen(_streamer->getLastError()) > 0)
            ? String(_streamer->getLastError())
            : String("Echec arret capture locale microphone lors de la suspension");
        _metrics.errorCount++;
        setState(WakeWordState::ERROR);
        if (_onError) _onError(_lastError);
        return false;
    }
    _bridge.clear();
    if (_activeEngine) {
        _activeEngine->reset();
    }
    _resumePendingAfterBusy = false;
    setState(WakeWordState::SUSPENDED);
    return true;
}

bool WakeWordManager::resume() {
    uint32_t t0Us = micros();
    if (!_enabled.load(std::memory_order_acquire)) {
        return false;
    }
    WakeWordState cur = _state.load(std::memory_order_acquire);
    if (cur == WakeWordState::ARMED) {
        recordReactivationDurationUs(micros() - t0Us);
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
        recordReactivationDurationUs(micros() - t0Us);
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
        recordReactivationDurationUs(micros() - t0Us);
        return false;
    }

    _voiceActivitySeenSinceSuspend = false;
    _resumePendingAfterBusy = false;
    setState(WakeWordState::ARMED);
    recordReactivationDurationUs(micros() - t0Us);
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
    bool activeNow = isVoiceBusy || isSpeaking || (_voiceBusyCheck && _voiceBusyCheck());

    if (activeNow) {
        _lastVoiceActivityEndMs = millis();
        if (_lastVoiceActivityEndMs == 0) _lastVoiceActivityEndMs = 1;

        if (_enabled.load(std::memory_order_acquire)) {
            WakeWordState cur = _state.load(std::memory_order_acquire);
            if (cur == WakeWordState::ARMED) {
                // Suspension automatique immédiate pour éviter tout conflit I2S ou faux réveil haut-parleur
                if (suspend()) {
                    _voiceActivitySeenSinceSuspend = true;
                }
            } else if (cur == WakeWordState::DETECTED) {
                // Si une activité vocale (listen/say/Conversation) est déclenchée directement depuis
                // le callback onWakeWordDetected, basculer immédiatement de DETECTED à SUSPENDED
                _voiceActivitySeenSinceSuspend = true;
                setState(WakeWordState::SUSPENDED);
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
    if (_inUpdate || !_enabled.load(std::memory_order_acquire)) {
        return;
    }

    const uint32_t updateStartUs = micros();
    struct UpdateReentrancyGuard {
        bool& flag;
        WakeWordManager* self;
        uint32_t startUs;
        UpdateReentrancyGuard(bool& f, WakeWordManager* s, uint32_t t0) : flag(f), self(s), startUs(t0) { flag = true; }
        ~UpdateReentrancyGuard() {
            flag = false;
            if (self) {
                self->recordUpdateTotalDurationUs(micros() - startUs);
            }
        }
    } updateGuard(_inUpdate, this, updateStartUs);

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

    // 2. Dépilage et traitement borné des trames PCM 16 kHz hors du callback audio critique (Section 3 — v0.6.2.1)
    size_t maxFrames = _maxFramesPerUpdate > 0 ? _maxFramesPerUpdate : 1;
    size_t processedThisTick = 0;
    uint32_t updateProcDurUs = 0;
    bool budgetExceededThisTick = false;

    auto handleDetectionResult = [&](const WakeWordResult& res) {
        // Couper immédiatement le pont audio avant d'invoquer les callbacks
        _bridge.setEnabled(false);
        if (_deferCaptureStopOnDetection && _useLocalMicCapture && _streamer) {
            // En mode WakeWordConversation (v0.6.3.1), signaler immédiatement l'arrêt asynchrone à live_mic_task
            // sans cumuler l'attente de terminaison matérielle I2S dans le même cycle update() que l'inférence WakeNet.
            // L'arrêt matériel complet est garanti au début du tick suivant (WAKE_DETECTED -> ensureCaptureStopped()).
            _streamer->requestStopLocalCaptureAsync();
            _deferredCaptureStopPending = true;
        } else if (!stopCaptureIfNeeded()) {
            _lastError = (_streamer && strlen(_streamer->getLastError()) > 0)
                ? String(_streamer->getLastError())
                : String("Echec arret capture locale microphone apres detection Wake Word");
            _metrics.errorCount++;
            setState(WakeWordState::ERROR);
            if (_onError) {
                _onError(_lastError);
            }
            return;
        }
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
        if (_internalEventHook && _enabled.load(std::memory_order_acquire)) {
            _internalEventHook(_pendingEvent);
        }
        setState(WakeWordState::DETECTED);

        // Vérifier que le callback onStateChanged(DETECTED) n'a pas désactivé le gestionnaire
        if (_enabled.load(std::memory_order_acquire) &&
            _state.load(std::memory_order_acquire) != WakeWordState::INACTIVE &&
            _state.load(std::memory_order_acquire) != WakeWordState::ERROR) {
            if (_onDetected) {
                _onDetected(detectedWord);
            }
            if (_onEvent && _enabled.load(std::memory_order_acquire)) {
                _onEvent(_pendingEvent);
            }
        }

        // Transition 2 : DETECTED -> SUSPENDED (si un callback n'a pas déjà changé l'état vers DISABLED, SUSPENDED ou ARMED)
        if (_state.load(std::memory_order_acquire) == WakeWordState::DETECTED) {
            _voiceActivitySeenSinceSuspend = _wasVoiceBusy || (_voiceBusyCheck && _voiceBusyCheck());
            setState(WakeWordState::SUSPENDED);
        }
    };

    while (processedThisTick < maxFrames && _state.load(std::memory_order_acquire) == WakeWordState::ARMED) {
        const WakeWordAudioBridge::FrameSlot* slot = _bridge.peekFrame();
        if (!slot) {
            break;
        }

        const uint32_t procStartUs = micros();
        WakeWordResult res = _activeEngine->process(slot->samples, slot->sampleCount, slot->rms);
        const uint32_t elapsedUs = micros() - procStartUs;
        const uint32_t procDurUs = std::max(elapsedUs, res.processingTimeUs);

        // Libération du slot dans la file bornée uniquement APRÈS achèvement complet de process()
        _bridge.consumeFrame();
        processedThisTick++;
        updateProcDurUs += procDurUs;

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
            handleDetectionResult(res);
            break;
        }

        // 4. Contrôle du budget temporel par appel update() (Section 3 — v0.6.2.1)
        if (_processingBudgetUs > 0 && updateProcDurUs >= _processingBudgetUs) {
            if (procDurUs > _processingBudgetUs || updateProcDurUs > _processingBudgetUs) {
                if (!budgetExceededThisTick) {
                    _metrics.processingBudgetExceeded++;
                    budgetExceededThisTick = true;
                }
            }
            break;
        }
    }

    // Si un déclenchement de test explicite est en attente alors qu'aucune trame DMA n'était dans le pont
    // (ex: test sans flux micro actif), exécuter le déclenchement sans polluer WakeWordAudioBridge ni le RMS acquis.
    if (processedThisTick == 0 &&
        _state.load(std::memory_order_acquire) == WakeWordState::ARMED &&
        _activeEngine &&
        _activeEngine->getType() == WakeWordEngineType::TEST &&
        _testEngine.isTriggerPending()) {
        const uint32_t procStartUs = micros();
        WakeWordResult res = _testEngine.process(nullptr, 0, 0);
        const uint32_t elapsedUs = static_cast<uint32_t>(micros() - procStartUs);
        const uint32_t procDurUs = std::max(elapsedUs, res.processingTimeUs);
        updateProcDurUs += procDurUs;
        _metrics.lastWakeProcessDurationUs = procDurUs;
        if (procDurUs > _metrics.maxWakeProcessDurationUs) {
            _metrics.maxWakeProcessDurationUs = procDurUs;
        }
        if (res.detected) {
            handleDetectionResult(res);
        }
    }

    if (processedThisTick > 0 || updateProcDurUs > 0) {
        _metrics.lastFramesProcessedInUpdate = processedThisTick;
        _metrics.lastUpdateProcessDurationUs = updateProcDurUs;
        if (updateProcDurUs > _metrics.maxUpdateProcessDurationUs) {
            _metrics.maxUpdateProcessDurationUs = updateProcDurUs;
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

    return _testEngine.requestTrigger(customWord.isEmpty() ? _targetWakeWord : customWord);
}

bool WakeWordManager::handleSerialCommand(const String& cmd) {
    if (!_enabled.load(std::memory_order_acquire) ||
        _state.load(std::memory_order_acquire) != WakeWordState::ARMED ||
        !_activeEngine ||
        _activeEngine->getType() != WakeWordEngineType::TEST) {
        return false;
    }
    return _testEngine.handleSerialCommand(cmd);
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
    _metrics.lastRms = _bridge.getLastRms();
    _metrics.peakRms = _bridge.getPeakRms();
    _metrics.maxFramesPerUpdate = _maxFramesPerUpdate;
    _metrics.processingBudgetUs = _processingBudgetUs;
}

const WakeWordMetrics& WakeWordManager::getMetrics() const {
    refreshMemoryMetrics();
    return _metrics;
}

void WakeWordManager::resetMetrics() {
    _bridge.resetMetrics();
    _metrics.reset();
    _hasArmedOnce = (_state.load(std::memory_order_acquire) == WakeWordState::ARMED);
    refreshMemoryMetrics();
}
