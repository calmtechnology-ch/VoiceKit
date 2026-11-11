#pragma once

#include <Arduino.h>
#include <functional>
#include <memory>
#include "VoicePipelineConfig.h"
#include "VoiceCommandDispatcher.h"
#include "../../VoxCoreVoice.h"
#include "../tools/ToolRegistry.h"
#include "../voiceux/VoiceUxController.h"

/**
 * @enum PipelineState
 * @brief Machine à états centrale pour l'orchestration du tour complet.
 *        VoiceKit v0.6.5 — Section 8
 */
enum class PipelineState : uint8_t {
    UNINITIALIZED = 0,      ///< Non initialisé
    READY,                  ///< Prêt pour une nouvelle interaction (micro et haut-parleur disponibles)
    LISTENING,              ///< PTT actif : capture audio micro et streaming en cours
    TRANSCRIBING,           ///< PTT relâché : en attente de la transcription finale STT
    ROUTING,                ///< Décision de routage (filtrage TextGate, intentions UX, outils, IA)
    LOCAL_PROCESSING,       ///< Traitement d'une intention locale (REPEAT, PREVIOUS, PAUSE, RESUME)
    TOOL_EXECUTING,         ///< Exécution d'une commande enregistrée dans ToolRegistry
    WAITING_CONFIRMATION,   ///< En attente de confirmation explicite utilisateur pour commande SENSITIVE
    AI_PROCESSING,          ///< Requête transmise au fournisseur d'IA (Gemini / OpenAI)
    SPEAKING,               ///< Restitution vocale en cours par le haut-parleur (TTS ou réponse IA)
    STOPPING,               ///< Arrêt et annulation en cours (STOP prioritaire)
    ERROR                   ///< Erreur récupérable
};

inline const char* pipelineStateToString(PipelineState state) {
    switch (state) {
        case PipelineState::UNINITIALIZED:        return "UNINITIALIZED";
        case PipelineState::READY:                return "READY";
        case PipelineState::LISTENING:            return "LISTENING";
        case PipelineState::TRANSCRIBING:         return "TRANSCRIBING";
        case PipelineState::ROUTING:              return "ROUTING";
        case PipelineState::LOCAL_PROCESSING:     return "LOCAL_PROCESSING";
        case PipelineState::TOOL_EXECUTING:       return "TOOL_EXECUTING";
        case PipelineState::WAITING_CONFIRMATION: return "WAITING_CONFIRMATION";
        case PipelineState::AI_PROCESSING:        return "AI_PROCESSING";
        case PipelineState::SPEAKING:             return "SPEAKING";
        case PipelineState::STOPPING:             return "STOPPING";
        case PipelineState::ERROR:                return "ERROR";
        default:                                  return "UNKNOWN";
    }
}

/**
 * @enum PipelineResult
 * @brief Résultats terminaux d'un tour du pipeline (Section 8).
 */
enum class PipelineResult : uint8_t {
    NONE = 0,
    COMPLETED,  ///< Tour achevé avec succès (outil exécuté ou réponse IA restituée)
    CANCELLED,  ///< Tour interrompu volontairement par STOP
    REJECTED,   ///< Demande refusée (incompréhensible, non autorisée, confirmation rejetée)
    TIMEOUT,    ///< Expiration de délai (STT, confirmation, tâche ou réponse IA)
    FAILED      ///< Échec d'exécution ou erreur réseau/audio
};

inline const char* pipelineResultToString(PipelineResult res) {
    switch (res) {
        case PipelineResult::NONE:      return "NONE";
        case PipelineResult::COMPLETED: return "COMPLETED";
        case PipelineResult::CANCELLED: return "CANCELLED";
        case PipelineResult::REJECTED:  return "REJECTED";
        case PipelineResult::TIMEOUT:   return "TIMEOUT";
        case PipelineResult::FAILED:    return "FAILED";
        default:                        return "UNKNOWN";
    }
}

