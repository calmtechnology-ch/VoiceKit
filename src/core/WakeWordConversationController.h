#pragma once
#include <Arduino.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>
#include "VoiceKitConfig.h"
#include "core/IVoiceProvider.h"
#include "core/LiveSessionController.h"
#include "core/LiveAudioStreamer.h"
#include "core/LiveAudioPlayer.h"
#include "core/ConversationController.h"
#include "core/WakeWordManager.h"
#include "core/VoiceActivityDetector.h"

/**
 * @brief États de la machine à états de conversation pilotée par mot de réveil (VoiceKit v0.6.3).
 *
 * Strictement séparés des états internes de WakeWordManager (WakeWordState).
 */
enum class WakeWordConversationState : uint8_t {
    DISABLED = 0,
    WAKE_ARMED,
    WAKE_DETECTED,
    PREPARING_SESSION,
    CONNECTING,
    CONVERSATION_LISTENING,
    CONVERSATION_SPEAKING,
    ENDING_SESSION,
    RETURNING_TO_WAKE,
    RECOVERABLE_ERROR
};

inline const char* wakeWordConversationStateToString(WakeWordConversationState state) {
    switch (state) {
        case WakeWordConversationState::DISABLED:               return "DISABLED";
        case WakeWordConversationState::WAKE_ARMED:             return "WAKE_ARMED";
        case WakeWordConversationState::WAKE_DETECTED:          return "WAKE_DETECTED";
        case WakeWordConversationState::PREPARING_SESSION:      return "PREPARING_SESSION";
        case WakeWordConversationState::CONNECTING:             return "CONNECTING";
        case WakeWordConversationState::CONVERSATION_LISTENING: return "CONVERSATION_LISTENING";
        case WakeWordConversationState::CONVERSATION_SPEAKING:  return "CONVERSATION_SPEAKING";
        case WakeWordConversationState::ENDING_SESSION:         return "ENDING_SESSION";
        case WakeWordConversationState::RETURNING_TO_WAKE:      return "RETURNING_TO_WAKE";
        case WakeWordConversationState::RECOVERABLE_ERROR:      return "RECOVERABLE_ERROR";
        default:                                                return "UNKNOWN";
    }
}

/**
 * @brief Raisons de fin d'une session conversationnelle déclenchée par mot de réveil (v0.6.3).
 */
enum class WakeWordSessionEndReason : uint8_t {
    NONE = 0,
    SILENCE_TIMEOUT,
    MAX_DURATION_TIMEOUT,
    MANUAL_STOP,
    VOICE_COMMAND,
    NETWORK_INTERRUPTION,
    MIC_TRANSITION_ERROR,
    CONTROLLER_DISABLED
};

inline const char* wakeWordSessionEndReasonToString(WakeWordSessionEndReason reason) {
    switch (reason) {
        case WakeWordSessionEndReason::NONE:                  return "NONE";
        case WakeWordSessionEndReason::SILENCE_TIMEOUT:       return "SILENCE_TIMEOUT";
        case WakeWordSessionEndReason::MAX_DURATION_TIMEOUT:  return "MAX_DURATION_TIMEOUT";
        case WakeWordSessionEndReason::MANUAL_STOP:           return "MANUAL_STOP";
        case WakeWordSessionEndReason::VOICE_COMMAND:         return "VOICE_COMMAND";
        case WakeWordSessionEndReason::NETWORK_INTERRUPTION:  return "NETWORK_INTERRUPTION";
        case WakeWordSessionEndReason::MIC_TRANSITION_ERROR:  return "MIC_TRANSITION_ERROR";
        case WakeWordSessionEndReason::CONTROLLER_DISABLED:   return "CONTROLLER_DISABLED";
        default:                                              return "UNKNOWN";
    }
}

/**
 * @brief Types d'événements applicatifs sûrs pour boucle principale / LVGL 9 (v0.6.3).
 */
