#include "LiveAudioStreamer.h"
#include "hal/HardwareManager.h"
#include <cstring>
#include <cmath>

LiveAudioStreamer::LiveAudioStreamer(IAudioInput* input, IAudioOutput* output)
    : _audioInput(input), _audioOutput(output) {
    memset(_ringBuffers, 0, sizeof(_ringBuffers));
    for (size_t i = 0; i < LIVE_AUDIO_TX_QUEUE_MAX; ++i) {
        _ringBuffers[i].state = LiveBufferState::FREE;
    }
}

LiveAudioStreamer::~LiveAudioStreamer() {
    cancel();
#if defined(ESP32) || defined(ARDUINO)
    // Ne détruire la file et l'EventGroup que si les deux tâches sont formellement arrêtées
    if (!_taskQuarantined && !_txTaskQuarantined &&
        _micTaskHandle == nullptr && !_micTaskRunning &&
        _txTaskHandle == nullptr && !_txTaskRunning) {
        if (_txQueue) {
            vQueueDelete(_txQueue);
            _txQueue = nullptr;
        }
        if (_taskEventGroup) {
            vEventGroupDelete(_taskEventGroup);
            _taskEventGroup = nullptr;
        }
    } else {
        Serial.println("[LiveStreamer] Quarantaine : file et EventGroup conserves pour eviter use-after-free");
    }
#endif
}

bool LiveAudioStreamer::waitForTaskTermination(uint32_t timeoutMs) {
#if defined(ESP32) || defined(ARDUINO)
    if (_micTaskHandle == nullptr) {
        _micTaskRunning = false;
        return true;
    }

    if (!_taskEventGroup) {
        uint32_t t0 = millis();
        while (_micTaskRunning && (millis() - t0 < timeoutMs)) {
            delay(2);
        }
        if (!_micTaskRunning) {
            _micTaskHandle = nullptr;
            _taskQuarantined = false;
            return true;
        }
        _taskQuarantined = true;
        _lastStatus = AudioHardwareStatus::TIMEOUT;
        strncpy(_lastError, "Timeout terminaison live_mic_task", sizeof(_lastError));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        _taskEventGroup,
        LIVE_MIC_TASK_STOPPED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeoutMs)
    );

    if (bits & LIVE_MIC_TASK_STOPPED_BIT) {
        _micTaskRunning = false;
        _micTaskHandle = nullptr;
        _taskQuarantined = false;
        return true;
    }

    _taskQuarantined = true;
    _lastStatus = AudioHardwareStatus::TIMEOUT;
    strncpy(_lastError, "Timeout terminaison live_mic_task (ressource en quarantaine)", sizeof(_lastError));
    Serial.println("[LiveStreamer] ALERTE : Timeout terminaison tache FreeRTOS micro ! Quarantaine activee.");
    return false;
#else
    if (_simMicTerminationTimeout || _simTerminationTimeout) {
        _taskQuarantined = true;
        _lastStatus = AudioHardwareStatus::TIMEOUT;
        strncpy(_lastError, "Timeout terminaison live_mic_task (simulation)", sizeof(_lastError));
        return false;
    }
    _micTaskRunning = false;
    _micTaskHandle = nullptr;
    _taskQuarantined = false;
    return true;
#endif
}

bool LiveAudioStreamer::waitForTxTaskTermination(uint32_t timeoutMs) {
#if defined(ESP32) || defined(ARDUINO)
    if (_txTaskHandle == nullptr) {
        _txTaskRunning = false;
        return true;
    }

    if (!_taskEventGroup) {
        uint32_t t0 = millis();
        while (_txTaskRunning && (millis() - t0 < timeoutMs)) {
            delay(2);
        }
        if (!_txTaskRunning) {
            _txTaskHandle = nullptr;
            _txTaskQuarantined = false;
            return true;
        }
        _txTaskQuarantined = true;
        _lastStatus = AudioHardwareStatus::TIMEOUT;
        strncpy(_lastError, "Timeout terminaison live_tx_task", sizeof(_lastError));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        _taskEventGroup,
        LIVE_TX_TASK_STOPPED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeoutMs)
    );

    if (bits & LIVE_TX_TASK_STOPPED_BIT) {
        _txTaskRunning = false;
        _txTaskHandle = nullptr;
        _txTaskQuarantined = false;
        return true;
    }

    _txTaskQuarantined = true;
    _lastStatus = AudioHardwareStatus::TIMEOUT;
    strncpy(_lastError, "Timeout terminaison live_tx_task (ressource en quarantaine)", sizeof(_lastError));
    Serial.println("[LiveStreamer] ALERTE : Timeout terminaison tache FreeRTOS TX ! Quarantaine activee.");
    return false;
#else
    if (_simTxTerminationTimeout || _simTerminationTimeout) {
        _txTaskQuarantined = true;
        _lastStatus = AudioHardwareStatus::TIMEOUT;
        strncpy(_lastError, "Timeout terminaison live_tx_task (simulation)", sizeof(_lastError));
        return false;
    }
    _txTaskRunning = false;
    _txTaskHandle = nullptr;
    _txTaskQuarantined = false;
    return true;
#endif
}

