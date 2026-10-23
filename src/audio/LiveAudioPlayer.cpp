#include "LiveAudioPlayer.h"
#include "AudioTraceLog.h"
#include <cstring>
#include <algorithm>

#if defined(ESP32) || defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

constexpr size_t LiveAudioPlayer::DMA_CHUNK_SAMPLES;
constexpr size_t LiveAudioPlayer::DMA_BUFFER_COUNT;
constexpr size_t LiveAudioPlayer::JITTER_START_SAMPLES;
constexpr size_t LiveAudioPlayer::JITTER_RESUME_SAMPLES;

LiveAudioPlayer::LiveAudioPlayer(IAudioOutput* output)
    : _audioOutput(output) {
    _bufferCapacity = LIVE_AUDIO_RX_BUFFER_SAMPLES;
    for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
        _dmaBufferStates[i].store(DmaBufferState::FREE, std::memory_order_relaxed);
        _dmaBufferGeneration[i] = 0;
    }
    if (_audioOutput) {
        _audioOutput->setBufferReleaseCallback(this, &LiveAudioPlayer::onBufferReleaseStatic);
    }
}

void LiveAudioPlayer::setAudioOutput(IAudioOutput* output) {
    _audioOutput = output;
    if (_audioOutput) {
        _audioOutput->setBufferReleaseCallback(this, &LiveAudioPlayer::onBufferReleaseStatic);
    }
}

LiveAudioPlayer::~LiveAudioPlayer() {
    stop();
#if (defined(ESP32) || defined(ARDUINO)) && (VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_DEDICATED_TASK)
    if (_audioTaskHandle) {
        _audioTaskStopRequested = true;
        xTaskNotifyGive(_audioTaskHandle);
        uint32_t waitMs = 0;
        while (_audioTaskRunning.load(std::memory_order_relaxed) && waitMs < 100) {
            vTaskDelay(pdMS_TO_TICKS(5));
            waitMs += 5;
        }
        _audioTaskHandle = nullptr;
    }
#endif
    if (_audioOutput) {
        _audioOutput->setBufferReleaseCallback(nullptr, nullptr);
    }
#if defined(ESP32) || defined(ARDUINO)
    if (_rxPcmBuffer) {
        heap_caps_free(_rxPcmBuffer);
        _rxPcmBuffer = nullptr;
    }
#else
    if (_rxPcmBuffer) {
        delete[] _rxPcmBuffer;
        _rxPcmBuffer = nullptr;
    }
#endif
}

