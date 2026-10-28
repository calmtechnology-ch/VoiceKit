#pragma once

#include <Arduino.h>
#include <functional>
#include <atomic>
#include "IWakeWordEngine.h"
#include "WakeWordAudioBridge.h"
#include "TestWakeWordEngine.h"
#include "WakeNetEngine.h"

class LiveAudioStreamer;

/**
 * @class WakeWordManager
 * @brief Gestionnaire de la machine à états, du cycle de vie et des événements du détecteur
 *        local de mot de réveil (VoiceKit v0.6.2).
 *
 * Responsabilités :
 * - Gestion stricte des états : DISABLED, INITIALIZING, ARMED, DETECTED, SUSPENDED, ERROR.
 * - Distinction explicite entre :
 *   1. Fonction activée (isEnabled)
 *   2. Moteur disponible dans l'environnement (isEngineAvailable)
 *   3. Moteur initialisé (isEngineInitialized)
 *   4. Détection réellement en écoute (isListening)
 *   5. Moteur simulé de test vs reconnaissance vocale réelle (isSimulated)
 * - Coordination de la capture micro locale hors ligne via LiveAudioStreamer (zéro double init).
 * - Suspension automatique pendant toute session de conversation, TTS ou STT pour éviter
 *   tout conflit I2S/DMA et tout faux réveil provoqué par le haut-parleur.
 * - Reprise contrôlée après la fin de la réponse vocale (avec délai d'extinction acoustique).
 * - Exécution de la détection et des callbacks dans update() (contexte boucle principale, compatible LVGL).
 */
class WakeWordManager {
public:
    using WakeWordDetectedCallback = std::function<void(const String& word)>;
    using WakeWordEventCallback = std::function<void(const WakeWordEvent& event)>;
    using WakeWordStateCallback = std::function<void(WakeWordState state)>;
    using WakeWordErrorCallback = std::function<void(const String& error)>;
    using VoiceBusyCheckCallback = std::function<bool()>;

    WakeWordManager();
    ~WakeWordManager();

    WakeWordManager(const WakeWordManager&) = delete;
    WakeWordManager& operator=(const WakeWordManager&) = delete;

    /**
     * @brief Associe le gestionnaire au streamer audio propriétaire du microphone CoreS3 ES7210.
     */
    void begin(LiveAudioStreamer* streamer);

    /**
     * @brief Active ou désactive la détection du mot de réveil.
     * @param enable true pour activer (DISABLED -> INITIALIZING -> ARMED/SUSPENDED/ERROR),
     *               false pour désactiver (retour à DISABLED).
     * @return true si l'opération a abouti sans erreur, false si le moteur est indisponible ou en échec.
     */
    bool enable(bool enable = true);

    /**
     * @brief Désactive complètement le détecteur et libère les tampons du pont audio.
     */
    void disable();

    /**
     * @brief Suspend temporairement le détecteur (ARMED -> SUSPENDED) et libère le microphone.
     * @return true si le détecteur est maintenant en état SUSPENDED.
     */
    bool suspend();

    /**
     * @brief Reprend l'écoute du mot de réveil (SUSPENDED -> ARMED) si le moteur vocal est au repos.
     * @return true si le détecteur est revenu à ARMED (ou programmé pour reprise dès la fin d'activité).
     */
    bool resume();

    /**
     * @brief Traitement cyclique hors callback audio (à appeler dans VoxCoreVoice::update()).
     *        Dépile le pont audio, invoque IWakeWordEngine::process() et émet les événements.
     */
    void update();

    /**
     * @brief Réception d'un bloc PCM 16 kHz depuis la tâche audio critique (non bloquant).
     */
    void onAudioChunkFromTask(const int16_t* samples, size_t sampleCount, uint32_t rms);

    /**
     * @brief Notifie l'état d'occupation vocale de VoiceKit (TTS, STT, Conversation) pour
     *        gérer la suspension automatique et la reprise contrôlée après réponse vocale.
     */
    void notifyVoiceActivity(bool isVoiceBusy, bool isSpeaking);

    // =========================================================================
    // Sélection et Configuration du Moteur (IWakeWordEngine)
    // =========================================================================

    /**
     * @brief Sélectionne le type de moteur à utiliser (WAKENET par défaut, ou TEST).
     */
    bool setEngineType(WakeWordEngineType type);