bool LiveAudioStreamer::checkQuarantinedTasks() {
#if defined(ESP32) || defined(ARDUINO)
    if (_taskQuarantined && _taskEventGroup) {
        EventBits_t bits = xEventGroupGetBits(_taskEventGroup);
        if ((bits & LIVE_MIC_TASK_STOPPED_BIT) || !_micTaskRunning) {
            _taskQuarantined = false;
            _micTaskHandle = nullptr;
            _micTaskRunning = false;
            Serial.println("[LiveStreamer] Quarantaine levee pour live_mic_task");
        }
    }
    if (_txTaskQuarantined && _taskEventGroup) {
        EventBits_t bits = xEventGroupGetBits(_taskEventGroup);
        if ((bits & LIVE_TX_TASK_STOPPED_BIT) || !_txTaskRunning) {
            _txTaskQuarantined = false;
            _txTaskHandle = nullptr;
            _txTaskRunning = false;
            for (size_t i = 0; i < LIVE_AUDIO_TX_QUEUE_MAX; ++i) {
                if (_ringBuffers[i].state == LiveBufferState::SENDING) {
                    _ringBuffers[i].state = LiveBufferState::FREE;
                }
            }
            Serial.println("[LiveStreamer] Quarantaine levee pour live_tx_task");
        }
    }
#else
    if (_txTaskQuarantined && !_txTaskRunning) {
        _txTaskQuarantined = false;
        _txTaskHandle = nullptr;
        for (size_t i = 0; i < LIVE_AUDIO_TX_QUEUE_MAX; ++i) {
            if (_ringBuffers[i].state == LiveBufferState::SENDING) {
                _ringBuffers[i].state = LiveBufferState::FREE;
            }
        }
    }
    if (_taskQuarantined && !_micTaskRunning) {
        _taskQuarantined = false;
        _micTaskHandle = nullptr;
    }
#endif
    return !_taskQuarantined && !_txTaskQuarantined;
}

bool LiveAudioStreamer::begin() {
    if (!_audioInput) {
        _audioInput = HardwareManager::getInstance().getAudioInput();
    }
    if (!_audioOutput) {
        _audioOutput = HardwareManager::getInstance().getAudioOutput();
    }

#if defined(ESP32) || defined(ARDUINO)
    if (!_txQueue) {
        _txQueue = xQueueCreate(LIVE_AUDIO_TX_QUEUE_MAX, sizeof(size_t));
    }
    if (!_taskEventGroup) {
        _taskEventGroup = xEventGroupCreate();
    }
#endif
    return true;
}

#if defined(ESP32) || defined(ARDUINO)
void LiveAudioStreamer::micTaskEntry(void* param) {
    auto* self = static_cast<LiveAudioStreamer*>(param);
    if (self) {
        self->micTaskLoop();
    }
    vTaskDelete(nullptr);
}

void LiveAudioStreamer::txTaskEntry(void* param) {
    auto* self = static_cast<LiveAudioStreamer*>(param);
    if (self) {
        self->txTaskLoop();
    }
    vTaskDelete(nullptr);
}

void LiveAudioStreamer::txTaskLoop() {
    _txTaskRunning = true;
    if (_taskEventGroup) {
        xEventGroupSetBits(_taskEventGroup, LIVE_TX_TASK_STARTED_BIT);
    }

    _lastConsumerPassUs = micros();

    while (_isStreaming || (_stopDrainRequested && _txQueue && uxQueueMessagesWaiting(_txQueue) > 0)) {
        size_t slot = 0;
        // Dépilage avec attente bornée (20 ms) pour libérer le cœur CPU
        if (_txQueue && xQueueReceive(_txQueue, &slot, pdMS_TO_TICKS(20)) == pdTRUE) {
            uint32_t nowUs = micros();
            uint32_t intervalUs = (nowUs >= _lastConsumerPassUs) ? (nowUs - _lastConsumerPassUs) : 0;
            if (intervalUs > _maxConsumerIntervalUs) {
                _maxConsumerIntervalUs = intervalUs;
            }
            _lastConsumerPassUs = nowUs;

            if (slot < LIVE_AUDIO_TX_QUEUE_MAX) {
                _ringBuffers[slot].state = LiveBufferState::SENDING;

                uint32_t sendStartUs = micros();
                bool sent = false;
                if (_client && _client->isConnected()) {
                    sent = _client->sendAudioChunk(_ringBuffers[slot].samples, AUDIO_CHUNK_SAMPLES);
                }
                uint32_t sendDurationUs = micros() - sendStartUs;

                _lastSendDurationUs = sendDurationUs;
                if (sendDurationUs > _maxSendDurationUs) {
                    _maxSendDurationUs = sendDurationUs;
                }
                if (sendDurationUs > 32000) {
                    _sendsOver32msCount++;
                }
                if (sendDurationUs > 100000) {
                    _sendsOver100msCount++;
                }

                _ringBuffers[slot].state = LiveBufferState::FREE;

                if (sent) {
                    _chunksSent++;
                    _bytesSent += AUDIO_CHUNK_SAMPLES * sizeof(int16_t);
                } else {
                    _sendRefusedCount++;
                    _lastTxStatus = LiveTxStatus::TX_SEND_FAILED;
                    if (_client && !_client->isConnected()) {
                        _isStreaming = false;
                        strncpy(_lastError, "WebSocket deconnecte pendant streaming", sizeof(_lastError));
                        break;
                    }
                }
            }
        }
    }

    // Vidage ordonné suivi du signal de fin d'activité vocale (activityEnd)
    // RÈGLE v0.2.1.2 : Ne pas envoyer activityEnd si des blocs ont été perdus sans signaler l'échec
    if (_stopDrainRequested && !_queueSaturated && _lastTxStatus != LiveTxStatus::TX_STOP_REQUESTED) {
        if (_consecutiveDroppedBlocks > 0 || _blocksDropped > 0 || _lastTxStatus == LiveTxStatus::TX_SEND_FAILED || _lastTxStatus == LiveTxStatus::TX_QUEUE_FULL || _lastTxStatus == LiveTxStatus::TX_SLOT_UNAVAILABLE) {
            Serial.printf("[LiveStreamer] ALERTE : Blocs perdus (%u perdus) ou echec TX (%s) ! activityEnd annule avec signalement d'echec.\n",
                          (unsigned)_blocksDropped, liveTxStatusToString(_lastTxStatus));
            strncpy(_lastError, "Echec transmission : blocs perdus avant activityEnd", sizeof(_lastError));
        } else {
            if (_client && _client->isConnected()) {
                if (_client->sendActivityEnd()) {
                    _activityEndSent = true;
                    _activityEndSentMs = millis();
                    if (_activityEndSentMs == 0) _activityEndSentMs = 1;
                }
            }
        }
    }

    _txTaskRunning = false;
    if (_taskEventGroup) {
        xEventGroupSetBits(_taskEventGroup, LIVE_TX_TASK_STOPPED_BIT);
    }
}

