#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include "VoiceHubConfig.h"
#include "VoiceHubTypes.h"
#include "GeminiLiveClient.h"
#include "core/IVoiceProvider.h"
#include "hal/IAudioInput.h"
#include "hal/IAudioOutput.h"

#include <functional>

#if defined(ESP32) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#else
#include <queue>
#include <mutex>
#endif

// Primitives de synchronisation FreeRTOS pour le cycle de vie des tâches micro et émission (v0.1.0.3 & v0.2.1.1)
static constexpr uint32_t LIVE_MIC_TASK_STARTED_BIT    = (1 << 0);
static constexpr uint32_t LIVE_MIC_TASK_START_FAIL_BIT = (1 << 1);
static constexpr uint32_t LIVE_MIC_TASK_STOPPED_BIT    = (1 << 2);
static constexpr uint32_t LIVE_TX_TASK_STARTED_BIT     = (1 << 3);
static constexpr uint32_t LIVE_TX_TASK_START_FAIL_BIT  = (1 << 4);
static constexpr uint32_t LIVE_TX_TASK_STOPPED_BIT     = (1 << 5);

// Tailles de pile FreeRTOS pour le streaming audio (v0.5.3.1 - Fix Stack Canary Overflow)
static constexpr uint32_t LIVE_MIC_TASK_STACK_SIZE    = 6144;
static constexpr uint32_t LIVE_TX_TASK_STACK_SIZE     = 10240; // 10 Ko pour absorber msgBuf Base64 et la pile reseau TLS

// =============================================================================
// LiveAudioStreamer : Capture progressive et streaming audio micro PCM 16 kHz
// VoxCore v0.2.1.1 (Embedded Voice Application Framework)
// - Format : PCM 16 kHz mono 16 bits signé, Little-endian
// - Blocs de 512 échantillons (~32 ms)
// - Double-buffering / synchronisation stricte HAL IAudioInput (aucun tampon partiel)
// - Découplage complet : tâche d'acquisition micro FreeRTOS + tâche d'émission TX dédiée
// - Protocole officiel Gemini Live : activityStart -> audio -> activityEnd
// - Diagnostics audio : blocs acquis/terminés/envoyés, durée réelle/théorique, RMS
// =============================================================================

enum class LiveBufferState : uint8_t {
    FREE = 0,
    RECORDING,
    READY,
    SENDING
};

inline const char* liveBufferStateToString(LiveBufferState s) {
    switch (s) {
        case LiveBufferState::FREE:      return "FREE";
        case LiveBufferState::RECORDING: return "RECORDING";
        case LiveBufferState::READY:     return "READY";
        case LiveBufferState::SENDING:   return "SENDING";
        default:                         return "UNKNOWN";
    }
}

// Diagnostic précis des causes de défaillance TX (v0.2.1.1 - Correction C)
enum class LiveTxStatus : uint8_t {
    OK = 0,
    TX_QUEUE_FULL,
    TX_SEND_TIMEOUT,
    TX_SEND_FAILED,
    TX_SLOT_UNAVAILABLE,
    TX_STOP_REQUESTED
};

inline const char* liveTxStatusToString(LiveTxStatus s) {
    switch (s) {
        case LiveTxStatus::OK:                 return "OK";
        case LiveTxStatus::TX_QUEUE_FULL:       return "TX_QUEUE_FULL";
        case LiveTxStatus::TX_SEND_TIMEOUT:     return "TX_SEND_TIMEOUT";
        case LiveTxStatus::TX_SEND_FAILED:      return "TX_SEND_FAILED";
        case LiveTxStatus::TX_SLOT_UNAVAILABLE: return "TX_SLOT_UNAVAILABLE";
        case LiveTxStatus::TX_STOP_REQUESTED:   return "TX_STOP_REQUESTED";
        default:                               return "UNKNOWN";
    }
}

class LiveAudioStreamer {
public:
    explicit LiveAudioStreamer(IAudioInput* input = nullptr, IAudioOutput* output = nullptr);
    ~LiveAudioStreamer();

