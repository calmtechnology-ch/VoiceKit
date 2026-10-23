#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <atomic>
#include "hal/IAudioOutput.h"

#if defined(ESP32) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

// Constantes de référence pipeline audio VoxCore v0.2.1.3.6
#ifndef LIVE_AUDIO_SAMPLE_RATE_RX
#define LIVE_AUDIO_SAMPLE_RATE_RX 24000
#endif

#ifndef LIVE_AUDIO_RX_BUFFER_SAMPLES
#define LIVE_AUDIO_RX_BUFFER_SAMPLES 120000 // 5.0 secondes à 24 kHz
#endif

#ifndef VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
#define VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK 1
#endif

#ifndef VOXCORE_AUDIO_MODE_DEDICATED_TASK
#define VOXCORE_AUDIO_MODE_DEDICATED_TASK    2
#endif

#ifndef VOXCORE_AUDIO_MODE
#if defined(ESP32) || defined(ARDUINO)
#define VOXCORE_AUDIO_MODE VOXCORE_AUDIO_MODE_DEDICATED_TASK
#else
#define VOXCORE_AUDIO_MODE VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
#endif
#endif

#ifndef VOXCORE_AUDIO_CHANNEL_DEFAULT
#define VOXCORE_AUDIO_CHANNEL_DEFAULT 0
#endif

/**
 * @brief Restitution audio PCM 24 kHz native Gemini Live.
 * Issu de l'architecture stabilisée VoxCore v0.2.1.3.6 / v0.2.2.0.
 * - Réception et mise en tampon circulaire PCM 24 kHz mono 16 bits en PSRAM (120 000 échantillons = 5,0 s)
 * - Tâche FreeRTOS dédiée audioPlaybackTask (Mode 2) sur Core 1, prio 3
 * - Quadruple buffer DMA (4 x 40 ms = 160 ms de marge matérielle)
 * - Régulation de flux par backpressure préventive et découpage par tranches sans perte
 * - Notification asynchrone M5Unified par setBufferReleaseCallback
 * - Attente de vidage matériel complet (allBuffersFree + getHardwareDrainMs) avant onPlaybackFinished
 * - Zéro coupure de fin de phrase
 */
class LiveAudioPlayer {
public:
    using Callback = std::function<void()>;

    explicit LiveAudioPlayer(IAudioOutput* output = nullptr);
    ~LiveAudioPlayer();

    bool begin(size_t customCapacity = 0);

    // Réception et mise en tampon d'un bloc d'échantillons PCM 24 kHz
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

    // États & Diagnostics
    bool isPlaying() const { return _isPlaying; }
    bool isTurnComplete() const { return _turnComplete; }
    bool isBuffering() const { return _isBuffering; }
    bool hasHardwareError() const { return _hardwareError; }
    void clearHardwareError() { _hardwareError = false; }
    void setPlaybackAllowed(bool allowed);
    bool isPlaybackAllowed() const { return _playbackAllowed; }

    size_t getBufferedSamples() const;
    size_t getBufferCapacity() const { return _bufferCapacity; }
    uint32_t getSamplesReceivedTotal() const { return _samplesReceivedTotal; }
    uint32_t getSamplesAcceptedBuffer() const { return _samplesAcceptedBuffer; }
    uint32_t getSamplesPlayedTotal() const { return _samplesPlayedTotal; }
    uint32_t getUnderflowCount() const { return _underflowCount; }
    uint32_t getOverflowDroppedSamples() const { return _overflowDroppedSamples; }
    size_t getMaxBufferedSamples() const { return _maxBufferedSamples; }

    double getDurationReceivedSec() const { return (double)_samplesReceivedTotal / LIVE_AUDIO_SAMPLE_RATE_RX; }
    double getDurationPlayedSec() const { return (double)_samplesPlayedTotal / LIVE_AUDIO_SAMPLE_RATE_RX; }
    double getConservationRate() const {
        return (_samplesReceivedTotal > 0) ? (100.0 * _samplesPlayedTotal / _samplesReceivedTotal) : 100.0;
    }

    bool checkBufferIntegrity() const;
    int32_t getBufferIntegrityDiscrepancy() const;

    // États des tampons DMA
    enum class DmaBufferState : uint8_t {
        FREE = 0,
        RESERVED = 1,
        IN_USE = 2
    };