enum class WakeWordConversationEventType : uint8_t {
    NONE = 0,
    WAKE_WORD_DETECTED,
    CONNECTING_STARTED,
    CONVERSATION_READY,
    USER_LISTENING,
    RESPONSE_SPEAKING,
    CONVERSATION_ENDED,
    RETURNED_TO_WAKE,
    RECOVERABLE_ERROR,
    STATE_CHANGED
};

/**
 * @brief Événement consommable sans blocage depuis la boucle principale ou un timer LVGL 9.
 */
struct WakeWordConversationEvent {
    WakeWordConversationEventType type = WakeWordConversationEventType::NONE;
    WakeWordConversationState state = WakeWordConversationState::DISABLED;
    WakeWordSessionEndReason endReason = WakeWordSessionEndReason::NONE;
    String text = "";
    float confidence = 0.0f;
    bool isSimulated = false;
    uint32_t timestampMs = 0;
};

/**
 * @brief Métriques de performance, latence et stabilité pour WakeWordConversationController (Section 9 — v0.6.3).
 */
struct WakeWordConversationMetrics {
    // Chronométrage des transitions (ms)
    uint32_t wakeToConnectStartMs = 0;      // Durée Wake Word -> début connexion
    uint32_t wakeToConversationReadyMs = 0; // Durée Wake Word -> conversation prête
    uint32_t wsConnectMs = 0;               // Temps d'établissement WebSocket
    uint32_t lastSessionDurationMs = 0;     // Durée totale de la dernière session
    uint32_t totalSessionDurationMs = 0;    // Durée cumulée des sessions
    uint32_t returnToArmedMs = 0;           // Durée de retour à WAKE_ARMED

    // Compteurs de session et d'événements (Section 8 & 9)
    uint32_t sessionCount = 0;              // Nombre de sessions conversationnelles ouvertes
    uint32_t successfulSessionCount = 0;    // Nombre de sessions terminées normalement avec succès (v0.6.3.1)
    uint32_t detectionCount = 0;            // Nombre de détections Wake Word confirmées
    uint32_t realDetectionCount = 0;        // Nombre de détections acoustiques réelles (WakeNet)
    uint32_t simulatedDetectionCount = 0;   // Nombre de détections simulées (moteur TEST)
    uint32_t ignoredConcurrentDetections = 0; // Détections ignorées pendant session/connexion active
    uint32_t turnCount = 0;                 // Nombre total de tours d'échange vocal
    uint32_t reconnectCount = 0;            // Nombre de reconnexions fournisseur
    uint32_t errorCount = 0;                // Nombre total d'erreurs récupérables
    uint32_t recoveryCount = 0;             // Nombre de réarmements réussis après erreur

    // Compteurs de défaillances et saturations spécifiques (Section 5 — v0.6.3.1)
    uint32_t micTransitionFailures = 0;     // Échecs de transition microphone (arrêt/démarrage)
    uint32_t micReservationFailures = 0;    // Échecs de réservation propriétaire unique du microphone
    uint32_t networkFailures = 0;           // Échecs réseau (Wi-Fi / WebSocket / timeout)
    uint32_t wakeQueueSaturations = 0;      // Saturations de la file Wake Word Audio Bridge
    uint32_t wakeFramesDropped = 0;         // Pertes de trames Wake Word

    // Mémoire RAM interne et PSRAM (octets)
    uint32_t freeInternalHeap = 0;
    uint32_t minFreeInternalHeap = 0;
    uint32_t freePsram = 0;
    uint32_t minFreePsram = 0;

    // Temps d'exécution non bloquant (µs) — Instrumentation détaillée par sous-système (Section 1 & 5 — v0.6.3.1)
    uint32_t lastVoiceUpdateDurationUs = 0;
    uint32_t avgVoiceUpdateDurationUs = 0;
    uint32_t maxVoiceUpdateDurationUs = 0;
    uint32_t voiceUpdateCallCount = 0;
    uint64_t totalVoiceUpdateDurationUs = 0;