    /**
     * @brief Injecte un moteur personnalisé implémentant IWakeWordEngine.
     */
    bool setCustomEngine(IWakeWordEngine* customEngine);

    WakeWordEngineType getEngineType() const { return _selectedEngineType; }
    IWakeWordEngine* getActiveEngine() const { return _activeEngine; }
    const char* getEngineName() const;
    const char* getModelName() const;

    void setTargetWakeWord(const String& word);
    String getTargetWakeWord() const { return _targetWakeWord; }

    void setAutoResumeAfterVoiceActivity(bool autoResume) { _autoResumeAfterVoice = autoResume; }
    bool isAutoResumeAfterVoiceActivity() const { return _autoResumeAfterVoice; }

    void setResumeDelayMs(uint32_t delayMs) { _resumeDelayMs = delayMs; }
    uint32_t getResumeDelayMs() const { return _resumeDelayMs; }

    void setUseLocalMicCapture(bool useLocalMic) { _useLocalMicCapture = useLocalMic; }
    bool isUsingLocalMicCapture() const { return _useLocalMicCapture; }

    // =========================================================================
    // Budget CPU et Fluidité Boucle Principale / LVGL (Section 3 — v0.6.2.1)
    // =========================================================================
    static constexpr size_t DEFAULT_MAX_FRAMES_PER_UPDATE = 2;
    static constexpr uint32_t DEFAULT_PROCESSING_BUDGET_US = 5000;

    /**
     * @brief Limite le nombre maximal de trames audio traitées par appel à update().
     * @param maxFrames Nombre maximal de trames (1 .. MAX_QUEUE_CAPACITY, défaut : 2).
     */
    void setMaxFramesPerUpdate(size_t maxFrames);
    size_t getMaxFramesPerUpdate() const { return _maxFramesPerUpdate; }

    /**
     * @brief Définit le budget temporel maximal cumulé dans update() en microsecondes.
     *        Au moins 1 trame est toujours traitée par appel pour garantir la progression,
     *        puis la boucle s'interrompt dès que le budget est atteint.
     * @param budgetUs Budget en microsecondes (0 = sans limite de temps, défaut : 5000 µs).
     */
    void setProcessingBudgetUs(uint32_t budgetUs);
    uint32_t getProcessingBudgetUs() const { return _processingBudgetUs; }
    uint32_t getProcessingBudgetExceededCount() const { return _metrics.processingBudgetExceeded; }

    // =========================================================================
    // États & Distinctions Explicites (Sections 6 — v0.6.2 / v0.6.2.1)
    // =========================================================================

    /**
     * @brief 1. Fonction activée : indique si l'utilisateur a activé la fonctionnalité Wake Word.
     */
    bool isEnabled() const { return _enabled.load(std::memory_order_acquire); }

    /**
     * @brief 2a. Support compilé : indique si le support binaire du moteur sélectionné est compilé.
     */
    bool isEngineCompiledSupportAvailable() const;

    /**
     * @brief 2b. Modèle disponible : indique si le modèle binaire (ex: wn10_fr_bonjouresp) est présent.
     */
    bool isEngineModelAvailable() const;

    /**
     * @brief 2. Moteur disponible : indique si le moteur sélectionné est disponible (support compilé + modèle).
     */
    bool isEngineAvailable() const;
    bool isEngineAvailable(WakeWordEngineType type) const;

    /**
     * @brief 3. Moteur initialisé : indique si le moteur actif a réussi son begin().
     */
    bool isEngineInitialized() const;

    /**
     * @brief 4. Détection réellement active : vrai uniquement si l'état est ARMED, le moteur initialisé
     *        et le pont audio actif.
     */
    bool isListening() const;

    /**
     * @brief 5. Indique si le moteur actif est un moteur de test simulé (non acoustique).
     */
    bool isSimulated() const;

    /**
     * @brief 6. Indique si une véritable reconnaissance vocale acoustique (ex: WakeNet) est active et en écoute.
     */
    bool isRealRecognitionActive() const {
        return isListening() && !isSimulated();
    }

    /**
     * @brief 7. Indique si le détecteur est en état d'erreur (WakeWordState::ERROR).
     */
    bool isError() const { return _state.load(std::memory_order_acquire) == WakeWordState::ERROR; }