void LiveAudioStreamer::micTaskLoop() {
    _micTaskRunning = true;

    if (!_audioInput) {
        _isStreaming = false;
        _micTaskRunning = false;
        if (_taskEventGroup) {
            xEventGroupSetBits(_taskEventGroup, LIVE_MIC_TASK_START_FAIL_BIT | LIVE_MIC_TASK_STOPPED_BIT);
        }
        return;
    }

    // Pré-armement des deux slots matériels pour flux continu sans trou
    size_t recQueue[2] = {0, 1};
    size_t recCount = 0;

    _ringBuffers[0].state = LiveBufferState::RECORDING;
    if (_audioInput->record(_ringBuffers[0].samples, AUDIO_CHUNK_SAMPLES, AUDIO_SAMPLE_RATE_HZ, false)) {
        _blocksAcquired++;
        recQueue[recCount++] = 0;
    } else {
        _ringBuffers[0].state = LiveBufferState::FREE;
    }

    _ringBuffers[1].state = LiveBufferState::RECORDING;
    if (_audioInput->record(_ringBuffers[1].samples, AUDIO_CHUNK_SAMPLES, AUDIO_SAMPLE_RATE_HZ, false)) {
        _blocksAcquired++;
        recQueue[recCount++] = 1;
    } else {
        _ringBuffers[1].state = LiveBufferState::FREE;
    }

    if (recCount < 2) {
        strncpy(_lastError, "Echec initialisation double-tampon micro", sizeof(_lastError));
        for (size_t i = 0; i < recCount; ++i) {
            _ringBuffers[recQueue[i]].state = LiveBufferState::FREE;
        }
        _isStreaming = false;
        _micTaskRunning = false;
        if (_taskEventGroup) {
            xEventGroupSetBits(_taskEventGroup, LIVE_MIC_TASK_START_FAIL_BIT | LIVE_MIC_TASK_STOPPED_BIT);
        }
        return;
    }

    // Confirmation explicite de mise en service effective
    if (_taskEventGroup) {
        xEventGroupSetBits(_taskEventGroup, LIVE_MIC_TASK_STARTED_BIT);
    }

    while (_isStreaming) {
        // Attente active non bloquante que le bloc actif en tête soit 100% acquis par DMA
        uint32_t waitStart = millis();
        bool timedOut = false;
        while (_isStreaming && _audioInput->isRecording() > (int)(recCount - 1)) {
            vTaskDelay(pdMS_TO_TICKS(2));
            if (millis() - waitStart > 120) {
                timedOut = true;
                Serial.println("[LiveStreamer] Timeout DMA microphone (> 120 ms) !");
                break;
            }
        }
        if (!_isStreaming) break;

        size_t currentSlot = recQueue[0];
        recQueue[0] = recQueue[1];
        recCount--;

        if (timedOut) {
            _blocksDropped++;
            _ringBuffers[currentSlot].state = LiveBufferState::FREE;
            Serial.printf("[LiveStreamer] Bloc %u abandonne suite au timeout DMA (non complet)\n", (unsigned)currentSlot);
        } else {
            _ringBuffers[currentSlot].state = LiveBufferState::READY;
            _blocksCompleted++;
            uint32_t rms = computeRms(_ringBuffers[currentSlot].samples, AUDIO_CHUNK_SAMPLES);
            if (_chunkCallback) {
                _chunkCallback(_ringBuffers[currentSlot].samples, AUDIO_CHUNK_SAMPLES, rms);
            }

            // Envoi de l'index du bloc prêt dans la file d'émission réseau (non bloquant)
            if (_txQueue) {
                if (xQueueSend(_txQueue, &currentSlot, 0) == pdTRUE) {
                    _consecutiveDroppedBlocks = 0;
                    size_t qSize = uxQueueMessagesWaiting(_txQueue);
                    if (qSize > _maxQueueOccupancy) {
                        _maxQueueOccupancy = qSize;
                    }
                } else {
                    // Saturation temporaire de la file TX (Correction C)
                    _blocksDropped++;
                    _queueSaturationCount++;
                    _consecutiveDroppedBlocks++;
                    _lastTxStatus = LiveTxStatus::TX_QUEUE_FULL;
                    _ringBuffers[currentSlot].state = LiveBufferState::FREE;

                    if (_consecutiveDroppedBlocks >= MAX_CONSECUTIVE_DROPPED_BLOCKS) {
                        _queueSaturated = true;
                        Serial.printf("[LiveStreamer] Saturation critique file TX (%u blocs consecutifs perdus) ! Abandon.\n",
                                      (unsigned)_consecutiveDroppedBlocks);
                        break;
                    } else {
                        Serial.printf("[LiveStreamer] TX_QUEUE_FULL : bloc %u abandonne (perte consecutive: %u/%u)\n",
                                      (unsigned)currentSlot, (unsigned)_consecutiveDroppedBlocks, (unsigned)MAX_CONSECUTIVE_DROPPED_BLOCKS);
                    }
                }
            }
        }

        // Réarmement d'un slot disponible pour maintenir le double-buffering
        if (_audioInput->isRecording() < 2 && _isStreaming) {
            int freeSlot = -1;
            for (size_t i = 0; i < LIVE_AUDIO_TX_QUEUE_MAX; ++i) {
                if (_ringBuffers[i].state == LiveBufferState::FREE) {
                    freeSlot = (int)i;
                    break;
                }
            }

            if (freeSlot >= 0) {
                _ringBuffers[freeSlot].state = LiveBufferState::RECORDING;
                if (_audioInput->record(_ringBuffers[freeSlot].samples, AUDIO_CHUNK_SAMPLES, AUDIO_SAMPLE_RATE_HZ, false)) {
                    _blocksAcquired++;
                    recQueue[recCount++] = (size_t)freeSlot;
                } else {
                    _ringBuffers[freeSlot].state = LiveBufferState::FREE;
                }
            } else {
                // RÈGLE v0.4.1.2 & v0.2.1.1 : Aucun tampon FREE disponible
                _blocksDropped++;
                _consecutiveDroppedBlocks++;
                _lastTxStatus = LiveTxStatus::TX_SLOT_UNAVAILABLE;
                if (_consecutiveDroppedBlocks >= MAX_CONSECUTIVE_DROPPED_BLOCKS) {
                    _queueSaturated = true;
                    Serial.println("[LiveStreamer] Saturation critique : aucun tampon FREE disponible ! Abandon propre.");
                    break;
                } else {
                    Serial.printf("[LiveStreamer] TX_SLOT_UNAVAILABLE : aucun tampon FREE (perte consecutive: %u/%u)\n",
                                  (unsigned)_consecutiveDroppedBlocks, (unsigned)MAX_CONSECUTIVE_DROPPED_BLOCKS);
                }
            }
        }
    }

    _micTaskRunning = false;
    if (_taskEventGroup) {
        xEventGroupSetBits(_taskEventGroup, LIVE_MIC_TASK_STOPPED_BIT);
    }
}
#endif