    uint32_t lastControllerUpdateDurationUs = 0;
    uint32_t maxControllerUpdateDurationUs = 0;
    uint32_t lastStateTransitionDurationUs = 0;
    uint32_t maxStateTransitionDurationUs = 0;

    uint32_t lastWakeWordUpdateDurationUs = 0;
    uint32_t maxWakeWordUpdateDurationUs = 0;
    uint32_t lastSessionTickDurationUs = 0;
    uint32_t maxSessionTickDurationUs = 0;
    uint32_t lastWsProcessDurationUs = 0;
    uint32_t maxWsProcessDurationUs = 0;
    uint32_t lastMicTransitionDurationUs = 0;
    uint32_t maxMicTransitionDurationUs = 0;
    uint32_t lastSessionStopDurationUs = 0;
    uint32_t maxSessionStopDurationUs = 0;
    uint32_t lastWakeReactivationDurationUs = 0;
    uint32_t maxWakeReactivationDurationUs = 0;
};

/**
 * @brief Paramètres configurables du contrôleur WakeWordConversationController (v0.6.3).
 */
struct WakeWordConversationConfig {
    uint32_t silenceTimeoutMs = 30000;       // Timeout de silence/inactivité par défaut : 30 s
    uint32_t maxSessionDurationMs = 180000;  // Durée maximale d'une session (3 min par défaut, 0 = illimitée)
    uint32_t connectTimeoutMs = 15000;       // Timeout d'établissement de session vocale (15 s)
    uint32_t responseTimeoutMs = 15000;      // Timeout d'attente de réponse serveur après fin de parole (15 s)
    uint32_t maxTurnDurationMs = 20000;      // Durée maximale de capture continue par tour (20 s)
    uint32_t turnRestartDelayMs = 150;       // Garde acoustique entre fin de réponse TTS et écoute du tour suivant
    uint32_t returnToWakeDelayMs = 250;      // Garde acoustique/matérielle avant réactivation de WakeNet
    uint32_t errorRecoveryDelayMs = 1500;    // Délai de stabilisation en RECOVERABLE_ERROR avant retour à WAKE_ARMED
    uint32_t maxPlaybackDrainMs = 10000;     // Délai maximal d'attente de vidage des derniers échantillons TTS
    uint16_t vadThresholdRms = 400;          // Seuil RMS de détection d'activité vocale locale
    uint32_t vadSilenceDurationMs = 1000;    // Silence requis après parole pour clore le tour utilisateur
    uint8_t vadMinSpeechFrames = 2;          // Blocs consécutifs requis pour valider le début de parole
    bool enableVoiceStopCommand = true;      // Fermeture optionnelle par commande vocale sur transcription finale fiable
    bool disconnectAfterSession = true;      // Libération complète de la session WebSocket en fin de conversation
};

/**
 * @brief Contrôleur réutilisable de conversation mains libres déclenchée par mot de réveil (VoiceKit v0.6.3).
 *
 * Coordonne WakeWordManager, LiveSessionController, ConversationController, LiveAudioStreamer
 * et LiveAudioPlayer sans créer de second gestionnaire microphone, de second transport WebSocket
 * ni de seconde chaîne de restitution audio.
 *
 * Toutes les transitions et tous les callbacks sont exécutés exclusivement depuis update()
 * dans la tâche principale (compatibilité directe avec LVGL 9).
 */
class WakeWordConversationController {
public:
    using WakeDetectedCallback = std::function<void(const String& word, float confidence, bool isSimulated)>;
    using WakeWordSimpleCallback = std::function<void(const String& word)>;
    using SimpleCallback = std::function<void()>;
    using SessionEndedCallback = std::function<void(WakeWordSessionEndReason reason)>;
    using ErrorCallback = std::function<void(const String& error)>;
    using StateCallback = std::function<void(WakeWordConversationState state)>;
    using NetworkReadyCallback = std::function<bool()>;
    using EnableConversationCallback = std::function<bool()>;

    WakeWordConversationController();
    ~WakeWordConversationController();

