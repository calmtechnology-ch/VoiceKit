#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include "hal/IAudioOutput.h"
#include "hal/IAudioInput.h"

/**
 * @brief Mode de gestion du reseau Wi-Fi.
 */
enum class WiFiMode : uint8_t {
    MANAGED_WIFI = 0,   ///< VoiceKit gere la connexion Wi-Fi et la synchronisation NTP
    EXTERNAL_WIFI = 1   ///< L'application hote gere entierement le Wi-Fi et NTP
};

/**
 * @brief Politique de traitement lorsque le moteur est deja en train de parler.
 */
enum class QueuePolicy : uint8_t {
    REJECT_IF_BUSY = 0,   ///< Rejette immediatement la nouvelle demande si occupe (defaut)
    QUEUE_FIFO = 1,       ///< Enfile la demande dans une file bornee (maxQueueSize)
    INTERRUPT_CURRENT = 2 ///< Interrompt la parole en cours et commence immediatement
};

/**
 * @brief Etats operationnels de VoxCoreVoice.
 */
enum class VoiceState : uint8_t {
    UNINITIALIZED = 0,
    IDLE,
    CONNECTING_WIFI,
    CONNECTING_SERVICE,
    READY,
    GENERATING,
    WAITING_RESPONSE = GENERATING,
    SPEAKING,
    LISTENING,        ///< Acquisition microphone et streaming en cours (STT)
    TRANSCRIBING,     ///< Acquisition terminée, attente de la transcription finale Gemini (STT)
    ERROR_STATE
};

inline const char* voiceStateToString(VoiceState state) {
    switch (state) {
        case VoiceState::UNINITIALIZED:      return "NON_INITIALISE";
        case VoiceState::IDLE:               return "EN_REPOS";
        case VoiceState::CONNECTING_WIFI:    return "CONNEXION_WIFI";
        case VoiceState::CONNECTING_SERVICE: return "CONNEXION_SERVICE";
        case VoiceState::READY:              return "PRET";
        case VoiceState::GENERATING:         return "GENERATION_EN_COURS";
        case VoiceState::SPEAKING:           return "RESTITUTION_EN_COURS";
        case VoiceState::LISTENING:          return "ECOUTE_EN_COURS";
        case VoiceState::TRANSCRIBING:       return "TRANSCRIPTION_EN_COURS";
        case VoiceState::ERROR_STATE:        return "ERREUR";
        default:                             return "INCONNU";
    }
}

#ifndef VOICEKIT_DEFAULT_MODEL
#define VOICEKIT_DEFAULT_MODEL "models/gemini-3.8-live"
#endif

#ifndef VOICEKIT_DEFAULT_VOICE
#define VOICEKIT_DEFAULT_VOICE "Aoede" // Voix supportees : Aoede, Charon, Fenrir, Kore, Puck
#endif

#define VOICEKIT_DEFAULT_TTS_INSTRUCTION \
    "Tu es un moteur de synthese vocale TTS pur en francais pour un banc d'essai audio. " \
    "Ton unique role est de prononcer oralement et tres exactement le texte demande mot pour mot. " \
    "Ne formule aucun commentaire, aucune reponse conversationnelle, aucun mot d'introduction ni de conclusion. " \
    "Prononce uniquement et tres exactement le texte fourni par l'utilisateur."

#define VOICEKIT_DEFAULT_STT_INSTRUCTION \
    "Tu es un transcripteur vocal de haute precision en temps reel pour de longs textes et discours. " \
    "Ton unique mission est de transcrire fidelement et mot a mot l'integralite des propos prononces par " \
    "l'utilisateur en francais ou dans sa langue, sans jamais ajouter d'introduction ni de conclusion, " \
    "sans commenter, sans repondre aux questions dictees, et sans generer de son audio superflu. " \
    "Reste rigoureusement silencieux sur le plan sonore."

/**
 * @brief Configuration d'initialisation de VoxCore VoiceKit v0.3.0.
 */
struct VoiceKitConfig {
    // Cle d'API Google Gemini
    String apiKey = "";

    // Modele et voix Gemini Live centralises
    String model = VOICEKIT_DEFAULT_MODEL;
    String voice = VOICEKIT_DEFAULT_VOICE;

    // Instructions systeme strictes pour TTS mot a mot fidele
    String systemInstruction = VOICEKIT_DEFAULT_TTS_INSTRUCTION;

    // Prefixe de consigne pour reproduction mot a mot (TTS)
    String promptPrefix = "Prononce exactement et uniquement ce texte : ";

    // Mode Wi-Fi
    WiFiMode wifiMode = WiFiMode::EXTERNAL_WIFI;
    String wifiSSID = "";
    String wifiPassword = "";

    // Volume initial du haut-parleur (0 - 100 %)
    uint8_t volume = 65;

    // Politique de gestion des requetes concurrentes
    QueuePolicy queuePolicy = QueuePolicy::REJECT_IF_BUSY;
    size_t maxQueueSize = 4;

    // Sortie audio optionnelle (utilise M5AudioOutput par defaut)
    IAudioOutput* audioOutput = nullptr;

    // Entree audio optionnelle (utilise M5AudioInput par defaut)
    IAudioInput* audioInput = nullptr;

    // Mute du haut-parleur pendant l'écoute STT (évite l'effet larsen acoustique)
    bool muteSpeakerDuringSTT = true;

    // Timeout maximal d'attente de la transcription finale Gemini (en ms)
    uint32_t sttFinalTimeoutMs = 6000;

    // Champs de compatibilite ascendante pour applications hotes autonomes
    bool hardwareAlreadyInitialized = true;
    bool connectOnDemand = false;
    uint32_t autoDisconnectIdleMs = 0;
};