bool LiveAudioStreamer::start(GeminiLiveClient* client) {
    if (!client || !client->isConnected() || !client->isSetupComplete()) {
        strncpy(_lastError, "Client Gemini Live non pret", sizeof(_lastError));
        _lastStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
        return false;
    }

    // CORRECTION E : Contrôle et levée des quarantaines si les anciennes tâches sont enfin terminées
    checkQuarantinedTasks();

    // Interdire tout nouveau démarrage tant que les anciens workers ne sont pas formellement terminés
    if (_micTaskRunning || _micTaskHandle != nullptr || _txTaskRunning || _txTaskHandle != nullptr ||
        _taskQuarantined || _txTaskQuarantined) {
        strncpy(_lastError, "Taches precedentes non terminees ou en quarantaine", sizeof(_lastError));
        _lastStatus = AudioHardwareStatus::DEVICE_BUSY;
        Serial.printf("[LiveStreamer] Refus demarrage : %s\n", _lastError);
        return false;
    }

    // RÈGLE v0.2.1.2 : Interdiction de réutiliser des buffers appartenant encore à une ancienne tâche
    if (!areAllBuffersFree()) {
        strncpy(_lastError, "Tampons non libres (detenus par ancienne tache)", sizeof(_lastError));
        _lastStatus = AudioHardwareStatus::DEVICE_BUSY;
        Serial.printf("[LiveStreamer] Refus demarrage : %s\n", _lastError);
        return false;
    }

    // Réinitialisation de l'état de tous les tampons à FREE
    for (size_t i = 0; i < LIVE_AUDIO_TX_QUEUE_MAX; ++i) {
        _ringBuffers[i].state = LiveBufferState::FREE;
    }

    _client = client;
    _queueSaturated = false;
    _stopDrainRequested = false;
    _lastError[0] = '\0';
    _chunksSent = 0;
    _bytesSent = 0;
    _blocksAcquired = 0;
    _blocksCompleted = 0;
    _blocksDropped = 0;
    _sendRefusedCount = 0;
    _maxQueueOccupancy = 0;
    _lastSendDurationUs = 0;
    _maxSendDurationUs = 0;
    _sendsOver32msCount = 0;
    _sendsOver100msCount = 0;
    _maxConsumerIntervalUs = 0;
    _lastConsumerPassUs = micros();
    _queueSaturationCount = 0;
    _consecutiveDroppedBlocks = 0;
    _lastTxStatus = LiveTxStatus::OK;
    _lastPeriodicLogMs = millis();
    _sumRms = 0;
    _rmsCount = 0;
    _peakRms = 0;
    _lastChunkRms = 0;
    _activityStartSent = false;
    _activityEndSent = false;
    _micStoppedMs = 0;
    _activityEndSentMs = 0;

    if (!_audioInput) {
        _audioInput = HardwareManager::getInstance().getAudioInput();
    }
    if (!_audioOutput) {
        _audioOutput = HardwareManager::getInstance().getAudioOutput();
    }

    // Arbitrage matériel via HAL : Arrêt et libération stricte du haut-parleur avant activation micro
    if (HardwareManager::getInstance().getAudioOutput() != nullptr) {
        if (!HardwareManager::getInstance().prepareMicCapture()) {
            strncpy(_lastError, "Arbitrage I2S refuse (HP actif)", sizeof(_lastError));
            _lastStatus = HardwareManager::getInstance().getLastAudioStatus();
            Serial.println("[LiveStreamer] Echec : Arbitrage I2S refuse pour capture microphone.");
            return false;
        }
    } else if (_audioOutput && _audioOutput->isRunning()) {
        _audioOutput->end();
    }

    if (_audioInput && !_audioInput->isRunning()) {
        if (!_audioInput->begin()) {
            strncpy(_lastError, "Echec initialisation micro", sizeof(_lastError));
            _lastStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
            HardwareManager::getInstance().restoreSpeakerAfterMic();
            return false;
        }
    }

#if defined(ESP32) || defined(ARDUINO)
    if (!_txQueue) {
        _txQueue = xQueueCreate(LIVE_AUDIO_TX_QUEUE_MAX, sizeof(size_t));
        if (!_txQueue) {
            strncpy(_lastError, "Echec creation file txQueue (memoire insuffisante)", sizeof(_lastError));
            _lastStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
            stopMicHardware();
            return false;
        }
    } else {
        xQueueReset(_txQueue);
    }

    if (!_taskEventGroup) {
        _taskEventGroup = xEventGroupCreate();
        if (!_taskEventGroup) {
            strncpy(_lastError, "Echec creation EventGroup (memoire insuffisante)", sizeof(_lastError));
            _lastStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
            stopMicHardware();
            return false;
        }
    }
    xEventGroupClearBits(_taskEventGroup,
        LIVE_MIC_TASK_STARTED_BIT | LIVE_MIC_TASK_START_FAIL_BIT | LIVE_MIC_TASK_STOPPED_BIT |
        LIVE_TX_TASK_STARTED_BIT  | LIVE_TX_TASK_START_FAIL_BIT  | LIVE_TX_TASK_STOPPED_BIT);
#else
    while (!_nativeTxQueue.empty()) _nativeTxQueue.pop();
#endif

    _startTimeMs = millis();
    _durationMs = 0;
    _isStreaming = true;

    // Signalisation du début d'activité vocale officielle selon le protocole Gemini Live
    // Garantit l'ordonnancement strict : activityStart AVANT tout bloc audio (Correction B)
    if (_client->sendActivityStart()) {
        _activityStartSent = true;
    }

#if defined(ESP32) || defined(ARDUINO)
    // Démarrage de la tâche FreeRTOS dédiée à l'émission réseau (Correction B)
    BaseType_t txTaskRes = xTaskCreatePinnedToCore(
        txTaskEntry,
        "live_tx_task",
        LIVE_TX_TASK_STACK_SIZE,
        this,
        3, // Priorité 3 (au-dessus UI, au même niveau que le micro)
        &_txTaskHandle,
        1  // Core 1
    );

    if (txTaskRes != pdPASS || _txTaskHandle == nullptr) {
        _isStreaming = false;
        _txTaskHandle = nullptr;
        _txTaskRunning = false;
        char errBuf[64];
        strncpy(errBuf, "Echec creation live_tx_task (memoire FreeRTOS)", sizeof(errBuf));
        stopMicHardware();
        strncpy(_lastError, errBuf, sizeof(_lastError));
        _lastStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
        return false;
    }

    // Démarrage de la tâche FreeRTOS dédiée à l'acquisition micro
    BaseType_t micTaskRes = xTaskCreatePinnedToCore(
        micTaskEntry,
        "live_mic_task",
        LIVE_MIC_TASK_STACK_SIZE,
        this,
        3,
        &_micTaskHandle,
        1  // Core 1
    );

    if (micTaskRes != pdPASS || _micTaskHandle == nullptr) {
        _isStreaming = false;
        _micTaskHandle = nullptr;
        _micTaskRunning = false;
        char errBuf[64];
        strncpy(errBuf, "Echec creation live_mic_task (memoire FreeRTOS)", sizeof(errBuf));
        waitForTxTaskTermination(150);
        stopMicHardware();
        strncpy(_lastError, errBuf, sizeof(_lastError));
        _lastStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
        return false;
    }

    // Synchronisation explicite de démarrage des deux tâches
    EventBits_t bits = xEventGroupWaitBits(
        _taskEventGroup,
        LIVE_MIC_TASK_STARTED_BIT | LIVE_TX_TASK_STARTED_BIT,
        pdFALSE,
        pdTRUE, // Attendre les deux
        pdMS_TO_TICKS(250)
    );

    if ((bits & (LIVE_MIC_TASK_STARTED_BIT | LIVE_TX_TASK_STARTED_BIT)) !=
        (LIVE_MIC_TASK_STARTED_BIT | LIVE_TX_TASK_STARTED_BIT)) {
        _isStreaming = false;
        char errBuf[64];
        strncpy(errBuf, "Timeout confirmation demarrage des taches micro/TX", sizeof(errBuf));
        Serial.printf("[LiveStreamer] ERREUR : %s (bits=0x%02X)\n", errBuf, (unsigned)bits);
        waitForTaskTermination(200);
        waitForTxTaskTermination(200);
        stopMicHardware();
        strncpy(_lastError, errBuf, sizeof(_lastError));
        _lastStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
        return false;
    }
#else
    if (_simTaskCreateHook) {
        bool ok = _simTaskCreateHook(this);
        if (!ok) {
            _isStreaming = false;
            _micTaskHandle = nullptr;
            _txTaskHandle = nullptr;
            _micTaskRunning = false;
            _txTaskRunning = false;
            char errBuf[64];
            strncpy(errBuf, "Echec creation live_mic_task (memoire FreeRTOS insuffisante)", sizeof(errBuf));
            stopMicHardware();
            strncpy(_lastError, errBuf, sizeof(_lastError));
            _lastStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
            return false;
        }
    }
    if (_simStartupFailure) {
        _isStreaming = false;
        _micTaskHandle = nullptr;
        _txTaskHandle = nullptr;
        _micTaskRunning = false;
        _txTaskRunning = false;
        char errBuf[64];
        strncpy(errBuf, "Echec initialisation double-tampon micro", sizeof(errBuf));
        stopMicHardware();
        strncpy(_lastError, errBuf, sizeof(_lastError));
        _lastStatus = AudioHardwareStatus::RECOVERABLE_ERROR;
        return false;
    }
    _micTaskHandle = (void*)0x1234;
    _txTaskHandle = (void*)0x5678;
    _micTaskRunning = true;
    _txTaskRunning = true;
#endif

    _lastStatus = AudioHardwareStatus::SUCCESS;
    Serial.println("[LiveStreamer] Demarrage streaming micro PCM 16 kHz vers Gemini Live (Pipeline TX decouple)");
    return true;
}

