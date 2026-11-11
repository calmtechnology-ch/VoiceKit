#pragma once

#include <Arduino.h>
#include <functional>
#include "VoiceUxTypes.h"
#include "VoiceUxController.h"
#include "ReplyMemory.h"

/**
 * @enum VoiceUxBridgeState
 * @brief Machine à états applicative pour le pont vocal temps réel.
 *        VoiceKit v0.6.4.3.2 — Voice UX Live Bridge
 */
enum class VoiceUxBridgeState : uint8_t {
    READY = 0,          ///< Prêt pour une nouvelle écoute ou une commande
    LISTENING,          ///< Microphone ouvert, capture audio en cours
    TRANSCRIBING,       ///< Capture terminée, en attente de la transcription finale STT
    VOICE_UX_PROCESSING,///< Traitement local de l'intention par VoiceUxController
    ACTION_PENDING,     ///< Requête d'action prête, en attente d'exécution sécurisée dans loop()
    SPEAKING,           ///< Restitution audio TTS en cours par le haut-parleur
    PAUSED,             ///< Session en pause (requêtes conversationnelles ordinaires ignorées)
    ERROR               ///< État d'erreur non-bloquant avec récupération possible
};

/**
 * @enum VoiceUxSttPhase
 * @brief Phases explicites du cycle de capture et de validation STT.
 *        VoiceKit v0.6.4.3.2 — Validation stricte de capture
 */
enum class VoiceUxSttPhase : uint8_t {
    IDLE = 0,           ///< Aucune capture active ni autorisée (état de repos initial ou après achèvement)
    LISTENING,          ///< Acquisition microphone en cours (capture ouverte via onListeningStarted)
    TRANSCRIBING,       ///< Capture achevée (onListeningFinished), attente de transcription finale du tour
    COMPLETED,          ///< Transcription finale reçue et traitée pour ce tour
    CANCELLED           ///< Capture ou tour définitivement annulé (STOP prioritaire ou erreur)
};

/**
 * @brief Convertit la phase de capture STT en chaîne lisible.
 */
inline const char* voiceUxSttPhaseToString(VoiceUxSttPhase phase) {
    switch (phase) {
        case VoiceUxSttPhase::IDLE:         return "IDLE";
        case VoiceUxSttPhase::LISTENING:    return "LISTENING";
        case VoiceUxSttPhase::TRANSCRIBING: return "TRANSCRIBING";
        case VoiceUxSttPhase::COMPLETED:    return "COMPLETED";
        case VoiceUxSttPhase::CANCELLED:    return "CANCELLED";
        default:                            return "UNKNOWN";
    }
}

/**
 * @brief Convertit l'état du pont en chaîne lisible.
 */
inline const char* voiceUxBridgeStateToString(VoiceUxBridgeState state) {
    switch (state) {
        case VoiceUxBridgeState::READY:               return "READY";
        case VoiceUxBridgeState::LISTENING:           return "LISTENING";
        case VoiceUxBridgeState::TRANSCRIBING:        return "TRANSCRIBING";
        case VoiceUxBridgeState::VOICE_UX_PROCESSING: return "VOICE_UX_PROCESSING";
        case VoiceUxBridgeState::ACTION_PENDING:      return "ACTION_PENDING";
        case VoiceUxBridgeState::SPEAKING:           return "SPEAKING";
        case VoiceUxBridgeState::PAUSED:              return "PAUSED";
        case VoiceUxBridgeState::ERROR:               return "ERROR";
        default:                                      return "UNKNOWN";
    }
}

/**
 * @enum VoiceUxBridgeActionType
 * @brief Nature de l'action structurée produite par le pont.
 */
enum class VoiceUxBridgeActionType : uint8_t {
    NONE = 0,
    TTS_SPEAK,          ///< Demande de restitution vocale (REPEAT, PREVIOUS, annonce locale)
    STOP,               ///< Demande d'arrêt immédiat (interruption prioritaire)
    PAUSE,              ///< Demande de mise en pause de l'activité conversationnelle
    RESUME,             ///< Demande de reprise après une pause
    FORWARD_AI,         ///< Requête ordinaire à transmettre à l'assistant IA distant
    IGNORE              ///< Transcription ignorée (vide ou hésitation isolée)
};

/**
 * @brief Convertit le type d'action en chaîne lisible.
 */
