#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <atomic>
#include "VoiceHubConfig.h"
#include "VoiceHubTypes.h"
#include "hal/IAudioOutput.h"

#if defined(ESP32) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

// =============================================================================
// LiveAudioPlayer : Restitution audio PCM 24 kHz native Gemini Live
// VoxCore v0.1.0 (Protégé v0.4.1.3)
// - Réception et mise en tampon circulaire PCM 24 kHz mono 16 bits en PSRAM
//   (Capacité étendue à 120 000 échantillons = 5.0 secondes avec repli sécurisé)
// - Intégrité stricte du flux : interdiction absolue de l'écrasement des anciens
//   échantillons, préservation rigoureuse de l'ordre d'arrivée
// - Vérification de capacité disponible avant insertion ; rejet explicite et
//   comptabilisation sans corrompre les données existantes
// - Sécurisation de playRaw() : vérification systématique de l'acceptation par
//   la HAL IAudioOutput. Ne retire de la file que les blocs acceptés. En cas de refus,
//   conservation intacte du bloc pour nouvelle tentative au prochain cycle
// - Quadruple tampon DMA (4x40 ms = 160 ms de marge) garantissant la persistance
//   mémoire jusqu'à la libération matérielle par le DMA I2S
// - Démarrage fluide avec Jitter Buffer adapté (200 ms initial, 100 ms reprise)
// - Diagnostic complet par tour : échantillons reçus/acceptés/joués, refus playRaw,
//   overflow, underflow, pic d'occupation, durées et bilan série final
// - Protection thread-safe stricte entre websocket_task et tâche principale
// - Arrêt immédiat et vidage des tampons sur appui STOP
// =============================================================================

class LiveAudioPlayer {
public:
    using Callback = std::function<void()>;

    explicit LiveAudioPlayer(IAudioOutput* output = nullptr);
    ~LiveAudioPlayer();

    bool begin(size_t customCapacity = 0);

    // Réception et mise en tampon d'un bloc d'échantillons PCM 24 kHz
    // Retourne true si tous les échantillons ont été insérés sans perte, false si saturation
    bool feedPcm(const int16_t* samples, size_t sampleCount);

    // Signalement de la fin de génération du tour par Gemini Live
    void notifyTurnComplete();

    // Progression de la lecture (appelé dans tick)
    void tick();

    // Arrêt immédiat sur appui STOP
    void stop(AudioStopReason reason = AudioStopReason::USER_STOP);

    // Réinitialisation pour un nouveau tour
    void reset();

    // Contrôle du volume
    void setVolume(unsigned volumePercent);
    unsigned getVolume() const { return _volume; }

    // Callbacks
    void setOnFirstAudio(Callback cb) { _onFirstAudio = std::move(cb); }
    void setOnPlaybackFinished(Callback cb) { _onPlaybackFinished = std::move(cb); }

    // États & Métriques de diagnostic (v0.4.1.3, v0.1.0.2 & v0.1.0.3)
    bool isPlaying() const { return _isPlaying; }
    bool isTurnComplete() const { return _turnComplete; }
    bool isBuffering() const { return _isBuffering; }
    bool hasHardwareError() const { return _hardwareError; }
    void clearHardwareError() { _hardwareError = false; }
    void setSimulateHardwareError(bool err) { _hardwareError = err; }
    bool hasBufferOverflow() const { return _overflowCount > 0; }
    bool isPlaybackCompleteClean() const {
        return _turnComplete && (_availableSamples == 0) && (_overflowDroppedSamples == 0) && (_samplesPlayedTotal == _samplesReceivedTotal) && (_samplesCancelledTotal == 0);
    }
    size_t getBufferedSamples() const;
    size_t getBufferCapacity() const { return _bufferCapacity; }
    uint32_t getChunksReceived() const { return _chunksReceived; }
    uint32_t getUnderflowCount() const { return _underflowCount; }