bool LiveAudioPlayer::begin(size_t customCapacity) {
    if (_audioOutput) {
        _audioOutput->setBufferReleaseCallback(this, &LiveAudioPlayer::onBufferReleaseStatic);
    }

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
    Serial.println("[VoiceKit:Player] Mode audio : MODE 1 (REACTIVE_CALLBACK)");
#else
    Serial.println("[VoiceKit:Player] Mode audio : MODE 2 (DEDICATED_AUDIO_TASK - FreeRTOS)");
#endif

#if (defined(ESP32) || defined(ARDUINO)) && (VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_DEDICATED_TASK)
    if (!_audioTaskHandle) {
        _audioTaskStopRequested = false;
        BaseType_t res = xTaskCreatePinnedToCore(
            &LiveAudioPlayer::audioPlaybackTaskEntry,
            "live_audio_task",
            4096,
            this,
            3, // Priorité 3 (Core 1)
            &_audioTaskHandle,
            1  // Core 1
        );
        if (res == pdPASS) {
            _audioTaskRunning = true;
            Serial.println("[VoiceKit:Player] AudioPlaybackTask FreeRTOS creee (prio 3, Core 1)");
        } else {
            Serial.println("[VoiceKit:Player] ERREUR CRITIQUE : Echec creation AudioPlaybackTask");
        }
    }
#endif

    if (_rxPcmBuffer) return true;

    size_t targetCap = (customCapacity > 0) ? customCapacity : LIVE_AUDIO_RX_BUFFER_SAMPLES;

#if defined(ESP32) || defined(ARDUINO)
    _bufferCapacity = targetCap;
    const size_t bytesNeeded = _bufferCapacity * sizeof(int16_t);
    // Allocation prioritaire en PSRAM
    _rxPcmBuffer = static_cast<int16_t*>(heap_caps_malloc(bytesNeeded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!_rxPcmBuffer && targetCap > 120000) {
        Serial.printf("[VoiceKit:Player] Repli PSRAM 120 000 echantillons\n");
        _bufferCapacity = 120000;
        _rxPcmBuffer = static_cast<int16_t*>(heap_caps_malloc(_bufferCapacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!_rxPcmBuffer) {
        Serial.println("[VoiceKit:Player] Repli PSRAM 48 000 echantillons");
        _bufferCapacity = 48000;
        _rxPcmBuffer = static_cast<int16_t*>(heap_caps_malloc(_bufferCapacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!_rxPcmBuffer) {
        Serial.println("[VoiceKit:Player] Repli memoire interne 24 000 echantillons");
        _bufferCapacity = 24000;
        _rxPcmBuffer = static_cast<int16_t*>(heap_caps_malloc(_bufferCapacity * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!_rxPcmBuffer) {
        Serial.println("[VoiceKit:Player] ERREUR CRITIQUE : Echec allocation memoire tampon PCM 24 kHz");
        return false;
    }
    Serial.printf("[VoiceKit:Player] Tampon circulaire alloue : %u echantillons (%u Ko)\n",
                  (unsigned)_bufferCapacity, (unsigned)((_bufferCapacity * sizeof(int16_t)) / 1024));
#else
    _bufferCapacity = targetCap;
    _rxPcmBuffer = new int16_t[_bufferCapacity];
#endif

    memset(_rxPcmBuffer, 0, _bufferCapacity * sizeof(int16_t));
    reset();

    return true;
}

void LiveAudioPlayer::reset() {
#if defined(ESP32) || defined(ARDUINO)
    portENTER_CRITICAL(&_mux);
#endif
    _writeIndex = 0;
    _readIndex = 0;
    _availableSamples = 0;
#if defined(ESP32) || defined(ARDUINO)
    portEXIT_CRITICAL(&_mux);
#endif
    _turnGeneration.fetch_add(1, std::memory_order_relaxed);
    _turnComplete = false;
    _isPlaying = false;
    _isBuffering = false;
    _hardwareError = false;
    _firstAudioNotified = false;
    _dmaIndex = 0;
    for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
        _dmaBufferStates[i].store(DmaBufferState::FREE, std::memory_order_relaxed);
        _dmaBufferGeneration[i] = 0;
    }
    _isSubmitting.store(false);
    _bufferReleaseCallbackCount = 0;
    _prematureReuseAvoidedCount = 0;
    _reactiveRefillCount = 0;

    _chunksReceived = 0;
    _underflowCount = 0;
    _samplesReceivedTotal = 0;
    _samplesAcceptedBuffer = 0;
    _samplesPlayedTotal = 0;
    _playRawRefusedCount = 0;
    _playRawSuccessCount = 0;
    _overflowCount = 0;
    _overflowDroppedSamples = 0;
    _maxBufferedSamples = 0;
    _minBufferedSamples = 0;
    _playbackEverStarted = false;

    _hardwareDrainStartTimeMs = 0;
    _playbackFinishedNotified = false;
    _turnCompleteTimeMs = 0;
    _samplesPresentedToPlayRaw = 0;
    _samplesCancelledTotal = 0;
    _bufferEmptyLogged = false;
    _isPlayingZeroLogged = false;

    _playbackStartTimeMs = 0;
    _playbackEndTimeMs = 0;
    _playbackDurationMs = 0;
    _backpressureWaitCount = 0;
    _backpressureWaitMsTotal = 0;
    _isPlayingZeroTimeMs = 0;
    _playbackFinishedTimeMs = 0;

#if !defined(ESP32) && !defined(ARDUINO)
    _simSpeakerPlayingChunks = 0;
    recordedPlayedSamples.clear();
#endif
}

bool LiveAudioPlayer::checkBufferIntegrity() const {
    bool eq1 = (_samplesReceivedTotal == (_samplesAcceptedBuffer + _overflowDroppedSamples));
    bool eq2 = (_samplesAcceptedBuffer == (_samplesPlayedTotal + _availableSamples + _samplesCancelledTotal));
    return eq1 && eq2;
}

int32_t LiveAudioPlayer::getBufferIntegrityDiscrepancy() const {
    int32_t diff1 = static_cast<int32_t>(_samplesReceivedTotal) - static_cast<int32_t>(_samplesAcceptedBuffer + _overflowDroppedSamples);
    if (diff1 != 0) return diff1;
    int32_t diff2 = static_cast<int32_t>(_samplesAcceptedBuffer) - static_cast<int32_t>(_samplesPlayedTotal + _availableSamples + _samplesCancelledTotal);
    return diff2;
}

size_t LiveAudioPlayer::getBufferedSamples() const {
    return _availableSamples;
}

void LiveAudioPlayer::wakeAudioTask() {
#if (defined(ESP32) || defined(ARDUINO)) && (VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_DEDICATED_TASK)
    if (_audioTaskHandle) {
        xTaskNotifyGive(_audioTaskHandle);
    }
#endif
}

#if defined(ESP32) || defined(ARDUINO)
void LiveAudioPlayer::audioPlaybackTaskEntry(void* arg) {
    auto* self = static_cast<LiveAudioPlayer*>(arg);
    self->audioPlaybackTaskLoop();
}

void LiveAudioPlayer::audioPlaybackTaskLoop() {
    while (!_audioTaskStopRequested.load(std::memory_order_relaxed)) {
        if (!_playbackAllowed || !_isPlaying || _isBuffering) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            continue;
        }

        while (_playbackAllowed && _isPlaying && !_isBuffering) {
            int curPlaying = _audioOutput ? _audioOutput->isPlaying(VOXCORE_AUDIO_CHANNEL_DEFAULT) : 0;
            if (curPlaying >= 2) {
                break;
            }

            size_t avail = 0;
            portENTER_CRITICAL(&_mux);
            avail = _availableSamples;
            portEXIT_CRITICAL(&_mux);

            if (avail == 0) {
                break;
            }

            if (!submitNextChunk()) {
                break;
            }
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }
    _audioTaskRunning = false;
    vTaskDelete(nullptr);
}
#endif

void LiveAudioPlayer::setPlaybackAllowed(bool allowed) {
    _playbackAllowed = allowed;
    if (_playbackAllowed && !_isPlaying && _availableSamples > 0) {
        if (_availableSamples >= JITTER_START_SAMPLES || _turnComplete) {
            startPlaybackHardware();
        }
    }
    wakeAudioTask();
}

bool LiveAudioPlayer::feedPcm(const int16_t* samples, size_t sampleCount) {
    if (!_rxPcmBuffer || !samples || sampleCount == 0) return false;

    _chunksReceived++;
    _samplesReceivedTotal += sampleCount;
    _bufferEmptyLogged = false;
    _isPlayingZeroLogged = false;

    if (_hardwareDrainStartTimeMs > 0 && _playbackAllowed) {
        _hardwareDrainStartTimeMs = 0;
        _isPlaying = true;
    }

#if defined(ESP32) || defined(ARDUINO)
    // Régulation préventive High-Water Mark (>90%)
    if (_isPlaying && _playbackAllowed) {
        size_t curAvail = 0;
        portENTER_CRITICAL(&_mux);
        curAvail = _availableSamples;
        portEXIT_CRITICAL(&_mux);
        if (curAvail > (_bufferCapacity * 90) / 100) {
            wakeAudioTask();
            vTaskDelay(pdMS_TO_TICKS(2));
            _backpressureWaitCount++;
            _backpressureWaitMsTotal += 2;
        }
    }
#endif

    // Ingestion intégrale garantie sans perte
    size_t samplesInserted = 0;
    uint32_t totalWaitMs = 0;

    while (samplesInserted < sampleCount) {
#if defined(ESP32) || defined(ARDUINO)
        portENTER_CRITICAL(&_mux);
#endif
        size_t freeSpace = (_availableSamples < _bufferCapacity) ? (_bufferCapacity - _availableSamples) : 0;
        size_t toWrite = std::min(sampleCount - samplesInserted, freeSpace);

        for (size_t i = 0; i < toWrite; ++i) {
            _rxPcmBuffer[_writeIndex] = samples[samplesInserted + i];
            _writeIndex = (_writeIndex + 1) % _bufferCapacity;
        }
        _availableSamples += toWrite;
        _samplesAcceptedBuffer += toWrite;
        samplesInserted += toWrite;

        if (_availableSamples > _maxBufferedSamples) {
            _maxBufferedSamples = _availableSamples;
        }
        size_t currentAvail = _availableSamples;
#if defined(ESP32) || defined(ARDUINO)
        portEXIT_CRITICAL(&_mux);
#endif

        if (toWrite > 0) {
            AudioTraceLog::record(AudioTraceEventType::PCM_ACCEPTED_PSRAM, toWrite, currentAvail);
        }

        if (_playbackAllowed) {
            if (!_isPlaying) {
                if (currentAvail >= JITTER_START_SAMPLES || _turnComplete) {
                    startPlaybackHardware();
                }
            } else if (_isBuffering) {
                if (currentAvail >= JITTER_RESUME_SAMPLES || _turnComplete) {
                    _isBuffering = false;
                    wakeAudioTask();
                }
            } else {
                wakeAudioTask();
            }
        }

        if (samplesInserted >= sampleCount) {
            break;
        }

#if defined(ESP32) || defined(ARDUINO)
        // Temporisation coopérative si tampon temporairement plein
        if (_isPlaying && _playbackAllowed && !_hardwareError && totalWaitMs < 2000) {
            wakeAudioTask();
            vTaskDelay(pdMS_TO_TICKS(5));
            totalWaitMs += 5;
            _backpressureWaitCount++;
            _backpressureWaitMsTotal += 5;
        } else {
            size_t dropped = sampleCount - samplesInserted;
            _overflowDroppedSamples += dropped;
            _overflowCount++;
            AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED, dropped, 2);
            Serial.printf("[VoiceKit:Player] ALERTE SATURATION : %u echantillons rejetes\n", (unsigned)dropped);
            return false;
        }
#else
        size_t dropped = sampleCount - samplesInserted;
        _overflowDroppedSamples += dropped;
        _overflowCount++;
        AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED, dropped, 2);
        return false;
#endif
    }

    return true;
}

void LiveAudioPlayer::notifyTurnComplete() {
    _turnComplete = true;
    _turnCompleteTimeMs = millis();
    _isBuffering = false;
    AudioTraceLog::record(AudioTraceEventType::TURN_COMPLETE_RECEIVED, _availableSamples, 0);

    if (_playbackAllowed && !_isPlaying && _availableSamples > 0) {
        startPlaybackHardware();
    }
    wakeAudioTask();
}

bool LiveAudioPlayer::startPlaybackHardware() {
    if (!_playbackAllowed) return false;

    if (_audioOutput) {
        if (!_audioOutput->isRunning()) {
            if (!_audioOutput->begin()) {
                Serial.println("[VoiceKit:Player] ERREUR : Echec initialisation AudioOutput");
                _isPlaying = false;
                _hardwareError = true;
                return false;
            }
        }
        _audioOutput->setBufferReleaseCallback(this, &LiveAudioPlayer::onBufferReleaseStatic);
        _audioOutput->setVolume(_volume);
    }

    _hardwareError = false;
    _isPlaying = true;
    _isBuffering = false;
    Serial.printf("[VoiceKit:Player] Demarrage restitution audio 24 kHz (%u echantillons en tampon)\n",
                  (unsigned)_availableSamples);
    wakeAudioTask();
    return true;
}

void LiveAudioPlayer::onBufferReleaseStatic(void* args, const void* data, uint8_t channel) {
    if (args) {
        static_cast<LiveAudioPlayer*>(args)->handleBufferRelease(data, channel);
    }
}

void LiveAudioPlayer::handleBufferRelease(const void* data, uint8_t channel) {
    if (channel != VOXCORE_AUDIO_CHANNEL_DEFAULT) return;
    _bufferReleaseCallbackCount++;

    int releasedIdx = -1;
    for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
        if (_dmaBuffers[i] == data) {
            _dmaBufferStates[i].store(DmaBufferState::FREE, std::memory_order_release);
            releasedIdx = static_cast<int>(i);
            break;
        }
    }

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
    if (_playbackAllowed && _isPlaying && !_isBuffering && _availableSamples > 0) {
        while (submitNextChunk()) {
            _reactiveRefillCount++;
        }
    }
#else
    if (releasedIdx >= 0) {
        uint32_t bufGen = _dmaBufferGeneration[releasedIdx];
        if (bufGen != _turnGeneration.load(std::memory_order_relaxed)) {
            return;
        }
    }
    wakeAudioTask();
#endif
}

bool LiveAudioPlayer::submitNextChunk() {
    if (!_playbackAllowed || !_isPlaying || _isBuffering) return false;

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
    bool expected = false;
    if (!_isSubmitting.compare_exchange_strong(expected, true)) return false;
#endif

    int curPlaying = _audioOutput ? _audioOutput->isPlaying(VOXCORE_AUDIO_CHANNEL_DEFAULT) : 0;
#if !defined(ESP32) && !defined(ARDUINO)
    if (!_audioOutput) {
        curPlaying = _simSpeakerPlayingChunks;
    }
#endif
    if (curPlaying >= 2) {
#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
        _isSubmitting.store(false);
#endif
        return false;
    }

    int targetBuffer = -1;
    for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
        size_t candidate = (_dmaIndex + i) % DMA_BUFFER_COUNT;
        if (_dmaBufferStates[candidate].load(std::memory_order_relaxed) == DmaBufferState::FREE) {
            targetBuffer = static_cast<int>(candidate);
            break;
        }
    }

    if (targetBuffer < 0) {
        _prematureReuseAvoidedCount++;
#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
        _isSubmitting.store(false);
#endif
        return false;
    }

    size_t avail = 0;
    size_t readPos = 0;
#if defined(ESP32) || defined(ARDUINO)
    portENTER_CRITICAL(&_mux);
#endif
    avail = _availableSamples;
    readPos = _readIndex;
#if defined(ESP32) || defined(ARDUINO)
    portEXIT_CRITICAL(&_mux);
#endif

    if (avail == 0) {
#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
        _isSubmitting.store(false);
#endif
        return false;
    }

    size_t toRead = std::min(avail, DMA_CHUNK_SAMPLES);
    int16_t* dest = _dmaBuffers[targetBuffer];

    _dmaBufferStates[targetBuffer].store(DmaBufferState::RESERVED, std::memory_order_release);
    _dmaBufferGeneration[targetBuffer] = _turnGeneration.load(std::memory_order_relaxed);

    size_t firstPart = std::min(toRead, _bufferCapacity - readPos);
    memcpy(dest, &_rxPcmBuffer[readPos], firstPart * sizeof(int16_t));
    if (toRead > firstPart) {
        memcpy(dest + firstPart, &_rxPcmBuffer[0], (toRead - firstPart) * sizeof(int16_t));
    }

    if (!_firstAudioNotified) {
        _firstAudioNotified = true;
        if (_onFirstAudio) _onFirstAudio();
    }

    _samplesPresentedToPlayRaw += toRead;
    AudioTraceLog::record(AudioTraceEventType::PLAYRAW_SUBMITTED, toRead, avail);

    bool accepted = false;
    if (_audioOutput) {
        accepted = _audioOutput->playRaw(dest, toRead, LIVE_AUDIO_SAMPLE_RATE_RX, false, VOXCORE_AUDIO_CHANNEL_DEFAULT);
    }
#if !defined(ESP32) && !defined(ARDUINO)
    else {
        accepted = mockPlayRaw(dest, toRead);
    }
#endif

    if (accepted) {
        if (!_isPlaying || _turnGeneration.load(std::memory_order_relaxed) != _dmaBufferGeneration[targetBuffer]) {
            _dmaBufferStates[targetBuffer].store(DmaBufferState::FREE, std::memory_order_release);
#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
            _isSubmitting.store(false);
#endif
            return false;
        }

        _dmaBufferStates[targetBuffer].store(DmaBufferState::IN_USE, std::memory_order_release);
        _dmaIndex = (targetBuffer + 1) % DMA_BUFFER_COUNT;

#if defined(ESP32) || defined(ARDUINO)
        portENTER_CRITICAL(&_mux);
#endif
        _readIndex = (_readIndex + toRead) % _bufferCapacity;
        if (_availableSamples >= toRead) {
            _availableSamples -= toRead;
        } else {
            _availableSamples = 0;
        }
#if defined(ESP32) || defined(ARDUINO)
        portEXIT_CRITICAL(&_mux);
#endif

        if (_playbackStartTimeMs == 0) {
            _playbackStartTimeMs = millis();
        }
        _samplesPlayedTotal += toRead;
        _playRawSuccessCount++;
        AudioTraceLog::record(AudioTraceEventType::PLAYRAW_ACCEPTED, toRead, _availableSamples);

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
        _isSubmitting.store(false);
#endif
        return true;
    } else {
        _dmaBufferStates[targetBuffer].store(DmaBufferState::FREE, std::memory_order_release);
        _playRawRefusedCount++;
        AudioTraceLog::record(AudioTraceEventType::PLAYRAW_REFUSED, toRead, avail);

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
        _isSubmitting.store(false);
#endif
        return false;
    }
}

void LiveAudioPlayer::tick() {
    if (!_playbackAllowed || !_isPlaying || !_rxPcmBuffer) return;

#if defined(ESP32) || defined(ARDUINO)
    if (_audioOutput && !_audioOutput->isRunning()) {
        if (!startPlaybackHardware()) {
            return;
        }
    }

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
    while (!_isBuffering && _audioOutput && _audioOutput->isPlaying(VOXCORE_AUDIO_CHANNEL_DEFAULT) < 2) {
        if (!submitNextChunk()) break;
    }
#else
    if (!_isBuffering && _audioOutput && _audioOutput->isPlaying(VOXCORE_AUDIO_CHANNEL_DEFAULT) < 2 && _availableSamples > 0) {
        wakeAudioTask();
    }
#endif

    size_t remaining = 0;
    portENTER_CRITICAL(&_mux);
    remaining = _availableSamples;
    portEXIT_CRITICAL(&_mux);

    if (remaining == 0) {
        if (!_bufferEmptyLogged) {
            _bufferEmptyLogged = true;
            AudioTraceLog::record(AudioTraceEventType::PSRAM_BUFFER_EMPTY, _samplesPlayedTotal, 0);
        }
        int currentlyPlaying = _audioOutput ? _audioOutput->isPlaying(VOXCORE_AUDIO_CHANNEL_DEFAULT) : 0;
        if (_turnComplete) {
            bool allBuffersFree = true;
            for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
                if (_dmaBufferStates[i].load(std::memory_order_relaxed) != DmaBufferState::FREE) {
                    allBuffersFree = false;
                    break;
                }
            }

            if (currentlyPlaying <= 0 && allBuffersFree) {
                if (_isPlayingZeroTimeMs == 0) {
                    _isPlayingZeroTimeMs = millis();
                    if (_isPlayingZeroTimeMs == 0) _isPlayingZeroTimeMs = 1;
                }
                if (!_isPlayingZeroLogged) {
                    _isPlayingZeroLogged = true;
                    AudioTraceLog::record(AudioTraceEventType::IS_PLAYING_ZERO, 0, 0);
                }

                uint32_t drainMs = _audioOutput ? _audioOutput->getHardwareDrainMs() : 0;
                if (drainMs > 0) {
                    if (_hardwareDrainStartTimeMs == 0) {
                        _hardwareDrainStartTimeMs = millis();
                        if (_hardwareDrainStartTimeMs == 0) _hardwareDrainStartTimeMs = 1;
                        return; // Vidange DMA en cours
                    }
                    if (millis() - _hardwareDrainStartTimeMs < drainMs) {
                        return;
                    }
                }

                if (!_playbackFinishedNotified) {
                    _playbackFinishedNotified = true;
                    _playbackFinishedTimeMs = millis();
                    _playbackEndTimeMs = _playbackFinishedTimeMs;
                    if (_playbackStartTimeMs > 0 && _playbackEndTimeMs >= _playbackStartTimeMs) {
                        _playbackDurationMs = _playbackEndTimeMs - _playbackStartTimeMs;
                    }
                    AudioTraceLog::record(AudioTraceEventType::PLAYBACK_FINISHED, _samplesPlayedTotal, 0);
                    _isPlaying = false;
                    _isBuffering = false;
                    _hardwareDrainStartTimeMs = 0;
                    printDiagnosticSummary();
                    Serial.println("[VoiceKit:Player] Restitution audio completee avec succes");
                    if (_onPlaybackFinished) _onPlaybackFinished();
                }
            }
        } else if (!_isBuffering && currentlyPlaying <= 0) {
            _underflowCount++;
            _isBuffering = true;
        }
    }
#else
    if (_audioOutput && !_audioOutput->isRunning()) {
        if (!startPlaybackHardware()) return;
    }
    int currentlyPlaying = _audioOutput ? _audioOutput->isPlaying(0) : _simSpeakerPlayingChunks;

    while (!_isBuffering && currentlyPlaying < 2 && _availableSamples > 0) {
        if (!submitNextChunk()) break;
        currentlyPlaying = _audioOutput ? _audioOutput->isPlaying(0) : _simSpeakerPlayingChunks;
    }

    if (_availableSamples == 0) {
        if (!_bufferEmptyLogged) {
            _bufferEmptyLogged = true;
            AudioTraceLog::record(AudioTraceEventType::PSRAM_BUFFER_EMPTY, _samplesPlayedTotal, 0);
        }
        currentlyPlaying = _audioOutput ? _audioOutput->isPlaying(0) : _simSpeakerPlayingChunks;
        if (_turnComplete) {
            bool allBuffersFree = true;
            for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
                if (_dmaBufferStates[i].load(std::memory_order_relaxed) != DmaBufferState::FREE) {
                    allBuffersFree = false;
                    break;
                }
            }
            if (currentlyPlaying <= 0 && allBuffersFree) {
                if (!_playbackFinishedNotified) {
                    _playbackFinishedNotified = true;
                    _playbackFinishedTimeMs = millis();
                    _isPlaying = false;
                    _isBuffering = false;
                    AudioTraceLog::record(AudioTraceEventType::PLAYBACK_FINISHED, _samplesPlayedTotal, 0);
                    if (_onPlaybackFinished) _onPlaybackFinished();
                }
            }
        }
    }
#endif
}

