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
 * @brief Façade simplifiée, haut niveau et sécurisée pour l'écosystème vocal multi-fournisseurs VoiceKit v0.6.0.
 * 
 * Cette façade unifie et simplifie l'accès aux quatre modes vocaux majeurs :
 * 1. TTS : Synthèse vocale asynchrone PCM 24 kHz haute fidélité (`say`).
 * 2. STT : Reconnaissance vocale manuelle en streaming PCM 16 kHz (`listen` / `stopListening`).
 * 3. ContinuousSTT : Reconnaissance continue mains libres avec VAD intégré (`startContinuousListening`).
 * 4. Conversation : Dialogue vocal bidirectionnel complet avec Gemini Live (`startConversation`).
 * 
 * Architecture & Garanties :
 * - Délégation transparente et exclusive au moteur VoxCore (`VoxCoreVoice`).
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

    VoiceKit();
    ~VoiceKit();

    VoiceKit(const VoiceKit&) = delete;
    VoiceKit& operator=(const VoiceKit&) = delete;

    // =========================================================================
    // Initialisation et Cycle de Vie
    // =========================================================================

    /**
     * @brief Initialise le moteur vocal VoiceKit avec la configuration fournie.
     * @param config Paramètres (clé API Gemini, Wi-Fi, voix, volume, etc.).
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

    /**
     * @brief Callback de réception des transcriptions (partielles et finales).
     *        Distingue strictement les textes intermédiaires (isFinal = false) des définitifs (isFinal = true).
     *        Contexte : Tâche principale (loop / update).
     */
    void onTranscription(TranscriptionCallback cb) { _onTranscription = std::move(cb); }

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
    void onListeningFinished(VoidCallback cb) { _onListeningFinished = std::move(cb); }
    void onStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }
    void onConversationTurnMetrics(TurnFinishedCallback cb) { _onConversationTurnMetrics = std::move(cb); }

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
    TranscriptionCallback _onTranscription;
    VoidCallback _onConversationFinished;
    TranscriptionCallback _onUserText;
    AssistantTextCallback _onAssistantText;
    VoidCallback _onListeningStarted;
    VoidCallback _onListeningFinished;
    StateCallback _onStateChanged;
    TurnFinishedCallback _onConversationTurnMetrics;
    AudioChunkCallback _userAudioChunk;
};