    uint32_t getSamplesReceivedTotal() const { return _samplesReceivedTotal; }
    uint32_t getSamplesAcceptedBuffer() const { return _samplesAcceptedBuffer; }
    uint32_t getSamplesPlayedTotal() const { return _samplesPlayedTotal; }
    uint32_t getPlayRawRefusedCount() const { return _playRawRefusedCount; }
    uint32_t getPlayRawSuccessCount() const { return _playRawSuccessCount; }
    uint32_t getOverflowCount() const { return _overflowCount; }
    uint32_t getOverflowDroppedSamples() const { return _overflowDroppedSamples; }
    uint32_t getDroppedSamples() const { return _overflowDroppedSamples; }
    size_t getMaxBufferedSamples() const { return _maxBufferedSamples; }

    double getDurationReceivedSec() const { return (double)_samplesReceivedTotal / LIVE_AUDIO_SAMPLE_RATE_RX; }
    double getDurationPlayedSec() const { return (double)_samplesPlayedTotal / LIVE_AUDIO_SAMPLE_RATE_RX; }
    double getConservationRate() const {
        return (_samplesReceivedTotal > 0) ? (100.0 * _samplesPlayedTotal / _samplesReceivedTotal) : 100.0;
    }

    // Diagnostics de restitution v0.2.1.1 (Correction E) & v0.2.1.2 (Corrections A, C, D)
    size_t getMinBufferedSamples() const { return _minBufferedSamples; }
    uint32_t getMaxTickIntervalUs() const { return _maxTickIntervalUs; }
    uint32_t getMaxIncomingPcmIntervalUs() const { return _maxIncomingPcmIntervalUs; }
    uint32_t getRebufferingCount() const { return _rebufferingCount; }
    uint32_t getTotalRebufferingDurationMs() const { return _totalRebufferingDurationMs; }
    uint32_t getMaxRebufferingDurationMs() const { return _maxRebufferingDurationMs; }

    uint32_t getSamplesRemainingAtTurnComplete() const { return _samplesRemainingAtTurnComplete; }
    uint32_t getSamplesRemainingAtFinish() const { return _samplesRemainingAtFinish; }
    uint32_t getDurationTurnCompleteToFinishMs() const { return _durationTurnCompleteToFinishMs; }
    bool isPlaybackFinishedNotified() const { return _playbackFinishedNotified; }
    bool isTurnCompleteReceived() const { return _turnComplete; }
    uint32_t getHardwareDrainDurationMs() const { return _audioOutput ? _audioOutput->getHardwareDrainMs() : 0; }
    bool isHardwareDraining() const { return _hardwareDrainStartTimeMs > 0; }

    // Diagnostics CORRECTION C (v0.2.1.3) : Intégrité stricte du tampon PCM
    uint32_t getSamplesPresentedToPlayRaw() const { return _samplesPresentedToPlayRaw; }
    uint32_t getPlaybackFinishDeclarationsCount() const { return _playbackFinishDeclarationsCount; }
    uint32_t getSamplesCancelledTotal() const { return _samplesCancelledTotal; }
    bool checkBufferIntegrity() const;
    int32_t getBufferIntegrityDiscrepancy() const;

    // Diagnostics CORRECTION A & B (v0.2.1.3.1) : Débit réel, cadencement tick & backpressure
    uint32_t getTickCount() const { return _tickCount; }
    uint32_t getTickIntervalOver40msCount() const { return _tickIntervalOver40msCount; }
    uint32_t getTickIntervalOver80msCount() const { return _tickIntervalOver80msCount; }
    uint32_t getTickIntervalOver160msCount() const { return _tickIntervalOver160msCount; }
    uint32_t getTicksOver40msCount() const { return _tickIntervalOver40msCount; }
    uint32_t getTicksOver80msCount() const { return _tickIntervalOver80msCount; }
    uint32_t getTicksOver160msCount() const { return _tickIntervalOver160msCount; }
    uint32_t getPlaybackDurationMs() const { return _playbackDurationMs; }
    double getEffectiveConsumptionRate() const;
    double getEffectiveConsumptionRatePerSec() const { return getEffectiveConsumptionRate(); }
    float getMinConsumptionRatePerSec() const { return _minWindowConsumptionRate; }
    float getMaxConsumptionRatePerSec() const { return _maxWindowConsumptionRate; }
    float getAvgConsumptionRatePerSec() const;
    uint32_t getBackpressureWaitCount() const { return _backpressureWaitCount; }
    uint32_t getBackpressureWaitMsTotal() const { return _backpressureWaitMsTotal; }