bool LiveAudioStreamer::tick() {
    if (!_isStreaming || !_client) return false;

    // Détection de saturation de la file (si le réseau est plus lent que le micro)
    if (_queueSaturated) {
        _isStreaming = false;
        strncpy(_lastError, "File emission saturee (reseau lent)", sizeof(_lastError));
        Serial.println("[LiveStreamer] ALERTE : Saturation de la file d'emission ! Abandon.");
        stopMicHardware();
        waitForTaskTermination(150);
        waitForTxTaskTermination(150);
        return false;
    }

    size_t qSize = getQueueSize();
    if (qSize > _maxQueueOccupancy) {
        _maxQueueOccupancy = qSize;
    }

#if defined(ESP32) || defined(ARDUINO)
    // Sous FreeRTOS, live_tx_task consomme les blocs de façon découplée sans bloquer le tick()
    uint32_t now = millis();
    if (now - _lastPeriodicLogMs >= 3000) {
        _lastPeriodicLogMs = now;
        Serial.printf("[LiveStreamer TX] Blocs: acq=%u, env=%u, perdus=%u | File: %u/%u (pic %u) | Envoi: der=%u us, max=%u us (>32ms: %u, >100ms: %u) | Statut: %s\n",
                      _blocksAcquired, _chunksSent, _blocksDropped,
                      (unsigned)qSize, (unsigned)LIVE_AUDIO_TX_QUEUE_MAX, (unsigned)_maxQueueOccupancy,
                      _lastSendDurationUs, _maxSendDurationUs, _sendsOver32msCount, _sendsOver100msCount,
                      liveTxStatusToString(_lastTxStatus));
    }
#else
    // Dépilage direct sous environnement hôte (tests natifs MinGW)
    while (!_nativeTxQueue.empty()) {
        uint32_t nowUs = micros();
        if (_lastConsumerPassUs > 0) {
            uint32_t intervalUs = (nowUs >= _lastConsumerPassUs) ? (nowUs - _lastConsumerPassUs) : 0;
            if (intervalUs > _maxConsumerIntervalUs) {
                _maxConsumerIntervalUs = intervalUs;
            }
        }
        _lastConsumerPassUs = nowUs;

        size_t slot = _nativeTxQueue.front();
        _nativeTxQueue.pop();
        _ringBuffers[slot].state = LiveBufferState::SENDING;

        uint32_t sendStartUs = micros();
        bool sent = _client->sendAudioChunk(_ringBuffers[slot].samples, AUDIO_CHUNK_SAMPLES);
        uint32_t sendDurationUs = micros() - sendStartUs;

        _lastSendDurationUs = sendDurationUs;
        if (sendDurationUs > _maxSendDurationUs) {
            _maxSendDurationUs = sendDurationUs;
        }
        if (sendDurationUs > 32000) {
            _sendsOver32msCount++;
        }
        if (sendDurationUs > 100000) {
            _sendsOver100msCount++;
        }

        _ringBuffers[slot].state = LiveBufferState::FREE;
        if (!sent) {
            _sendRefusedCount++;
            _lastTxStatus = LiveTxStatus::TX_SEND_FAILED;
            strncpy(_lastError, "Echec transmission reseau bloc audio", sizeof(_lastError));
            _isStreaming = false;
            return false;
        }
        _chunksSent++;
        _bytesSent += AUDIO_CHUNK_SAMPLES * sizeof(int16_t);
    }
#endif

    return true;
}

