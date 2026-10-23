#include "AudioTraceLog.h"

AudioTraceEvent AudioTraceLog::_events[AudioTraceLog::CAPACITY];
size_t AudioTraceLog::_head = 0;
size_t AudioTraceLog::_count = 0;

void AudioTraceLog::record(AudioTraceEventType type, uint32_t val1, uint32_t val2) {
    uint32_t now = millis();
    _events[_head] = {now, type, val1, val2};
    _head = (_head + 1) % CAPACITY;
    if (_count < CAPACITY) {
        _count++;
    }
}

void AudioTraceLog::clear() {
    _head = 0;
    _count = 0;
}

size_t AudioTraceLog::getEventCount() {
    return _count;
}

AudioTraceEvent AudioTraceLog::getEvent(size_t index) {
    if (index >= _count) {
        return {0, AudioTraceEventType::NONE, 0, 0};
    }
    size_t start = (_count < CAPACITY) ? 0 : _head;
    size_t actualIdx = (start + index) % CAPACITY;
    return _events[actualIdx];
}

const char* AudioTraceLog::eventTypeToString(AudioTraceEventType type) {
    switch (type) {
        case AudioTraceEventType::NONE:                   return "AUCUN";
        case AudioTraceEventType::WS_AUDIO_RECEIVED:      return "WS_AUDIO_RECU";
        case AudioTraceEventType::PCM_DECODED:            return "PCM_DECODE";
        case AudioTraceEventType::PCM_DECODE_FAILED:      return "PCM_DECODE_ECHEC";
        case AudioTraceEventType::PCM_ACCEPTED_PSRAM:     return "PCM_ACCEPTE_PSRAM";
        case AudioTraceEventType::PCM_REJECTED:           return "PCM_REJETE";
        case AudioTraceEventType::TURN_COMPLETE_RECEIVED: return "TURN_COMPLETE_RECU";
        case AudioTraceEventType::PLAYRAW_SUBMITTED:      return "PLAYRAW_SOUMIS";
        case AudioTraceEventType::PLAYRAW_ACCEPTED:       return "PLAYRAW_ACCEPTE";
        case AudioTraceEventType::PLAYRAW_REFUSED:        return "PLAYRAW_REFUSE";
        case AudioTraceEventType::PSRAM_BUFFER_EMPTY:     return "PSRAM_VIDE";
        case AudioTraceEventType::IS_PLAYING_ZERO:        return "IS_PLAYING_ZERO";
        case AudioTraceEventType::PLAYBACK_FINISHED:      return "FIN_LECTURE_NOTIFIEE";
        case AudioTraceEventType::SESSION_CONNECTED:      return "SESSION_CONNECTEE";
        default:                                          return "INCONNU";
    }
}

void AudioTraceLog::printSummary(uint32_t pcmDecodedTotal,
                                 uint32_t pcmDispatchedTotal,
                                 uint32_t pcmAcceptedTotal,
                                 uint32_t pcmPlayedTotal,
                                 uint32_t pcmLostTotal) {
    Serial.println("\n----------------- JOURNAL CHRONOLOGIQUE AUDIO (VoiceKit) -----------------");
    Serial.printf("Synthese: Decodes=%u | Dispatches=%u | Acceptes=%u | Joues=%u | Pertes=%u\n",
                  pcmDecodedTotal, pcmDispatchedTotal, pcmAcceptedTotal, pcmPlayedTotal, pcmLostTotal);
    size_t n = getEventCount();
    for (size_t i = 0; i < n; ++i) {
        AudioTraceEvent ev = getEvent(i);
        Serial.printf("  [%6u ms] %-22s : val1=%u, val2=%u\n",
                      ev.timestampMs, eventTypeToString(ev.type), ev.value1, ev.value2);
    }
    Serial.println("--------------------------------------------------------------------------\n");
}
