#pragma once
#include "hal/IAudioOutput.h"

/**
 * @brief Gestionnaire HAL minimal pour compatibilite descendante avec LiveAudioPlayer.
 */
class HardwareManager {
public:
    static HardwareManager& getInstance() {
        static HardwareManager instance;
        return instance;
    }

    IAudioOutput* getAudioOutput() const { return _audioOutput; }
    void setAudioOutput(IAudioOutput* output) { _audioOutput = output; }
    void* getAudioInput() const { return nullptr; }
    bool isFailed() const { return false; }
    bool restoreSpeakerAfterMic() { return true; }

private:
    HardwareManager() = default;
    IAudioOutput* _audioOutput = nullptr;
};
