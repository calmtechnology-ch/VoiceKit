#pragma once

#include <Arduino.h>

/**
 * @enum PipelineDefaultDestination
 * @brief Destination par défaut pour les phrases non reconnues localement.
 *        VoiceKit v0.6.5 — Integrated Voice Pipeline
 */
enum class PipelineDefaultDestination : uint8_t {
    AI = 0,         ///< Transmettre la demande au fournisseur IA (Gemini / OpenAI)
    REJECT          ///< Refuser explicitement la demande inconnue
};

inline const char* pipelineDefaultDestinationToString(PipelineDefaultDestination d) {
    switch (d) {
        case PipelineDefaultDestination::AI:     return "AI";
        case PipelineDefaultDestination::REJECT: return "REJECT";
        default:                                 return "AI";
    }
}

/**
 * @struct VoicePipelineConfig
 * @brief Configuration centralisée pour IntegratedVoicePipeline (VoiceKit v0.6.5).
 */
struct VoicePipelineConfig {
    // 1. Activation des sous-systèmes
    bool enableVoiceUx = true;           ///< Activation des intentions conversationnelles locales (STOP, REPEAT, PREVIOUS, PAUSE, RESUME)
    bool enableLocalTools = true;        ///< Autorisation d'exécution des commandes ToolRegistry
    bool enableAiProcessing = true;      ///< Autorisation de transmission à l'IA pour les requêtes ordinaires
    PipelineDefaultDestination defaultDestination = PipelineDefaultDestination::AI;

    // 2. Délais et timeouts (en millisecondes)
    uint32_t sttTimeoutMs = 6000;            ///< Délai d'attente max pour la transcription finale après fin d'écoute
    uint32_t toolExecutionTimeoutMs = 10000; ///< Délai max d'exécution d'un outil local pour tâches différées (les handlers synchrones s'exécutent jusqu'à complétion)
    uint32_t aiResponseTimeoutMs = 15000;    ///< Délai max d'attente d'une réponse de l'IA
    uint32_t confirmationTimeoutMs = 30000;  ///< Délai d'expiration du jeton de confirmation pour commande SENSITIVE
    uint32_t ttsStartTimeoutMs = 8000;       ///< Délai d'attente max pour le début de restitution vocale TTS
    uint32_t ttsPlaybackTimeoutMs = 25000;   ///< Délai max d'émission audio TTS avant timeout de sécurité

    // 3. Sécurité et confirmations
    bool requireConfirmationForSensitive = true; ///< Commande sensible requiert une confirmation explicite obligatoire

    // 4. Politique de réponse vocale
    bool speakToolResult = true;         ///< Restitution vocale du résultat de la commande locale
    bool speakRejection = true;          ///< Restitution vocale en cas de refus explicite
    bool speakConfirmationRequired = true; ///< Annoncer oralement la nécessité d'une confirmation
    size_t maxSpeechLength = 512;        ///< Limite de longueur des réponses prononcées

    // 5. Formulations et consignes
    String confirmationPrompt = "Confirmation requise sur l'ecran.";
    String unknownCommandResponse = "Commande non reconnue.";
    String rejectionResponse = "Je n'ai pas compris votre demande.";
    String toolExecutionFailedResponse = "Impossible d'executer la commande.";
    String aiPromptPrefix = "Reponds brievement et directement en francais en une ou deux phrases : ";
};