/**
 * @enum PipelineDestination
 * @brief Destination décidée lors de la phase de routage.
 */
enum class PipelineDestination : uint8_t {
    NONE = 0,
    LOCAL_UX,      ///< Intention conversationnelle locale (REPEAT, PREVIOUS, PAUSE, RESUME)
    LOCAL_TOOL,    ///< Commande locale ToolRegistry
    AI_PROVIDER,   ///< Fournisseur IA distant (Gemini Live / OpenAI Realtime)
    REJECTED       ///< Rejet explicite
};

inline const char* pipelineDestinationToString(PipelineDestination dest) {
    switch (dest) {
        case PipelineDestination::NONE:        return "NONE";
        case PipelineDestination::LOCAL_UX:    return "LOCAL_UX";
        case PipelineDestination::LOCAL_TOOL:  return "LOCAL_TOOL";
        case PipelineDestination::AI_PROVIDER: return "AI_PROVIDER";
        case PipelineDestination::REJECTED:    return "REJECTED";
        default:                               return "UNKNOWN";
    }
}

/**
 * @class IntegratedVoicePipeline
 * @brief Orchestrateur central du cycle vocal complet pour VoiceKit v0.6.5.
 * 
 * Pipeline intégré :
 * Activation PTT -> Capture STT -> Décision -> Action locale ou IA -> Résultat -> TTS -> READY
 * 
 * Caractéristiques architecturales :
 * - Un seul moteur audio : réutilise VoxCoreVoice sans dupliquer de tâche FreeRTOS ni de tampon DMA.
 * - Un seul tour actif : exécution séquentielle stricte avec identifiants monotones turnId.
 * - Aucune double exécution : corrélation stricte des transcriptions et fins TTS.
 * - STOP prioritaire universel à chaque instant.
 * - Commandes sensibles avec validation par jeton explicite et non-blocage.
 */
class IntegratedVoicePipeline {
public:
    // Callbacks d'observation (Section 11)
    using StateCallback = std::function<void(PipelineState oldState, PipelineState newState)>;
    using TurnCallback = std::function<void(uint32_t turnId)>;
    using TranscriptionCallback = std::function<void(uint32_t turnId, const String& text, bool isFinal)>;
    using CommandRecognizedCallback = std::function<void(uint32_t turnId, const String& toolId, const ToolArgs& args)>;
    using RoutingDecisionCallback = std::function<void(uint32_t turnId, PipelineDestination dest, const String& detail)>;
    using ToolExecutionCallback = std::function<void(uint32_t turnId, const String& toolId)>;
    using ConfirmationRequiredCallback = std::function<void(uint32_t turnId, const String& toolId, const String& token, const String& prompt)>;
    using ToolResultCallback = std::function<void(uint32_t turnId, const String& toolId, const ToolResult& result)>;
    using AiResponseCallback = std::function<void(uint32_t turnId, const String& responseText)>;
    using SpeechCallback = std::function<void(uint32_t turnId, uint32_t speechId)>;
    using StopCallback = std::function<void(uint32_t turnId)>;
    using ErrorCallback = std::function<void(uint32_t turnId, const String& error)>;
    using TurnFinishedCallback = std::function<void(uint32_t turnId, PipelineResult result, const String& summary)>;

    IntegratedVoicePipeline();
    ~IntegratedVoicePipeline() = default;

    IntegratedVoicePipeline(const IntegratedVoicePipeline&) = delete;
    IntegratedVoicePipeline& operator=(const IntegratedVoicePipeline&) = delete;

    // =========================================================================
    // Initialisation
    // =========================================================================

    /**
     * @brief Initialise le pipeline avec les références aux composants existants.
     * @param voice Référence au moteur VoxCoreVoice principal (propriété unique du micro et haut-parleur).
     * @param tools Référence au registre des commandes locales ToolRegistry.
     * @param config Paramètres de configuration du pipeline.
     */
    bool begin(VoxCoreVoice& voice, ToolRegistry& tools, const VoicePipelineConfig& config = VoicePipelineConfig());

