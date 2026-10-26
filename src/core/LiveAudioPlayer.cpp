#include "LiveAudioPlayer.h"
#include "hal/HardwareManager.h"
#include "core/AudioTraceLog.h"
#include <cstring>
#include <algorithm>

#if defined(ESP32) || defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

// Définitions hors-ligne des constantes statiques pour conformité C++14 / C++17
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
    // RÈGLE v0.2.1.3.4 : Ne jamais laisser un callback M5Unified actif pointant vers cet objet détruit
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
    if (!_audioOutput) {
        _audioOutput = HardwareManager::getInstance().getAudioOutput();
    }
    if (_audioOutput) {
        _audioOutput->setBufferReleaseCallback(this, &LiveAudioPlayer::onBufferReleaseStatic);
    }

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
    Serial.println("[LivePlayer] Mode audio : MODE 1 (REACTIVE_CALLBACK — Reference v0.2.1.3.3)");
#else
    Serial.println("[LivePlayer] Mode audio : MODE 2 (DEDICATED_AUDIO_TASK — Tache FreeRTOS dediee v0.2.1.3.4)");
#endif

#if (defined(ESP32) || defined(ARDUINO)) && (VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_DEDICATED_TASK)
    if (!_audioTaskHandle) {
        _audioTaskStopRequested = false;
        BaseType_t res = xTaskCreatePinnedToCore(
            &LiveAudioPlayer::audioPlaybackTaskEntry,
            "live_audio_task",
            4096,
            this,
            3, // Priorité 3 (au-dessus UI loop prio 1 et spk_task prio 2)
            &_audioTaskHandle,
            1  // Core 1
        );
        if (res == pdPASS) {
            _audioTaskRunning = true;
            Serial.println("[LivePlayer] AudioPlaybackTask FreeRTOS creee (prio 3, Core 1)");
        } else {
            Serial.println("[LivePlayer] ERREUR CRITIQUE : Echec creation AudioPlaybackTask");
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
        Serial.printf("[LivePlayer] AVERTISSEMENT : Echec allocation PSRAM %u echantillons, repli 120 000\n", (unsigned)targetCap);
        _bufferCapacity = 120000;
        _rxPcmBuffer = static_cast<int16_t*>(heap_caps_malloc(_bufferCapacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!_rxPcmBuffer) {
        Serial.println("[LivePlayer] AVERTISSEMENT : Repli PSRAM 48 000 echantillons");
        _bufferCapacity = 48000;
        _rxPcmBuffer = static_cast<int16_t*>(heap_caps_malloc(_bufferCapacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!_rxPcmBuffer) {
        Serial.println("[LivePlayer] AVERTISSEMENT : Repli memoire interne 24 000 echantillons");
        _bufferCapacity = 24000;
        _rxPcmBuffer = static_cast<int16_t*>(heap_caps_malloc(_bufferCapacity * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!_rxPcmBuffer) {
        Serial.println("[LivePlayer] ERREUR CRITIQUE : Echec allocation memoire tampon PCM 24 kHz");
        return false;
    }
    Serial.printf("[LivePlayer] Tampon circulaire alloue : %u echantillons (%u Ko)\n",
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
    _lastTickTimeUs = 0;
    _maxTickIntervalUs = 0;
    _lastIncomingPcmTimeUs = 0;
    _maxIncomingPcmIntervalUs = 0;
    _rebufferingCount = 0;
    _rebufferingStartTimeMs = 0;
    _totalRebufferingDurationMs = 0;
    _maxRebufferingDurationMs = 0;
    _playbackEverStarted = false;

    // Métriques et vidange matérielle v0.2.1.2 (Corrections A, C, D)
    _hardwareDrainStartTimeMs = 0;
    _playbackFinishedNotified = false;
    _turnCompleteTimeMs = 0;
    _samplesRemainingAtTurnComplete = 0;
    _samplesRemainingAtFinish = 0;
    _durationTurnCompleteToFinishMs = 0;

    // Métriques et traçabilité v0.2.1.3 (Corrections C & E)
    _samplesPresentedToPlayRaw = 0;
    _playbackFinishDeclarationsCount = 0;
    _samplesCancelledTotal = 0;
    _bufferEmptyLogged = false;
    _isPlayingZeroLogged = false;

    // Métriques et régulation v0.2.1.3.1 (Corrections A, B, D, F)
    _tickCount = 0;
    _tickIntervalOver40msCount = 0;
    _tickIntervalOver80msCount = 0;
    _tickIntervalOver160msCount = 0;
    _playbackStartTimeMs = 0;
    _playbackEndTimeMs = 0;
    _playbackDurationMs = 0;

    _lastWindowTimeMs = 0;
    _windowSamplesPlayed = 0;
    _windowSamplesReceived = 0;
    _windowSamplesAccepted = 0;
    _minWindowConsumptionRate = 0.0f;
    _maxWindowConsumptionRate = 0.0f;
    _sumWindowRates = 0.0;
    _rateWindowsCount = 0;

    _backpressureWaitCount = 0;
    _backpressureWaitMsTotal = 0;

    _geminiRxStartTimeMs = 0;
    _geminiRxEndTimeMs = 0;
    _firstPlayRawAcceptedTimeMs = 0;
    _driverQueueFullCount = 0;
    _driverQueueFullMsTotal = 0;

    _lastPlayRawAcceptedTimeMs = 0;
    _lastBufferReleasedTimeMs = 0;
    _isPlayingZeroTimeMs = 0;
    _playbackFinishedTimeMs = 0;

#if !defined(ESP32) && !defined(ARDUINO)
    _simSpeakerPlayingChunks = 0;
    recordedPlayedSamples.clear();
#endif
}

bool LiveAudioPlayer::checkBufferIntegrity() const {
    // Équation 1 : PCM remis au lecteur = PCM acceptés + PCM rejetés saturation
    bool eq1 = (_samplesReceivedTotal == (_samplesAcceptedBuffer + _overflowDroppedSamples));

    // Équation 2 : PCM acceptés = PCM soumis avec succès + PCM encore en attente + PCM annulés (STOP)
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
        // Si la lecture n'est pas active, en attente ou en cours de rebuffering :
        if (!_playbackAllowed || !_isPlaying || _isBuffering) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            continue;
        }

        // Lecture active : soumettre des blocs dans la file matérielle (< 2 slots)
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

        // Attente du prochain callback de libération ou timeout (10 ms pour éviter la famine)
        uint32_t tWait0 = millis();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        uint32_t waitElapsed = millis() - tWait0;
        int curPlayingAfter = _audioOutput ? _audioOutput->isPlaying(VOXCORE_AUDIO_CHANNEL_DEFAULT) : 0;
        if (_isPlaying && !_isBuffering && curPlayingAfter >= 2) {
            _driverQueueFullCount++;
            _driverQueueFullMsTotal += waitElapsed;
        }
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

double LiveAudioPlayer::getEffectiveConsumptionRate() const {
    if (_playbackDurationMs > 0) {
        return (double)_samplesPlayedTotal / ((double)_playbackDurationMs / 1000.0);
    }
    return 0.0;
}

float LiveAudioPlayer::getAvgConsumptionRatePerSec() const {
    if (_rateWindowsCount > 0) {
        return static_cast<float>(_sumWindowRates / _rateWindowsCount);
    }
    return 0.0f;
}

bool LiveAudioPlayer::feedPcm(const int16_t* samples, size_t sampleCount) {
    if (!_rxPcmBuffer || !samples || sampleCount == 0) return false;

    _chunksReceived++;
    _samplesReceivedTotal += sampleCount;
    _windowSamplesReceived += sampleCount;
    _bufferEmptyLogged = false;
    _isPlayingZeroLogged = false;

    if (_geminiRxStartTimeMs == 0) {
        _geminiRxStartTimeMs = millis();
    }
    _geminiRxEndTimeMs = millis();

    if (_hardwareDrainStartTimeMs > 0 && _playbackAllowed) {
        _hardwareDrainStartTimeMs = 0;
        _isPlaying = true;
    }

    uint32_t nowUs = micros();
    if (_lastIncomingPcmTimeUs > 0) {
        uint32_t interval = (nowUs >= _lastIncomingPcmTimeUs) ? (nowUs - _lastIncomingPcmTimeUs) : 0;
        if (interval > _maxIncomingPcmIntervalUs) {
            _maxIncomingPcmIntervalUs = interval;
        }
    }
    _lastIncomingPcmTimeUs = nowUs;

#if defined(ESP32) || defined(ARDUINO)
    // Régulation préventive High-Water Mark (Backpressure douce) :
    // Si le tampon dépasse 90% de sa capacité, céder 2 ms pour laisser la tâche audio
    // alimenter M5.Speaker sans bloquer dangereusement la tâche réseau WebSocket.
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

    // Ingestion intégrale garantie sans perte ni écrasement (TTSTest v0.1.1):
    // Découpage et insertion par tranches au fur et à mesure que la tâche audio libère de l'espace.
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
            _windowSamplesAccepted += toWrite;
            AudioTraceLog::record(AudioTraceEventType::PCM_ACCEPTED_PSRAM, toWrite, currentAvail);
        }

        // Démarrage initial ou reprise après rebuffering UNIQUEMENT si _playbackAllowed (Correction F)
        if (_playbackAllowed) {
            if (!_isPlaying) {
                if (currentAvail >= JITTER_START_SAMPLES || _turnComplete) {
                    startPlaybackHardware();
                }
            } else if (_isBuffering) {
                if (currentAvail >= JITTER_RESUME_SAMPLES || _turnComplete) {
                    _isBuffering = false;
                    if (_rebufferingStartTimeMs > 0) {
                        uint32_t dur = millis() - _rebufferingStartTimeMs;
                        _totalRebufferingDurationMs += dur;
                        if (dur > _maxRebufferingDurationMs) {
                            _maxRebufferingDurationMs = dur;
                        }
                        _rebufferingStartTimeMs = 0;
                    }
                    wakeAudioTask();
                }
            } else {
                wakeAudioTask();
            }
        }

        if (samplesInserted >= sampleCount) {
            break; // Tout le bloc a été inséré sans aucune perte
        }

#if defined(ESP32) || defined(ARDUINO)
        // Tampon plein à 100% : temporiser par tranches de 5 ms pour laisser le lecteur DMA consommer
        // Préservation de l'attente coopérative même si turnComplete est déjà positionné (protection des dernières syllabes)
        if (_isPlaying && _playbackAllowed && !_hardwareError && totalWaitMs < 2000) {
            wakeAudioTask();
            vTaskDelay(pdMS_TO_TICKS(5));
            totalWaitMs += 5;
            _backpressureWaitCount++;
            _backpressureWaitMsTotal += 5;
        } else {
            // Sécurité anti-blocage réseau si anomalie matérielle ou timeout extrême :
            size_t dropped = sampleCount - samplesInserted;
            _overflowDroppedSamples += dropped;
            _overflowCount++;
            AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED, dropped, 2);
            Serial.printf("[LivePlayer] ALERTE SATURATION : %u echantillons rejetes (erreur mat/timeout)\n",
                          (unsigned)dropped);
            return false;
        }
#else
        // Environnement de test hôte sans FreeRTOS :
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
#if defined(ESP32) || defined(ARDUINO)
    portENTER_CRITICAL(&_mux);
#endif
    _samplesRemainingAtTurnComplete = _availableSamples;
#if defined(ESP32) || defined(ARDUINO)
    portEXIT_CRITICAL(&_mux);
#endif
    if (_playbackAllowed && !_isPlaying && _availableSamples > 0) {
        startPlaybackHardware();
    }
    wakeAudioTask();
}

bool LiveAudioPlayer::startPlaybackHardware() {
    if (!_playbackAllowed) {
        return false;
    }

    if (HardwareManager::getInstance().isFailed()) {
        _isPlaying = false;
        _hardwareError = true;
        return false;
    }

    if (!_audioOutput) {
        _audioOutput = HardwareManager::getInstance().getAudioOutput();
    }

    if (_audioOutput) {
        if (HardwareManager::getInstance().getAudioOutput() != nullptr || HardwareManager::getInstance().getAudioInput() != nullptr) {
            bool restored = HardwareManager::getInstance().restoreSpeakerAfterMic();
            if (!restored) {
                Serial.println("[LivePlayer] ERREUR : Arbitrage I2S refuse par restoreSpeakerAfterMic(). Lecture non demarree.");
                _isPlaying = false;
                _hardwareError = true;
                return false;
            }
        }
        _audioOutput->setBufferReleaseCallback(this, &LiveAudioPlayer::onBufferReleaseStatic);
        _audioOutput->setVolume(_volume);
    } else {
#if defined(ESP32) || defined(ARDUINO)
        Serial.println("[LivePlayer] ERREUR : Aucun peripherique audio configure.");
        _isPlaying = false;
        _hardwareError = true;
        return false;
#endif
    }

    _hardwareError = false;
    _isPlaying = true;
    _isBuffering = false;
    Serial.printf("[LivePlayer] Demarrage restitution audio 24 kHz (%u echantillons en tampon)\n",
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
    if (channel != VOXCORE_AUDIO_CHANNEL_DEFAULT) {
        return;
    }
    _bufferReleaseCallbackCount++;
    _lastBufferReleasedTimeMs = millis();

    int releasedIdx = -1;
    for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
        if (_dmaBuffers[i] == data) {
            _dmaBufferStates[i].store(DmaBufferState::FREE, std::memory_order_release);
            releasedIdx = static_cast<int>(i);
            break;
        }
    }

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
    // MODE 1 (Référence v0.2.1.3.3) : Rechargement réactif direct depuis le callback M5Unified
    if (_playbackAllowed && _isPlaying && !_isBuffering && _availableSamples > 0) {
        while (submitNextChunk()) {
            _reactiveRefillCount++;
        }
    }
#else
    // MODE 2 (Dédié v0.2.1.3.4) : Notification ultra-légère sans copie, sans playRaw, sans mutex
    if (releasedIdx >= 0) {
        uint32_t bufGen = _dmaBufferGeneration[releasedIdx];
        if (bufGen != _turnGeneration.load(std::memory_order_relaxed)) {
            // Reliquat d'un tour précédent : le buffer est repassé FREE, ne pas réveiller
            return;
        }
    }
    wakeAudioTask();
#endif
}

bool LiveAudioPlayer::submitNextChunk() {
    if (!_playbackAllowed || !_isPlaying || _isBuffering) {
        return false;
    }

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
    // Protection atomique contre les accès concurrents entre spk_task et loop() en Mode 1
    bool expected = false;
    if (!_isSubmitting.compare_exchange_strong(expected, true)) {
        return false;
    }
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

    // RÈGLE v0.2.1.3.4 : Recherche d'un buffer FREE sans remise à zéro arbitraire
    int targetBuffer = -1;
    for (size_t i = 0; i < DMA_BUFFER_COUNT; ++i) {
        size_t candidate = (_dmaIndex + i) % DMA_BUFFER_COUNT;
        if (_dmaBufferStates[candidate].load(std::memory_order_relaxed) == DmaBufferState::FREE) {
            targetBuffer = static_cast<int>(candidate);
            break;
        }
    }

    if (targetBuffer < 0) {
        // Aucun buffer libre : le pilote détient encore les tampons (interdiction d'écraser)
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

    // Transition d'état : RESERVED pendant la préparation
    _dmaBufferStates[targetBuffer].store(DmaBufferState::RESERVED, std::memory_order_release);
    _dmaBufferGeneration[targetBuffer] = _turnGeneration.load(std::memory_order_relaxed);

    // Copie mémoire rapide sans incrémenter readIndex à l'avance
    size_t firstPart = std::min(toRead, _bufferCapacity - readPos);
    memcpy(dest, &_rxPcmBuffer[readPos], firstPart * sizeof(int16_t));
    if (toRead > firstPart) {
        memcpy(dest + firstPart, &_rxPcmBuffer[0], (toRead - firstPart) * sizeof(int16_t));
    }

    // Notification du premier son au tout premier envoi effectif
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
        // RÈGLE DE SÉCURITÉ CONCURRENTE v0.2.1.3.4 : Si STOP est survenu pendant l'exécution de playRaw(),
        // le tour a été annulé et les tampons ont été remis à zéro. Ne pas corrompre les indices !
        if (!_isPlaying || _turnGeneration.load(std::memory_order_relaxed) != _dmaBufferGeneration[targetBuffer]) {
            _dmaBufferStates[targetBuffer].store(DmaBufferState::FREE, std::memory_order_release);
#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
            _isSubmitting.store(false);
#endif
            return false;
        }

        // Transition d'état : IN_USE (détenu par le pilote matériel)
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

        if (_firstPlayRawAcceptedTimeMs == 0) {
            _firstPlayRawAcceptedTimeMs = millis();
            if (_firstPlayRawAcceptedTimeMs == 0) _firstPlayRawAcceptedTimeMs = 1;
        }
        if (_playbackStartTimeMs == 0) {
            _playbackStartTimeMs = millis();
        }
        _lastPlayRawAcceptedTimeMs = millis();
        _samplesPlayedTotal += toRead;
        _windowSamplesPlayed += toRead;
        _playRawSuccessCount++;
        AudioTraceLog::record(AudioTraceEventType::PLAYRAW_ACCEPTED, toRead, _availableSamples);

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
        _isSubmitting.store(false);
#endif
        return true;
    } else {
        // Refus temporaire de playRaw() : retour de RESERVED à FREE sans perte d'échantillons
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
    _tickCount++;
    if (!_playbackAllowed || !_isPlaying || !_rxPcmBuffer) return;

    uint32_t nowUs = micros();
    uint32_t nowMs = millis();
    if (_lastTickTimeUs > 0) {
        uint32_t interval = (nowUs >= _lastTickTimeUs) ? (nowUs - _lastTickTimeUs) : 0;
        if (interval > _maxTickIntervalUs) {
            _maxTickIntervalUs = interval;
        }
        if (interval > 160000) {
            _tickIntervalOver160msCount++;
        } else if (interval > 80000) {
            _tickIntervalOver80msCount++;
        } else if (interval > 40000) {
            _tickIntervalOver40msCount++;
        }
    }
    _lastTickTimeUs = nowUs;

    // Analyse du débit de consommation par fenêtres temporelles (Correction A)
    if (_lastWindowTimeMs == 0) {
        _lastWindowTimeMs = nowMs;
    } else if (nowMs - _lastWindowTimeMs >= RATE_WINDOW_MS) {
        uint32_t winDur = nowMs - _lastWindowTimeMs;
        if (winDur > 0) {
            float rate = (_windowSamplesPlayed * 1000.0f) / winDur;
            if (_rateWindowsCount == 0 || rate < _minWindowConsumptionRate) {
                _minWindowConsumptionRate = rate;
            }
            if (rate > _maxWindowConsumptionRate) {
                _maxWindowConsumptionRate = rate;
            }
            _sumWindowRates += rate;
            _rateWindowsCount++;
        }
        _windowSamplesPlayed = 0;
        _windowSamplesReceived = 0;
        _windowSamplesAccepted = 0;
        _lastWindowTimeMs = nowMs;
    }

    // Suivi de l'occupation minimale pendant la lecture active
    if (!_isBuffering && _availableSamples > 0) {
        if (!_playbackEverStarted) {
            _playbackEverStarted = true;
            _minBufferedSamples = _availableSamples;
        } else if (_availableSamples < _minBufferedSamples) {
            _minBufferedSamples = _availableSamples;
        }
    }

#if defined(ESP32) || defined(ARDUINO)
    if (!_audioOutput) {
        _audioOutput = HardwareManager::getInstance().getAudioOutput();
    }

    // S'assurer que le périphérique de sortie est actif
    if (_audioOutput && !_audioOutput->isRunning()) {
        if (!startPlaybackHardware()) {
            return;
        }
    }

#if VOXCORE_AUDIO_MODE == VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
    // MODE 1 (Référence v0.2.1.3.3) : tick() soumet directement tant que isPlaying < 2
    while (!_isBuffering && _audioOutput && _audioOutput->isPlaying(VOXCORE_AUDIO_CHANNEL_DEFAULT) < 2) {
        if (!submitNextChunk()) {
            break;
        }
    }
#else
    // MODE 2 (Dédié v0.2.1.3.4) : tick() ne soumet JAMAIS directement de blocs PCM.
    // Seule AudioPlaybackTask en est propriétaire.
    // Réveil sécurisé si nécessaire (ex: reprise après rebuffering ou buffers disponibles)
    if (!_isBuffering && _audioOutput && _audioOutput->isPlaying(VOXCORE_AUDIO_CHANNEL_DEFAULT) < 2 && _availableSamples > 0) {
        wakeAudioTask();
    }
#endif

    // Contrôle d'épuisement du tampon et fin de lecture
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
            // Vérification rigoureuse : file matérielle vide ET tous les buffers DMA sont FREE
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
                        return; // Attente non-bloquante : les derniers échantillons sortent de la DMA
                    }
                    if (millis() - _hardwareDrainStartTimeMs < drainMs) {
                        return; // Vidange DMA matérielle en cours
                    }
                }

                if (!_playbackFinishedNotified) {
                    _playbackFinishedNotified = true;
                    _playbackFinishedTimeMs = millis();
                    if (_playbackFinishedTimeMs == 0) _playbackFinishedTimeMs = 1;
                    _playbackEndTimeMs = _playbackFinishedTimeMs;
                    if (_playbackStartTimeMs > 0 && _playbackEndTimeMs >= _playbackStartTimeMs) {
                        _playbackDurationMs = _playbackEndTimeMs - _playbackStartTimeMs;
                    }
                    _playbackFinishDeclarationsCount++;
                    AudioTraceLog::record(AudioTraceEventType::PLAYBACK_FINISHED, _samplesPlayedTotal, 0);
                    _isPlaying = false;
                    _isBuffering = false;
                    _hardwareDrainStartTimeMs = 0;
                    _samplesRemainingAtFinish = 0;
                    _durationTurnCompleteToFinishMs = (_turnCompleteTimeMs > 0) ? (millis() - _turnCompleteTimeMs) : 0;
                    printDiagnosticSummary();
                    Serial.println("[LivePlayer] Restitution audio completee");
                    if (_onPlaybackFinished) _onPlaybackFinished();
                }
            }
        } else if (!_isBuffering && currentlyPlaying <= 0) {
            // Sous-alimentation en cours de génération : activer rebuffering
            _underflowCount++;
            _rebufferingCount++;
            _isBuffering = true;
            _rebufferingStartTimeMs = millis();
        }
    }
#else
    if (_audioOutput && !_audioOutput->isRunning()) {
        if (!startPlaybackHardware()) {
            return;
        }
    }
    // Simulation tests natifs C++ (avec support injection IAudioOutput ou mockPlayRaw legacy)
    int currentlyPlaying = _audioOutput ? _audioOutput->isPlaying(0) : _simSpeakerPlayingChunks;

    while (!_isBuffering && currentlyPlaying < 2 && _availableSamples > 0) {
        if (!submitNextChunk()) {
            break;
        }
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
                        return; // Attente non-bloquante de la vidange DMA matérielle
                    }
                    if (millis() - _hardwareDrainStartTimeMs < drainMs) {
                        return; // Vidange DMA matérielle en cours
                    }
                }

                if (!_playbackFinishedNotified) {
                    _playbackFinishedNotified = true;
                    _playbackFinishedTimeMs = millis();
                    if (_playbackFinishedTimeMs == 0) _playbackFinishedTimeMs = 1;
                    _playbackEndTimeMs = _playbackFinishedTimeMs;
                    if (_playbackStartTimeMs > 0 && _playbackEndTimeMs >= _playbackStartTimeMs) {
                        _playbackDurationMs = _playbackEndTimeMs - _playbackStartTimeMs;
                    }
                    _playbackFinishDeclarationsCount++;
                    AudioTraceLog::record(AudioTraceEventType::PLAYBACK_FINISHED, _samplesPlayedTotal, 0);
                    _isPlaying = false;
                    _isBuffering = false;
                    _hardwareDrainStartTimeMs = 0;
                    _samplesRemainingAtFinish = 0;
                    _durationTurnCompleteToFinishMs = (_turnCompleteTimeMs > 0) ? (millis() - _turnCompleteTimeMs) : 0;
                    printDiagnosticSummary();
                    if (_onPlaybackFinished) _onPlaybackFinished();
                }
            }
        } else if (!_isBuffering && currentlyPlaying <= 0) {
            _underflowCount++;
            _rebufferingCount++;
            _isBuffering = true;
            _rebufferingStartTimeMs = millis();
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

    if (_isBuffering && _rebufferingStartTimeMs > 0) {
        uint32_t dur = millis() - _rebufferingStartTimeMs;
        _totalRebufferingDurationMs += dur;
        if (dur > _maxRebufferingDurationMs) {
            _maxRebufferingDurationMs = dur;
        }
        _rebufferingStartTimeMs = 0;
    }

    if (_samplesReceivedTotal > 0) {
        Serial.println("[LivePlayer] Arret force (STOP) pendant lecture");
        printDiagnosticSummary();
    }

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

    Serial.println("[LivePlayer] Arret immediat et vidage des tampons");
}

