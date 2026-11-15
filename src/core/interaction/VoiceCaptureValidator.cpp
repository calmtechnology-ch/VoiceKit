#include "VoiceCaptureValidator.h"
#include <cmath>

VoiceCaptureValidator::VoiceCaptureValidator(const VoiceCaptureConfig& config)
    : _config(config),
      _noiseFloor(config.initialNoiseFloor) {
    reset();
}

VoiceCaptureValidator::VoiceCaptureValidator(uint32_t minSpeechMs,
                                             uint32_t minConsecutiveMs,
                                             uint32_t minCaptureMs,
                                             float initialNoiseFloor)
    : _noiseFloor(initialNoiseFloor) {
    _config.minSpeechDurationMs = minSpeechMs;
    _config.minConsecutiveSpeechMs = minConsecutiveMs;
    _config.minCaptureDurationMs = minCaptureMs;
    _config.initialNoiseFloor = initialNoiseFloor;
    reset();
}

void VoiceCaptureValidator::reset() {
    _peakRms = 0;
    _sumRms = 0;
    _totalChunks = 0;
    _speechChunks = 0;
    _consecutiveSpeechChunks = 0;
    _maxConsecutiveSpeechChunks = 0;
    _startTimeMs = 0;
    _active = false;
    _lastMetrics = VoiceCaptureMetrics();
    if (_noiseFloor < _config.minNoiseFloor) {
        _noiseFloor = _config.initialNoiseFloor;
    }
}

void VoiceCaptureValidator::start(uint32_t nowMs) {
    reset();
    _startTimeMs = nowMs;
    _active = true;
}

void VoiceCaptureValidator::stop() {
    _active = false;
}

void VoiceCaptureValidator::setConfig(const VoiceCaptureConfig& config) {
    _config = config;
    if (_noiseFloor < _config.minNoiseFloor) {
        _noiseFloor = _config.minNoiseFloor;
    } else if (_noiseFloor > _config.maxNoiseFloor) {
        _noiseFloor = _config.maxNoiseFloor;
    }
}

uint32_t VoiceCaptureValidator::getSpeechThreshold() const {
    uint32_t thresh = static_cast<uint32_t>(_noiseFloor * _config.noiseMultiplier) + _config.noiseOffsetRms;
    return (thresh < _config.speechThresholdFloorRms) ? _config.speechThresholdFloorRms : thresh;
}

void VoiceCaptureValidator::feedChunk(uint32_t rms) {
    if (!_active) return;

    _totalChunks++;
    _sumRms += rms;
    if (rms > _peakRms) {
        _peakRms = rms;
    }

    uint32_t speechThresh = getSpeechThreshold();

    if (rms >= speechThresh) {
        _speechChunks++;
        _consecutiveSpeechChunks++;
        if (_consecutiveSpeechChunks > _maxConsecutiveSpeechChunks) {
            _maxConsecutiveSpeechChunks = _consecutiveSpeechChunks;
        }
    } else {
        _consecutiveSpeechChunks = 0;
        // Adaptation douce du bruit de fond uniquement sous le seuil de parole
        _noiseFloor = _noiseFloor * (1.0f - _config.noiseAlpha) + (static_cast<float>(rms) * _config.noiseAlpha);
        if (_noiseFloor < _config.minNoiseFloor) _noiseFloor = _config.minNoiseFloor;
        if (_noiseFloor > _config.maxNoiseFloor) _noiseFloor = _config.maxNoiseFloor;
    }
}

void VoiceCaptureValidator::updateIdleRms(uint32_t rms) {
    if (!_active) {
        _noiseFloor = _noiseFloor * (1.0f - _config.noiseAlpha) + (static_cast<float>(rms) * _config.noiseAlpha);
        if (_noiseFloor < _config.minNoiseFloor) _noiseFloor = _config.minNoiseFloor;
        if (_noiseFloor > _config.maxNoiseFloor) _noiseFloor = _config.maxNoiseFloor;
    }
}

VoiceCaptureMetrics VoiceCaptureValidator::evaluate(uint32_t nowMs) {
    VoiceCaptureMetrics m;
    m.totalChunksCount = _totalChunks;
    m.speechChunksCount = _speechChunks;
    m.peakRms = _peakRms;
    m.averageRms = (_totalChunks > 0) ? static_cast<uint32_t>(_sumRms / _totalChunks) : 0;
    m.noiseFloor = static_cast<uint32_t>(_noiseFloor);
    m.speechThreshold = getSpeechThreshold();
    m.cumulativeSpeechMs = _speechChunks * _config.chunkDurationMs;
    m.maxConsecutiveSpeechMs = _maxConsecutiveSpeechChunks * _config.chunkDurationMs;

    uint32_t dur = (_startTimeMs > 0 && nowMs >= _startTimeMs) 
        ? (nowMs - _startTimeMs) 
        : (_totalChunks * _config.chunkDurationMs);
    m.captureDurationMs = dur;

    // 1. Filtrage appui trop court (< minCaptureDurationMs ou < minChunksCount)
    if (dur < _config.minCaptureDurationMs || _totalChunks < _config.minChunksCount) {
        m.isValid = false;
        m.rejectReason = "TOO_SHORT";
        _lastMetrics = m;
        return m;
    }

    // 2. Détection de silence : le signal n'a jamais franchi le seuil de parole
    if (_peakRms < m.speechThreshold || _speechChunks == 0) {
        m.isValid = false;
        m.rejectReason = "SILENCE";
        _lastMetrics = m;
        return m;
    }

    // 3. Immunisation contre les bruits brefs (tapotement de boîtier, choc, clic mécanique)
    if (m.cumulativeSpeechMs < _config.minSpeechDurationMs || m.maxConsecutiveSpeechMs < _config.minConsecutiveSpeechMs) {
        m.isValid = false;
        m.rejectReason = "BRIEF_NOISE";
        _lastMetrics = m;
        return m;
    }

    // 4. Validation du rapport énergie moyenne / bruit ambiant
    if (m.averageRms < m.noiseFloor + _config.lowEnergyMarginRms) {
        m.isValid = false;
        m.rejectReason = "LOW_ENERGY";
        _lastMetrics = m;
        return m;
    }

    // Parole confirmée
    m.isValid = true;
    m.rejectReason = "OK";
    _lastMetrics = m;
    return m;
}
