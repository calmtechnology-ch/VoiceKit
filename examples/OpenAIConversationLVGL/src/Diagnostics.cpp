/**
 * @file Diagnostics.cpp
 * @brief Implementation du gestionnaire de diagnostics et de metriques.
 */

#include "Diagnostics.h"
#include "AppConfig.h"

Diagnostics::Diagnostics()
    : _lastPrintMs(0) {}

void Diagnostics::logPttPress(uint32_t contactId, int32_t x, int32_t y, const char* pttState, const char* voiceState) {
    Serial.printf("[PTT][PRESS] Contact #%u a (%d, %d) | ptt_state=%s | voice_state=%s\n",
                  (unsigned)contactId, (int)x, (int)y, pttState, voiceState);
}

void Diagnostics::logPttRelease(uint32_t contactId, int32_t x, int32_t y, uint32_t durationMs, bool inListening) {
    Serial.printf("[PTT][RELEASE] Contact #%u relache a (%d, %d) apres %u ms | inListening=%d\n",
                  (unsigned)contactId, (int)x, (int)y, (unsigned)durationMs, inListening ? 1 : 0);
}

void Diagnostics::logPttReject(const char* context, const char* cause, const char* voiceState, uint32_t stateDurMs,
                              const char* sessionState, bool busy, bool turnActive, bool spk, bool mic, bool ready, bool inListening) {
    _pttRejectCount++;
    Serial.printf("[PTT][REJECT] Refus commande (%s) #%u : cause=\"%s\" | voice_state=%s (%u ms) | session=%s | busy=%d (turnActive=%d, spk=%d, mic=%d) | ready=%d | inListening=%d\n",
                  context, (unsigned)_pttRejectCount, cause, voiceState, (unsigned)stateDurMs,
                  sessionState, busy ? 1 : 0, turnActive ? 1 : 0, spk ? 1 : 0, mic ? 1 : 0, ready ? 1 : 0, inListening ? 1 : 0);
}

void Diagnostics::logPttRecovery(const char* reason, uint32_t durationMs) {
    _pttRecoveryCount++;
    Serial.printf("[PTT][RECOVERY] #%u Declenchement recuperation non bloquante : motif=\"%s\" apres %u ms -> nettoyage controle\n",
                  (unsigned)_pttRecoveryCount, reason, (unsigned)durationMs);
}

void Diagnostics::logVoiceStateChange(const char* oldState, const char* newState, uint32_t prevDurMs) {
    Serial.printf("[VOICE][STATE] %s -> %s (duree etat precedent : %u ms)\n",
                  oldState, newState, (unsigned)prevDurMs);
}

void Diagnostics::logTouch(const char* action, int32_t x, int32_t y) {
    Serial.printf("[LVGL][TOUCH] %s a (%d, %d)\n", action, (int)x, (int)y);
}

void Diagnostics::logLvglFlush(uint32_t w, uint32_t h, uint32_t durUs) {
    if (durUs > 15000) { // Journalise les transferts longs (> 15 ms) pour tracer les anomalies
        Serial.printf("[LVGL][FLUSH] Zone %ux%u px transferee en %u us\n", (unsigned)w, (unsigned)h, (unsigned)durUs);
    }
}

void Diagnostics::recordVoiceUpdate(uint32_t durUs) {
    _updateCount++;
    _sumUpdateDurUs += durUs;
    if (durUs > _maxUpdateDurUs) {
        _maxUpdateDurUs = durUs;
    }
}

void Diagnostics::recordLvglRender(uint32_t durUs) {
    _renderCount++;
    _sumRenderDurUs += durUs;
    if (durUs > _maxRenderDurUs) {
        _maxRenderDurUs = durUs;
    }
}

void Diagnostics::recordFlush(uint32_t durUs) {
    _flushCount++;
    _sumFlushDurUs += durUs;
    if (durUs > _maxFlushDurUs) {
        _maxFlushDurUs = durUs;
    }
}