    DmaBufferState getDmaBufferState(size_t index) const {
        return (index < DMA_BUFFER_COUNT) ? _dmaBufferStates[index].load(std::memory_order_relaxed) : DmaBufferState::FREE;
    }
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

    void printDiagnosticSummary() const;
    bool startPlaybackHardware();

    void setAudioOutput(IAudioOutput* output);
    IAudioOutput* getAudioOutput() const { return _audioOutput; }

    static constexpr size_t DMA_CHUNK_SAMPLES = 960;      // 40 ms à 24 kHz
    static constexpr size_t DMA_BUFFER_COUNT = 4;         // Quadruple tampon DMA (4x40 ms = 160 ms de marge)
    static constexpr size_t JITTER_START_SAMPLES = 4800;  // 200 ms à 24 kHz
    static constexpr size_t JITTER_RESUME_SAMPLES = 2400; // 100 ms à 24 kHz

#if !defined(ESP32) && !defined(ARDUINO)
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
#endif

private:
    IAudioOutput* _audioOutput = nullptr;
    int16_t* _rxPcmBuffer = nullptr;
    size_t _bufferCapacity = LIVE_AUDIO_RX_BUFFER_SAMPLES;
    size_t _writeIndex = 0;
    size_t _readIndex = 0;
    size_t _availableSamples = 0;

    int16_t _dmaBuffers[DMA_BUFFER_COUNT][DMA_CHUNK_SAMPLES];
    std::atomic<DmaBufferState> _dmaBufferStates[DMA_BUFFER_COUNT];
    uint32_t _dmaBufferGeneration[DMA_BUFFER_COUNT];
    size_t _dmaIndex = 0;
    std::atomic<uint32_t> _turnGeneration{0};

    std::atomic<bool> _isPlaying{false};
    std::atomic<bool> _turnComplete{false};
    std::atomic<bool> _isBuffering{false};
    std::atomic<bool> _hardwareError{false};
    std::atomic<bool> _playbackAllowed{true};
    std::atomic<bool> _isSubmitting{false};
    bool _firstAudioNotified = false;
    unsigned _volume = 65;

    Callback _onFirstAudio;
    Callback _onPlaybackFinished;

#if defined(ESP32) || defined(ARDUINO)
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t _audioTaskHandle = nullptr;
    std::atomic<bool> _audioTaskRunning{false};
    std::atomic<bool> _audioTaskStopRequested{false};
#endif

    // Métriques et traçabilité
    uint32_t _chunksReceived = 0;
    uint32_t _underflowCount = 0;
    uint32_t _samplesReceivedTotal = 0;
    uint32_t _samplesAcceptedBuffer = 0;
    uint32_t _samplesPlayedTotal = 0;
    uint32_t _playRawRefusedCount = 0;
    uint32_t _playRawSuccessCount = 0;
    uint32_t _overflowCount = 0;
    uint32_t _overflowDroppedSamples = 0;
    size_t _maxBufferedSamples = 0;
    size_t _minBufferedSamples = 0;
    bool _playbackEverStarted = false;

    uint32_t _hardwareDrainStartTimeMs = 0;
    bool _playbackFinishedNotified = false;
    uint32_t _turnCompleteTimeMs = 0;
    uint32_t _playbackStartTimeMs = 0;
    uint32_t _playbackEndTimeMs = 0;
    uint32_t _playbackDurationMs = 0;
    uint32_t _isPlayingZeroTimeMs = 0;
    uint32_t _playbackFinishedTimeMs = 0;
    bool _bufferEmptyLogged = false;
    bool _isPlayingZeroLogged = false;

    uint32_t _backpressureWaitCount = 0;
    uint32_t _backpressureWaitMsTotal = 0;
    uint32_t _samplesCancelledTotal = 0;
    uint32_t _samplesPresentedToPlayRaw = 0;
    uint32_t _bufferReleaseCallbackCount = 0;
    uint32_t _prematureReuseAvoidedCount = 0;
    uint32_t _reactiveRefillCount = 0;

#if !defined(ESP32) && !defined(ARDUINO)
    MockPlayRawHook _mockPlayRawHook;
    int _simSpeakerPlayingChunks = 0;
    std::vector<int16_t> recordedPlayedSamples;
    bool mockPlayRaw(const int16_t* data, size_t length);
#endif
};
