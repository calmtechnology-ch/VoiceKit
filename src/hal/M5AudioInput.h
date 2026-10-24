#pragma once
#include "hal/IAudioInput.h"

class M5AudioInput : public IAudioInput {
public:
    M5AudioInput();
    ~M5AudioInput() override;

    bool begin() override;
    void end() override;
    bool isRunning() const override;
    int isRecording() const override;
    bool record(int16_t* dest, size_t sampleCount, uint32_t sampleRateHz = 16000, bool stereo = false) override;
    void stop() override;
    const char* getLastError() const override { return _lastError; }

private:
    char _lastError[64] = {};
    bool _initialized = false;
};
