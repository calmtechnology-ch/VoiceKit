#include "M5AudioOutput.h"
#include <algorithm>

#if defined(ESP32) || defined(ARDUINO)
#include <Arduino.h>
#include <M5Unified.h>

M5AudioOutput::M5AudioOutput() = default;
M5AudioOutput::~M5AudioOutput() {
    end(AudioStopReason::RESET);
}

void M5AudioOutput::resetTurnMetrics() {
    _stopCallCountInTurn = 0;
    _endCallCountInTurn = 0;
    _beginCallCountInTurn = 0;
    _playRawCallCountInTurn = 0;
    _lastStopTimeMs = 0;
    _lastEndTimeMs = 0;
    _lastPlayRawAcceptedTimeMs = 0;
    _lastStopReason = AudioStopReason::NONE;
    _lastEndReason = AudioStopReason::NONE;
    _samplesAcceptedInTurn = 0;
}

bool M5AudioOutput::begin() {
    _beginCallCountInTurn++;
    _lastBeginTimeMs = millis();

    // Gestion explicite de la propriete du haut-parleur (v0.1.0.1 - Correction C)
    if (!M5.Speaker.isRunning()) {
        // Cas 1 : VoiceKit initialise le haut-parleur physique (proprietaire)
        _isOwner = true;
        auto spkCfg = M5.Speaker.config();
        spkCfg.sample_rate = 44100;
        spkCfg.dma_buf_len = 512;
        spkCfg.dma_buf_count = 8;
        M5.Speaker.config(spkCfg);
        _initialized = M5.Speaker.begin();
        M5.Speaker.setVolume(255);
        setVolume(_volume);
        if (_releaseCb) {
            M5.Speaker.setBufferReleaseCallback(_releaseCbArgs, _releaseCb);
        }
        return _initialized;
    }

    // Cas 2 : L'application hote possede deja le haut-parleur
    // VoiceKit reutilise l'instance existante sans ecraser la configuration materielle
    _isOwner = false;
    _initialized = true;
    setVolume(_volume);
    if (_releaseCb) {
        M5.Speaker.setBufferReleaseCallback(_releaseCbArgs, _releaseCb);
    }
    return true;
}

void M5AudioOutput::end(AudioStopReason reason) {
    _endCallCount++;
    _endCallCountInTurn++;
    _lastEndTimeMs = millis();
    _lastEndReason = reason;

    if (_isOwner) {
        // Cas 1 : VoiceKit est proprietaire -> arret complet et liberation materielle
        if (M5.Speaker.isRunning()) {
            M5.Speaker.setBufferReleaseCallback(nullptr, nullptr);
            M5.Speaker.end();
            delay(5);
        }
    } else {
        // Cas 2 : L'application hote est proprietaire -> NE PAS appeler M5.Speaker.end() !
        // Arreter uniquement notre canal (canal 0) et detacher notre callback
        if (M5.Speaker.isPlaying(0) > 0) {
            M5.Speaker.stop(0);
        }
        M5.Speaker.setBufferReleaseCallback(nullptr, nullptr);
    }
    _initialized = false;
}

bool M5AudioOutput::isRunning() const {
    return M5.Speaker.isRunning();
}

bool M5AudioOutput::playRaw(const int16_t* samples, size_t sampleCount, 
                            uint32_t sampleRateHz, bool stereo, uint8_t channel) {
    if (!samples || sampleCount == 0) return false;
    _playRawCallCountInTurn++;
    if (!M5.Speaker.isRunning()) {
        return false;
    }
    // Appel sécurisé au pilote M5Unified
    // Retourne true si accepté dans la file matérielle DMA, false si refusé
    bool accepted = M5.Speaker.playRaw(samples, sampleCount, sampleRateHz, stereo, 1, channel);
    if (accepted) {
        _lastPlayRawAcceptedTimeMs = millis();
        _samplesAcceptedInTurn += sampleCount;
    }
    return accepted;
}

int M5AudioOutput::isPlaying(uint8_t channel) const {
    return M5.Speaker.isPlaying(channel);
}

void M5AudioOutput::stop(uint8_t channel, AudioStopReason reason) {
    _stopCallCount++;
    _stopCallCountInTurn++;
    _lastStopTimeMs = millis();
    _lastStopReason = reason;
    if (M5.Speaker.isPlaying(channel) > 0) {
        M5.Speaker.stop(channel);
    }
}

