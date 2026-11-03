#pragma once

#include <Arduino.h>
#include <functional>
#include <cstdint>

/**
 * @enum ToolEventType
 * @brief Types d'événements émis lors du cycle de vie d'une commande dans le ToolRegistry.
 * Ces événements sont conçus pour être exploitables ultérieurement par VoiceKit-SmartUI.
 */
enum class ToolEventType : uint8_t {
    TOOL_REQUESTED = 0,             ///< Requête d'exécution reçue et prise en compte
    TOOL_STARTED,                   ///< Exécution effective de l'outil démarrée
    TOOL_COMPLETED,                 ///< Exécution terminée avec succès
    TOOL_FAILED,                    ///< Échec (validation, non trouvé, accès refusé, exception)
    TOOL_CONFIRMATION_REQUIRED      ///< Commande sensible : confirmation explicite requise
};

/**
 * @brief Convertit un type d'événement en chaîne lisible.
 */
inline const char* toolEventTypeToString(ToolEventType type) {
    switch (type) {
        case ToolEventType::TOOL_REQUESTED:             return "TOOL_REQUESTED";
        case ToolEventType::TOOL_STARTED:               return "TOOL_STARTED";
        case ToolEventType::TOOL_COMPLETED:             return "TOOL_COMPLETED";
        case ToolEventType::TOOL_FAILED:                return "TOOL_FAILED";
        case ToolEventType::TOOL_CONFIRMATION_REQUIRED: return "TOOL_CONFIRMATION_REQUIRED";
        default:                                        return "UNKNOWN";
    }
}

/**
 * @struct ToolEvent
 * @brief Données associées à un événement de commande.
 */
struct ToolEvent {
    ToolEventType type = ToolEventType::TOOL_REQUESTED; ///< Type d'événement
    String toolId;                                      ///< Identifiant unique de la commande
    String requestId;                                   ///< Identifiant unique de la requête
    String message;                                     ///< Détail, motif d'échec ou invite de confirmation
    uint32_t timestampMs = 0;                           ///< Horodatage (millis)
};

/**
 * @brief Signature du callback d'écoute des événements du registre d'outils.
 */
using ToolEventCallback = std::function<void(const ToolEvent& event)>;