#if !defined(ESP32) && !defined(ARDUINO)
bool LiveAudioPlayer::mockPlayRaw(const int16_t* data, size_t length) {
    if (_mockPlayRawHook) {
        bool ok = _mockPlayRawHook(data, length);
        if (ok) {
            _simSpeakerPlayingChunks++;
            recordedPlayedSamples.insert(recordedPlayedSamples.end(), data, data + length);
        }
        return ok;
    }
    _simSpeakerPlayingChunks++;
    recordedPlayedSamples.insert(recordedPlayedSamples.end(), data, data + length);
    return true;
}
#endif

void LiveAudioPlayer::stop(AudioStopReason reason) {
    _turnGeneration.fetch_add(1, std::memory_order_relaxed);
    _hardwareError = false;
    _hardwareDrainStartTimeMs = 0;
    _playbackFinishedNotified = false;
    _playbackEndTimeMs = millis();
    if (_playbackStartTimeMs > 0 && _playbackEndTimeMs >= _playbackStartTimeMs) {
        _playbackDurationMs = _playbackEndTimeMs - _playbackStartTimeMs;
    }
    if (_audioOutput) {
        _audioOutput->stop(VOXCORE_AUDIO_CHANNEL_DEFAULT, reason);
    }

    wakeAudioTask();

#if defined(ESP32) || defined(ARDUINO)
    portENTER_CRITICAL(&_mux);
#endif
    _samplesCancelledTotal += _availableSamples;
    _writeIndex = 0;
    _readIndex = 0;
    _availableSamples = 0;
#if defined(ESP32) || defined(ARDUINO)
    portEXIT_CRITICAL(&_mux);
#endif
    _isPlaying = false;
    _isBuffering = false;
    _turnComplete = false;
    _firstAudioNotified = false;
    _dmaIndex = 0;
    for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
        _dmaBufferStates[i].store(DmaBufferState::FREE, std::memory_order_release);
        _dmaBufferGeneration[i] = 0;
    }
