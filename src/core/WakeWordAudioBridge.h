#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <memory>
#include <functional>
#include "IWakeWordEngine.h"
#include "VoiceHubConfig.h"

/**
 * @class WakeWordAudioBridge
 * @brief Pont audio temps réel non bloquant entre la capture PCM 16 kHz de VoiceKit
 *        et le détecteur de mot de réveil (VoiceKit v0.6.2).
 *
 * Garanties temps réel :
 * - Allocation unique hors boucle audio (lors de begin()), libérée dans end() lorsque désactivé.
 * - Zéro allocation dynamique et zéro écriture série dans pushAudioChunk() (contexte tâche audio).
 * - File circulaire SPSC (Single-Producer Single-Consumer) bornée.
 * - Une seule copie mémoire (memcpy DMA -> slot du pont) ; lecture directe sans copie supplémentaire
 *   côté consommateur via peekFrame() / consumeFrame().
 * - Conservation stricte de l'ordre séquentiel des trames et des échantillons PCM 16 kHz 16 bits.
 * - Politique explicite de saturation (DROP_NEWEST par défaut ou DROP_OLDEST) avec compteurs atomiques.
 */
class WakeWordAudioBridge {
public:
    static constexpr size_t MAX_FRAME_SAMPLES = AUDIO_CHUNK_SAMPLES; // 512 échantillons (32 ms @ 16 kHz)
    static constexpr size_t DEFAULT_QUEUE_CAPACITY = 8;              // 8 blocs = 256 ms (8 Ko)
    static constexpr size_t MAX_QUEUE_CAPACITY = 32;

    struct FrameSlot {
        int16_t samples[MAX_FRAME_SAMPLES];
        size_t sampleCount = 0;
        uint32_t rms = 0;
        uint32_t sequence = 0;
        uint32_t timestampMs = 0;
    };

    WakeWordAudioBridge();
    ~WakeWordAudioBridge();

    WakeWordAudioBridge(const WakeWordAudioBridge&) = delete;
    WakeWordAudioBridge& operator=(const WakeWordAudioBridge&) = delete;

    /**
     * @brief Alloue la file bornée et prépare le pont audio.
     * @param capacity Nombre maximal de trames dans la file (défaut : 8).
     * @return true si l'initialisation a réussi.
     */
    bool begin(size_t capacity = DEFAULT_QUEUE_CAPACITY);

    /**
     * @brief Désactive le pont et libère la mémoire des tampons (coût minimal au repos).
     */
    void end();

    /**
     * @brief Vide la file sans désallouer les tampons.
     */
    void clear();

    /**
     * @brief Réinitialise tous les compteurs diagnostiques et vide la file.
     */
    void resetMetrics();

