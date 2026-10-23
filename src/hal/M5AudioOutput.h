#pragma once
#include "IAudioOutput.h"

/**
 * @brief Adaptateur de restitution audio pour M5Stack CoreS3 via M5Unified.
 * Respecte rigoureusement la configuration de l'application hôte :
 * ne réinitialise pas le matériel si déjà actif, s'intègre en composant invité.
 */
class M5AudioOutput : public IAudioOutput {
public:
    M5AudioOutput();
    ~M5AudioOutput() override;

    bool begin() override;
    void end(AudioStopReason reason = AudioStopReason::RESET) override;
    bool isRunning() const override;

    bool playRaw(const int16_t* samples, size_t sampleCount, 
                 uint32_t sampleRateHz = 24000, bool stereo = false, 
                 uint8_t channel = 0) override;

    int isPlaying(uint8_t channel = 0) const override;
    void stop(uint8_t channel = 0, AudioStopReason reason = AudioStopReason::NORMAL_FINISH) override;
    void setVolume(unsigned volumePercent) override;
    void setChannelVolume(uint8_t channel, uint8_t volume255) override;
    unsigned getVolume() const override { return _volume; }
    uint32_t getHardwareDrainMs() const override;
    void setBufferReleaseCallback(void* args, BufferReleaseCallback cb) override;

    // Métriques et traçabilité
    void resetTurnMetrics() override;
    bool wasStopCalledInTurn() const override { return _stopCallCountInTurn > 0; }
    bool wasEndCalledInTurn() const override { return _endCallCountInTurn > 0; }
    uint32_t getLastStopTimeMs() const override { return _lastStopTimeMs; }
    uint32_t getLastEndTimeMs() const override { return _lastEndTimeMs; }
    AudioStopReason getLastStopReason() const override { return _lastStopReason; }
    AudioStopReason getLastEndReason() const override { return _lastEndReason; }
    uint32_t getLastPlayRawAcceptedTimeMs() const override { return _lastPlayRawAcceptedTimeMs; }
    uint32_t getStopCallCountInTurn() const override { return _stopCallCountInTurn; }
    uint32_t getEndCallCountInTurn() const override { return _endCallCountInTurn; }
    uint32_t getBeginCallCountInTurn() const override { return _beginCallCountInTurn; }
    uint32_t getPlayRawCallCountInTurn() const override { return _playRawCallCountInTurn; }
    size_t getSamplesAcceptedInTurn() const override { return _samplesAcceptedInTurn; }

private:
    unsigned _volume = 65;
    bool _initialized = false;
    void* _releaseCbArgs = nullptr;
    BufferReleaseCallback _releaseCb = nullptr;

    uint32_t _stopCallCount = 0;
    uint32_t _stopCallCountInTurn = 0;
    uint32_t _lastStopTimeMs = 0;
    AudioStopReason _lastStopReason = AudioStopReason::NONE;

    uint32_t _endCallCount = 0;
    uint32_t _endCallCountInTurn = 0;
    uint32_t _lastEndTimeMs = 0;
    AudioStopReason _lastEndReason = AudioStopReason::NONE;

    uint32_t _beginCallCountInTurn = 0;
    uint32_t _lastBeginTimeMs = 0;

    uint32_t _playRawCallCountInTurn = 0;
    uint32_t _lastPlayRawAcceptedTimeMs = 0;
    size_t _samplesAcceptedInTurn = 0;
};