bool LiveAudioStreamer::stop() {
    bool micActive = _audioInput ? (_audioInput->isRunning() || _audioInput->isRecording() > 0) : false;
    if (!_isStreaming && !micActive && !_micTaskRunning && _micTaskHandle == nullptr) return false;

    _durationMs = millis() - _startTimeMs;

#if defined(ESP32) || defined(ARDUINO)
    // Règle v0.4.1.1 : Sur relâchement, attendre la fin du bloc en cours d'enregistrement (max 40 ms)
    uint32_t waitBlockStart = millis();
    while (_audioInput && _audioInput->isRecording() > 1 && (millis() - waitBlockStart < 40)) {
        delay(2);
    }
#endif

    _isStreaming = false;
    _stopDrainRequested = true;

    // Terminaison garantie de la tâche micro avant arrêt matériel
    bool termOk = waitForTaskTermination(250);
    if (!termOk) {
        Serial.println("[LiveStreamer] AVERTISSEMENT : La tâche micro ne s'est pas arrêtée dans le délai imparti !");
    }

    // Arrêt matériel sécurisé du microphone & réarmement du haut-parleur via HAL
    bool hwOk = stopMicHardware();

#if defined(ESP32) || defined(ARDUINO)
    // Attente que la tâche TX dédiée vide les derniers blocs valides et émette activityEnd
    // Basé sur le comportement réel du transport WebSocket (1000 ms timeout + 200 ms marge)
    bool txTermOk = waitForTxTaskTermination(LIVE_TX_TERMINATION_TIMEOUT_MS);
    if (!txTermOk) {
        Serial.println("[LiveStreamer] AVERTISSEMENT : La tache TX ne s'est pas arretee dans le delai imparti ! Tampons maintenus en quarantaine.");
    }
#else
    if (_client && _client->isConnected() && _lastTxStatus != LiveTxStatus::TX_STOP_REQUESTED) {
        bool sendFailed = false;
        while (!_nativeTxQueue.empty()) {
            size_t slot = _nativeTxQueue.front();
            _nativeTxQueue.pop();
            _ringBuffers[slot].state = LiveBufferState::SENDING;
            if (_client->sendAudioChunk(_ringBuffers[slot].samples, AUDIO_CHUNK_SAMPLES)) {
                _chunksSent++;
                _bytesSent += AUDIO_CHUNK_SAMPLES * sizeof(int16_t);
            } else {
                _sendRefusedCount++;
                _lastTxStatus = LiveTxStatus::TX_SEND_FAILED;
                sendFailed = true;
            }
            _ringBuffers[slot].state = LiveBufferState::FREE;
        }
        if (!sendFailed && _blocksDropped == 0 && !_queueSaturated) {
            if (_client->sendActivityEnd()) {
                _activityEndSent = true;
                _activityEndSentMs = millis();
                if (_activityEndSentMs == 0) _activityEndSentMs = 1;
            }
        } else {
            Serial.println("[LiveStreamer] activityEnd annule suite a perte de blocs ou echec envoi");
            strncpy(_lastError, "Echec transmission : blocs perdus avant activityEnd", sizeof(_lastError));
        }
    }
    bool txTermOk = waitForTxTaskTermination(LIVE_TX_TERMINATION_TIMEOUT_MS);
#endif

    // RÈGLE v0.2.1.2 : Ne JAMAIS libérer les buffers si la tâche TX est encore active ou en quarantaine !
    if (txTermOk && !_txTaskRunning && !_txTaskQuarantined) {
        for (size_t i = 0; i < LIVE_AUDIO_TX_QUEUE_MAX; ++i) {
            _ringBuffers[i].state = LiveBufferState::FREE;
        }
    } else {
        Serial.println("[LiveStreamer] Quarantaine : tampons TX non liberes car la tache TX est toujours active");
    }

    printDiagnosticSummary();

    if (!hwOk) {
        Serial.println("[LiveStreamer] AVERTISSEMENT : Echec transition materielle dans stop() !");
    }
    return hwOk;
}

