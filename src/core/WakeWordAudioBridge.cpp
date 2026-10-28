#include "WakeWordAudioBridge.h"
#include <cstring>

WakeWordAudioBridge::WakeWordAudioBridge() = default;

WakeWordAudioBridge::~WakeWordAudioBridge() {
    end();
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
    _head.store(0, std::memory_order_relaxed);
    _tail.store(0, std::memory_order_relaxed);
    _initialized.store(true, std::memory_order_release);
    return true;
}

void WakeWordAudioBridge::end() {
    _enabled.store(false, std::memory_order_release);
    _initialized.store(false, std::memory_order_release);
    _head.store(0, std::memory_order_relaxed);
    _tail.store(0, std::memory_order_relaxed);
    _slots.reset();
    _capacity = 0;
}

void WakeWordAudioBridge::clear() {
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
        // File bornée saturée : application de la politique explicite sans bloquer
        _queueSaturations.fetch_add(1, std::memory_order_relaxed);
        _framesDropped.fetch_add(1, std::memory_order_relaxed);

        if (_overflowPolicy.load(std::memory_order_relaxed) == WakeWordOverflowPolicy::DROP_OLDEST) {
            // Avance la queue pour libérer le slot le plus ancien
            _tail.store(tail + 1, std::memory_order_release);
            used = _capacity - 1;
        } else {
            // DROP_NEWEST (défaut) : abandon immédiat de la trame entrante, préserve l'intégrité du lecteur
            const uint32_t durUs = micros() - startUs;
            _lastPushDurationUs.store(durUs, std::memory_order_relaxed);
            uint32_t prevMax = _maxPushDurationUs.load(std::memory_order_relaxed);
            while (durUs > prevMax && !_maxPushDurationUs.compare_exchange_weak(prevMax, durUs, std::memory_order_relaxed)) {}
            return false;
        }
    }

    // Copie unique (1 seule copie mémoire) du bloc DMA vers le slot préalloué
    FrameSlot& slot = _slots[head % _capacity];
    memcpy(slot.samples, samples, sampleCount * sizeof(int16_t));
    slot.sampleCount = sampleCount;
    slot.rms = rms;
    uint32_t seq = _nextSequence.fetch_add(1, std::memory_order_relaxed);
    slot.sequence = seq;
    slot.timestampMs = millis();

    _lastSeqEnqueued.store(seq, std::memory_order_relaxed);
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
        return nullptr;
    }
    size_t head = _head.load(std::memory_order_acquire);
    size_t tail = _tail.load(std::memory_order_relaxed);
    if (tail >= head) {
        return nullptr;
    }
    return &_slots[tail % _capacity];
}

bool WakeWordAudioBridge::consumeFrame() {
    if (!_initialized.load(std::memory_order_acquire) || !_slots || _capacity == 0) {
        return false;
    }
    size_t head = _head.load(std::memory_order_acquire);
    size_t tail = _tail.load(std::memory_order_relaxed);
    if (tail >= head) {
        return false;
    }
    const FrameSlot& slot = _slots[tail % _capacity];
    _lastSeqConsumed.store(slot.sequence, std::memory_order_relaxed);
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

