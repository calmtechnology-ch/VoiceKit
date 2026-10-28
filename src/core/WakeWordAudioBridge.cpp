#include "WakeWordAudioBridge.h"
#include <cstring>

WakeWordAudioBridge::WakeWordAudioBridge() = default;

WakeWordAudioBridge::~WakeWordAudioBridge() {
    end();
}

void WakeWordAudioBridge::waitForActiveWriters() const {
    uint32_t spins = 0;
    while (_activeWriters.load(std::memory_order_acquire) > 0 && spins < 2000) {
        spins++;
    }
}

void WakeWordAudioBridge::setEnabled(bool enabled) {
    if (!enabled) {
        _enabled.store(false, std::memory_order_release);
        _epoch.fetch_add(1, std::memory_order_acq_rel);
        waitForActiveWriters();
    } else {
        _epoch.fetch_add(1, std::memory_order_acq_rel);
        _enabled.store(true, std::memory_order_release);
    }
}

bool WakeWordAudioBridge::begin(size_t capacity) {
    if (capacity == 0) {
        capacity = DEFAULT_QUEUE_CAPACITY;
    }
    if (capacity > MAX_QUEUE_CAPACITY) {
        capacity = MAX_QUEUE_CAPACITY;
    }

    if (_initialized.load(std::memory_order_acquire) && _capacity == capacity && _slots != nullptr) {
        clear();
        return true;
    }

    _enabled.store(false, std::memory_order_release);
    _initialized.store(false, std::memory_order_release);
    _epoch.fetch_add(1, std::memory_order_acq_rel);
    waitForActiveWriters();
    _consumerPeeking.store(false, std::memory_order_release);

    std::unique_ptr<FrameSlot[]> newSlots(new (std::nothrow) FrameSlot[capacity]);
    if (!newSlots) {
        _capacity = 0;
        return false;
    }

    for (size_t i = 0; i < capacity; ++i) {
        memset(newSlots[i].samples, 0, sizeof(newSlots[i].samples));
        newSlots[i].sampleCount = 0;
        newSlots[i].rms = 0;
        newSlots[i].sequence = 0;
        newSlots[i].timestampMs = 0;
    }

    _slots = std::move(newSlots);
    _capacity = capacity;
    _head.store(0, std::memory_order_release);
    _tail.store(0, std::memory_order_release);
    _initialized.store(true, std::memory_order_release);
    return true;
}

void WakeWordAudioBridge::end() {
    _enabled.store(false, std::memory_order_release);
    _initialized.store(false, std::memory_order_release);
    _epoch.fetch_add(1, std::memory_order_acq_rel);
    waitForActiveWriters();
    _consumerPeeking.store(false, std::memory_order_release);

    // Sécurité v0.6.2.1 : ne détruire la mémoire des tampons que si aucun producteur n'est en cours d'écriture
    if (_activeWriters.load(std::memory_order_acquire) == 0) {
        _head.store(0, std::memory_order_release);
        _tail.store(0, std::memory_order_release);
        _slots.reset();
        _capacity = 0;
    }
}

void WakeWordAudioBridge::clear() {
    _epoch.fetch_add(1, std::memory_order_acq_rel);
    waitForActiveWriters();
    _consumerPeeking.store(false, std::memory_order_release);
    size_t head = _head.load(std::memory_order_acquire);
    _tail.store(head, std::memory_order_release);
}

void WakeWordAudioBridge::resetMetrics() {
    clear();
    _maxOccupancy.store(0, std::memory_order_relaxed);
    _framesReceived.store(0, std::memory_order_relaxed);
    _framesEnqueued.store(0, std::memory_order_relaxed);
    _framesConsumed.store(0, std::memory_order_relaxed);
    _framesDropped.store(0, std::memory_order_relaxed);
    _queueSaturations.store(0, std::memory_order_relaxed);
    _unsupportedPolicyRequests.store(0, std::memory_order_relaxed);
    _lastPushDurationUs.store(0, std::memory_order_relaxed);
    _maxPushDurationUs.store(0, std::memory_order_relaxed);
    _lastRms.store(0, std::memory_order_relaxed);
    _peakRms.store(0, std::memory_order_relaxed);
    _nextSequence.store(1, std::memory_order_relaxed);
    _lastSeqEnqueued.store(0, std::memory_order_relaxed);
    _lastSeqConsumed.store(0, std::memory_order_relaxed);
}

size_t WakeWordAudioBridge::getOccupancy() const {
    if (!_initialized.load(std::memory_order_acquire) || _capacity == 0) {
        return 0;
    }
    size_t head = _head.load(std::memory_order_acquire);
    size_t tail = _tail.load(std::memory_order_acquire);
    return (head >= tail) ? (head - tail) : 0;
}

