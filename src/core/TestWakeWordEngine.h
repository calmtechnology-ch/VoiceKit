#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <atomic>
#include "IWakeWordEngine.h"

/**
 * @class TestWakeWordEngine
 * @brief Moteur de test déterministe implémentant IWakeWordEngine (Section 9 — VoiceKit v0.6.2).
 *
 * Rôle :
 * - Valider la chaîne de capture et de transport audio PCM 16 kHz / 16 bits.
 * - Valider la machine à états (DISABLED -> INITIALIZING -> ARMED -> DETECTED -> SUSPENDED -> ARMED).
 * - Valider les callbacks, la suspension pendant conversation/TTS/STT et la reprise contrôlée.
 * - Valider la gestion des erreurs d'initialisation et d'exécution.
 * - Mesurer les performances temps réel sans dépendre d'un binaire ESP-SR externe.
 *
 * AVERTISSEMENT EXPLICITE :
 * Ce moteur ne réalise AUCUNE reconnaissance vocale acoustique. Le déclenchement
 * s'effectue uniquement par injection explicite (requestTrigger) ou commande série.
 * isSimulatedEngine() retourne toujours true et WakeWordResult::isSimulated vaut toujours true.
 */
class TestWakeWordEngine : public IWakeWordEngine {
public:
    TestWakeWordEngine() {
        strncpy(_lastError, "", sizeof(_lastError));
    }

    ~TestWakeWordEngine() override {
        end();
    }

    const char* getName() const override { return "TEST"; }
    WakeWordEngineType getType() const override { return WakeWordEngineType::TEST; }
    const char* getModelName() const override { return "test_simulated_trigger (non-acoustic)"; }

    bool isAvailable() const override {
        return !_simulateUnavailable.load(std::memory_order_relaxed);
    }

    bool isSimulatedEngine() const override {
        return true; // Toujours simulé : ne jamais présenter comme une vraie reconnaissance vocale
    }

    bool begin(uint32_t sampleRateHz = 16000, uint8_t channels = 1) override {
        if (_simulateUnavailable.load(std::memory_order_relaxed)) {
            strncpy(_lastError, "Moteur TEST marque indisponible (simulation)", sizeof(_lastError) - 1);
            _initialized.store(false, std::memory_order_release);
            return false;
        }
        if (_simulateInitFailure.load(std::memory_order_relaxed)) {
            strncpy(_lastError, "Echec initialisation simule sur TestWakeWordEngine", sizeof(_lastError) - 1);
            _initialized.store(false, std::memory_order_release);
            return false;
        }
        if (sampleRateHz != 16000 || channels != 1) {
            strncpy(_lastError, "Format audio invalide : PCM 16000 Hz mono requis", sizeof(_lastError) - 1);
            _initialized.store(false, std::memory_order_release);
            return false;
        }

        _sampleRateHz = sampleRateHz;
        _channels = channels;
        _triggerRequested.store(false, std::memory_order_relaxed);
        _framesProcessed.store(0, std::memory_order_relaxed);
        _samplesProcessed.store(0, std::memory_order_relaxed);
        _lastRms.store(0, std::memory_order_relaxed);
        _peakRms.store(0, std::memory_order_relaxed);
        _lastError[0] = '\0';
        _initialized.store(true, std::memory_order_release);
        return true;
    }

    void end() override {
        _initialized.store(false, std::memory_order_release);
        _triggerRequested.store(false, std::memory_order_relaxed);
    }

    bool isInitialized() const override {
        return _initialized.load(std::memory_order_acquire);
    }

    void reset() override {
        _triggerRequested.store(false, std::memory_order_relaxed);
        _pendingTriggerWord = "";
    }

    WakeWordResult process(const int16_t* samples, size_t sampleCount, uint32_t rms) override {
        const uint32_t startUs = micros();
        WakeWordResult res;
        res.isSimulated = true;

        if (!_initialized.load(std::memory_order_acquire)) {
            strncpy(_lastError, "TestWakeWordEngine non initialise", sizeof(_lastError) - 1);
            return res;
        }

        if (_simulateRuntimeError.load(std::memory_order_relaxed)) {
            strncpy(_lastError, "Erreur d'execution simulee dans TestWakeWordEngine::process", sizeof(_lastError) - 1);
            return res;
        }

        if (samples && sampleCount > 0) {
            _framesProcessed.fetch_add(1, std::memory_order_relaxed);
            _samplesProcessed.fetch_add((uint32_t)sampleCount, std::memory_order_relaxed);
            _lastRms.store(rms, std::memory_order_relaxed);
            uint32_t prevPeak = _peakRms.load(std::memory_order_relaxed);
            while (rms > prevPeak && !_peakRms.compare_exchange_weak(prevPeak, rms, std::memory_order_relaxed)) {}
            _lastFirstSample.store(samples[0], std::memory_order_relaxed);
            _lastEndSample.store(samples[sampleCount - 1], std::memory_order_relaxed);
        }

        if (_triggerRequested.exchange(false, std::memory_order_acq_rel)) {
            res.detected = true;
            res.word = _pendingTriggerWord.isEmpty() ? _targetWakeWord : _pendingTriggerWord;
            res.confidence = 1.0f;
            res.isSimulated = true;
            _pendingTriggerWord = "";
            _triggersFired.fetch_add(1, std::memory_order_relaxed);
        }

        uint32_t elapsedUs = micros() - startUs;
        uint32_t simDurUs = _simulatedProcessDurationUs.load(std::memory_order_relaxed);
        res.processingTimeUs = (simDurUs > elapsedUs) ? simDurUs : elapsedUs;
        return res;
    }