    // Diagnostics de débit et de blocage (TTSTest v0.1.1)
    uint32_t getGeminiRxStartTimeMs() const { return _geminiRxStartTimeMs; }
    uint32_t getGeminiRxEndTimeMs() const { return _geminiRxEndTimeMs; }
    uint32_t getGeminiRxDurationMs() const {
        return (_geminiRxEndTimeMs >= _geminiRxStartTimeMs) ? (_geminiRxEndTimeMs - _geminiRxStartTimeMs) : 0;
    }
    double getGeminiRxRate() const {
        uint32_t dur = getGeminiRxDurationMs();
        return (dur > 0) ? ((double)_samplesReceivedTotal * 1000.0 / dur) : 0.0;
    }

    uint32_t getFirstPlayRawAcceptedTimeMs() const { return _firstPlayRawAcceptedTimeMs; }
    uint32_t getDriverSubmissionDurationMs() const {
        return (_lastPlayRawAcceptedTimeMs >= _firstPlayRawAcceptedTimeMs) ? (_lastPlayRawAcceptedTimeMs - _firstPlayRawAcceptedTimeMs) : 0;
    }
    double getDriverSubmissionRate() const {
        uint32_t dur = getDriverSubmissionDurationMs();
        return (dur > 0) ? ((double)_samplesPlayedTotal * 1000.0 / dur) : 0.0;
    }

    uint32_t getDriverQueueFullCount() const { return _driverQueueFullCount; }
    uint32_t getDriverQueueFullMsTotal() const { return _driverQueueFullMsTotal; }

    // Diagnostics de fin de restitution v0.2.1.3.5 (AUDIO END CHECK)
    uint32_t getLastPlayRawAcceptedTimeMs() const { return _lastPlayRawAcceptedTimeMs; }
    uint32_t getLastBufferReleasedTimeMs() const { return _lastBufferReleasedTimeMs; }
    uint32_t getIsPlayingZeroTimeMs() const { return _isPlayingZeroTimeMs; }
    uint32_t getPlaybackFinishedTimeMs() const { return _playbackFinishedTimeMs; }
    uint32_t getTurnCompleteTimeMs() const { return _turnCompleteTimeMs; }
    size_t getSamplesAcceptedPlayRaw() const { return _samplesPlayedTotal; }
    size_t getSamplesRemaining() const { return _availableSamples; }

    // Contrôle d'arbitrage microphone / haut-parleur (Correction F v0.2.1.3.1)
    void setPlaybackAllowed(bool allowed);
    bool isPlaybackAllowed() const { return _playbackAllowed; }

    // États des tampons DMA M5Unified (v0.2.1.3.4)
    enum class DmaBufferState : uint8_t {
        FREE = 0,
        RESERVED = 1,
        IN_USE = 2
    };
    static constexpr DmaBufferState SUBMITTED = DmaBufferState::RESERVED;

    // Diagnostics v0.2.1.3.4 : Cycle de vie des buffers DMA & Mode de restitution
    DmaBufferState getDmaBufferState(size_t index) const {
        return (index < DMA_BUFFER_COUNT) ? _dmaBufferStates[index].load(std::memory_order_relaxed) : DmaBufferState::FREE;
    }
    uint32_t getBufferReleaseCallbackCount() const { return _bufferReleaseCallbackCount; }
    uint32_t getPrematureReuseAvoidedCount() const { return _prematureReuseAvoidedCount; }
    uint32_t getReactiveRefillCount() const { return _reactiveRefillCount; }
    uint32_t getTurnGeneration() const { return _turnGeneration.load(std::memory_order_relaxed); }
    static constexpr int getCompiledAudioMode() { return VOXCORE_AUDIO_MODE; }