    /**
     * @brief Surcharge permettant d'injecter un contrôleur VoiceUxController externe.
     */
    bool begin(VoxCoreVoice& voice, ToolRegistry& tools, VoiceUxController& voiceUx, const VoicePipelineConfig& config = VoicePipelineConfig());

    // =========================================================================
    // Contrôle Push-to-Talk et Cycle de Vie
    // =========================================================================

    /**
     * @brief Démarre une écoute Push-to-Talk (appui bouton).
     * @return true si acceptée, false si une opération incompatible est active.
     */
    bool startListening();

    /**
     * @brief Termine l'écoute Push-to-Talk (relâchement bouton) et bascule en attente de transcription.
     * @return true si l'arrêt a été initié proprement.
     */
    bool stopListening();

    /**
     * @brief Interrompt immédiatement toute opération en cours (STOP prioritaire).
     */
    void cancel();

    /**
     * @brief Traitement périodique obligatoire à appeler dans loop().
     */
    void update();

    // =========================================================================
    // Gestion des Confirmations pour Commandes Sensibles
    // =========================================================================

    /**
     * @brief Confirme explicitement l'action sensible en attente.
     * @param token Jeton de confirmation optionnel (utilise le jeton en cours par défaut).
     * @return true si l'exécution a été déclenchée.
     */
    bool confirmPendingAction(const String& token = "");

    /**
     * @brief Refuse explicitement l'action sensible en attente et libère le tour.
     */
    bool rejectPendingAction(const String& token = "");

    /**
     * @brief Récupère d'une erreur et réinitialise l'état vers READY si les ressources sont disponibles.
     */
    bool recoverFromError();

    // =========================================================================
    // Consultation de l'État & Métriques
    // =========================================================================

    PipelineState getState() const { return _state; }
    PipelineResult getLastResult() const { return _lastResult; }
    PipelineDestination getLastDestination() const { return _lastDestination; }
    uint32_t getActiveTurnId() const { return _activeTurnId; }

    const String& getLastTranscription() const { return _lastTranscription; }
    const String& getLastSpokenText() const { return _lastSpokenText; }
    const ToolResult& getLastToolResult() const { return _lastToolResult; }
    const String& getLastAiResponse() const { return _lastAiResponse; }
    const String& getPendingConfirmationToken() const { return _pendingConfirmationToken; }
    const String& getPendingToolId() const { return _pendingToolId; }

    bool isReady() const { return _state == PipelineState::READY; }
    bool isListening() const { return _state == PipelineState::LISTENING; }
    bool isTranscribing() const { return _state == PipelineState::TRANSCRIBING; }
    bool isSpeaking() const { return _state == PipelineState::SPEAKING; }
    bool isWaitingConfirmation() const { return _state == PipelineState::WAITING_CONFIRMATION; }
    bool isPaused() const { return _isPaused; }

    void setPaused(bool paused) { _isPaused = paused; }
    void pause() { _isPaused = true; }
    void resume() { _isPaused = false; }

    // =========================================================================
    // Configuration et Accès aux Composants
    // =========================================================================

    VoiceCommandDispatcher& getDispatcher() { return _dispatcher; }
    const VoiceCommandDispatcher& getDispatcher() const { return _dispatcher; }

    const VoicePipelineConfig& getConfig() const { return _config; }
    void setConfig(const VoicePipelineConfig& config) { _config = config; }

    VoiceUxController& getVoiceUxController();
    ToolRegistry* getToolRegistry() { return _tools; }
    VoxCoreVoice* getVoiceEngine() { return _voice; }

    // =========================================================================
    // Callbacks d'Observation (Section 11)
    // =========================================================================