void LiveAudioStreamer::cancel() {
    if (_isStreaming || _micTaskRunning || _micTaskHandle != nullptr || _txTaskRunning || _txTaskHandle != nullptr || _txTaskQuarantined || _taskQuarantined) {
        _isStreaming = false;
        _stopDrainRequested = false;
        _lastTxStatus = LiveTxStatus::TX_STOP_REQUESTED;
        _durationMs = millis() - _startTimeMs;

#if defined(ESP32) || defined(ARDUINO)
        if (_txQueue) {
            xQueueReset(_txQueue);
        }
#else
        while (!_nativeTxQueue.empty()) _nativeTxQueue.pop();
#endif

        bool micTermOk = waitForTaskTermination(250);
        bool txTermOk = waitForTxTaskTermination(LIVE_TX_TERMINATION_TIMEOUT_MS);

        stopMicHardware();

        // RÈGLE v0.2.1.2 : Ne libérer les tampons que si les deux tâches sont formellement arrêtées !
        if (txTermOk && micTermOk && !_txTaskRunning && !_txTaskQuarantined && !_micTaskRunning && !_taskQuarantined) {
            for (size_t i = 0; i < LIVE_AUDIO_TX_QUEUE_MAX; ++i) {
                _ringBuffers[i].state = LiveBufferState::FREE;
            }
            Serial.println("[LiveStreamer] Annulation STOP : abandon des blocs non transmis et liberation confirmee");
        } else {
            Serial.println("[LiveStreamer] Annulation STOP : tache encore active, tampons conserves en quarantaine");
        }
        printDiagnosticSummary();
    }
}

void LiveAudioStreamer::printDiagnosticSummary() const {
#if VOXCORE_DIAGNOSTICS_LEVEL >= 2
    Serial.println("\n--------------------------------------------------");
    Serial.println("  [AUDIO TX BILAN] Diagnostic Emission Microphone  ");
    Serial.println("--------------------------------------------------");
    Serial.printf("  - Blocs PCM acquis / produits : %u\n", _blocksAcquired);
    Serial.printf("  - Blocs PCM termines (DMA)   : %u\n", _blocksCompleted);
    Serial.printf("  - Blocs transmis vers Gemini  : %u (%u octets)\n", _chunksSent, _bytesSent);
    Serial.printf("  - Blocs abandonnes (perdus)   : %u\n", _blocksDropped);
    Serial.printf("  - Refus d'envoi WebSocket     : %u\n", _sendRefusedCount);
    Serial.printf("  - Saturations de file TX      : %u\n", _queueSaturationCount);
    Serial.printf("  - Pic d'occupation file TX    : %u / %u blocs\n", (unsigned)_maxQueueOccupancy, (unsigned)LIVE_AUDIO_TX_QUEUE_MAX);
    Serial.printf("  - Duree dernier envoi WS      : %u us (%.2f ms)\n", _lastSendDurationUs, _lastSendDurationUs / 1000.0);
    Serial.printf("  - Duree maximale envoi WS     : %u us (%.2f ms)\n", _maxSendDurationUs, _maxSendDurationUs / 1000.0);
    Serial.printf("  - Envois depassant 32 ms      : %u\n", _sendsOver32msCount);
    Serial.printf("  - Envois depassant 100 ms     : %u\n", _sendsOver100msCount);
    Serial.printf("  - Intervalle max consommateur : %u us (%.2f ms)\n", _maxConsumerIntervalUs, _maxConsumerIntervalUs / 1000.0);
    Serial.printf("  - Dernier statut TX           : %s\n", liveTxStatusToString(_lastTxStatus));
    Serial.printf("  - Duree reelle / theorique    : %u ms / %u ms (RMS moy: %u, crete: %u)\n",
                  _durationMs, getTheoreticalDurationMs(), getAverageRms(), _peakRms);
    Serial.printf("  - activityStart : %s | activityEnd : %s\n",
                  _activityStartSent ? "OUI" : "NON", _activityEndSent ? "OUI" : "NON");
    Serial.println("--------------------------------------------------\n");
#elif VOXCORE_DIAGNOSTICS_LEVEL == 1
    if (_blocksDropped > 0 || _sendRefusedCount > 0 || _queueSaturationCount > 0) {
        Serial.printf("[LiveStreamer] Alerte TX : %u blocs abandonnes, %u refus WS, %u saturations file TX\n",
                      _blocksDropped, _sendRefusedCount, _queueSaturationCount);
    }
#endif
}