    void setTargetWakeWord(const String& word) override {
        if (!word.isEmpty()) {
            _targetWakeWord = word;
        }
    }

    String getTargetWakeWord() const override {
        return _targetWakeWord;
    }

    const char* getLastError() const override {
        return _lastError;
    }

    // =========================================================================
    // Méthodes spécifiques au moteur de test (Simulation & Injection)
    // =========================================================================

    /**
     * @brief Demande explicitement un déclenchement simulé du mot de réveil.
     * @param customWord Mot personnalisé optionnel (utilise getTargetWakeWord() si vide).
     * @return true si la demande est enregistrée (moteur initialisé), false sinon.
     */
    bool requestTrigger(const String& customWord = "") {
        if (!_initialized.load(std::memory_order_acquire)) {
            return false;
        }
        _pendingTriggerWord = customWord;
        _triggerRequested.store(true, std::memory_order_release);
        return true;
    }

    bool isTriggerPending() const {
        return _triggerRequested.load(std::memory_order_acquire);
    }

    /**
     * @brief Analyse une commande série pour déclencher un test ("wake", "bonjour", "bonjour esp", "w", "trigger").
     * @return true si la commande a déclenché une simulation de réveil.
     */
    bool handleSerialCommand(const String& rawCmd) {
        String cmd = rawCmd;
        cmd.trim();
        String lower = cmd;
        lower.toLowerCase();
        if (lower == "wake" || lower == "bonjour" || lower == "bonjour esp" || lower == "trigger" || lower == "detect") {
            return requestTrigger(_targetWakeWord);
        }
        return false;
    }

    // Injection de pannes pour validation de la machine à états (Section 9 & 13)
    void setSimulateUnavailable(bool unavail) { _simulateUnavailable.store(unavail, std::memory_order_relaxed); }
    void setSimulateInitFailure(bool fail) { _simulateInitFailure.store(fail, std::memory_order_relaxed); }
    void setSimulateRuntimeError(bool err) { _simulateRuntimeError.store(err, std::memory_order_relaxed); }
    bool isSimulatingRuntimeError() const { return _simulateRuntimeError.load(std::memory_order_relaxed); }
    void setSimulatedProcessDurationUs(uint32_t us) { _simulatedProcessDurationUs.store(us, std::memory_order_relaxed); }
    uint32_t getSimulatedProcessDurationUs() const { return _simulatedProcessDurationUs.load(std::memory_order_relaxed); }

    // Compteurs de vérification du transport audio
    uint32_t getFramesProcessed() const { return _framesProcessed.load(std::memory_order_relaxed); }
    uint32_t getSamplesProcessed() const { return _samplesProcessed.load(std::memory_order_relaxed); }
    uint32_t getTriggersFired() const { return _triggersFired.load(std::memory_order_relaxed); }
    uint32_t getLastRms() const { return _lastRms.load(std::memory_order_relaxed); }
    uint32_t getPeakRms() const { return _peakRms.load(std::memory_order_relaxed); }
    int16_t getLastFirstSample() const { return _lastFirstSample.load(std::memory_order_relaxed); }
    int16_t getLastEndSample() const { return _lastEndSample.load(std::memory_order_relaxed); }

private:
    String _targetWakeWord = "Bonjour ESP";
    String _pendingTriggerWord = "";
    uint32_t _sampleRateHz = 16000;
    uint8_t _channels = 1;
    char _lastError[128] = {};

    std::atomic<bool> _initialized{false};
    std::atomic<bool> _triggerRequested{false};
    std::atomic<bool> _simulateUnavailable{false};
    std::atomic<bool> _simulateInitFailure{false};
    std::atomic<bool> _simulateRuntimeError{false};
    std::atomic<uint32_t> _simulatedProcessDurationUs{0};

    std::atomic<uint32_t> _framesProcessed{0};
    std::atomic<uint32_t> _samplesProcessed{0};
    std::atomic<uint32_t> _triggersFired{0};
    std::atomic<uint32_t> _lastRms{0};
    std::atomic<uint32_t> _peakRms{0};
    std::atomic<int16_t> _lastFirstSample{0};
    std::atomic<int16_t> _lastEndSample{0};
};