    bool begin();

    // Démarrage de la capture et du streaming sur appui PARLER (v0.6.0 IVoiceProvider)
    bool start(IVoiceProvider* provider);

    // Surcharge de compatibilité ascendante avec GeminiLiveClient (v0.5.x)
    bool start(GeminiLiveClient* client);

    // Progression de la transmission réseau (dépile les blocs complétés)
    bool tick();

    // Arrêt normal sur relâchement du bouton PARLER (termine le bloc en cours, transmet, envoie activityEnd)
    bool stop();

    // Annulation immédiate sur appui STOP (abandon propre sans émission activityEnd)
    void cancel();

    // Capture locale hors ligne pour détection de mot de réveil (v0.6.2 — sans fournisseur réseau ni tâche TX)
    bool startLocalCapture();
    bool stopLocalCapture();
    bool isLocalCapture() const { return _isLocalCapture; }

    // États & Métriques
    bool isStreaming() const { return _isStreaming; }
    bool isQueueSaturated() const { return _queueSaturated; }
    uint32_t getChunksSent() const { return _chunksSent; }
    uint32_t getBytesSent() const { return _bytesSent; }
    uint32_t getDurationMs() const;
    uint32_t getTheoreticalDurationMs() const {
        return (_chunksSent * AUDIO_CHUNK_SAMPLES * 1000UL) / AUDIO_SAMPLE_RATE_HZ;
    }
    size_t getQueueSize() const;
    const char* getLastError() const { return _lastError; }
    AudioHardwareStatus getLastStatus() const { return _lastStatus; }

    // Diagnostics v0.4.1.2 & v0.2.1.1 (Correction A)
    uint32_t getBlocksAcquired() const { return _blocksAcquired; }
    uint32_t getBlocksProduced() const { return _blocksAcquired; }
    uint32_t getBlocksCompleted() const { return _blocksCompleted; }
    uint32_t getBlocksSent() const { return _chunksSent; }
    uint32_t getBlocksDropped() const { return _blocksDropped; }
    uint32_t getSendRefusedCount() const { return _sendRefusedCount; }
    uint32_t getAverageRms() const { return _rmsCount > 0 ? (uint32_t)(_sumRms / _rmsCount) : 0; }
    uint32_t getPeakRms() const { return _peakRms; }
    uint32_t getLastChunkRms() const { return _lastChunkRms; }

    // Callback léger sur chaque bloc audio acquis (pour VAD, visualiseur ou métriques)
    using AudioChunkCallback = std::function<void(const int16_t* samples, size_t count, uint32_t rms)>;
    void onAudioChunk(AudioChunkCallback cb) { _chunkCallback = std::move(cb); }
    void setWakeWordAudioCallback(AudioChunkCallback cb) { _wakeWordChunkCallback = std::move(cb); }

    bool isActivityStartConfirmed() const { return _activityStartSent; }
    bool isActivityEndConfirmed() const { return _activityEndSent; }
    bool isActivityEndSent() const { return _activityEndSent; }
    uint32_t getMicStoppedMs() const { return _micStoppedMs; }
    uint32_t getActivityEndSentMs() const { return _activityEndSentMs; }
    bool isMicTaskRunning() const { return _micTaskRunning; }
    bool isTxTaskRunning() const { return _txTaskRunning; }
    bool isTaskQuarantined() const { return _taskQuarantined || _txTaskQuarantined; }
    bool isTxTaskQuarantined() const { return _txTaskQuarantined; }
    void* getMicTaskHandle() const {
#if defined(ESP32) || defined(ARDUINO)
        return (void*)_micTaskHandle;
#else
        return _micTaskHandle;
#endif
    }
    void* getTxTaskHandle() const {
#if defined(ESP32) || defined(ARDUINO)
        return (void*)_txTaskHandle;
#else
        return _txTaskHandle;
#endif
    }

