#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>

// =============================================================================
// AudioTraceLog : Journal chronologique compact des événements audio RX
// VoxCore v0.2.1.3 (Correction E)
// - Historique circulaire POD borné en mémoire statique (~512 octets)
// - Enregistrement sans allocation dynamique dans le chemin critique
// - Diagnostic chronologique précis des événements de fin de tour vocal
// - Restitution à la fin du tour ou sur commande de diagnostic
// =============================================================================

enum class AudioTraceEventType : uint8_t {
    NONE = 0,
    WS_AUDIO_RECEIVED,          // Message WebSocket contenant de l'audio
    PCM_DECODED,                // Échantillons PCM décodés depuis Base64
    PCM_DECODE_FAILED,          // Échec de décodage Base64
    PCM_ACCEPTED_PSRAM,         // Échantillons acceptés dans le tampon circulaire PSRAM
    PCM_REJECTED,               // Paquet rejeté par le contrôleur ou saturation tampon
    TURN_COMPLETE_RECEIVED,     // Événement turnComplete reçu de Gemini Live
    PLAYRAW_SUBMITTED,          // Bloc présenté à IAudioOutput::playRaw()
    PLAYRAW_ACCEPTED,           // Bloc accepté par IAudioOutput::playRaw()
    PLAYRAW_REFUSED,            // Refus temporaire par IAudioOutput::playRaw()
    PSRAM_BUFFER_EMPTY,         // Tampon PSRAM épuisé (_availableSamples == 0)
    IS_PLAYING_ZERO,            // IAudioOutput::isPlaying() retombé à 0
    PLAYBACK_FINISHED,          // Notification onPlaybackFinished émise
    SESSION_CONNECTED           // Transition de session vers CONNECTED
};

struct AudioTraceEvent {
    uint32_t timestampMs;
    AudioTraceEventType type;
    uint32_t value1;            // e.g. sample count, bytes, or reason code
    uint32_t value2;            // e.g. available samples, state code
};

class AudioTraceLog {
public:
    static constexpr size_t CAPACITY = 32;

    // Enregistrement rapide d'un événement (thread-safe, O(1), zéro allocation)
    static void record(AudioTraceEventType type, uint32_t val1 = 0, uint32_t val2 = 0);

    // Réinitialisation de l'historique
    static void clear();

    // Nombre d'événements enregistrés dans l'historique circulaire
    static size_t getEventCount();

    // Accès indexé à un événement
    static AudioTraceEvent getEvent(size_t index);

    // Conversion lisible du type d'événement
    static const char* eventTypeToString(AudioTraceEventType type);

    // Affichage formaté du journal de fin de tour dans la console série
    static void printSummary(uint32_t pcmDecodedTotal,
                             uint32_t pcmDispatchedTotal,
                             uint32_t pcmAcceptedTotal,
                             uint32_t pcmPlayedTotal,
                             uint32_t pcmLostTotal);

private:
    static AudioTraceEvent _events[CAPACITY];
    static size_t _head;
    static size_t _count;
};
