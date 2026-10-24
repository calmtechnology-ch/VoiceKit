#include "M5AudioInput.h"
#include <cstring>

#if defined(ESP32) || defined(ARDUINO)
#include <M5CoreS3.h>
#include <M5Unified.h>

M5AudioInput::M5AudioInput() = default;
M5AudioInput::~M5AudioInput() {
    end();
}

bool M5AudioInput::begin() {
    auto micCfg = M5.Mic.config();
    micCfg.sample_rate = 16000;
    micCfg.stereo = true; // ES7210 stereo materiel sur CoreS3
    micCfg.magnification = 8;
    M5.Mic.config(micCfg);
    _initialized = M5.Mic.begin();
    if (!_initialized) {
        strncpy(_lastError, "Echec initialisation M5.Mic.begin()", sizeof(_lastError));
    } else {
        _lastError[0] = '\0';
    }
    return _initialized;
}

void M5AudioInput::end() {
    if (M5.Mic.isRunning()) {
        M5.Mic.end();
        delay(5);
    }
    _initialized = false;
}

bool M5AudioInput::isRunning() const {
    return M5.Mic.isRunning();
}

int M5AudioInput::isRecording() const {
    return M5.Mic.isRecording();
}

bool M5AudioInput::record(int16_t* dest, size_t sampleCount, uint32_t sampleRateHz, bool stereo) {
    if (!dest || sampleCount == 0) return false;
    if (!M5.Mic.isRunning()) {
        if (!begin()) return false;
    }
    return M5.Mic.record(dest, sampleCount, sampleRateHz, stereo);
}

void M5AudioInput::stop() {
    uint32_t t0 = millis();
    while (M5.Mic.isRecording() && (millis() - t0 < 100)) {
        delay(2);
    }
    if (M5.Mic.isRunning()) {
        M5.Mic.end();
        delay(5);
    }
}

#else
// Mock d'implementation factice pour tests unitaires natifs MinGW
M5AudioInput::M5AudioInput() = default;
M5AudioInput::~M5AudioInput() = default;
bool M5AudioInput::begin() { _initialized = true; return true; }
void M5AudioInput::end() { _initialized = false; }
bool M5AudioInput::isRunning() const { return _initialized; }
int M5AudioInput::isRecording() const { return 0; }
bool M5AudioInput::record(int16_t*, size_t, uint32_t, bool) { return true; }
void M5AudioInput::stop() { _initialized = false; }
#endif