    // Callback de libération de buffer M5Unified
    static void onBufferReleaseStatic(void* args, const void* data, uint8_t channel);
    void handleBufferRelease(const void* data, uint8_t channel);
    bool submitNextChunk();
    void wakeAudioTask();

#if defined(ESP32) || defined(ARDUINO)
    static void audioPlaybackTaskEntry(void* arg);
    void audioPlaybackTaskLoop();
#endif

    // Affiche le bilan de diagnostic dans le moniteur série
    void printDiagnosticSummary() const;

    // Démarrage sécurisé du matériel de restitution (v0.1.0.2)
    bool startPlaybackHardware();

    // Injection HAL
    void setAudioOutput(IAudioOutput* output);
    IAudioOutput* getAudioOutput() const { return _audioOutput; }

    // Paramètres audio haute fluidité
    static constexpr size_t DMA_CHUNK_SAMPLES = 960;      // 40 ms à 24 kHz
    static constexpr size_t DMA_BUFFER_COUNT = 4;         // Quadruple tampon DMA (4x40 ms = 160 ms de marge)
    static constexpr size_t JITTER_START_SAMPLES = 4800;  // 200 ms à 24 kHz
    static constexpr size_t JITTER_RESUME_SAMPLES = 2400; // 100 ms à 24 kHz

#if !defined(ESP32) && !defined(ARDUINO)
    // Hooks de simulation pour les tests unitaires natifs
    using MockPlayRawHook = std::function<bool(const int16_t* data, size_t length)>;
    void setMockPlayRawHook(MockPlayRawHook hook) { _mockPlayRawHook = std::move(hook); }
    void setSimSpeakerPlayingChunks(int count) { _simSpeakerPlayingChunks = count; }
    int getSimSpeakerPlayingChunks() const { return _simSpeakerPlayingChunks; }
    void simFinishSpeakerChunk() { 
        if (_simSpeakerPlayingChunks > 0) _simSpeakerPlayingChunks--; 
        for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
            if (_dmaBufferStates[i].load() == DmaBufferState::IN_USE) {
                _dmaBufferStates[i].store(DmaBufferState::FREE);
                break;
            }
        }
    }
    void clearRecordedPlayedSamples() { recordedPlayedSamples.clear(); }
    const std::vector<int16_t>& getRecordedPlayedSamples() const { return recordedPlayedSamples; }
#endif

private:
    IAudioOutput* _audioOutput = nullptr;

    int16_t* _rxPcmBuffer = nullptr;
    size_t _bufferCapacity = LIVE_AUDIO_RX_BUFFER_SAMPLES;
    size_t _writeIndex = 0;
    size_t _readIndex = 0;
    size_t _availableSamples = 0;

    bool _isPlaying = false;
    bool _isBuffering = false;
    bool _hardwareError = false;
    bool _turnComplete = false;
    bool _firstAudioNotified = false;

    uint32_t _chunksReceived = 0;
    uint32_t _underflowCount = 0;
    unsigned _volume = DEFAULT_SPEAKER_VOLUME;

    // Compteurs de diagnostic v0.4.1.3
    uint32_t _samplesReceivedTotal = 0;
    uint32_t _samplesAcceptedBuffer = 0;
    uint32_t _samplesPlayedTotal = 0;
    uint32_t _playRawRefusedCount = 0;
    uint32_t _playRawSuccessCount = 0;
    uint32_t _overflowCount = 0;
    uint32_t _overflowDroppedSamples = 0;
    size_t _maxBufferedSamples = 0;

    // Quadruple tampon pour transmission au codec sans écrasement mémoire
    int16_t _dmaBuffers[DMA_BUFFER_COUNT][DMA_CHUNK_SAMPLES] = {};
    size_t _dmaIndex = 0;
    std::atomic<DmaBufferState> _dmaBufferStates[DMA_BUFFER_COUNT];
    uint32_t _dmaBufferGeneration[DMA_BUFFER_COUNT] = {};
    std::atomic<uint32_t> _turnGeneration{0};

    std::atomic<bool> _isSubmitting{false};
    uint32_t _bufferReleaseCallbackCount = 0;
    uint32_t _prematureReuseAvoidedCount = 0;
    uint32_t _reactiveRefillCount = 0;