bool WakeWordAudioBridge::pushAudioChunk(const int16_t* samples, size_t sampleCount, uint32_t rms) {
    struct WriterGuard {
        std::atomic<uint32_t>& count;
        explicit WriterGuard(std::atomic<uint32_t>& c) : count(c) {
            count.fetch_add(1, std::memory_order_acq_rel);
        }
        ~WriterGuard() {
            count.fetch_sub(1, std::memory_order_acq_rel);
        }
    } guard(_activeWriters);

    const uint32_t startEpoch = _epoch.load(std::memory_order_acquire);
    if (!_enabled.load(std::memory_order_acquire) || !_initialized.load(std::memory_order_acquire) || !_slots || _capacity == 0) {
        return false;
    }

    const uint32_t startUs = micros();
    _framesReceived.fetch_add(1, std::memory_order_relaxed);

    if (!samples || sampleCount == 0 || sampleCount > MAX_FRAME_SAMPLES) {
        _framesDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    _lastRms.store(rms, std::memory_order_relaxed);
    uint32_t prevPeak = _peakRms.load(std::memory_order_relaxed);
    while (rms > prevPeak && !_peakRms.compare_exchange_weak(prevPeak, rms, std::memory_order_relaxed)) {}

    size_t head = _head.load(std::memory_order_relaxed);
    size_t tail = _tail.load(std::memory_order_acquire);
    size_t used = (head >= tail) ? (head - tail) : 0;

    if (used >= _capacity) {
        // File bornée saturée : application stricte de DROP_NEWEST (v0.6.2.1).
        // Le producteur ne modifie JAMAIS _tail et n'écrase jamais un slot non consommé
        // afin de protéger la durée de vie de peekFrame() pendant IWakeWordEngine::process().
        _queueSaturations.fetch_add(1, std::memory_order_relaxed);
        _framesDropped.fetch_add(1, std::memory_order_relaxed);

        const uint32_t durUs = micros() - startUs;
        _lastPushDurationUs.store(durUs, std::memory_order_relaxed);
        uint32_t prevMax = _maxPushDurationUs.load(std::memory_order_relaxed);
        while (durUs > prevMax && !_maxPushDurationUs.compare_exchange_weak(prevMax, durUs, std::memory_order_relaxed)) {}
        return false;
    }

    // Garantie SPSC : used < _capacity implique (head % _capacity) != (tail % _capacity).
    // Le slot ciblé est strictement hors de la plage [tail, head) détenue par le consommateur.
    FrameSlot& slot = _slots[head % _capacity];
    memcpy(slot.samples, samples, sampleCount * sizeof(int16_t));
    slot.sampleCount = sampleCount;
    slot.rms = rms;
    slot.timestampMs = millis();

#if !defined(ESP32) && !defined(ARDUINO)
    if (_simMidWriteHook) {
        _simMidWriteHook();
    }
#endif

    // Si une suspension (setEnabled(false)), une vidange (clear()) ou un arrêt (end())
    // est intervenu pendant la copie mémoire, abandonner la publication de la trame
    // pour qu'aucune trame partielle ou post-suspension ne soit visible par le consommateur.
    if (!_enabled.load(std::memory_order_acquire) ||
        !_initialized.load(std::memory_order_acquire) ||
        _epoch.load(std::memory_order_acquire) != startEpoch) {
        _framesDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    uint32_t seq = _nextSequence.fetch_add(1, std::memory_order_relaxed);
    slot.sequence = seq;
    _lastSeqEnqueued.store(seq, std::memory_order_relaxed);

    // Publication atomique (release) uniquement après écriture intégrale des échantillons et métadonnées
    _head.store(head + 1, std::memory_order_release);
    _framesEnqueued.fetch_add(1, std::memory_order_relaxed);

    size_t newOccupancy = used + 1;
    size_t prevMaxOcc = _maxOccupancy.load(std::memory_order_relaxed);
    while (newOccupancy > prevMaxOcc && !_maxOccupancy.compare_exchange_weak(prevMaxOcc, newOccupancy, std::memory_order_relaxed)) {}

    const uint32_t durUs = micros() - startUs;
    _lastPushDurationUs.store(durUs, std::memory_order_relaxed);
    uint32_t prevMax = _maxPushDurationUs.load(std::memory_order_relaxed);
    while (durUs > prevMax && !_maxPushDurationUs.compare_exchange_weak(prevMax, durUs, std::memory_order_relaxed)) {}

    return true;
}

const WakeWordAudioBridge::FrameSlot* WakeWordAudioBridge::peekFrame() const {
    if (!_initialized.load(std::memory_order_acquire) || !_slots || _capacity == 0) {
        _consumerPeeking.store(false, std::memory_order_release);
        return nullptr;
    }
    size_t head = _head.load(std::memory_order_acquire);
    size_t tail = _tail.load(std::memory_order_relaxed);
    if (tail >= head) {
        _consumerPeeking.store(false, std::memory_order_release);
        return nullptr;
    }
    _consumerPeeking.store(true, std::memory_order_release);
    return &_slots[tail % _capacity];
}

bool WakeWordAudioBridge::consumeFrame() {
    if (!_initialized.load(std::memory_order_acquire) || !_slots || _capacity == 0) {
        _consumerPeeking.store(false, std::memory_order_release);
        return false;
    }
    size_t head = _head.load(std::memory_order_acquire);
    size_t tail = _tail.load(std::memory_order_relaxed);
    if (tail >= head) {
        _consumerPeeking.store(false, std::memory_order_release);
        return false;
    }
    const FrameSlot& slot = _slots[tail % _capacity];
    _lastSeqConsumed.store(slot.sequence, std::memory_order_relaxed);
    _consumerPeeking.store(false, std::memory_order_release);
    _tail.store(tail + 1, std::memory_order_release);
    _framesConsumed.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool WakeWordAudioBridge::popFrame(int16_t* outSamples, size_t maxSamples, size_t& outCount, uint32_t& outRms, uint32_t* outSeq) {
    const FrameSlot* slot = peekFrame();
    if (!slot) {
        outCount = 0;
        outRms = 0;
        return false;
    }
    size_t toCopy = (slot->sampleCount <= maxSamples) ? slot->sampleCount : maxSamples;
    if (outSamples && toCopy > 0) {
        memcpy(outSamples, slot->samples, toCopy * sizeof(int16_t));
    }
    outCount = toCopy;
    outRms = slot->rms;
    if (outSeq) {
        *outSeq = slot->sequence;
    }
    return consumeFrame();
}

