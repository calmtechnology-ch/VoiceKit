#pragma once

#include <Arduino.h>
#include <functional>
#include <atomic>
#include "VoiceKitConfig.h"
#include "VoxCoreVoice.h"
#include "ContinuousListeningController.h"

/**
 * @enum VoiceKitMode
 * @brief Mode fonctionnel opérationnel de la façade VoiceKit.
 */
enum class VoiceKitMode : uint8_t {
    IDLE = 0,               ///< Mode standard : permet say() TTS ou listen() STT unitaire
    CONTINUOUS_LISTENING,   ///< Mode reconnaissance vocale continue automatique (ContinuousSTT)
    CONVERSATION            ///< Mode conversationnel bidirectionnel natif (Gemini Live Mode B)
};

inline const char* voiceKitModeToString(VoiceKitMode m) {
    switch (m) {
        case VoiceKitMode::IDLE:                 return "IDLE";
        case VoiceKitMode::CONTINUOUS_LISTENING: return "CONTINUOUS_LISTENING";
        case VoiceKitMode::CONVERSATION:         return "CONVERSATION";
        default:                                 return "INCONNU";
    }
}

/**
 * @class VoiceKit
 * @brief Façade simplifiée, haut niveau et sécurisée pour l'écosystème vocal multi-fournisseurs VoiceKit v0.6.2.
 * 
 * Cette façade unifie et simplifie l'accès aux quatre modes vocaux majeurs ainsi qu'à la
 * fondation de détection locale de mot de réveil (Wake Word) hors ligne :
 * 1. TTS : Synthèse vocale asynchrone PCM 24 kHz haute fidélité (`say`).
 * 2. STT : Reconnaissance vocale manuelle en streaming PCM 16 kHz (`listen` / `stopListening`).
 * 3. ContinuousSTT : Reconnaissance continue mains libres avec VAD intégré (`startContinuousListening`).
 * 4. Conversation : Dialogue vocal bidirectionnel complet avec Gemini Live / OpenAI (`startConversation`).
 * 5. Wake Word : Fondation locale hors ligne pour détection de mot de réveil (`enableWakeWord`).
 * 
 * Architecture & Garanties :
 * - Délégation transparente et exclusive au moteur VoxCore (`VoxCoreVoice`).
 * - Propriété unique du microphone CoreS3 ES7210 (PCM 16 kHz 16-bit mono partagé sans double init).
 * - Zéro duplication des contrôleurs audio, des connexions WebSocket TLS ou des machines à états.
 * - Validation stricte des opérations incompatibles avec retours explicites (`bool`).
 * - Garantie d'unicité des événements de terminaison (aucun callback dupliqué).
 * - Absence totale de double capture microphone matériel (arbitrage HAL préservé).
 * - Contexte d'exécution garanti : tous les callbacks d'événements s'exécutent dans la tâche
 *   principale (thread de l'application / loop via `voice.update()`), à l'exception unique
 *   du callback `onAudioChunk` documenté comme exécuté dans la tâche FreeRTOS audio.
 */
