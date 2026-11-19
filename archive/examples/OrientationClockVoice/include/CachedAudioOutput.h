#pragma once

#include <Arduino.h>
#include <M5AudioOutput.h>

/**
 * Sortie audio VoiceKit avec cache PCM persistant de la derniere phrase.
 * Le flux est d'abord accepte par le pilote materiel, puis copie en PSRAM.
 * Il est ecrit dans LittleFS seulement lorsque la restitution est terminee.
 */
class CachedAudioOutput : public IAudioOutput {
public:
    ~CachedAudioOutput() override;

    bool begin() override;
    void end(AudioStopReason reason = AudioStopReason::RESET) override;
    bool isRunning() const override;
    bool playRaw(const int16_t* samples, size_t sample_count,
                 uint32_t sample_rate_hz = 24000, bool stereo = false,
                 uint8_t channel = 0) override;
    int isPlaying(uint8_t channel = 0) const override;
    void stop(uint8_t channel = 0,
              AudioStopReason reason = AudioStopReason::NORMAL_FINISH) override;
    void setVolume(unsigned volume_percent) override;
    void setChannelVolume(uint8_t channel, uint8_t volume_255) override;
    unsigned getVolume() const override;
    uint32_t getHardwareDrainMs() const override;
    void setBufferReleaseCallback(void* args, BufferReleaseCallback cb) override;

    void resetTurnMetrics() override;
    bool wasStopCalledInTurn() const override;
    bool wasEndCalledInTurn() const override;
    uint32_t getLastStopTimeMs() const override;
    uint32_t getLastEndTimeMs() const override;
    AudioStopReason getLastStopReason() const override;
    AudioStopReason getLastEndReason() const override;
    uint32_t getLastPlayRawAcceptedTimeMs() const override;
    uint32_t getStopCallCountInTurn() const override;
    uint32_t getEndCallCountInTurn() const override;
    uint32_t getBeginCallCountInTurn() const override;
    uint32_t getPlayRawCallCountInTurn() const override;
    size_t getSamplesAcceptedInTurn() const override;

    void startCapture(const String& text);
    bool finishCapture();
    void cancelCapture();
    bool hasCached(const String& text) const;
    String cachedText() const;
    bool playCached(const String& text);
    void update();
    bool isReplaying() const { return replaying_; }
    size_t cachedBytes() const { return cached_bytes_; }

private:
    static uint32_t hashText(const String& text);
    bool ensureBuffer();
    bool readHeader(uint32_t& text_hash, uint32_t& sample_rate,
                    uint32_t& sample_count, bool& stereo) const;

    M5AudioOutput output_;
    int16_t* buffer_ = nullptr;
    size_t buffer_capacity_samples_ = 0;
    size_t captured_samples_ = 0;
    size_t cached_bytes_ = 0;
    uint32_t capture_text_hash_ = 0;
    String capture_text_ = "";
    uint32_t capture_sample_rate_ = 24000;
    bool capture_stereo_ = false;
    bool capturing_ = false;
    bool capture_overflow_ = false;
    bool replaying_ = false;
    void* release_cb_args_ = nullptr;
    BufferReleaseCallback release_cb_ = nullptr;
};
