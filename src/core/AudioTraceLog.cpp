#include "core/AudioTraceLog.h"

#if defined(ESP32) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
static portMUX_TYPE s_traceMux = portMUX_INITIALIZER_UNLOCKED;
#else
#include <mutex>
static std::mutex s_traceMutex;
#endif

AudioTraceEvent AudioTraceLog::_events[AudioTraceLog::CAPACITY] = {};
size_t AudioTraceLog::_head = 0;
size_t AudioTraceLog::_count = 0;

void AudioTraceLog::record(AudioTraceEventType type, uint32_t val1, uint32_t val2) {
    uint32_t now = millis();

#if defined(ESP32) || defined(ARDUINO)
    portENTER_CRITICAL(&s_traceMux);
#else
    std::lock_guard<std::mutex> lock(s_traceMutex);
#endif

    _events[_head].timestampMs = now;
    _events[_head].type = type;
    _events[_head].value1 = val1;
    _events[_head].value2 = val2;

    _head = (_head + 1) % CAPACITY;
    if (_count < CAPACITY) {
        _count++;
    }

#if defined(ESP32) || defined(ARDUINO)
    portEXIT_CRITICAL(&s_traceMux);
#endif
}

void AudioTraceLog::clear() {
#if defined(ESP32) || defined(ARDUINO)
    portENTER_CRITICAL(&s_traceMux);
#else
    std::lock_guard<std::mutex> lock(s_traceMutex);
#endif

    _head = 0;
    _count = 0;
    for (size_t i = 0; i < CAPACITY; ++i) {
        _events[i] = {};
    }

#if defined(ESP32) || defined(ARDUINO)
    portEXIT_CRITICAL(&s_traceMux);
#endif
}

size_t AudioTraceLog::getEventCount() {
#if defined(ESP32) || defined(ARDUINO)
    portENTER_CRITICAL(&s_traceMux);
    size_t c = _count;
    portEXIT_CRITICAL(&s_traceMux);
    return c;
#else
    std::lock_guard<std::mutex> lock(s_traceMutex);
    return _count;
#endif
}

AudioTraceEvent AudioTraceLog::getEvent(size_t index) {
#if defined(ESP32) || defined(ARDUINO)
    portENTER_CRITICAL(&s_traceMux);
#else
    std::lock_guard<std::mutex> lock(s_traceMutex);
#endif

    if (index >= _count) {
#if defined(ESP32) || defined(ARDUINO)
        portEXIT_CRITICAL(&s_traceMux);
#endif
        return {};
    }

    size_t start = (_count < CAPACITY) ? 0 : _head;
    size_t actualIdx = (start + index) % CAPACITY;
    AudioTraceEvent ev = _events[actualIdx];

#if defined(ESP32) || defined(ARDUINO)
    portEXIT_CRITICAL(&s_traceMux);
#endif
    return ev;
}

const char* AudioTraceLog::eventTypeToString(AudioTraceEventType type) {
    switch (type) {
        case AudioTraceEventType::WS_AUDIO_RECEIVED:      return "Paquet WebSocket recu";
        case AudioTraceEventType::PCM_DECODED:            return "PCM decodes";
        case AudioTraceEventType::PCM_DECODE_FAILED:      return "Echec decodage Base64";
        case AudioTraceEventType::PCM_ACCEPTED_PSRAM:     return "PCM acceptes dans PSRAM";
        case AudioTraceEventType::PCM_REJECTED:           return "PCM rejete";
        case AudioTraceEventType::TURN_COMPLETE_RECEIVED: return "turnComplete recu";
        case AudioTraceEventType::PLAYRAW_SUBMITTED:      return "playRaw soumis";
        case AudioTraceEventType::PLAYRAW_ACCEPTED:       return "Dernier playRaw accepte";
        case AudioTraceEventType::PLAYRAW_REFUSED:        return "playRaw refuse (temporaire)";
        case AudioTraceEventType::PSRAM_BUFFER_EMPTY:     return "Tampon PSRAM vide";
        case AudioTraceEventType::IS_PLAYING_ZERO:        return "isPlaying = 0";
        case AudioTraceEventType::PLAYBACK_FINISHED:      return "onPlaybackFinished";
        case AudioTraceEventType::SESSION_CONNECTED:      return "Session CONNECTED";
        default:                                          return "Inconnu";
    }
}