    /**
     * @brief Initialise le contrôleur sur les composants partagés de VoiceKit.
     */
    bool begin(WakeWordManager* wakeWord,
               IVoiceProvider* provider,
               LiveSessionController* session,
               ConversationController* conversation,
               LiveAudioStreamer* streamer,
               LiveAudioPlayer* player,
               const WakeWordConversationConfig& config = WakeWordConversationConfig());

    /**
     * @brief Met à jour le fournisseur vocal actif (ex. basculement Gemini Live / OpenAI Realtime).
     */
    void setProvider(IVoiceProvider* provider, VoiceProviderType providerType);

    /**
     * @brief Active ou désactive le mode Wake Word Conversation.
     */
    bool enable(bool en = true);
    void disable();
    bool isEnabled() const { return _enabled; }

    /**
     * @brief Machine à états non bloquante à appeler régulièrement depuis voice.update().
     */
    void update(uint32_t nowMs = 0);

    /**
     * @brief Enregistre une détection de mot de réveil (appelé par le callback WakeWordManager).
     *
     * N'exécute AUCUNE opération longue ni transition directe : enregistre uniquement
     * la demande de démarrage qui sera traitée lors du prochain appel à update().
     */
    void notifyWakeWordDetected(const String& word, float confidence = 1.0f, bool isSimulated = false);
    void notifyWakeWordDetected(const WakeWordEvent& ev);

    /**
     * @brief Notifie une transcription utilisateur entrante (pour commande vocale d'arrêt fiable).
     *
     * Ne valide une commande de fermeture QUE si isFinal == true.
     */
    void notifyUserTranscription(const String& text, bool isFinal);

    /**
     * @brief Notifie la fin d'un tour de conversation (appelé quand la réponse vocale est terminée).
     */
    void notifyTurnFinished(uint64_t turnId);

    /**
     * @brief Réception non bloquante du RMS microphone depuis la tâche audio (pour le VAD local).
     */
    void onAudioChunkCaptured(uint16_t rms);

    /**
     * @brief Arrêt manuel explicite de la session conversationnelle en cours (Section 5).
     * @param immediate Si true, interrompt immédiatement la restitution/capture ; si false, attend la fin du tour.
     */
    bool stopSession(bool immediate = true);

    /**
     * @brief Demande la fermeture normale de la session à l'issue de la restitution complète du tour en cours.
     */
    bool requestEndSessionAfterTurn();

    // --- Configuration ---
    void setConfig(const WakeWordConversationConfig& config);
    const WakeWordConversationConfig& getConfig() const { return _config; }
    void setSilenceTimeoutMs(uint32_t timeoutMs);
    uint32_t getSilenceTimeoutMs() const { return _config.silenceTimeoutMs; }
    void setMaxSessionDurationMs(uint32_t maxDurationMs);
    uint32_t getMaxSessionDurationMs() const { return _config.maxSessionDurationMs; }
    void setConnectTimeoutMs(uint32_t timeoutMs) { _config.connectTimeoutMs = timeoutMs; }
    uint32_t getConnectTimeoutMs() const { return _config.connectTimeoutMs; }
    void setVoiceStopCommandEnabled(bool enabled) { _config.enableVoiceStopCommand = enabled; }
    bool isVoiceStopCommandEnabled() const { return _config.enableVoiceStopCommand; }
    void setDisconnectAfterSession(bool disconnectAfter) { _config.disconnectAfterSession = disconnectAfter; }
    bool isDisconnectAfterSession() const { return _config.disconnectAfterSession; }
    void setErrorRecoveryDelayMs(uint32_t delayMs) { _config.errorRecoveryDelayMs = delayMs; }
    uint32_t getErrorRecoveryDelayMs() const { return _config.errorRecoveryDelayMs; }
    void setReturnToWakeDelayMs(uint32_t delayMs) { _config.returnToWakeDelayMs = delayMs; }
    uint32_t getReturnToWakeDelayMs() const { return _config.returnToWakeDelayMs; }
    void setTurnRestartDelayMs(uint32_t delayMs) { _config.turnRestartDelayMs = delayMs; }
    uint32_t getTurnRestartDelayMs() const { return _config.turnRestartDelayMs; }
    void addCustomVoiceStopPhrase(const String& phrase);
    void clearCustomVoiceStopPhrases();