    // Métriques temporelles et saturations TX (v0.2.1.1)
    size_t getMaxQueueOccupancy() const { return _maxQueueOccupancy; }
    uint32_t getLastSendDurationUs() const { return _lastSendDurationUs; }
    uint32_t getMaxSendDurationUs() const { return _maxSendDurationUs; }
    uint32_t getSendsOver32msCount() const { return _sendsOver32msCount; }
    uint32_t getSendsOver100msCount() const { return _sendsOver100msCount; }
    uint32_t getMaxConsumerIntervalUs() const { return _maxConsumerIntervalUs; }
    uint32_t getQueueSaturationCount() const { return _queueSaturationCount; }
    LiveTxStatus getLastTxStatus() const { return _lastTxStatus; }

    // Bilan textuel structuré des diagnostics TX
    void printDiagnosticSummary() const;

    // Gestion explicite des états de tampons (v0.4.1.2)
    LiveBufferState getBufferState(size_t index) const {
        if (index < LIVE_AUDIO_TX_QUEUE_MAX) return _ringBuffers[index].state;
        return LiveBufferState::FREE;
    }
    bool areAllBuffersFree() const {
        for (size_t i = 0; i < LIVE_AUDIO_TX_QUEUE_MAX; ++i) {
            if (_ringBuffers[i].state != LiveBufferState::FREE) return false;
        }
        return true;
    }

    // Contrôle et levée de quarantaine (v0.2.1.2 - Correction E)
    bool checkQuarantinedTasks();

    // Injection HAL
    void setAudioInput(IAudioInput* input) { _audioInput = input; }
    void setAudioOutput(IAudioOutput* output) { _audioOutput = output; }

#if !defined(ESP32) && !defined(ARDUINO)
    // Méthodes d'injection et de simulation pour tests unitaires natifs (v0.1.0.3 & v0.2.1.1 & v0.2.1.2)
    bool pushSimulatedChunk(const int16_t* samples, size_t count);
    using TaskCreateHook = std::function<bool(LiveAudioStreamer*)>;
    void setSimTaskCreateHook(TaskCreateHook hook) { _simTaskCreateHook = std::move(hook); }
    void setSimStartupFailure(bool fail) { _simStartupFailure = fail; }
    void setSimTerminationTimeout(bool timeout) { 
        _simTerminationTimeout = timeout; 
        _simMicTerminationTimeout = timeout;
        _simTxTerminationTimeout = timeout;
    }
    void setSimMicTerminationTimeout(bool timeout) { _simMicTerminationTimeout = timeout; }
    void setSimTxTerminationTimeout(bool timeout) { _simTxTerminationTimeout = timeout; }
    void setSimTaskRunning(bool running) {
        _micTaskRunning = running;
        _txTaskRunning = running;
        if (running && _micTaskHandle == nullptr) {
            _micTaskHandle = (void*)0x1234;
            _txTaskHandle = (void*)0x5678;
        } else if (!running) {
            _micTaskHandle = nullptr;
            _txTaskHandle = nullptr;
        }
    }
    void setSimTxTaskRunning(bool running) {
        _txTaskRunning = running;
        if (running && _txTaskHandle == nullptr) {
            _txTaskHandle = (void*)0x5678;
        } else if (!running) {
            _txTaskHandle = nullptr;
        }
    }
    void setSimBufferState(size_t index, LiveBufferState state) {
        if (index < LIVE_AUDIO_TX_QUEUE_MAX) {
            _ringBuffers[index].state = state;
        }
    }
#endif

    // Délai de terminaison TX basé sur le comportement réel du transport WebSocket (1000 ms socket timeout + 200 ms marge)
    static constexpr uint32_t LIVE_TX_TERMINATION_TIMEOUT_MS = 1200;

    // Tolérance de saturation : abandon borné avant arrêt critique
    static constexpr size_t MAX_CONSECUTIVE_DROPPED_BLOCKS = 4;


private:
    bool stopMicHardware();
    bool waitForTaskTermination(uint32_t timeoutMs);
    bool waitForTxTaskTermination(uint32_t timeoutMs);
    uint32_t computeRms(const int16_t* samples, size_t count);