class VoiceKit {
public:
    using VoidCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const String& error)>;
    using StateCallback = std::function<void(VoiceState state)>;
    using TranscriptionCallback = std::function<void(const String& text, bool isFinal)>;
    using AssistantTextCallback = std::function<void(const String& text)>;
    using AudioChunkCallback = LiveAudioStreamer::AudioChunkCallback;
    using TurnFinishedCallback = std::function<void(const ConversationMetrics& metrics)>;
    using WakeWordDetectedCallback = WakeWordManager::WakeWordDetectedCallback;
    using WakeWordEventCallback = WakeWordManager::WakeWordEventCallback;
    using WakeWordStateCallback = WakeWordManager::WakeWordStateCallback;
    using SpeechFinishedCallback = VoxCoreVoice::SpeechFinishedCallback;
    using TranscriptionWithTurnCallback = VoxCoreVoice::TranscriptionWithTurnCallback;
    using ListeningTurnCallback = VoxCoreVoice::ListeningTurnCallback;

    VoiceKit();
    ~VoiceKit();

    VoiceKit(const VoiceKit&) = delete;
    VoiceKit& operator=(const VoiceKit&) = delete;

    // =========================================================================
    // Initialisation et Cycle de Vie
    // =========================================================================

    /**
     * @brief Initialise le moteur vocal VoiceKit avec la configuration fournie.
     * @param config Paramètres (clé API Gemini/OpenAI, Wi-Fi, voix, volume, Wake Word, etc.).
     * @return true si l'initialisation a réussi, false en cas d'erreur.
     */
    bool begin(const VoiceKitConfig& config);

    /**
     * @brief Traitement périodique obligatoire à appeler dans loop().
     */
    void update();

    /**
     * @brief Arrête immédiatement toute activité vocale (TTS, écoute STT, continu ou conversation).
     */
    void stop();

    /**
     * @brief Indique si le moteur est prêt à recevoir une nouvelle commande say(), listen(), etc.
     */
    bool isReady() const;

    /**
     * @brief Indique si le système est actif (génération, émission audio, écoute ou traitement).
     */
    bool isBusy() const;

    /**
     * @brief Retourne l'état opérationnel du moteur VoxCore.
     */
    VoiceState getState() const;

    /**
     * @brief Retourne le mode fonctionnel courant de la façade (IDLE, CONTINUOUS_LISTENING, CONVERSATION).
     */
    VoiceKitMode getMode() const { return _mode.load(std::memory_order_relaxed); }

    /**
     * @brief Retourne le dernier message d'erreur survenu.
     */
    String getLastError() const;

    /**
     * @brief Retourne les métriques instrumentées de démarrage et de connexion (v0.5.2 — Fast Connection).
     */
    const ConnectionMetrics& getConnectionMetrics() const { return _voice.getConnectionMetrics(); }

    /**
     * @brief Retourne les métriques instrumentées de latence du dernier tour vocal (v0.5.3).
     */
    const VoiceLatencyMetrics& getLatencyMetrics() const { return _voice.getLatencyMetrics(); }

    /**
     * @brief Configure le seuil de silence du VAD local en millisecondes (800, 1000, 1200 ms).
     *        Valeur par défaut stricte : 1 200 ms.
     */
    void setVadSilenceDurationMs(uint32_t ms);
    uint32_t getVadSilenceDurationMs() const;
    void setSilenceDurationMs(uint32_t ms) { setVadSilenceDurationMs(ms); }
    uint32_t getSilenceDurationMs() const { return getVadSilenceDurationMs(); }

    /**
     * @brief Règle le volume sonore (0 à 100 %).
     */
    void setVolume(uint8_t percent);

    /**
     * @brief Retourne le volume sonore actuel (0 à 100 %).
     */
    uint8_t getVolume() const;

    // =========================================================================
    // 1. Synthèse Vocale (TTS)
    // =========================================================================

    /**
     * @brief Envoie un texte à synthétiser oralement par Gemini Live.
     * @param text Texte à prononcer mot à mot.
     * @return true si la requête est acceptée, false si rejetée (moteur occupé ou mode incompatible).
     */
    bool say(const String& text);

    /**
     * @brief Indique si le haut-parleur est en cours de restitution sonore.
     */
    bool isSpeaking() const;

    // =========================================================================
    // 2. Reconnaissance Vocale Manuelle (STT - Push-to-Talk)
    // =========================================================================

    /**
     * @brief Démarre l'acquisition microphone et le streaming audio PCM 16 kHz vers Gemini Live.
     * @return true si la capture a démarré avec succès, false si refusée.
     */
    bool listen();

    /**
     * @brief Arrête la capture microphone et attend la transcription finale authentique.
     * @return true si l'arrêt a été émis avec succès, false si le microphone n'était pas ouvert.
     */
    bool stopListening();

    /**
     * @brief Annule immédiatement la capture microphone sans attendre de transcription.
     */
    void cancelListening();

    /**
     * @brief Indique si le microphone est actuellement en cours d'enregistrement.
     */
    bool isListening() const;

    /**
     * @brief Indique si la capture est achevée et en cours d'attente de transcription finale.
     */
    bool isTranscribing() const;

    // =========================================================================
    // 3. Reconnaissance Vocale Continue Mains Libres (ContinuousSTT)
    // =========================================================================

    /**
     * @brief Active le mode d'écoute continue mains libres avec détection automatique VAD.
     * @return true si activé avec succès, false si une opération incompatible est active.
     */
    bool startContinuousListening();

    /**
     * @brief Désactive le mode d'écoute continue et arrête le microphone si ouvert.
     * @return true si arrêté, false si le mode continu n'était pas actif.
     */
    bool stopContinuousListening();

    /**
     * @brief Suspend temporairement la dictée continue (sans fermer la session).
     * @return true si suspendu, false si inactif.
     */
    bool pauseContinuousListening();

    /**
     * @brief Reprend la dictée continue après suspension.
     * @return true si la reprise a démarré, false si le mode continu n'était pas suspendu.
     */
    bool resumeContinuousListening();

    /**
     * @brief Indique si le mode d'écoute continue est actuellement activé.
     */
    bool isContinuousListening() const { return (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING); }

    /**
     * @brief Indique si la dictée continue est en pause.
     */
    bool isContinuousListeningPaused() const { return _continuousController.isPaused(); }

    // =========================================================================
    // 4. Conversation Vocale Bidirectionnelle (Gemini Live Mode B)
    // =========================================================================

    /**
     * @brief Active le mode conversationnel bidirectionnel continu.
     * @return true si accepté, false si refusé (opération active ou mode continu actif).
     */
    bool startConversation();

    /**
     * @brief Désactive le mode conversationnel et revient au mode unitaire standard.
     * @return true si désactivé avec succès.
     */
    bool stopConversation();

    /**
     * @brief Indique si le mode conversationnel est actif et confirmé.
     */
    bool isConversationActive() const {
        return (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONVERSATION &&
                _voice.isConversationEnabled() &&
                !_voice.isModeSwitchPending());
    }

    // =========================================================================
    // 5. Détection Locale de Mot de Réveil (Wake Word Foundation — v0.6.2)
    // =========================================================================

    /**
     * @brief Active ou désactive le détecteur de mot de réveil hors ligne.
     *        Distingue strictement la fonction activée, la disponibilité du moteur,
     *        et l'écoute réelle ou simulée. N'ouvre jamais de connexion réseau automatique.
     * @param enable true pour activer, false pour désactiver.
     * @return true si le moteur est initialisé et actif (ARMED ou SUSPENDED),
     *         false si désactivé ou si le moteur est indisponible/en erreur (ERROR).
     */
    bool enableWakeWord(bool enable = true);

    /**
     * @brief Désactive complètement le détecteur de mot de réveil (coût CPU/RAM minimal).
     */
    void disableWakeWord();

    /**
     * @brief Suspend temporairement la détection Wake Word (état SUSPENDED).
     */
    bool suspendWakeWord();

    /**
     * @brief Reprend la détection Wake Word après une suspension (retour à ARMED).
     */
    bool resumeWakeWord();

    /**
     * @brief Indique si la fonctionnalité Wake Word a été activée par l'utilisateur.
     */
    bool isWakeWordEnabled() const { return _voice.isWakeWordEnabled(); }

    /**
     * @brief Indique si le détecteur écoute effectivement les trames PCM (état ARMED).
     */
    bool isWakeWordListening() const { return _voice.isWakeWordListening(); }

    /**
     * @brief Indique si le support ESP-SR WakeNet est compilé dans l'environnement courant (v0.6.2.1).
     */
    bool isWakeWordEngineCompiledSupportAvailable() const { return _voice.isWakeWordEngineCompiledSupportAvailable(); }

    /**
     * @brief Indique si la partition/modèle WakeNet est effectivement accessible et valide (v0.6.2.1).
     */
    bool isWakeWordEngineModelAvailable() const { return _voice.isWakeWordEngineModelAvailable(); }

    /**
     * @brief Indique si le moteur sélectionné est disponible dans l'environnement courant.
     */
    bool isWakeWordEngineAvailable() const { return _voice.isWakeWordEngineAvailable(); }

    /**
     * @brief Indique si le moteur sélectionné a été initialisé avec succès.
     */
    bool isWakeWordEngineInitialized() const { return _voice.isWakeWordEngineInitialized(); }

    /**
     * @brief Indique si le moteur actif est un moteur de test simulé (et non une reconnaissance acoustique).
     */
    bool isWakeWordSimulated() const { return _voice.isWakeWordSimulated(); }

    /**
     * @brief Indique si une véritable reconnaissance acoustique (non simulée) est active.
     */
    bool isWakeWordRealRecognitionActive() const { return _voice.isWakeWordRealRecognitionActive(); }

    /**
     * @brief Indique si le gestionnaire Wake Word est en état d'erreur (ERROR).
     */
    bool isWakeWordError() const { return _voice.isWakeWordError(); }

    /**
     * @brief Retourne l'état courant de la machine à états Wake Word.
     */
    WakeWordState getWakeWordState() const { return _voice.getWakeWordState(); }

    /**
     * @brief Retourne le dernier message d'erreur du sous-système Wake Word.
     */
    String getWakeWordLastError() const { return _voice.getWakeWordLastError(); }

    /**
     * @brief Retourne le dernier mot de réveil détecté.
     */
    String getLastDetectedWakeWord() const { return _voice.getLastDetectedWakeWord(); }

    /**
     * @brief Sélectionne le type de moteur Wake Word (WAKENET ou TEST).
     */
    bool setWakeWordEngine(WakeWordEngineType type) { return _voice.setWakeWordEngine(type); }

    /**
     * @brief Injecte un moteur personnalisé implémentant IWakeWordEngine.
     */
    bool setWakeWordEngine(IWakeWordEngine* customEngine) { return _voice.setWakeWordEngine(customEngine); }
    bool setCustomWakeWordEngine(IWakeWordEngine* engine) { return _voice.setWakeWordEngine(engine); }

    WakeWordEngineType getWakeWordEngineType() const { return _voice.getWakeWordEngineType(); }
    const char* getWakeWordEngineName() const { return _voice.getWakeWordEngineName(); }
    const char* getWakeWordModelName() const { return _voice.getWakeWordModelName(); }
    bool setWakeWordModelName(const String& modelName) { return _voice.setWakeWordModelName(modelName); }
    String getResolvedWakeWordModelName() const { return _voice.getResolvedWakeWordModelName(); }
    bool isWakeWordPhraseConsistentWithModel() const { return _voice.isWakeWordPhraseConsistentWithModel(); }

    void setWakeWordPhrase(const String& word) { _voice.setWakeWordPhrase(word); }
    String getWakeWordPhrase() const { return _voice.getWakeWordPhrase(); }

    void setWakeWordAutoResume(bool autoResume) { _voice.setWakeWordAutoResume(autoResume); }
    bool isWakeWordAutoResume() const { return _voice.isWakeWordAutoResume(); }

    void setWakeWordResumeDelayMs(uint32_t delayMs) { _voice.setWakeWordResumeDelayMs(delayMs); }
    uint32_t getWakeWordResumeDelayMs() const { return _voice.getWakeWordResumeDelayMs(); }

    /**
     * @brief Définit le nombre maximal de trames PCM traitées par appel à update() (fluidité LVGL — v0.6.2.1).
     */
    void setWakeWordMaxFramesPerUpdate(size_t maxFrames) { _voice.setWakeWordMaxFramesPerUpdate(maxFrames); }
    size_t getWakeWordMaxFramesPerUpdate() const { return _voice.getWakeWordMaxFramesPerUpdate(); }

    /**
     * @brief Définit le budget maximal de temps CPU (µs) par appel à update() pour le Wake Word (v0.6.2.1).
     */
    void setWakeWordProcessingBudgetUs(uint32_t budgetUs) { _voice.setWakeWordProcessingBudgetUs(budgetUs); }
    uint32_t getWakeWordProcessingBudgetUs() const { return _voice.getWakeWordProcessingBudgetUs(); }
    uint32_t getWakeWordProcessingBudgetExceededCount() const { return _voice.getWakeWordProcessingBudgetExceededCount(); }

    /**
     * @brief Déclenche une détection de test simulée (uniquement si moteur TEST et état ARMED).
     */
    bool triggerTestWakeWord(const String& customWord = "") { return _voice.triggerTestWakeWord(customWord); }

    /**
     * @brief Analyse une commande série ("wake", "bonjour", "bonjour esp") pour le moteur TEST.
     */
    bool handleWakeWordSerialCommand(const String& commandLine) {
        return _voice.handleWakeWordSerialCommand(commandLine);
    }

    /**
     * @brief Callback déclenché lors de la détection d'un mot de réveil.
     *        Contexte : Tâche principale (loop / update), compatible LVGL.
     */
    void onWakeWordDetected(WakeWordDetectedCallback cb) { _voice.onWakeWordDetected(std::move(cb)); }

    /**
     * @brief Callback détaillé incluant les métadonnées (isSimulated, latence, confiance).
     *        Contexte : Tâche principale (loop / update).
     */
    void onWakeWordEvent(WakeWordEventCallback cb) { _voice.onWakeWordEvent(std::move(cb)); }

    /**
     * @brief Callback déclenché à chaque transition d'état de la machine Wake Word.
     *        Contexte : Tâche principale (loop / update).
     */
    void onWakeWordStateChanged(WakeWordStateCallback cb) { _voice.onWakeWordStateChanged(std::move(cb)); }

    /**
     * @brief Callback déclenché en cas d'erreur d'initialisation ou d'exécution du moteur Wake Word.
     */
    void onWakeWordError(ErrorCallback cb) { _voice.onWakeWordError(std::move(cb)); }

    /**
     * @brief Récupère de manière non bloquante le dernier événement Wake Word pour la boucle LVGL.
     */
    bool pollWakeWordEvent(WakeWordEvent& outEvent) { return _voice.pollWakeWordEvent(outEvent); }
    bool hasPendingWakeWordEvent() const { return _voice.hasPendingWakeWordEvent(); }

    /**
     * @brief Retourne un instantané complet des métriques mémoire, CPU et files du sous-système Wake Word.
     */
    WakeWordMetrics getWakeWordMetrics() const { return _voice.getWakeWordMetrics(); }

    WakeWordManager& getWakeWordManager() { return _voice.getWakeWordManager(); }
    const WakeWordManager& getWakeWordManager() const { return _voice.getWakeWordManager(); }

    // =========================================================================
    // 6. Conversation Mains Libres par Mot de Réveil (Wake Word Conversation — v0.6.3)
    // =========================================================================

    bool enableWakeWordConversation(bool enable = true) { return _voice.enableWakeWordConversation(enable); }
    void disableWakeWordConversation() { _voice.disableWakeWordConversation(); }
    bool isWakeWordConversationEnabled() const { return _voice.isWakeWordConversationEnabled(); }
    bool isWakeWordConversationActive() const { return _voice.isWakeWordConversationActive(); }
    bool isWakeWordConversationInProgress() const { return _voice.isWakeWordConversationInProgress(); }

    bool stopWakeWordConversationSession(bool immediate = true) { return _voice.stopWakeWordConversationSession(immediate); }
    bool requestEndWakeWordConversationAfterTurn() { return _voice.requestEndWakeWordConversationAfterTurn(); }

    WakeWordConversationState getWakeWordConversationState() const { return _voice.getWakeWordConversationState(); }
    WakeWordSessionEndReason getWakeWordConversationEndReason() const { return _voice.getWakeWordConversationEndReason(); }
    const char* getWakeWordConversationStatusText() const { return _voice.getWakeWordConversationStatusText(); }
    const char* getWakeWordConversationBannerText() const { return _voice.getWakeWordConversationBannerText(); }
    const WakeWordConversationMetrics& getWakeWordConversationMetrics() const { return _voice.getWakeWordConversationMetrics(); }
    void resetWakeWordConversationMetrics() { _voice.resetWakeWordConversationMetrics(); }
    void printWakeWordConversationDiagnostics() const { _voice.printWakeWordConversationDiagnostics(); }
    bool pollWakeWordConversationEvent(WakeWordConversationEvent& outEvent) { return _voice.pollWakeWordConversationEvent(outEvent); }

    void setWakeConversationSilenceTimeoutMs(uint32_t timeoutMs) { _voice.setWakeConversationSilenceTimeoutMs(timeoutMs); }
    uint32_t getWakeConversationSilenceTimeoutMs() const { return _voice.getWakeConversationSilenceTimeoutMs(); }
    void setWakeConversationMaxSessionDurationMs(uint32_t maxDurationMs) { _voice.setWakeConversationMaxSessionDurationMs(maxDurationMs); }
    uint32_t getWakeConversationMaxSessionDurationMs() const { return _voice.getWakeConversationMaxSessionDurationMs(); }
    void setWakeConversationVoiceStopEnabled(bool enabled) { _voice.setWakeConversationVoiceStopEnabled(enabled); }
    bool isWakeConversationVoiceStopEnabled() const { return _voice.isWakeConversationVoiceStopEnabled(); }
    void setWakeConversationDisconnectAfterSession(bool disconnectAfter) { _voice.setWakeConversationDisconnectAfterSession(disconnectAfter); }
    bool isWakeConversationDisconnectAfterSession() const { return _voice.isWakeConversationDisconnectAfterSession(); }
    void addWakeConversationStopPhrase(const String& phrase) { _voice.addWakeConversationStopPhrase(phrase); }

    void onWakeConvDetected(WakeWordConversationController::WakeDetectedCallback cb) { _voice.onWakeConvDetected(std::move(cb)); }
    void onWakeConvDetectedSimple(WakeWordConversationController::WakeWordSimpleCallback cb) { _voice.onWakeConvDetectedSimple(std::move(cb)); }
    void onWakeConvConnecting(VoidCallback cb) { _voice.onWakeConvConnecting(std::move(cb)); }
    void onWakeConvReady(VoidCallback cb) { _voice.onWakeConvReady(std::move(cb)); }
    void onWakeConvListening(VoidCallback cb) { _voice.onWakeConvListening(std::move(cb)); }
    void onWakeConvSpeaking(VoidCallback cb) { _voice.onWakeConvSpeaking(std::move(cb)); }
    void onWakeConvEnded(WakeWordConversationController::SessionEndedCallback cb) { _voice.onWakeConvEnded(std::move(cb)); }
    void onWakeConvEndedSimple(VoidCallback cb) { _voice.onWakeConvEndedSimple(std::move(cb)); }
    void onWakeConvReturnedToWake(VoidCallback cb) { _voice.onWakeConvReturnedToWake(std::move(cb)); }
    void onWakeConvRecoverableError(ErrorCallback cb) { _voice.onWakeConvRecoverableError(std::move(cb)); }
    void onWakeConvStateChanged(WakeWordConversationController::StateCallback cb) { _voice.onWakeConvStateChanged(std::move(cb)); }

    WakeWordConversationController& getWakeWordConversationController() { return _voice.getWakeWordConversationController(); }
    const WakeWordConversationController& getWakeWordConversationController() const { return _voice.getWakeWordConversationController(); }

    // =========================================================================
    // Configuration Vocale & Profils (v0.4.1)
    // =========================================================================

    /**
     * @brief Modifie le profil vocal complet (voix, langue, style, ton, accent).
     * @return true si accepté/appliqué, false si refusé (moteur actif en parole/écoute).
     */
    bool setVoiceProfile(const String& voice, const String& language = "fr-FR",
                         VoiceStyle style = VoiceStyle::NATURAL,
                         VoiceTone tone = VoiceTone::NEUTRAL,
                         const String& accent = "");

    bool setVoice(const String& voice);
    bool setLanguage(const String& language);
    bool setStyle(VoiceStyle style);
    bool setTone(VoiceTone tone);
    bool setAccent(const String& accent);

    String getVoice() const;
    String getLanguage() const;
    VoiceStyle getStyle() const;
    VoiceTone getTone() const;
    String getAccent() const;

    // =========================================================================
    // Gestion des Événements & Callbacks
    // =========================================================================

    /**
     * @brief Callback déclenché dès que le moteur et la session Gemini Live sont prêts.
     *        Contexte : Tâche principale (loop / update).
     */
    void onReady(VoidCallback cb) { _onReady = std::move(cb); }

    /**
     * @brief Callback déclenché en cas d'erreur de connexion, audio ou protocole.
     *        Contexte : Tâche principale (loop / update).
     */
    void onError(ErrorCallback cb) { _onError = std::move(cb); }

    /**
     * @brief Callback déclenché au début effectif de la restitution vocale (TTS ou assistant).
     *        Contexte : Tâche principale (loop / update).
     */
    void onSpeechStarted(VoidCallback cb) { _onSpeechStarted = std::move(cb); }

    /**
     * @brief Callback déclenché à la fin exacte de la restitution sonore (haut-parleur libéré).
     *        Garanti émis une seule fois par opération terminée.
     *        Contexte : Tâche principale (loop / update).
     */
    void onSpeechFinished(VoidCallback cb) { _onSpeechFinished = std::move(cb); }
    void onSpeechFinished(SpeechFinishedCallback cb) { _onSpeechFinishedWithId = std::move(cb); }
    void onSpeechFinishedWithId(SpeechFinishedCallback cb) { _onSpeechFinishedWithId = std::move(cb); }

    /**
     * @brief Callback de réception des transcriptions (partielles et finales).
     *        Distingue strictement les textes intermédiaires (isFinal = false) des définitifs (isFinal = true).
     *        Contexte : Tâche principale (loop / update).
     */
    void onTranscription(TranscriptionCallback cb) { _onTranscription = std::move(cb); }
    void onTranscription(TranscriptionWithTurnCallback cb) { _onTranscriptionWithTurn = std::move(cb); }
    void onTranscriptionWithTurnId(TranscriptionWithTurnCallback cb) { _onTranscriptionWithTurn = std::move(cb); }

    /**
     * @brief Callback déclenché à la fin complète d'un tour de conversation (réponse audio terminée).
     *        Garanti émis une seule fois par tour terminé.
     *        Contexte : Tâche principale (loop / update).
     */
    void onConversationFinished(VoidCallback cb) { _onConversationFinished = std::move(cb); }

    // Callbacks complémentaires
    void onUserText(TranscriptionCallback cb) { _onUserText = std::move(cb); }
    void onAssistantText(AssistantTextCallback cb) { _onAssistantText = std::move(cb); }
    void onListeningStarted(VoidCallback cb) { _onListeningStarted = std::move(cb); }
    void onListeningStarted(ListeningTurnCallback cb) { _onListeningStartedWithTurn = std::move(cb); }
    void onListeningStartedWithTurnId(ListeningTurnCallback cb) { _onListeningStartedWithTurn = std::move(cb); }
    void onListeningFinished(VoidCallback cb) { _onListeningFinished = std::move(cb); }
    void onStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }
    void onConversationTurnMetrics(TurnFinishedCallback cb) { _onConversationTurnMetrics = std::move(cb); }

    /**
     * @brief Identifiants de corrélation STT / TTS exposés par la façade (v0.6.4.3.2)
     */
    uint32_t getActiveSpeechId() const { return _voice.getActiveSpeechId(); }
    uint32_t getActiveCaptureTurnId() const { return _voice.getActiveCaptureTurnId(); }
    uint32_t getPendingTranscriptionTurnId() const { return _voice.getPendingTranscriptionTurnId(); }
    uint32_t getLastCaptureTurnId() const { return _voice.getLastCaptureTurnId(); }
    const String& getLastFinalText() const { return _voice.getLastFinalText(); }

    /**
     * @brief Callback sur blocs audio PCM 16 kHz acquis (32 ms).
     *        ATTENTION : Exécuté dans le contexte de la tâche FreeRTOS audio (Core 1).
     *        Doit impérativement rester court, non bloquant et sans allocation mémoire.
     */
    void onAudioChunk(AudioChunkCallback cb) { _userAudioChunk = std::move(cb); }

    // =========================================================================
    // Accès aux Composants Sous-Jacents
    // =========================================================================
    VoxCoreVoice& getEngine() { return _voice; }
    const VoxCoreVoice& getEngine() const { return _voice; }

    ContinuousListeningController& getContinuousController() { return _continuousController; }
    const ContinuousListeningController& getContinuousController() const { return _continuousController; }

    VoiceActivityDetector& getVad() { return _continuousController.getVad(); }
    const VoiceActivityDetector& getVad() const { return _continuousController.getVad(); }

    const VoiceKitConfig& getConfig() const { return _voice.getConfig(); }
    void setVadConfig(const VadConfig& config) { _continuousController.setVadConfig(config); }

private:
    VoxCoreVoice _voice;
    ContinuousListeningController _continuousController;
    std::atomic<VoiceKitMode> _mode{VoiceKitMode::IDLE};

    VoidCallback _onReady;
    ErrorCallback _onError;
    VoidCallback _onSpeechStarted;
    VoidCallback _onSpeechFinished;
    SpeechFinishedCallback _onSpeechFinishedWithId;
    TranscriptionCallback _onTranscription;
    TranscriptionWithTurnCallback _onTranscriptionWithTurn;
    VoidCallback _onConversationFinished;
    TranscriptionCallback _onUserText;
    AssistantTextCallback _onAssistantText;
    VoidCallback _onListeningStarted;
    ListeningTurnCallback _onListeningStartedWithTurn;
    VoidCallback _onListeningFinished;
    StateCallback _onStateChanged;
    TurnFinishedCallback _onConversationTurnMetrics;
    AudioChunkCallback _userAudioChunk;
};