    // --- États & Inspection ---
    WakeWordConversationState getState() const { return _state; }
    WakeWordSessionEndReason getLastEndReason() const { return _lastEndReason; }
    const String& getLastError() const { return _lastError; }
    const String& getLastDetectedWakeWord() const { return _lastDetectedWord; }
    bool isLastDetectionSimulated() const { return _lastDetectionSimulated; }
    bool isSessionActive() const;
    bool isSessionInProgress() const;
    bool hasUserSpokenInTurn() const { return _userSpokeInTurn; }
    bool isWaitingTurnResponse() const { return _waitingForResponse; }
    uint32_t getCurrentSessionTurns() const { return _currentSessionTurns; }
    const char* getDisplayStatusText() const;
    const char* getUiTransitionBannerText() const;

    // --- Métriques & Diagnostics (Section 9) ---
    const WakeWordConversationMetrics& getMetrics() const { return _metrics; }
    void resetMetrics();
    void recordVoiceUpdateDurationUs(uint32_t durationUs);
    void refreshMemoryMetrics();
    void syncSubsystemMetrics();
    void printDiagnostics() const;

    // --- File d'événements non bloquante (LVGL 9) ---
    bool pollEvent(WakeWordConversationEvent& outEvent);

    // --- Callbacks applicatifs (Section 7) ---
    void setOnWakeWordDetected(WakeDetectedCallback cb) { _onWakeWordDetected = std::move(cb); }
    void setOnWakeWordDetectedSimple(WakeWordSimpleCallback cb) { _onWakeWordDetectedSimple = std::move(cb); }
    void setOnConnecting(SimpleCallback cb) { _onConnecting = std::move(cb); }
    void setOnConversationReady(SimpleCallback cb) { _onConversationReady = std::move(cb); }
    void setOnUserListening(SimpleCallback cb) { _onUserListening = std::move(cb); }
    void setOnResponseSpeaking(SimpleCallback cb) { _onResponseSpeaking = std::move(cb); }
    void setOnSpeaking(SimpleCallback cb) { _onResponseSpeaking = std::move(cb); }
    void setOnConversationEnded(SessionEndedCallback cb) { _onConversationEnded = std::move(cb); }
    void setOnConversationEndedSimple(SimpleCallback cb) { _onConversationEndedSimple = std::move(cb); }
    void setOnReturnedToWake(SimpleCallback cb) { _onReturnedToWake = std::move(cb); }
    void setOnRecoverableError(ErrorCallback cb) { _onRecoverableError = std::move(cb); }
    void setOnStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }

    // Callbacks système internes (injection par VoxCoreVoice)
    void setNetworkReadyCallback(NetworkReadyCallback cb) { _networkReadyCheck = std::move(cb); }
    void setEnableConversationCallback(EnableConversationCallback cb) { _enableConversationHook = std::move(cb); }