void AudioTraceLog::printSummary(uint32_t pcmDecodedTotal,
                                uint32_t pcmDispatchedTotal,
                                uint32_t pcmAcceptedTotal,
                                uint32_t pcmPlayedTotal,
                                uint32_t pcmLostTotal) {
    Serial.println("\n--------------------------------------------------");
    Serial.println("  [AUDIO RX TRACE] Chronologie Fin de Restitution  ");
    Serial.println("--------------------------------------------------");

    size_t n = getEventCount();
    for (size_t i = 0; i < n; ++i) {
        AudioTraceEvent ev = getEvent(i);
        switch (ev.type) {
            case AudioTraceEventType::WS_AUDIO_RECEIVED:
                Serial.printf("  %lu ms | %s (%u octets)\n", (unsigned long)ev.timestampMs, eventTypeToString(ev.type), (unsigned)ev.value1);
                break;
            case AudioTraceEventType::PCM_DECODED:
                Serial.printf("  %lu ms | %s : %u echantillons\n", (unsigned long)ev.timestampMs, eventTypeToString(ev.type), (unsigned)ev.value1);
                break;
            case AudioTraceEventType::PCM_DECODE_FAILED:
                Serial.printf("  %lu ms | ALERTE : %s (%u octets corrompus)\n", (unsigned long)ev.timestampMs, eventTypeToString(ev.type), (unsigned)ev.value1);
                break;
            case AudioTraceEventType::PCM_ACCEPTED_PSRAM:
                Serial.printf("  %lu ms | %s : %u (en attente : %u)\n", (unsigned long)ev.timestampMs, eventTypeToString(ev.type), (unsigned)ev.value1, (unsigned)ev.value2);
                break;
            case AudioTraceEventType::PCM_REJECTED:
                Serial.printf("  %lu ms | REJET : %s (%u ech, raison=%u)\n", (unsigned long)ev.timestampMs, eventTypeToString(ev.type), (unsigned)ev.value1, (unsigned)ev.value2);
                break;
            case AudioTraceEventType::PLAYRAW_ACCEPTED:
                Serial.printf("  %lu ms | %s : %u echantillons\n", (unsigned long)ev.timestampMs, eventTypeToString(ev.type), (unsigned)ev.value1);
                break;
            case AudioTraceEventType::PLAYRAW_REFUSED:
                Serial.printf("  %lu ms | %s : %u echantillons\n", (unsigned long)ev.timestampMs, eventTypeToString(ev.type), (unsigned)ev.value1);
                break;
            default:
                Serial.printf("  %lu ms | %s\n", (unsigned long)ev.timestampMs, eventTypeToString(ev.type));
                break;
        }
    }

    Serial.println("  ------------------------------------------------");
    Serial.printf("  PCM reseau decodes    : %u echantillons\n", (unsigned)pcmDecodedTotal);
    Serial.printf("  PCM controleur remis  : %u echantillons\n", (unsigned)pcmDispatchedTotal);
    Serial.printf("  PCM lecteur acceptes  : %u echantillons\n", (unsigned)pcmAcceptedTotal);
    Serial.printf("  PCM soumis au HP      : %u echantillons\n", (unsigned)pcmPlayedTotal);
    Serial.printf("  PCM perdus / rejetes  : %u echantillons\n", (unsigned)pcmLostTotal);
    Serial.printf("  Integrite du flux     : %s\n", (pcmLostTotal == 0 && pcmDecodedTotal == pcmPlayedTotal) ? "OK (0 perte)" : "ATTENTION ANOMALIE");
    Serial.println("--------------------------------------------------\n");
}