void M5AudioOutput::setVolume(unsigned volumePercent) {
    _volume = std::min(100u, volumePercent);
    uint8_t scaled = (_volume * 255 + 50) / 100;
    M5.Speaker.setChannelVolume(0, scaled);
}

void M5AudioOutput::setChannelVolume(uint8_t channel, uint8_t volume255) {
    M5.Speaker.setChannelVolume(channel, volume255);
}

uint32_t M5AudioOutput::getHardwareDrainMs() const {
    if (M5.Speaker.isRunning()) {
        auto spkCfg = M5.Speaker.config();
        if (spkCfg.sample_rate > 0) {
            uint32_t dmaFrames = (spkCfg.dma_buf_count + 1) * spkCfg.dma_buf_len;
            return (dmaFrames * 1000UL) / spkCfg.sample_rate + 30;
        }
    }
    return 135;
}

void M5AudioOutput::setBufferReleaseCallback(void* args, BufferReleaseCallback cb) {
    _releaseCbArgs = args;
    _releaseCb = cb;
    if (M5.Speaker.isRunning()) {
        M5.Speaker.setBufferReleaseCallback(args, cb);
    }
}

#else
// Fallback pour exécution hors cible (simulation tests hôte C++)
#include <chrono>
static inline uint32_t s_mockMillis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static bool s_hostSpeakerRunning = true;

void M5AudioOutput::setMockHostSpeakerRunning(bool running) {
    s_hostSpeakerRunning = running;
}
bool M5AudioOutput::isMockHostSpeakerRunning() {
    return s_hostSpeakerRunning;
}

M5AudioOutput::M5AudioOutput() = default;
M5AudioOutput::~M5AudioOutput() {
    end(AudioStopReason::RESET);
}

void M5AudioOutput::resetTurnMetrics() {
    _stopCallCountInTurn = 0;
    _endCallCountInTurn = 0;
    _beginCallCountInTurn = 0;
    _playRawCallCountInTurn = 0;
    _lastStopTimeMs = 0;
    _lastEndTimeMs = 0;
    _lastPlayRawAcceptedTimeMs = 0;
    _lastStopReason = AudioStopReason::NONE;
    _lastEndReason = AudioStopReason::NONE;
    _samplesAcceptedInTurn = 0;
}

bool M5AudioOutput::begin() {
    _beginCallCountInTurn++;
    _lastBeginTimeMs = s_mockMillis();
    if (!s_hostSpeakerRunning) {
        // Cas 1 : VoiceKit initialise le haut-parleur (propriétaire)
        _isOwner = true;
        s_hostSpeakerRunning = true;
    } else {
        // Cas 2 : L'hôte possède déjà le haut-parleur
        _isOwner = false;
    }
    _initialized = true;
    return true;
}

void M5AudioOutput::end(AudioStopReason reason) {
    _endCallCount++;
    _endCallCountInTurn++;
    _lastEndTimeMs = s_mockMillis();
    _lastEndReason = reason;
    if (_isOwner) {
        s_hostSpeakerRunning = false;
    }
    _initialized = false;
}

bool M5AudioOutput::isRunning() const { return s_hostSpeakerRunning; }

bool M5AudioOutput::playRaw(const int16_t* samples, size_t count, uint32_t, bool, uint8_t) { 
    if (!samples || count == 0 || !s_hostSpeakerRunning) return false;
    _playRawCallCountInTurn++;
    _lastPlayRawAcceptedTimeMs = s_mockMillis();
    _samplesAcceptedInTurn += count;
    return true; 
}

int M5AudioOutput::isPlaying(uint8_t) const { return 0; }

void M5AudioOutput::stop(uint8_t, AudioStopReason reason) {
    _stopCallCount++;
    _stopCallCountInTurn++;
    _lastStopTimeMs = s_mockMillis();
    _lastStopReason = reason;
}

void M5AudioOutput::setVolume(unsigned v) { _volume = v; }
void M5AudioOutput::setChannelVolume(uint8_t, uint8_t) {}
uint32_t M5AudioOutput::getHardwareDrainMs() const { return 0; }
void M5AudioOutput::setBufferReleaseCallback(void* args, BufferReleaseCallback cb) {
    _releaseCbArgs = args;
    _releaseCb = cb;
}
#endif
