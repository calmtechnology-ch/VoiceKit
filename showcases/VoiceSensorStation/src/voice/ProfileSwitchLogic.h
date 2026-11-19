#pragma once

#include <stdint.h>

/**
 * @file ProfileSwitchLogic.h
 * @brief Logique pure (sans dépendance Arduino/ESP32) de coordination de bascule
 *        de profil / fournisseur IA pour VoiceSensorStation v0.1.0.1.
 *
 * Permet de tester de manière déterministe en C++17 natif :
 * - L'élimination de la déconnexion redondante lorsque la session est CONNECTED
 * - La gestion du fallback DISCONNECTING_SESSION uniquement si session en transition
 * - La vérification stricte des callbacks (onProfileApplied, onReady fallback, callbacks tardifs)
 * - La gestion des timeouts, erreurs et interruptions STOP pendant une bascule
 */

namespace StationSwitch {

enum class Provider : uint8_t {
    GEMINI = 0,
    OPENAI = 1
};

enum class OpState : uint8_t {
    IDLE = 0,
    PENDING_STOP,
    DISCONNECTING_SESSION,
    APPLYING
};

enum class SessionState : uint8_t {
    DISCONNECTED = 0,
    CONNECTING,
    CONNECTED,
    WAITING_RESPONSE,
    PLAYING,
    INTERRUPTING,
    RECONNECTING,
    ERROR
};

enum class RequestAction : uint8_t {
    REJECT_BUSY = 0,
    REJECT_NO_KEY,
    ALREADY_ACTIVE,
    NEED_STOP,
    NEED_DISCONNECT,
    APPLY_NOW
};

enum class ReadyDecision : uint8_t {
    IGNORE = 0,
    CONFIRM_SWITCH
};

inline const char* providerName(Provider p) {
    return (p == Provider::OPENAI) ? "OpenAI" : "Gemini";
}

inline const char* opStateToString(OpState st) {
    switch (st) {
        case OpState::IDLE:                  return "IDLE";
        case OpState::PENDING_STOP:          return "PENDING_STOP";
        case OpState::DISCONNECTING_SESSION: return "DISCONNECTING_SESSION";
        case OpState::APPLYING:              return "APPLYING";
        default:                             return "UNKNOWN";
    }
}

/**
 * @brief Décide de la première action à entreprendre lors d'une demande de changement
 *        de profil ou de fournisseur.
 *
 * Règle v0.1.0.1 :
 * - Lorsque la session est CONNECTED (cas nominal OpenAI -> Gemini ou Gemini -> OpenAI),
 *   ne PAS appeler requestDisconnectSession() au préalable : déléguer directement à
 *   VoxCoreVoice::applyVoiceProfile() qui ferme proprement la session précédente puis
 *   connecte le nouveau fournisseur de façon atomique.
 * - N'utiliser DISCONNECTING_SESSION que si VoiceKit est déjà en cours de connexion /
 *   reconnexion ou de désactivation (cas où applyVoiceProfile() refuserait via isConnecting()).
 */
inline RequestAction decideSwitchAction(OpState currentOp,
                                        bool voiceKitProfilePending,
                                        bool stopActive,
                                        bool isProviderSwitch,
                                        Provider activeProvider,
                                        Provider targetProvider,
                                        bool hasTargetKey,
                                        bool engineIdle,
                                        SessionState sessionState,
                                        bool voiceConnecting,
                                        bool voiceDisconnecting) {
    if (currentOp != OpState::IDLE || voiceKitProfilePending || stopActive) {
        return RequestAction::REJECT_BUSY;
    }

    if (isProviderSwitch &&
        activeProvider == targetProvider &&
        sessionState == SessionState::CONNECTED &&
        !voiceConnecting &&
        !voiceDisconnecting) {
        return RequestAction::ALREADY_ACTIVE;
    }

    if (isProviderSwitch && !hasTargetKey) {
        return RequestAction::REJECT_NO_KEY;
    }

    if (!engineIdle) {
        return RequestAction::NEED_STOP;
    }

    if (voiceConnecting || voiceDisconnecting ||
        sessionState == SessionState::CONNECTING ||
        sessionState == SessionState::RECONNECTING) {
        return RequestAction::NEED_DISCONNECT;
    }

    return RequestAction::APPLY_NOW;
}

/**
 * @brief Évalue si un callback onProfileApplied() confirme effectivement l'opération en cours.
 *        Rejette les callbacks tardifs provenant d'un autre fournisseur.
 */
inline bool doesProfileAppliedConfirm(OpState currentOp,
                                      bool isProviderSwitch,
                                      Provider targetProvider,
                                      Provider confirmedProvider) {
    if (currentOp != OpState::APPLYING) {
        return false;
    }
    if (isProviderSwitch && confirmedProvider != targetProvider) {
        return false;
    }
    return true;
}

/**
 * @brief Évalue si un callback onReady() (sans onProfileApplied préalable) peut confirmer
 *        une bascule en cours. Exige que VoiceKit ne soit plus en transition, que la session
 *        soit CONNECTED et que le fournisseur actif dans VoiceKit soit exactement le fournisseur cible.
 */
inline ReadyDecision evaluateOnReadyDuringSwitch(OpState currentOp,
                                                 bool voiceKitProfilePending,
                                                 SessionState sessionState,
                                                 Provider voiceKitActiveProvider,
                                                 Provider targetProvider) {
    if (currentOp != OpState::APPLYING) {
        return ReadyDecision::IGNORE;
    }
    if (voiceKitProfilePending) {
        return ReadyDecision::IGNORE;
    }
    if (sessionState != SessionState::CONNECTED) {
        return ReadyDecision::IGNORE;
    }
    if (voiceKitActiveProvider != targetProvider) {
        return ReadyDecision::IGNORE;
    }
    return ReadyDecision::CONFIRM_SWITCH;
}

} // namespace StationSwitch
