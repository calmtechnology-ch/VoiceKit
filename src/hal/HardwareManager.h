#pragma once
#include "hal/IAudioOutput.h"
#include "hal/IAudioInput.h"
#include "VoiceHubTypes.h"

/**
 * @brief Gestionnaire HAL centralisé pour l'arbitrage I2S entre haut-parleur et microphone.
 * Empêche tout conflit matériel DMA ou effet larsen sur M5Stack CoreS3.
 */
class HardwareManager {
public:
    static HardwareManager& getInstance() {
        static HardwareManager instance;
        return instance;
    }

    IAudioOutput* getAudioOutput() const { return _audioOutput; }
    void setAudioOutput(IAudioOutput* output) { _audioOutput = output; }

    IAudioInput* getAudioInput() const { return _audioInput; }
    void setAudioInput(IAudioInput* input) { _audioInput = input; }

    AudioHardwareStatus getLastAudioStatus() const { return _lastAudioStatus; }
    bool isFailed() const { return _lastAudioStatus == AudioHardwareStatus::PERMANENT_FAULT; }
    bool isMicCaptureReserved() const { return _micCaptureReserved; }
    const void* getMicOwner() const { return _micOwner; }
    uint32_t getMicReservationFailureCount() const { return _micReservationFailures; }
    void recordMicReservationFailure() { _micReservationFailures++; }
    void resetMicReservationFailureCount() { _micReservationFailures = 0; }
    void releaseMicCaptureReservation() {
        _micCaptureReserved = false;
        _micOwner = nullptr;
    }

    /**
     * @brief Arbitrage matériel : Sécurise le bus I2S avant enregistrement micro.
     * Arrête et libère le haut-parleur pour éviter tout conflit matériel sur CoreS3.
     * Garantit un propriétaire unique du microphone (v0.6.3.1).
     */
    bool prepareMicCapture(const void* owner = nullptr) {
        if (owner != nullptr && _micCaptureReserved && _micOwner != nullptr && _micOwner != owner) {
            _lastAudioStatus = AudioHardwareStatus::DEVICE_BUSY;
            _micReservationFailures++;
            return false;
        }
        if (_audioOutput) {
            if (_audioOutput->isPlaying(0) > 0) {
                _audioOutput->stop(0, AudioStopReason::NEW_TURN);
#if defined(ESP32) || defined(ARDUINO)
                uint32_t t0 = millis();
                while (_audioOutput->isPlaying(0) > 0 && (millis() - t0 < 300)) {
                    delay(2);
                }
#endif
            }
            if (_audioOutput->isPlaying(0) > 0) {
                _lastAudioStatus = AudioHardwareStatus::DEVICE_BUSY;
                _micReservationFailures++;
                return false;
            }
            if (_audioOutput->isRunning()) {
                _audioOutput->end(AudioStopReason::NEW_TURN);
            }
        }
        _micCaptureReserved = true;
        if (owner != nullptr) {
            _micOwner = owner;
        }
        _lastAudioStatus = AudioHardwareStatus::SUCCESS;
        return true;
    }

    /**
     * @brief Arbitrage matériel : Rétablissement et réarmement sécurisé du haut-parleur.
     */
    bool restoreSpeakerAfterMic() {
        if (_audioInput) {
            if (_audioInput->isRunning()) {
                _audioInput->end();
            }
            if (_audioInput->isRecording() != 0) {
#if defined(ESP32) || defined(ARDUINO)
                uint32_t t0 = millis();
                while (_audioInput->isRecording() != 0 && (millis() - t0 < 100)) {
                    delay(2);
                }
#endif
            }
            if (_audioInput->isRecording() != 0) {
                _lastAudioStatus = AudioHardwareStatus::DEVICE_BUSY;
                return false;
            }
        }
        _micCaptureReserved = false;
        _micOwner = nullptr;

        if (_audioOutput && !_audioOutput->isRunning()) {
            if (!_audioOutput->begin()) {
                _lastAudioStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
                return false;
            }
        }

        _lastAudioStatus = AudioHardwareStatus::SUCCESS;
        return true;
    }

    void reset() {
        _micCaptureReserved = false;
        _micOwner = nullptr;
        _micReservationFailures = 0;
        _lastAudioStatus = AudioHardwareStatus::SUCCESS;
    }

private:
    HardwareManager() = default;
    IAudioOutput* _audioOutput = nullptr;
    IAudioInput* _audioInput = nullptr;
    AudioHardwareStatus _lastAudioStatus = AudioHardwareStatus::SUCCESS;
    bool _micCaptureReserved = false;
    const void* _micOwner = nullptr;
    uint32_t _micReservationFailures = 0;
};
