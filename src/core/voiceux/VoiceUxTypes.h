#pragma once

#include <Arduino.h>

/**
 * @enum TextGateDecision
 * @brief Décision du filtre TextGate pour une transcription textuelle.
 *        VoiceKit v0.6.4.1 — Voice UX Foundation
 */
enum class TextGateDecision : uint8_t {
    ACCEPT = 0,     ///< Texte valide et exploitable
    IGNORE,         ///< Transcription vide, bruit ou hésitation isolée à ignorer
    INCOMPLETE      ///< Texte potentiellement incomplet (informatif)
};

/**
 * @brief Convertit une décision TextGate en chaîne lisible.
 */
inline const char* textGateDecisionToString(TextGateDecision d) {
    switch (d) {
        case TextGateDecision::ACCEPT:     return "ACCEPT";
        case TextGateDecision::IGNORE:     return "IGNORE";
        case TextGateDecision::INCOMPLETE: return "INCOMPLETE";
        default:                           return "UNKNOWN";
    }
}

/**
 * @enum VoiceUxIntent
 * @brief Intentions conversationnelles locales reconnues par VoiceKit v0.6.4.1.
 */
enum class VoiceUxIntent : uint8_t {
    NONE = 0,       ///< Aucune intention locale (phrase ordinaire ou non reconnue)
    REPEAT,         ///< Répéter la dernière réponse de l'assistant
    PREVIOUS,       ///< Revenir à la réponse précédente
    STOP,           ///< Arrêter la restitution ou l'interaction
    PAUSE,          ///< Mettre en pause la session / lecture
    RESUME,         ///< Reprendre la lecture / session
    SLOWER,         ///< Demande de parler plus lentement
    REPHRASE        ///< Demande de reformuler / expliquer autrement
};

/**
 * @brief Convertit une intention conversationnelle en chaîne lisible.
 */
inline const char* voiceUxIntentToString(VoiceUxIntent intent) {
    switch (intent) {
        case VoiceUxIntent::NONE:     return "NONE";
        case VoiceUxIntent::REPEAT:   return "REPEAT";
        case VoiceUxIntent::PREVIOUS: return "PREVIOUS";
        case VoiceUxIntent::STOP:     return "STOP";
        case VoiceUxIntent::PAUSE:    return "PAUSE";
        case VoiceUxIntent::RESUME:   return "RESUME";
        case VoiceUxIntent::SLOWER:   return "SLOWER";
        case VoiceUxIntent::REPHRASE: return "REPHRASE";
        default:                      return "UNKNOWN";
    }
}

/**
 * @struct VoiceUxResult
 * @brief Résultat complet d'analyse d'une transcription par VoiceUxController.
 */
struct VoiceUxResult {
    VoiceUxIntent intent = VoiceUxIntent::NONE;
    TextGateDecision gateDecision = TextGateDecision::IGNORE;
    String rawText;              ///< Transcription textuelle originale (non modifiée)
    String normalizedText;       ///< Texte normalisé utilisé pour l'analyse lexicale
    String recallText;           ///< Texte mémorisé rappelé (ex: pour REPEAT ou PREVIOUS)
    String reason;               ///< Motif explicatif de la décision
    bool handledLocally = false; ///< Vrai si l'événement a été géré localement (filtré ou commande locale)
    uint32_t timestampMs = 0;    ///< Horodatage millis()
    uint32_t processingTimeUs = 0; ///< Temps d'exécution de l'analyse en microsecondes
};

/**
 * @struct VoiceUxStats
 * @brief Statistiques d'exploitation du contrôleur Voice UX.
 */
struct VoiceUxStats {
    uint32_t processedCount = 0;        ///< Nombre total de transcriptions analysées
    uint32_t acceptedCount = 0;         ///< Nombre de transcriptions acceptées
    uint32_t ignoredCount = 0;          ///< Nombre de transcriptions ignorées (vides ou hésitations)
    uint32_t recognizedIntentCount = 0; ///< Nombre d'intentions locales reconnues (hors NONE)
};