void LiveAudioPlayer::printDiagnosticSummary() const {
#if VOXCORE_DIAGNOSTICS_LEVEL >= 2
    double durationReceivedSec = (double)_samplesReceivedTotal / LIVE_AUDIO_SAMPLE_RATE_RX;
    double durationPlayedSec = (double)_samplesPlayedTotal / LIVE_AUDIO_SAMPLE_RATE_RX;
    double bufferOccupancyPct = (_bufferCapacity > 0) ? (100.0 * _maxBufferedSamples / _bufferCapacity) : 0.0;
    double minOccupancyPct = (_bufferCapacity > 0) ? (100.0 * _minBufferedSamples / _bufferCapacity) : 0.0;
    double conservationRate = (_samplesReceivedTotal > 0) ? (100.0 * _samplesPlayedTotal / _samplesReceivedTotal) : 100.0;
    double effectiveRate = getEffectiveConsumptionRate();
    float avgWinRate = getAvgConsumptionRatePerSec();

    const char* speakerState = "INCONNU";
    if (_audioOutput) {
        int spkPlaying = _audioOutput->isPlaying(VOXCORE_AUDIO_CHANNEL_DEFAULT);
        speakerState = (spkPlaying > 0) ? "OCCUPE" : "LIBRE";
    } else {
#if !defined(ESP32) && !defined(ARDUINO)
        speakerState = (_simSpeakerPlayingChunks > 0) ? "OCCUPE" : "LIBRE";
#else
        speakerState = "NON_CONFIGURE";
#endif
    }

    bool integrityOk = checkBufferIntegrity();
    int32_t discrepancy = getBufferIntegrityDiscrepancy();
    bool eq1Ok = (_samplesReceivedTotal == (_samplesAcceptedBuffer + _overflowDroppedSamples));
    bool eq2Ok = (_samplesAcceptedBuffer == (_samplesPlayedTotal + _availableSamples + _samplesCancelledTotal));

    Serial.printf("[AUDIO RX FIN DE TOUR] PCM recus : %u PCM acceptes : %u PCM presentes HP : %u PCM acceptes HP : %u PCM restants : %u PCM annules : %u Refus playRaw : %u Underflow : %u Overflow : %u Fins declarees : %u Recharges reactifs : %u Reutilisations evitees : %u Etat HP : %s Fin Gemini : %s Fin restitution : %s Integrite : %s\n",
                  (unsigned)_samplesReceivedTotal,
                  (unsigned)_samplesAcceptedBuffer,
                  (unsigned)_samplesPresentedToPlayRaw,
                  (unsigned)_samplesPlayedTotal,
                  (unsigned)_availableSamples,
                  (unsigned)_samplesCancelledTotal,
                  (unsigned)_playRawRefusedCount,
                  (unsigned)_underflowCount,
                  (unsigned)_overflowCount,
                  (unsigned)_playbackFinishDeclarationsCount,
                  (unsigned)_reactiveRefillCount,
                  (unsigned)_prematureReuseAvoidedCount,
                  speakerState,
                  _turnComplete ? "OUI" : "NON",
                  _playbackFinishedNotified ? "OUI" : "NON",
                  integrityOk ? "OK" : "ERREUR");

    Serial.println("\n--------------------------------------------------");
    Serial.println("  [AUDIO LIVE BILAN] Diagnostic Integrite Flux   ");
    Serial.println("--------------------------------------------------");
    Serial.printf("  - Echantillons PCM recus       : %u (%.2f s)\n", (unsigned)_samplesReceivedTotal, durationReceivedSec);
    Serial.printf("  - Acceptes dans le tampon      : %u (%.1f%%)\n", (unsigned)_samplesAcceptedBuffer, 
                  _samplesReceivedTotal > 0 ? (100.0 * _samplesAcceptedBuffer / _samplesReceivedTotal) : 100.0);
    Serial.printf("  - Rejetes saturation (overflow): %u (%u echantillons)\n", (unsigned)_overflowCount, (unsigned)_overflowDroppedSamples);
    Serial.printf("  - Presentes a playRaw() (DMA)  : %u\n", (unsigned)_samplesPresentedToPlayRaw);
    Serial.printf("  - Acceptes par playRaw() (DMA) : %u (%.2f s)\n", (unsigned)_samplesPlayedTotal, durationPlayedSec);
    Serial.printf("  - Refus playRaw() retentes     : %u (0 perte, conserves)\n", (unsigned)_playRawRefusedCount);
    Serial.printf("  - Liberations callback M5Unified: %u\n", (unsigned)_bufferReleaseCallbackCount);
    Serial.printf("  - Recharges reactifs callback  : %u blocs\n", (unsigned)_reactiveRefillCount);
    Serial.printf("  - Reutilisations evitees (busy): %u\n", (unsigned)_prematureReuseAvoidedCount);
    Serial.printf("  - Echantillons annules (STOP)  : %u\n", (unsigned)_samplesCancelledTotal);
    Serial.printf("  - Echantillons en attente      : %u\n", (unsigned)_availableSamples);
    Serial.printf("  - Fins de lecture declarees    : %u\n", (unsigned)_playbackFinishDeclarationsCount);
    Serial.printf("  - Sous-alimentations (underflow): %u\n", (unsigned)_underflowCount);
    Serial.printf("  - Interruptions rebuffering    : %u (duree tot: %u ms, max: %u ms)\n",
                  (unsigned)_rebufferingCount, (unsigned)_totalRebufferingDurationMs, (unsigned)_maxRebufferingDurationMs);
    if (_samplesRemainingAtTurnComplete > 0 || _durationTurnCompleteToFinishMs > 0) {
        Serial.printf("  - Restant a turnComplete       : %u echantillons\n", (unsigned)_samplesRemainingAtTurnComplete);
        Serial.printf("  - Restant a onPlaybackFinished : %u echantillons\n", (unsigned)_samplesRemainingAtFinish);
        Serial.printf("  - Duree turnComplete -> Finish : %u ms\n", (unsigned)_durationTurnCompleteToFinishMs);
    }
    Serial.printf("  - Tampon min / pic occupation  : %u / %u (%.1f%% / %.1f%%)\n", 
                  (unsigned)_minBufferedSamples, (unsigned)_maxBufferedSamples, minOccupancyPct, bufferOccupancyPct);
    Serial.printf("  - Intervalle max tick lecteur  : %u us (%.2f ms)\n", _maxTickIntervalUs, _maxTickIntervalUs / 1000.0);
    Serial.printf("  - Passages dans tick lecteur   : %u ticks\n", (unsigned)_tickCount);
    Serial.printf("  - Intervalles tick >40/80/160ms: %u / %u / %u\n",
                  (unsigned)_tickIntervalOver40msCount, (unsigned)_tickIntervalOver80msCount, (unsigned)_tickIntervalOver160msCount);
    Serial.printf("  - Intervalle max blocs Gemini  : %u us (%.2f ms)\n", _maxIncomingPcmIntervalUs, _maxIncomingPcmIntervalUs / 1000.0);
    if (_playbackDurationMs > 0) {
        Serial.printf("  - Duree restitution physique   : %u ms (%.2f s)\n", (unsigned)_playbackDurationMs, (double)_playbackDurationMs / 1000.0);
        Serial.printf("  - Debit effectif consommation  : %.1f ech/s (nominal: 24000 ech/s)\n", effectiveRate);
    }
    if (_rateWindowsCount > 0) {
        Serial.printf("  - Debits fenetres (500 ms)     : min %.1f / max %.1f / moy %.1f ech/s\n",
                      _minWindowConsumptionRate, _maxWindowConsumptionRate, avgWinRate);
    }
    if (_backpressureWaitCount > 0) {
        Serial.printf("  - Regulation backpressure      : %u fois (duree tot: %u ms)\n",
                      (unsigned)_backpressureWaitCount, (unsigned)_backpressureWaitMsTotal);
    }
    Serial.printf("  - Taux de conservation audio   : %.2f%%\n", conservationRate);
    Serial.printf("  - Equation 1 (Remis = Acc+Rej) : %u = %u + %u (%s)\n",
                  (unsigned)_samplesReceivedTotal, (unsigned)_samplesAcceptedBuffer, (unsigned)_overflowDroppedSamples, eq1Ok ? "OK" : "ERREUR");
    Serial.printf("  - Equation 2 (Acc = Soum+Att+Ann): %u = %u + %u + %u (%s)\n",
                  (unsigned)_samplesAcceptedBuffer, (unsigned)_samplesPlayedTotal, (unsigned)_availableSamples, (unsigned)_samplesCancelledTotal, eq2Ok ? "OK" : "ERREUR");
    if (integrityOk && _samplesCancelledTotal == 0 && _overflowDroppedSamples == 0) {
        Serial.println("  - Statut : INTEGRITE PARFAITE (0 perte)");
    } else if (integrityOk && _samplesCancelledTotal > 0) {
        Serial.printf("  - Statut : COHERENT AVEC ANNULATION (%u echantillons annules par STOP)\n", (unsigned)_samplesCancelledTotal);
    } else {
        Serial.printf("  - Statut : ATTENTION PERTES OU ANOMALIE DETECTEE (Ecart: %d)\n", (int)discrepancy);
    }
    Serial.println("--------------------------------------------------\n");
#elif VOXCORE_DIAGNOSTICS_LEVEL == 1
    if (_overflowCount > 0 || _underflowCount > 0 || !checkBufferIntegrity()) {
        Serial.printf("[LivePlayer] Alerte audio : %u overflow (%u ech perdus), %u underflow, %u refus DMA, integrite: %s\n",
                      (unsigned)_overflowCount, (unsigned)_overflowDroppedSamples,
                      (unsigned)_underflowCount, (unsigned)_playRawRefusedCount,
                      checkBufferIntegrity() ? "OK" : "ERREUR");
    }
#endif
}

void LiveAudioPlayer::setVolume(unsigned volumePercent) {
    _volume = std::min(100u, volumePercent);
    if (_audioOutput) {
        _audioOutput->setVolume(_volume);
    }
}