private:
    void transitionTo(WakeWordConversationState newState, uint32_t nowMs, const char* reason = nullptr);
    void enterRecoverableError(const String& errorMsg, WakeWordSessionEndReason reason, uint32_t nowMs, bool isMicFailure, bool isNetworkFailure);
    void pushEvent(WakeWordConversationEventType type, uint32_t nowMs, const String& text = "", float confidence = 0.0f, bool isSimulated = false);
    bool isNetworkReady() const;
    bool checkVoiceStopIntent(const String& finalText) const;
    static String normalizeStopIntentText(const String& input);
    bool startNextListeningTurn(uint32_t nowMs, bool isFirstTurnOfSession);

    WakeWordManager* _wakeWord = nullptr;
    IVoiceProvider* _provider = nullptr;
    LiveSessionController* _session = nullptr;
    ConversationController* _conversation = nullptr;
    LiveAudioStreamer* _streamer = nullptr;
    LiveAudioPlayer* _player = nullptr;
    VoiceProviderType _providerType = VoiceProviderType::GEMINI;

    WakeWordConversationConfig _config;
    WakeWordConversationState _state = WakeWordConversationState::DISABLED;
    WakeWordSessionEndReason _lastEndReason = WakeWordSessionEndReason::NONE;
    WakeWordConversationMetrics _metrics;
    VoiceActivityDetector _vad;

    bool _initialized = false;
    bool _enabled = false;
    bool _inUpdate = false;

    // Demande de déclenchement différée depuis onWakeWordDetected (Section 3)
    std::atomic<bool> _wakeRequestPending{false};
    String _pendingWakeWord = "Bonjour ESP";
    float _pendingWakeConfidence = 1.0f;
    bool _pendingWakeSimulated = false;
    uint32_t _pendingWakeTimestampMs = 0;

    // Demande de transcription finale différée vers update()
    struct PendingTranscription {
        bool pending = false;
        String text = "";
    };
    PendingTranscription _pendingFinalTranscription;

    // État de la session active
    String _lastDetectedWord = "";
    bool _lastDetectionSimulated = false;
    String _lastError = "";
    uint32_t _stateEnterMs = 0;
    uint32_t _wakeDetectedMs = 0;
    uint32_t _connectStartMs = 0;
    uint32_t _sessionStartMs = 0;
    uint32_t _lastActivityMs = 0;
    uint32_t _turnListenStartMs = 0;
    uint32_t _responseWaitStartMs = 0;
    uint32_t _turnFinishedMs = 0;
    uint32_t _endingEnterMs = 0;
    uint32_t _returnToWakeStartMs = 0;
    uint32_t _errorEnterMs = 0;

    uint32_t _currentSessionTurns = 0;
    uint32_t _lastSeenWakeStopFailures = 0;
    uint32_t _lastSeenSessionReconnects = 0;
    bool _hadNetworkFailureBeforeConnect = false;
    bool _userSpokeInTurn = false;
    bool _waitingForResponse = false;
    bool _turnCompletedFlag = false;
    bool _voiceStopConfirmed = false;
    bool _endSessionAfterTurn = false;
    bool _immediateStopRequested = false;
    bool _manualStopPending = false;
    bool _manualStopImmediate = true;

    std::vector<String> _customStopPhrases;

    // File SPSC lock-free pour les échantillons RMS du microphone vers update()
    static constexpr size_t RMS_QUEUE_CAPACITY = 64;
    volatile uint16_t _rmsQueue[RMS_QUEUE_CAPACITY] = {0};
    std::atomic<size_t> _rmsHead{0};
    std::atomic<size_t> _rmsTail{0};
    void clearRmsQueue();

    // File d'événements bornée pour LVGL 9 / boucle principale
    static constexpr size_t EVENT_QUEUE_CAPACITY = 16;
    WakeWordConversationEvent _eventQueue[EVENT_QUEUE_CAPACITY];
    size_t _eventHead = 0;
    size_t _eventTail = 0;
    size_t _eventCount = 0;

    // Callbacks utilisateur
    WakeDetectedCallback _onWakeWordDetected;
    WakeWordSimpleCallback _onWakeWordDetectedSimple;
    SimpleCallback _onConnecting;
    SimpleCallback _onConversationReady;
    SimpleCallback _onUserListening;
    SimpleCallback _onResponseSpeaking;
    SessionEndedCallback _onConversationEnded;
    SimpleCallback _onConversationEndedSimple;
    SimpleCallback _onReturnedToWake;
    ErrorCallback _onRecoverableError;
    StateCallback _onStateChanged;

    NetworkReadyCallback _networkReadyCheck;
    EnableConversationCallback _enableConversationHook;
};