    void onStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }
    void onListeningStarted(TurnCallback cb) { _onListeningStarted = std::move(cb); }
    void onListeningFinished(TurnCallback cb) { _onListeningFinished = std::move(cb); }
    void onTranscription(TranscriptionCallback cb) { _onTranscription = std::move(cb); }
    void onCommandRecognized(CommandRecognizedCallback cb) { _onCommandRecognized = std::move(cb); }
    void onRoutingDecision(RoutingDecisionCallback cb) { _onRoutingDecision = std::move(cb); }
    void onToolExecutionStarted(ToolExecutionCallback cb) { _onToolExecutionStarted = std::move(cb); }
    void onConfirmationRequired(ConfirmationRequiredCallback cb) { _onConfirmationRequired = std::move(cb); }
    void onToolResult(ToolResultCallback cb) { _onToolResult = std::move(cb); }
    void onAiResponse(AiResponseCallback cb) { _onAiResponse = std::move(cb); }
    void onSpeechStarted(SpeechCallback cb) { _onSpeechStarted = std::move(cb); }
    void onSpeechFinished(SpeechCallback cb) { _onSpeechFinished = std::move(cb); }
    void onStopped(StopCallback cb) { _onStopped = std::move(cb); }
    void onError(ErrorCallback cb) { _onError = std::move(cb); }
    void onTurnFinished(TurnFinishedCallback cb) { _onTurnFinished = std::move(cb); }

    /**
     * @brief Injection directe d'une transcription simulée pour tests unitaires et validation.
     */
    void feedSimulatedTranscription(const String& text, bool isFinal = true);

private:
    void setState(PipelineState newState);
    void finishTurn(PipelineResult result, const String& summary);
    void routeText(const String& text);
    void executeTool(const String& toolId, const ToolArgs& args);
    void transmitToAi(const String& text);
    void speakOutput(const String& text, bool isAiResponse = false, bool rememberReply = true);
    static String truncateUtf8Safe(const String& str, size_t maxBytes);

    VoxCoreVoice* _voice = nullptr;
    ToolRegistry* _tools = nullptr;
    VoiceUxController* _voiceUx = nullptr;
    std::unique_ptr<VoiceUxController> _internalVoiceUx;

    VoicePipelineConfig _config;
    VoiceCommandDispatcher _dispatcher;

    PipelineState _state = PipelineState::UNINITIALIZED;
    PipelineResult _lastResult = PipelineResult::NONE;
    PipelineDestination _lastDestination = PipelineDestination::NONE;

    uint32_t _turnCounter = 0;
    uint32_t _activeTurnId = 0;
    uint32_t _cancelledTurnId = 0;
    uint32_t _lastFinishedTurnId = 0;
    uint32_t _activeSpeechId = 0;

    uint32_t _stateStartTimeMs = 0;
    uint32_t _confirmationStartTimeMs = 0;
    uint32_t _speechStartTimeMs = 0;

    String _lastTranscription;
    String _lastSpokenText;
    String _lastAiResponse;
    ToolResult _lastToolResult;

    String _pendingConfirmationToken;
    String _pendingToolId;
    ToolArgs _pendingToolArgs;
    bool _isAiSpeechActive = false;
    bool _isPaused = false;
    bool _speechWaitingStart = false;
    bool _shouldRememberReply = false;
    bool _finalTranscriptionReceivedForTurn = false;

    // Callbacks
    StateCallback _onStateChanged;
    TurnCallback _onListeningStarted;
    TurnCallback _onListeningFinished;
    TranscriptionCallback _onTranscription;
    CommandRecognizedCallback _onCommandRecognized;
    RoutingDecisionCallback _onRoutingDecision;
    ToolExecutionCallback _onToolExecutionStarted;
    ConfirmationRequiredCallback _onConfirmationRequired;
    ToolResultCallback _onToolResult;
    AiResponseCallback _onAiResponse;
    SpeechCallback _onSpeechStarted;
    SpeechCallback _onSpeechFinished;
    StopCallback _onStopped;
    ErrorCallback _onError;
    TurnFinishedCallback _onTurnFinished;
};