uint32_t LiveAudioStreamer::getDurationMs() const {
    if (_isStreaming) {
        return millis() - _startTimeMs;
    }
    return _durationMs;
}

size_t LiveAudioStreamer::getQueueSize() const {
#if defined(ESP32) || defined(ARDUINO)
    if (_txQueue) {
        return uxQueueMessagesWaiting(_txQueue);
    }
    return 0;
#else
    return _nativeTxQueue.size();
#endif
}

bool LiveAudioStreamer::stopMicHardware() {
    _micStoppedMs = millis();
    if (_audioInput) {
        uint32_t t0 = millis();
        while (_audioInput->isRecording() && (millis() - t0 < 100)) {
            delay(2);
        }
        if (_audioInput->isRecording() != 0) {
            _lastStatus = AudioHardwareStatus::DEVICE_BUSY;
            strncpy(_lastError, "Microphone toujours occupe apres timeout", sizeof(_lastError));
            return false;
        }
        if (_audioInput->isRunning()) {
            _audioInput->end();
            delay(5);
        }
        if (_audioInput->isRunning()) {
            _lastStatus = AudioHardwareStatus::PERMANENT_FAULT;
            strncpy(_lastError, "Microphone refuse de s'arreter", sizeof(_lastError));
            return false;
        }
    }
    // Rétablissement sécurisé du haut-parleur via HAL centralisée
    if (HardwareManager::getInstance().getAudioOutput() != nullptr) {
        bool ok = HardwareManager::getInstance().restoreSpeakerAfterMic();
        if (!ok) {
            _lastStatus = HardwareManager::getInstance().getLastAudioStatus();
            strncpy(_lastError, "Echec restoreSpeakerAfterMic", sizeof(_lastError));
            return false;
        }
    } else if (_audioOutput) {
        if (!_audioOutput->isRunning()) {
            _audioOutput->begin();
        }
    }
    _lastStatus = AudioHardwareStatus::SUCCESS;
    return true;
}

uint32_t LiveAudioStreamer::computeRms(const int16_t* samples, size_t count) {
    if (!samples || count == 0) return 0;
    uint64_t sumSq = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t s = samples[i];
        sumSq += (uint64_t)(s * s);
    }
    uint32_t rms = static_cast<uint32_t>(sqrt((double)sumSq / count));
    _sumRms += rms;
    _rmsCount++;
    _lastChunkRms = rms;
    if (rms > _peakRms) {
        _peakRms = rms;
    }
    return rms;
}

#if !defined(ESP32) && !defined(ARDUINO)
bool LiveAudioStreamer::pushSimulatedChunk(const int16_t* samples, size_t count) {
    if (!_isStreaming || !samples || count != AUDIO_CHUNK_SAMPLES) return false;

    int freeSlot = -1;
    for (size_t i = 0; i < LIVE_AUDIO_TX_QUEUE_MAX; ++i) {
        if (_ringBuffers[i].state == LiveBufferState::FREE) {
            freeSlot = (int)i;
            break;
        }
    }

    if (freeSlot < 0 || _nativeTxQueue.size() >= LIVE_AUDIO_TX_QUEUE_MAX) {
        _blocksDropped++;
        _queueSaturationCount++;
        _consecutiveDroppedBlocks++;
        _lastTxStatus = (_nativeTxQueue.size() >= LIVE_AUDIO_TX_QUEUE_MAX) ? LiveTxStatus::TX_QUEUE_FULL : LiveTxStatus::TX_SLOT_UNAVAILABLE;
        if (_consecutiveDroppedBlocks >= MAX_CONSECUTIVE_DROPPED_BLOCKS) {
            _queueSaturated = true;
        }
        return false;
    }

    _consecutiveDroppedBlocks = 0;
    _ringBuffers[freeSlot].state = LiveBufferState::RECORDING;
    memcpy(_ringBuffers[freeSlot].samples, samples, count * sizeof(int16_t));
    _ringBuffers[freeSlot].state = LiveBufferState::READY;
    _blocksAcquired++;
    _blocksCompleted++;
    uint32_t rms = computeRms(samples, count);
    if (_chunkCallback) {
        _chunkCallback(samples, count, rms);
    }
    _nativeTxQueue.push((size_t)freeSlot);
    if (_nativeTxQueue.size() > _maxQueueOccupancy) {
        _maxQueueOccupancy = _nativeTxQueue.size();
    }
    return true;
}
#endif