inline const char* voiceUxBridgeActionTypeToString(VoiceUxBridgeActionType action) {
    switch (action) {
        case VoiceUxBridgeActionType::NONE:       return "NONE";
        case VoiceUxBridgeActionType::TTS_SPEAK:  return "TTS_SPEAK";
        case VoiceUxBridgeActionType::STOP:       return "STOP";
        case VoiceUxBridgeActionType::PAUSE:      return "PAUSE";
        case VoiceUxBridgeActionType::RESUME:     return "RESUME";
        case VoiceUxBridgeActionType::FORWARD_AI: return "FORWARD_AI";
        case VoiceUxBridgeActionType::IGNORE:     return "IGNORE";
        default:                                  return "UNKNOWN";
    }
}

/**
 * @enum VoiceUxActionStatus
 * @brief Statut du cycle de vie d'une action dans le pont vocal.
 *        VoiceKit v0.6.4.3
 */
enum class VoiceUxActionStatus : uint8_t {
    CREATED = 0,     ///< Action créée et insérée en file d'attente
    DISPATCHED,      ///< Action extraite de la file (prise en charge par l'application)
    ACCEPTED,        ///< Exécution acceptée par le moteur (ex: say() accepté)
    COMPLETED,       ///< Exécution terminée avec succès (ex: TTS fini)
    REJECTED,        ///< Exécution refusée ou échouée
    CANCELLED        ///< Exécution annulée (STOP ou abandon)
};

/**
 * @brief Convertit le statut de l'action en chaîne lisible.
 */
inline const char* voiceUxActionStatusToString(VoiceUxActionStatus status) {
    switch (status) {
        case VoiceUxActionStatus::CREATED:    return "CREATED";
        case VoiceUxActionStatus::DISPATCHED: return "DISPATCHED";
        case VoiceUxActionStatus::ACCEPTED:   return "ACCEPTED";
        case VoiceUxActionStatus::COMPLETED:  return "COMPLETED";
        case VoiceUxActionStatus::REJECTED:   return "REJECTED";
        case VoiceUxActionStatus::CANCELLED:  return "CANCELLED";
        default:                              return "UNKNOWN";
    }
}

/**
 * @struct VoiceUxBridgeAction
 * @brief Requête d'action structurée émise par VoiceUxLiveBridge.
 */
struct VoiceUxBridgeAction {
    VoiceUxBridgeActionType type = VoiceUxBridgeActionType::NONE;
    VoiceUxIntent intent = VoiceUxIntent::NONE;
    String text;                 ///< Texte associé (ex: phrase à prononcer ou requête IA)
    uint32_t turnId = 0;         // Identifiant de tour / génération
    bool isRecall = false;       ///< Vrai s'il s'agit d'un rappel mémoire (REPEAT/PREVIOUS)
    bool isFeedback = false;     ///< Vrai s'il s'agit d'un message d'information système
    bool isSimulated = false;    ///< Vrai si déclenché par injection simulée série
    uint32_t timestampMs = 0;    ///< Horodatage millis() de création
};

/**
 * @struct VoiceUxBridgeStats
 * @brief Statistiques diagnostiques complètes de VoiceUxLiveBridge.
 */
struct VoiceUxBridgeStats {
    uint32_t totalTranscriptions = 0;
    uint32_t realTranscriptions = 0;
    uint32_t simulatedTranscriptions = 0;
    uint32_t partialTranscriptionsIgnored = 0;
    uint32_t duplicateTranscriptionsIgnored = 0;
    uint32_t staleTranscriptionsIgnored = 0;
    uint32_t recognizedCommands = 0;
    uint32_t ignoredCommands = 0;
    uint32_t ttsDispatchedCount = 0;
    uint32_t ttsCompletedCount = 0;
    uint32_t ttsCancelledCount = 0;
    uint32_t stopRequestsCount = 0;
    uint32_t errorsCount = 0;
    uint32_t lastProcessingTimeUs = 0;
};

/**
 * @class VoiceUxLiveBridge
 * @brief Adaptateur indépendant reliant la reconnaissance STT, VoiceUxController,
 *        ReplyMemory et la synthèse vocale TTS pour M5Stack CoreS3.
 *        VoiceKit v0.6.4.2 — Voice UX Live Bridge
 * 
 * Principes et règles de sécurité :
 * 1. Ne prend jamais possession directe du microphone.
 * 2. Ne déclenche jamais directement d'opération audio (TTS) depuis un callback STT.
 * 3. Met en attente les actions pour une exécution contrôlée depuis loop().
 * 4. Gère les identifiants de tour (turnId) pour rejeter les transcriptions tardives ou dupliquées.
 * 5. Empêche strictement toute capture audio concurrente ou capture pendant la restitution TTS.
 * 6. STOP reste prioritaire à tout instant (tactile ou série).
 */