#if defined(ESP32) || defined(ARDUINO)
    TaskHandle_t _audioTaskHandle = nullptr;
    std::atomic<bool> _audioTaskRunning{false};
    std::atomic<bool> _audioTaskStopRequested{false};
#endif

    // Métriques de fluidité et de continuité de restitution v0.2.1.1 (Correction E)
    size_t _minBufferedSamples = 0;
    uint32_t _lastTickTimeUs = 0;
    uint32_t _maxTickIntervalUs = 0;
    uint32_t _lastIncomingPcmTimeUs = 0;
    uint32_t _maxIncomingPcmIntervalUs = 0;
    uint32_t _rebufferingCount = 0;
    uint32_t _rebufferingStartTimeMs = 0;
    uint32_t _totalRebufferingDurationMs = 0;
    uint32_t _maxRebufferingDurationMs = 0;
    bool _playbackEverStarted = false;

    // Synchronisation matérielle et fin de restitution v0.2.1.2 (Corrections A, C, D)
    uint32_t _hardwareDrainStartTimeMs = 0;
    bool _playbackFinishedNotified = false;
    uint32_t _turnCompleteTimeMs = 0;
    uint32_t _samplesRemainingAtTurnComplete = 0;
    uint32_t _samplesRemainingAtFinish = 0;
    uint32_t _durationTurnCompleteToFinishMs = 0;

    // Métriques et traçabilité v0.2.1.3 (Corrections C & E) & v0.2.1.3.5 (AUDIO END CHECK)
    uint32_t _samplesPresentedToPlayRaw = 0;
    uint32_t _playbackFinishDeclarationsCount = 0;
    uint32_t _samplesCancelledTotal = 0;
    bool _bufferEmptyLogged = false;
    bool _isPlayingZeroLogged = false;
    uint32_t _lastPlayRawAcceptedTimeMs = 0;
    uint32_t _lastBufferReleasedTimeMs = 0;
    uint32_t _isPlayingZeroTimeMs = 0;
    uint32_t _playbackFinishedTimeMs = 0;

    // Métriques et régulation v0.2.1.3.1 (Corrections A, B, D, F)
    bool _playbackAllowed = true;
    uint32_t _tickCount = 0;
    uint32_t _tickIntervalOver40msCount = 0;
    uint32_t _tickIntervalOver80msCount = 0;
    uint32_t _tickIntervalOver160msCount = 0;
    uint32_t _playbackStartTimeMs = 0;
    uint32_t _playbackEndTimeMs = 0;
    uint32_t _playbackDurationMs = 0;

    static constexpr uint32_t RATE_WINDOW_MS = 500;
    uint32_t _lastWindowTimeMs = 0;
    uint32_t _windowSamplesPlayed = 0;
    uint32_t _windowSamplesReceived = 0;
    uint32_t _windowSamplesAccepted = 0;
    float _minWindowConsumptionRate = 0.0f;
    float _maxWindowConsumptionRate = 0.0f;
    double _sumWindowRates = 0.0;
    uint32_t _rateWindowsCount = 0;

    uint32_t _backpressureWaitCount = 0;
    uint32_t _backpressureWaitMsTotal = 0;

    // Diagnostics de débit et de blocage (TTSTest v0.1.1)
    uint32_t _geminiRxStartTimeMs = 0;
    uint32_t _geminiRxEndTimeMs = 0;
    uint32_t _firstPlayRawAcceptedTimeMs = 0;
    uint32_t _driverQueueFullCount = 0;
    uint32_t _driverQueueFullMsTotal = 0;

#if defined(ESP32) || defined(ARDUINO)
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
#else
    MockPlayRawHook _mockPlayRawHook;
    int _simSpeakerPlayingChunks = 0;
    std::vector<int16_t> recordedPlayedSamples;
    bool mockPlayRaw(const int16_t* data, size_t length);
#endif

    Callback _onFirstAudio;
    Callback _onPlaybackFinished;
};