#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
    _isSubmitting.store(false);
#endif

    Serial.println("[VoiceKit:Player] Arret immediat et vidage des tampons");
}

void LiveAudioPlayer::setVolume(unsigned volumePercent) {
    _volume = std::min(100u, volumePercent);
    if (_audioOutput) {
        _audioOutput->setVolume(_volume);
    }
}

void LiveAudioPlayer::printDiagnosticSummary() const {
    double durationReceivedSec = (double)_samplesReceivedTotal / LIVE_AUDIO_SAMPLE_RATE_RX;
    double durationPlayedSec = (double)_samplesPlayedTotal / LIVE_AUDIO_SAMPLE_RATE_RX;
    double conservationRate = (_samplesReceivedTotal > 0) ? (100.0 * _samplesPlayedTotal / _samplesReceivedTotal) : 100.0;

    Serial.println("\n================= BILAN DE RESTITUTION VOCALE (VoiceKit) =================");
    Serial.printf("  Echantillons recus : %u (%.2f s)\n", _samplesReceivedTotal, durationReceivedSec);
    Serial.printf("  Echantillons joues : %u (%.2f s)\n", _samplesPlayedTotal, durationPlayedSec);
    Serial.printf("  Taux conservation  : %.2f %%\n", conservationRate);
    Serial.printf("  Echantillons rejetes: %u\n", _overflowDroppedSamples);
    Serial.printf("  Underflows / Rebuff: %u\n", _underflowCount);
    Serial.printf("  Integrite buffer   : %s (ecart: %d)\n",
                  checkBufferIntegrity() ? "CONFORME (0 perte)" : "ANOMALIE",
                  getBufferIntegrityDiscrepancy());
    Serial.println("=========================================================================\n");
}