class VoiceUxLiveBridge {
public:
    using StateChangeCallback = std::function<void(VoiceUxBridgeState oldState, VoiceUxBridgeState newState)>;
    using ActionPendingCallback = std::function<void(const VoiceUxBridgeAction& action)>;
    using ActionExecutedCallback = std::function<void(const VoiceUxBridgeAction& action)>;
    using ActionStatusCallback = std::function<void(const VoiceUxBridgeAction& action, VoiceUxActionStatus status)>;
    using DiagnosticCallback = std::function<void(const String& message)>;

    VoiceUxLiveBridge();
    ~VoiceUxLiveBridge() = default;

    /**
     * @brief Initialise le pont applicatif et le sous-système Voice UX sous-jacent.
     * @return true si l'initialisation réussit.
     */
    bool begin();

    /**
     * @brief Active ou désactive le traitement Voice UX du pont.
     */
    void setEnabled(bool enabled);

    /**
     * @brief Indique si Voice UX est actif.
     */
    bool isEnabled() const;

    /**
     * @brief Remplit la mémoire avec les 2 réponses déterministes de test prescrites.
     *        1. "La température actuelle est de vingt-deux degrés."
     *        2. "L'humidité relative est de quarante-huit pour cent."
     */
    void seedTestReplies();

    /**
     * @brief Notifie le début d'une capture microphone (ex: appui PARLER).
     * @param turnId Identifiant optionnel fourni par le moteur (0 = auto-incrémentation).
     * @return true si la capture est autorisée dans l'état courant, false sinon.
     */
    bool onListeningStarted(uint32_t turnId = 0);

    /**
     * @brief Notifie la fin d'une capture microphone (relâchement bouton ou timeout).
     */
    void onListeningFinished();

    /**
     * @brief Injecte une transcription reçue depuis le moteur STT de VoiceKit.
     * @param text Texte de la transcription.
     * @param isFinal Indique s'il s'agit d'une transcription finale ou partielle.
     * @param turnId Identifiant du tour (0 = tour actif courant).
     * @return true si une action a été mise en attente.
     */
    bool feedTranscription(const String& text, bool isFinal, uint32_t turnId = 0);

    /**
     * @brief Injecte une transcription simulée (ex: commande série 'T <texte>').
     * @param text Texte simulé.
     * @return true si une action a été mise en attente.
     */
    bool feedSimulatedText(const String& text);

    /**
     * @brief Indique si une action est actuellement en attente d'exécution.
     */
    bool hasPendingAction() const;

    /**
     * @brief Récupère l'action actuellement en attente sans la consommer.
     */
    const VoiceUxBridgeAction& getPendingAction() const;

    /**
     * @brief Consomme l'action en attente pour son exécution dans loop().
     * @return VoiceUxBridgeAction L'action extraite.
     */
    VoiceUxBridgeAction consumePendingAction();

    /**
     * @brief Annule toute action en attente.
     */
    void cancelPendingAction();

    /**
     * @brief Déclenche un arrêt prioritaire STOP.
     *        Interrompt la capture ou la restitution en cours et réinitialise le tour.
     */
    void triggerPriorityStop();

    /**
     * @brief Notifie le début d'une restitution vocale TTS.
     * @param spokenText Texte prononcé.
     * @param speechId Identifiant de génération de la restitution (0 pour attribution automatique).
     */
    void notifySpeechStarted(const String& spokenText = "", uint32_t speechId = 0);

    /**
     * @brief Notifie la fin d'une restitution vocale TTS.
     * @param completed true si la restitution est allée jusqu'au bout, false si interrompue.
     * @param speechId Identifiant de génération de la restitution (0 pour ignorer la vérification de corrélation).
     */
    void notifySpeechFinished(bool completed = true, uint32_t speechId = 0);

    /**
     * @brief Notifie le refus ou l'échec de démarrage de la restitution vocale TTS (ex: say() a échoué).
     *        Permet un retour déterministe à l'état READY (ou PAUSED) sans bloquer le pont.
     * @param reason Motif du refus.
     */
    void notifySpeechRefused(const String& reason = "");

    /**
     * @brief Notifie l'achèvement d'une action locale non-TTS (STOP, PAUSE, RESUME, FORWARD_AI).
     */
    void notifyActionCompleted();

    /**
     * @brief Notifie le refus ou l'échec d'une action.
     * @param reason Motif du refus.
     */
    void notifyActionRejected(const String& reason = "");

    /**
     * @brief Notifie une erreur technique (réseau, audio, timeout).
     * @param errorMsg Message descriptif.
     */
    void notifyError(const String& errorMsg);