    WakeWordState getState() const { return _state.load(std::memory_order_acquire); }
    const String& getLastError() const { return _lastError; }
    const String& getLastDetectedWord() const { return _lastDetectedWord; }

    // =========================================================================
    // Déclenchement Explicite de Test (Moteur TEST uniquement — Section 9)
    // =========================================================================

    /**
     * @brief Déclenche un événement de réveil simulé (uniquement si le moteur actif est TEST et ARMED).
     *        Ne simule jamais une détection si le moteur est WAKENET ou si l'état n'est pas ARMED.
     */
    bool triggerTestDetection(const String& customWord = "");

    /**
     * @brief Traite une commande texte série pour le moteur de test.
     */
    bool handleSerialCommand(const String& cmd);

    // =========================================================================
    // Callbacks & Polling LVGL (Section 6 & 12)
    // =========================================================================
    void onWakeWordDetected(WakeWordDetectedCallback cb) { _onDetected = std::move(cb); }
    void onWakeWordEvent(WakeWordEventCallback cb) { _onEvent = std::move(cb); }
    void onStateChanged(WakeWordStateCallback cb) { _onStateChanged = std::move(cb); }
    void onError(WakeWordErrorCallback cb) { _onError = std::move(cb); }
    void setVoiceBusyCheckCallback(VoiceBusyCheckCallback cb) { _voiceBusyCheck = std::move(cb); }

    /**
     * @brief Récupère et acquitte le dernier événement Wake Word en attente (compatible boucle LVGL).
     * @param outEvent Structure remplie si un événement était disponible.
     * @return true si un nouvel événement a été récupéré, false sinon.
     */
    bool pollEvent(WakeWordEvent& outEvent);
    bool hasPendingEvent() const { return _hasPendingEvent.load(std::memory_order_acquire); }

    // =========================================================================
    // Diagnostics & Accès aux Sous-Composants (Section 11)
    // =========================================================================
    const WakeWordMetrics& getMetrics() const;
    void resetMetrics();

    WakeWordAudioBridge& getAudioBridge() { return _bridge; }
    const WakeWordAudioBridge& getAudioBridge() const { return _bridge; }

    TestWakeWordEngine& getTestEngine() { return _testEngine; }
    const TestWakeWordEngine& getTestEngine() const { return _testEngine; }

    WakeNetEngine& getWakeNetEngine() { return _wakeNetEngine; }
    const WakeNetEngine& getWakeNetEngine() const { return _wakeNetEngine; }

private:
    void setState(WakeWordState newState);
    void selectActiveEnginePointer();
    bool startCaptureIfNeeded();
    bool stopCaptureIfNeeded();
    void refreshMemoryMetrics() const;

    LiveAudioStreamer* _streamer = nullptr;
    WakeWordAudioBridge _bridge;

    WakeNetEngine _wakeNetEngine;
    TestWakeWordEngine _testEngine;
    IWakeWordEngine* _customEngine = nullptr;
    IWakeWordEngine* _activeEngine = nullptr;
    WakeWordEngineType _selectedEngineType = WakeWordEngineType::WAKENET;

    std::atomic<WakeWordState> _state{WakeWordState::INACTIVE};
    std::atomic<bool> _enabled{false};

    String _targetWakeWord = "Bonjour ESP";
    String _lastDetectedWord = "";
    String _lastError = "";

    bool _useLocalMicCapture = true;
    bool _localCaptureStartedByWake = false;
    bool _autoResumeAfterVoice = true;
    bool _voiceActivitySeenSinceSuspend = false;
    bool _wasVoiceBusy = false;
    bool _resumePendingAfterBusy = false;
    bool _inUpdate = false;
    uint32_t _lastVoiceActivityEndMs = 0;
    uint32_t _resumeDelayMs = 250;
    size_t _maxFramesPerUpdate = DEFAULT_MAX_FRAMES_PER_UPDATE;
    uint32_t _processingBudgetUs = DEFAULT_PROCESSING_BUDGET_US;

    mutable WakeWordMetrics _metrics;
    WakeWordEvent _pendingEvent;
    std::atomic<bool> _hasPendingEvent{false};

    WakeWordDetectedCallback _onDetected;
    WakeWordEventCallback _onEvent;
    WakeWordStateCallback _onStateChanged;
    WakeWordErrorCallback _onError;
    VoiceBusyCheckCallback _voiceBusyCheck;
};