    IAudioInput* _audioInput = nullptr;
    IAudioOutput* _audioOutput = nullptr;

    IVoiceProvider* _provider = nullptr;
    GeminiLiveClient* _legacyClient = nullptr;
    std::unique_ptr<IVoiceProvider> _legacyAdapter;
    volatile bool _isStreaming = false;
    volatile bool _isLocalCapture = false;
    volatile bool _stopDrainRequested = false;
    bool _queueSaturated = false;
    uint32_t _startTimeMs = 0;
    uint32_t _durationMs = 0;
    uint32_t _chunksSent = 0;
    uint32_t _bytesSent = 0;
    char _lastError[64] = {};

    // Diagnostics audio
    uint32_t _blocksAcquired = 0;
    uint32_t _blocksCompleted = 0;
    uint32_t _blocksDropped = 0;
    uint64_t _sumRms = 0;
    uint32_t _rmsCount = 0;
    uint32_t _peakRms = 0;
    volatile uint32_t _lastChunkRms = 0;
    AudioChunkCallback _chunkCallback = nullptr;
    AudioChunkCallback _wakeWordChunkCallback = nullptr;
    bool _activityStartSent = false;
    bool _activityEndSent = false;
    uint32_t _micStoppedMs = 0;
    uint32_t _activityEndSentMs = 0;

    // Métriques avancées TX v0.2.1.1 (Correction A & C)
    volatile uint32_t _sendRefusedCount = 0;
    volatile size_t _maxQueueOccupancy = 0;
    volatile uint32_t _lastSendDurationUs = 0;
    volatile uint32_t _maxSendDurationUs = 0;
    volatile uint32_t _sendsOver32msCount = 0;
    volatile uint32_t _sendsOver100msCount = 0;
    volatile uint32_t _maxConsumerIntervalUs = 0;
    volatile uint32_t _lastConsumerPassUs = 0;
    volatile uint32_t _queueSaturationCount = 0;
    volatile size_t _consecutiveDroppedBlocks = 0;
    volatile LiveTxStatus _lastTxStatus = LiveTxStatus::OK;
    uint32_t _lastPeriodicLogMs = 0;

    // Tampons bornés circulaires pour double/multi-buffering DMA avec états explicites (v0.4.1.2)
    struct ChunkBuffer {
        int16_t samples[AUDIO_CHUNK_SAMPLES];
        volatile LiveBufferState state = LiveBufferState::FREE;
    };
    ChunkBuffer _ringBuffers[LIVE_AUDIO_TX_QUEUE_MAX];

#if defined(ESP32) || defined(ARDUINO)
    TaskHandle_t _micTaskHandle = nullptr;
    volatile bool _micTaskRunning = false;
    TaskHandle_t _txTaskHandle = nullptr;
    volatile bool _txTaskRunning = false;
    bool _txTaskQuarantined = false;
    QueueHandle_t _txQueue = nullptr;
    EventGroupHandle_t _taskEventGroup = nullptr;
    static void micTaskEntry(void* param);
    void micTaskLoop();
    static void txTaskEntry(void* param);
    void txTaskLoop();
#else
    void* _micTaskHandle = nullptr;
    volatile bool _micTaskRunning = false;
    void* _txTaskHandle = nullptr;
    volatile bool _txTaskRunning = false;
    bool _txTaskQuarantined = false;
    std::queue<size_t> _nativeTxQueue;
    TaskCreateHook _simTaskCreateHook;
    bool _simStartupFailure = false;
    bool _simTerminationTimeout = false;
    bool _simMicTerminationTimeout = false;
    bool _simTxTerminationTimeout = false;
#endif
    bool _taskQuarantined = false;
    AudioHardwareStatus _lastStatus = AudioHardwareStatus::SUCCESS;
};