    /**
     * @brief Active ou désactive l'acceptation des trames dans pushAudioChunk().
     */
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled.load(std::memory_order_acquire); }
    bool isInitialized() const { return _initialized.load(std::memory_order_acquire); }

    /**
     * @brief Configure la politique en cas de saturation de la file bornée.
     *
     * Note v0.6.2.1 : Seule la politique `DROP_NEWEST` est autorisée afin de garantir
     * que le producteur ne modifie jamais `_tail` et n'invalide jamais un pointeur
     * retourné par `peekFrame()` pendant `IWakeWordEngine::process()`.
     * Tout appel avec `DROP_OLDEST` maintient `DROP_NEWEST` et incrémente le compteur
     * de rejets de politique non sécurisée.
     */
    void setOverflowPolicy(WakeWordOverflowPolicy policy) {
        if (policy == WakeWordOverflowPolicy::DROP_OLDEST) {
            _unsupportedPolicyRequests.fetch_add(1, std::memory_order_relaxed);
            _overflowPolicy.store(WakeWordOverflowPolicy::DROP_NEWEST, std::memory_order_relaxed);
            return;
        }
        _overflowPolicy.store(WakeWordOverflowPolicy::DROP_NEWEST, std::memory_order_relaxed);
    }
    WakeWordOverflowPolicy getOverflowPolicy() const {
        return _overflowPolicy.load(std::memory_order_relaxed);
    }
    static bool isOverflowPolicySupported(WakeWordOverflowPolicy policy) {
        return policy == WakeWordOverflowPolicy::DROP_NEWEST;
    }
    uint32_t getUnsupportedPolicyRequestCount() const {
        return _unsupportedPolicyRequests.load(std::memory_order_relaxed);
    }

    /**
     * @brief Pousse une trame PCM 16 kHz depuis le callback audio critique (non bloquant).
     *        Effectue au plus 1 copie mémoire (memcpy) vers le slot préalloué.
     * @param samples Échantillons PCM mono 16 kHz signé 16 bits.
     * @param sampleCount Nombre d'échantillons (1 .. MAX_FRAME_SAMPLES).
     * @param rms Niveau RMS du bloc.
     * @return true si la trame a été enfilée, false si ignorée ou abandonnée (file pleine).
     */
    bool pushAudioChunk(const int16_t* samples, size_t sampleCount, uint32_t rms);

    /**
     * @brief Consulte la trame la plus ancienne en tête de file sans copie mémoire supplémentaire.
     *        À utiliser depuis la tâche principale (update()) avant d'appeler consumeFrame().
     * @return Pointeur constant vers le FrameSlot en tête, ou nullptr si la file est vide.
     */
    const FrameSlot* peekFrame() const;

    /**
     * @brief Libère la trame consultée via peekFrame() et avance le pointeur de lecture FIFO.
     * @return true si une trame a été consommée, false si la file était vide.
     */
    bool consumeFrame();

    /**
     * @brief Dépile une trame en copiant ses échantillons dans un tampon appelant (optionnel).
     */
    bool popFrame(int16_t* outSamples, size_t maxSamples, size_t& outCount, uint32_t& outRms, uint32_t* outSeq = nullptr);

    // =========================================================================
    // Métriques et Diagnostics (consultables sans bloquer le traitement)
    // =========================================================================
    size_t getCapacity() const { return _capacity; }
    size_t getOccupancy() const;
    size_t getMaxOccupancy() const { return _maxOccupancy.load(std::memory_order_relaxed); }
    bool isFull() const { return getOccupancy() >= _capacity && _capacity > 0; }
    bool isEmpty() const { return getOccupancy() == 0; }

    uint32_t getFramesReceived() const { return _framesReceived.load(std::memory_order_relaxed); }
    uint32_t getFramesEnqueued() const { return _framesEnqueued.load(std::memory_order_relaxed); }
    uint32_t getFramesConsumed() const { return _framesConsumed.load(std::memory_order_relaxed); }
    uint32_t getFramesDropped() const { return _framesDropped.load(std::memory_order_relaxed); }
    uint32_t getQueueSaturationCount() const { return _queueSaturations.load(std::memory_order_relaxed); }
    uint32_t getLastPushDurationUs() const { return _lastPushDurationUs.load(std::memory_order_relaxed); }
    uint32_t getMaxPushDurationUs() const { return _maxPushDurationUs.load(std::memory_order_relaxed); }
    uint32_t getLastRms() const { return _lastRms.load(std::memory_order_relaxed); }
    uint32_t getPeakRms() const { return _peakRms.load(std::memory_order_relaxed); }
    uint32_t getLastSequenceEnqueued() const { return _lastSeqEnqueued.load(std::memory_order_relaxed); }
    uint32_t getLastSequenceConsumed() const { return _lastSeqConsumed.load(std::memory_order_relaxed); }
    uint32_t getCopyCountPerFrame() const { return 1; } // Exactement 1 copie (DMA -> slot pont), 0 copie au dépilage
    bool isFramePeeked() const { return _consumerPeeking.load(std::memory_order_acquire); }
    uint32_t getActiveWriters() const { return _activeWriters.load(std::memory_order_acquire); }

#if !defined(ESP32) && !defined(ARDUINO)
    using WriteHook = std::function<void()>;
    void setSimMidWriteHook(WriteHook hook) { _simMidWriteHook = std::move(hook); }
#endif

private:
    void waitForActiveWriters() const;

    std::unique_ptr<FrameSlot[]> _slots;
    size_t _capacity = 0;

    std::atomic<bool> _initialized{false};
    std::atomic<bool> _enabled{false};
    std::atomic<WakeWordOverflowPolicy> _overflowPolicy{WakeWordOverflowPolicy::DROP_NEWEST};
    std::atomic<uint32_t> _unsupportedPolicyRequests{0};
    std::atomic<uint32_t> _activeWriters{0};
    std::atomic<uint32_t> _epoch{0};
    mutable std::atomic<bool> _consumerPeeking{false};

#if !defined(ESP32) && !defined(ARDUINO)
    WriteHook _simMidWriteHook;
#endif

    std::atomic<size_t> _head{0};
    std::atomic<size_t> _tail{0};
    std::atomic<size_t> _maxOccupancy{0};

    std::atomic<uint32_t> _framesReceived{0};
    std::atomic<uint32_t> _framesEnqueued{0};
    std::atomic<uint32_t> _framesConsumed{0};
    std::atomic<uint32_t> _framesDropped{0};
    std::atomic<uint32_t> _queueSaturations{0};
    std::atomic<uint32_t> _lastPushDurationUs{0};
    std::atomic<uint32_t> _maxPushDurationUs{0};
    std::atomic<uint32_t> _lastRms{0};
    std::atomic<uint32_t> _peakRms{0};
    std::atomic<uint32_t> _nextSequence{1};
    std::atomic<uint32_t> _lastSeqEnqueued{0};
    std::atomic<uint32_t> _lastSeqConsumed{0};
};