    /**
     * @brief Récupère d'une erreur et réinitialise l'état à READY (ou PAUSED).
     */
    void recoverFromError();

    /**
     * @brief Active ou désactive le mode pause.
     */
    void setPaused(bool paused);

    /**
     * @brief Indique si le pont est en pause.
     */
    bool isPaused() const;

    /**
     * @brief Indique si le microphone peut être ouvert dans l'état courant.
     */
    bool canStartListening() const;

    /**
     * @brief Indique si la restitution haut-parleur est autorisée dans l'état courant.
     */
    bool canSpeak() const;

    /**
     * @brief Récupère l'état courant de la machine à états.
     */
    VoiceUxBridgeState getState() const;

    /**
     * @brief Récupère la phase de capture STT courante.
     */
    VoiceUxSttPhase getSttPhase() const { return _sttPhase; }

    /**
     * @brief Récupère l'identifiant du tour actif courant.
     */
    uint32_t getActiveTurnId() const;

    /**
     * @brief Récupère l'action actuellement prise en charge / active.
     */
    const VoiceUxBridgeAction& getActiveAction() const;

    /**
     * @brief Indique si la restitution vocale active est un rappel mémoire (REPEAT/PREVIOUS).
     */
    bool isCurrentSpeechRecall() const;

    /**
     * @brief Indique si la restitution vocale active est un message système (ex: mémoire vide).
     */
    bool isCurrentSpeechFeedback() const;

    /**
     * @brief Identifiant de tour associé à la restitution active.
     */
    uint32_t getCurrentSpeechTurnId() const;

    /**
     * @brief Identifiant de génération unique de la restitution vocale active.
     */
    uint32_t getCurrentSpeechId() const;

    /**
     * @brief Texte associé à la restitution active.
     */
    const String& getCurrentSpokenText() const;

    /**
     * @brief Récupère le dernier texte partiel (intérim).
     */
    const String& getInterimText() const;

    /**
     * @brief Récupère la dernière transcription finale reçue.
     */
    const String& getLastFinalText() const;

    /**
     * @brief Récupère le dernier message d'erreur.
     */
    const String& getLastError() const;

    /**
     * @brief Accès direct au contrôleur VoiceUxController interne.
     */
    VoiceUxController& getController();
    const VoiceUxController& getController() const;

    /**
     * @brief Accès direct à la mémoire ReplyMemory.
     */
    ReplyMemory& getReplyMemory();
    const ReplyMemory& getReplyMemory() const;

    /**
     * @brief Mémorise manuellement une réponse complète valide.
     */
    bool rememberReply(const String& reply);

    /**
     * @brief Vide la mémoire conversationnelle.
     */
    void clearMemory();

    /**
     * @brief Récupère les métriques statistiques du pont.
     */
    const VoiceUxBridgeStats& getBridgeStats() const;

    /**
     * @brief Réinitialise les statistiques.
     */
    void resetBridgeStats();

    /**
     * @brief Enregistre les callbacks d'événements.
     */
    void onStateChanged(StateChangeCallback cb);
    void onActionPending(ActionPendingCallback cb);
    void onActionExecuted(ActionExecutedCallback cb);
    void onActionStatus(ActionStatusCallback cb);
    void onDiagnostic(DiagnosticCallback cb);

private:
    void setState(VoiceUxBridgeState newState);
    bool processInternal(const String& text, bool isSimulated, uint32_t turnId);
    void emitDiagnostic(const String& msg);

    VoiceUxBridgeState _state;
    VoiceUxBridgeState _stateBeforePause;
    VoiceUxController _controller;
    VoiceUxBridgeAction _pendingAction;
    VoiceUxBridgeAction _activeAction;
    bool _hasPendingAction;

    uint32_t _activeTurnId;
    VoiceUxSttPhase _sttPhase;
    bool _turnInProgress;
    bool _captureActive;
    bool _finalReceivedForTurn;
    bool _turnCancelled;
    bool _activeActionTerminated;

    String _interimText;
    String _lastFinalText;
    String _currentSpokenText;
    bool _currentSpeechIsRecall;
    bool _currentSpeechIsFeedback;
    uint32_t _currentSpeechTurnId;
    uint32_t _speechGeneration;
    uint32_t _currentSpeechId;
    bool _speechActive;
    String _lastError;

    VoiceUxBridgeStats _stats;

    StateChangeCallback _stateCb;
    ActionPendingCallback _actionPendingCb;
    ActionExecutedCallback _actionExecutedCb;
    ActionStatusCallback _actionStatusCb;
    DiagnosticCallback _diagCb;
};
