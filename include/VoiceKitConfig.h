#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include "hal/IAudioOutput.h"

/**
 * @brief Mode de gestion du réseau Wi-Fi.
 */
enum class WiFiMode : uint8_t {
    MANAGED_WIFI = 0,   ///< VoiceKit gère sa connexion Wi-Fi (SSID, mot de passe, reconnexion)
    EXTERNAL_WIFI = 1   ///< L'application conserve entièrement le contrôle de sa connexion Wi-Fi
};

/**
 * @brief Politique de traitement lorsque le moteur est déjà en train de parler.
 */
enum class QueuePolicy : uint8_t {
    REJECT_IF_BUSY = 0,   ///< Rejette immédiatement la nouvelle demande si occupé (défaut)
    QUEUE_FIFO = 1,       ///< Enfile la demande dans une file bornée (maxQueueSize)
    INTERRUPT_CURRENT = 2 ///< Interrompt la parole en cours et commence immédiatement la nouvelle annonce
};

/**
 * @brief États opérationnels de VoxCoreVoice.
 */
enum class VoiceState : uint8_t {
    UNINITIALIZED = 0,
    IDLE,
    CONNECTING_WIFI,
    CONNECTING_SERVICE,
    READY,
    GENERATING,
    SPEAKING,
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
        case VoiceState::ERROR_STATE:        return "ERREUR";
        default:                             return "INCONNU";
    }
}

/**
 * @brief Configuration d'initialisation de VoxCore VoiceKit v0.1.0.
 */
struct VoiceKitConfig {
    // Clé d'API Google Gemini
    String apiKey = "";

    // Modèle et voix Gemini Live
    String model = "models/gemini-2.0-flash-exp";
    String voice = "Aoede"; // Voix supportées : Aoede, Charon, Fenrir, Kore, Puck

    // Instructions système strictes pour TTS mot à mot fidèle
    String systemInstruction =
        "Tu es un moteur de synthese vocale TTS pur en francais. "
        "Ton unique role est de prononcer oralement et tres exactement le texte demande mot pour mot. "
        "Ne formule aucun commentaire, aucune reponse conversationnelle, aucun mot d'introduction ni de conclusion. "
        "Prononce uniquement et tres exactement le texte fourni par l'utilisateur.";

    // Préfixe de consigne pour forcer la reproduction mot pour mot
    String promptPrefix = "Prononce exactement et uniquement ce texte : ";

    // Mode Wi-Fi
    WiFiMode wifiMode = WiFiMode::EXTERNAL_WIFI;
    String wifiSSID = "";
    String wifiPassword = "";

    // Volume initial du haut-parleur (0 - 100 %)
    uint8_t volume = 65;

    // Politique de gestion des requêtes concurrentes
    QueuePolicy queuePolicy = QueuePolicy::REJECT_IF_BUSY;
    size_t maxQueueSize = 4;

    // Démarrage vocal à la demande (économie d'énergie pour appareils sur batterie)
    bool connectOnDemand = false;

    // Déconnexion automatique après inactivité en ms (0 = session maintenue en continu)
    uint32_t autoDisconnectIdleMs = 0;

    // Indique si le matériel CoreS3 (M5.begin()) a déjà été initialisé par l'application hôte
    bool hardwareAlreadyInitialized = true;

    // Sortie audio personnalisée (si nullptr, une instance interne M5AudioOutput est utilisée)
    IAudioOutput* audioOutput = nullptr;
};