void Diagnostics::recordTouch() {
    _touchEventCount++;
}

void Diagnostics::recordFirstAudioLatency(uint32_t latencyMs) {
    _lastFirstAudioLatencyMs = latencyMs;
}

void Diagnostics::recordTurnCompleted() {
    _turnsCompleted++;
}

void Diagnostics::recordStopInterrupt() {
    _stopInterruptCount++;
}

void Diagnostics::update(uint32_t nowMs, const char* voiceState, uint32_t curVoiceStateDurMs, bool wifiConnected, int8_t rssi, const char* pttState) {
    if (nowMs - _lastPrintMs >= AppConfig::DIAG_INTERVAL_MS) {
        _lastPrintMs = nowMs;
        printReport(voiceState, curVoiceStateDurMs, wifiConnected, rssi, pttState);
    }
}

void Diagnostics::printReport(const char* voiceState, uint32_t curVoiceStateDurMs, bool wifiConnected, int8_t rssi, const char* pttState) {
    uint32_t avgUpUs = (_updateCount > 0) ? (uint32_t)(_sumUpdateDurUs / _updateCount) : 0;
    uint32_t avgRenderUs = (_renderCount > 0) ? (uint32_t)(_sumRenderDurUs / _renderCount) : 0;
    uint32_t avgFlushUs = (_flushCount > 0) ? (uint32_t)(_sumFlushDurUs / _flushCount) : 0;

    Serial.println("--------------------------------------------------------");
    Serial.printf("  [DIAGNOSTICS] %s %s (LVGL 9 Edition)\n", AppConfig::APP_NAME, AppConfig::APP_VERSION);
    Serial.printf("[DIAG][LOOP]   voice.update()   : Moyenne = %u us, Max = %u us (%llu cycles)\n",
                  (unsigned)avgUpUs, (unsigned)_maxUpdateDurUs, (unsigned long long)_updateCount);
    Serial.printf("[LVGL][RENDER] lv_timer_handler : Moyenne = %u us, Max = %u us (%llu cycles)\n",
                  (unsigned)avgRenderUs, (unsigned)_maxRenderDurUs, (unsigned long long)_renderCount);
    Serial.printf("[LVGL][FLUSH]  DMA pushImage   : Moyenne = %u us, Max = %u us (%llu transferts)\n",
                  (unsigned)avgFlushUs, (unsigned)_maxFlushDurUs, (unsigned long long)_flushCount);
    Serial.printf("[DIAG][MEM]    Heap libre : %u octets (min: %u) | PSRAM libre : %u octets (min: %u)\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getFreePsram(), (unsigned)ESP.getMinFreePsram());
    Serial.printf("[DIAG][PTT]    Rejets PTT : %u | Recuperations PTT : %u | Touch events : %u\n",
                  (unsigned)_pttRejectCount, (unsigned)_pttRecoveryCount, (unsigned)_touchEventCount);
    Serial.printf("[DIAG][STAT]   Tours termines : %u | Stops : %u | Latence 1er son : %u ms\n",
                  (unsigned)_turnsCompleted, (unsigned)_stopInterruptCount, (unsigned)_lastFirstAudioLatencyMs);
    Serial.printf("[DIAG][STATE]  PTT: %s | Moteur: %s (%u ms) | Wi-Fi: %s (%d dBm)\n",
                  pttState, voiceState, (unsigned)curVoiceStateDurMs,
                  wifiConnected ? "CONNECTE" : "DECONNECTE", wifiConnected ? rssi : 0);
    Serial.println("--------------------------------------------------------");

    // Reset periodic counters
    _updateCount = 0;
    _sumUpdateDurUs = 0;
    _maxUpdateDurUs = 0;
    _renderCount = 0;
    _sumRenderDurUs = 0;
    _maxRenderDurUs = 0;
    _flushCount = 0;
    _sumFlushDurUs = 0;
    _maxFlushDurUs = 0;
}

