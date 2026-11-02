/**
 * @file Diagnostics.h
 * @brief Gestionnaire de diagnostics temps reel, metriques de performance et traces d'execution.
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

class Diagnostics {
public:
    Diagnostics();

    // Journalisation d'evenements
    void logPttPress(uint32_t contactId, int32_t x, int32_t y, const char* pttState, const char* voiceState);
    void logPttRelease(uint32_t contactId, int32_t x, int32_t y, uint32_t durationMs, bool inListening);
    void logPttReject(const char* context, const char* cause, const char* voiceState, uint32_t stateDurMs,
                      const char* sessionState, bool busy, bool turnActive, bool spk, bool mic, bool ready, bool inListening);
    void logPttRecovery(const char* reason, uint32_t durationMs);
    void logVoiceStateChange(const char* oldState, const char* newState, uint32_t prevDurMs);
    void logTouch(const char* action, int32_t x, int32_t y);
    void logLvglFlush(uint32_t w, uint32_t h, uint32_t durUs);

    // Enregistrement des mesures de boucle
    void recordVoiceUpdate(uint32_t durUs);
    void recordLvglRender(uint32_t durUs);
    void recordFlush(uint32_t durUs);
    void recordTouch();
    void recordFirstAudioLatency(uint32_t latencyMs);
    void recordTurnCompleted();
    void recordStopInterrupt();

    // Surveillance cyclique
    void update(uint32_t nowMs, const char* voiceState, uint32_t curVoiceStateDurMs, bool wifiConnected, int8_t rssi, const char* pttState);
    void printReport(const char* voiceState, uint32_t curVoiceStateDurMs, bool wifiConnected, int8_t rssi, const char* pttState);

    // Accesseurs
    uint32_t getPttRejectCount() const { return _pttRejectCount; }
    uint32_t getPttRecoveryCount() const { return _pttRecoveryCount; }
    uint32_t getTouchCount() const { return _touchEventCount; }
    uint32_t getTurnsCompleted() const { return _turnsCompleted; }
    uint32_t getStopInterruptCount() const { return _stopInterruptCount; }
    uint32_t getLastFirstAudioLatency() const { return _lastFirstAudioLatencyMs; }

private:
    uint32_t _lastPrintMs = 0;

    // Compteurs et cumuls voice.update()
    uint64_t _updateCount = 0;
    uint64_t _sumUpdateDurUs = 0;
    uint32_t _maxUpdateDurUs = 0;

    // Compteurs et cumuls lv_timer_handler()
    uint64_t _renderCount = 0;
    uint64_t _sumRenderDurUs = 0;
    uint32_t _maxRenderDurUs = 0;

    // Compteurs et cumuls flush DMA
    uint64_t _flushCount = 0;
    uint64_t _sumFlushDurUs = 0;
    uint32_t _maxFlushDurUs = 0;

    // Compteurs metier
    uint32_t _touchEventCount = 0;
    uint32_t _pttRejectCount = 0;
    uint32_t _pttRecoveryCount = 0;
    uint32_t _turnsCompleted = 0;
    uint32_t _stopInterruptCount = 0;
    uint32_t _lastFirstAudioLatencyMs = 0;
};

