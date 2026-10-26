#pragma once
#include <Arduino.h>
#include <functional>
#include <queue>
#include <memory>
#include "VoiceKitConfig.h"
#include "hal/IAudioOutput.h"
#include "hal/M5AudioOutput.h"
#include "hal/IAudioInput.h"
#include "hal/M5AudioInput.h"
#include "hal/HardwareManager.h"
#include "LiveAudioPlayer.h"
#include "core/GeminiLiveClient.h"
#include "core/LiveSessionController.h"
#include "core/LiveAudioStreamer.h"
#include "core/ConversationController.h"
#include "WiFiManager.h"

/**
 * @class VoxCoreVoice
 * @brief Bibliothèque autonome TTS, STT et Conversation vocale bidirectionnelle
 *        pour M5Stack CoreS3 avec Google Gemini Live.
 * 
 * Construit directement sur le moteur stabilisé de VoxCore :
 * - TTS : WebSocket TLS, PCM 24 kHz haute fluidité, buffer DMA, zéro coupure.
 * - STT : Microphone ES7210 16 kHz mono, double-buffering DMA, tâches FreeRTOS découplées,
 *         transcriptions partielles et finales Gemini Live, arbitrage matériel strict.
 * - Conversation (v0.4.0) : Conversation bidirectionnelle native (Mode B), gestion des tours,
 *         coordination anti-larsen, restitution de la réponse complète sans troncature.
 * - Zéro couplage avec LVGL ni UIManager.
 */
class VoxCoreVoice {
public:
    using VoidCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const String& error)>;
    using StateCallback = std::function<void(VoiceState state)>;
    using TranscriptionCallback = std::function<void(const String& text, bool isFinal)>;
    using AssistantTextCallback = std::function<void(const String& text)>;
    using TurnFinishedCallback = std::function<void(const ConversationMetrics& metrics)>;


    VoxCoreVoice();
    ~VoxCoreVoice();

    /**
     * @brief Initialise la bibliothèque et ses sous-systèmes VoxCore.
     * @param config Configuration VoiceKit (clé API, Wi-Fi, volume, etc.).
     * @return true si l'initialisation a réussi.
     */
    bool begin(const VoiceKitConfig& config);

    // =========================================================================
    // API TTS (Text-to-Speech)
    // =========================================================================

    /**
     * @brief Lance la synthèse vocale asynchrone d'un texte via Gemini Live.
     * @param text Texte à prononcer mot à mot.
     * @return true si la requête a été acceptée ou mise en file, false si rejetée.
     */
    bool say(const String& text);

    /**
     * @brief Traitement périodique obligatoire. À appeler régulièrement dans loop().
     */
    void update();

    /**
     * @brief Interrompt immédiatement toute génération ou restitution sonore en cours,
     *        ou annule l'écoute micro active.
     */
    void stop();

    /**
     * @brief Indique si le service vocal est prêt à accepter un nouvel appel say() ou startListening().
     */
    bool isReady() const;

    /**
     * @brief Indique si le moteur est occupé (génération, restitution sonore ou écoute micro).
     */
    bool isBusy() const;

    /**
     * @brief Indique si le haut-parleur émet actuellement du son.
     */
    bool isSpeaking() const;

    /**
     * @brief Retourne le dernier message d'erreur.
     */
    String getLastError() const { return _lastError; }

    /**
     * @brief Retourne l'état opérationnel courant.
     */
    VoiceState getState() const { return _state; }

    /**
     * @brief Règle le volume sonore (0 à 100 %).
     */
    void setVolume(uint8_t percent);

    /**
     * @brief Retourne le volume sonore actuel (0 à 100 %).
     */
    uint8_t getVolume() const;

    // Callbacks TTS & Cycle de vie
    void onReady(VoidCallback cb) { _onReady = std::move(cb); }
    void onSpeechStarted(VoidCallback cb) { _onSpeechStarted = std::move(cb); }
    void onSpeechFinished(VoidCallback cb) { _onSpeechFinished = std::move(cb); }
    void onError(ErrorCallback cb) { _onError = std::move(cb); }
    void onStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }

    // =========================================================================
    // API Configuration Vocale & Profils (v0.4.1 / v0.4.1.1)
    // =========================================================================

    /**
     * @brief Modifie le profil vocal complet (voix, langue, style, ton, accent).
     * 
     * Sémantique des valeurs de retour (v0.4.1.1) :
     * - true  : Demande acceptée. Si la session est connectée au repos, une reconnexion
     *           maîtrisée est initiée ; le profil actif précédent est conservé intact
     *           jusqu'à confirmation du nouveau profil par le serveur (setupComplete).
     *           Si la session n'est pas connectée, le profil est directement appliqué
     *           pour la prochaine connexion.
     * - false : Demande refusée. Le moteur est actif (parole, écoute, transcription,
     *           tour de conversation) ou une connexion/reconnexion est déjà en cours.
     *           Le profil actif précédent est strictement préservé sans altération.
     * 
     * @return true si la modification a été acceptée / appliquée, false si refusée ou échec.
     */
    bool setVoiceProfile(const String& voice, const String& language = "fr-FR",
                         VoiceStyle style = VoiceStyle::NATURAL,
                         VoiceTone tone = VoiceTone::NEUTRAL,
                         const String& accent = "");

    /**
     * @brief Modifie la voix Gemini Live (Aoede, Charon, Fenrir, Kore, Puck).
     * @return true si accepté / appliqué, false si le moteur est actif ou en connexion.
     */
    bool setVoice(const String& voice);

    /**
     * @brief Modifie la langue d'expression (code BCP-47, ex: fr-FR, en-US, de-DE).
     * @return true si accepté / appliqué, false si le moteur est actif ou en connexion.
     */
    bool setLanguage(const String& language);

    /**
     * @brief Modifie le style d'élocution (natural, professional, expressive, narrative).
     * @return true si accepté / appliqué, false si le moteur est actif ou en connexion.
     */
    bool setStyle(VoiceStyle style);

    /**
     * @brief Modifie la tonalité vocale (neutral, calm, warm, cheerful, serious).
     * @return true si accepté / appliqué, false si le moteur est actif ou en connexion.
     */
    bool setTone(VoiceTone tone);

    /**
     * @brief Définit une consigne de prononciation ou accentuation optionnelle.
     * @return true si accepté / appliqué, false si le moteur est actif ou en connexion.
     */
    bool setAccent(const String& accent);

    /**
     * @brief Indique si une connexion ou reconnexion vers Gemini Live est en cours.
     */
    bool isConnecting() const;

    /**
     * @brief Indique si un changement de profil vocal est en cours de négociation avec Gemini Live.
     */
    bool isProfileChangePending() const { return _profileChangePending; }

    /**
     * @brief Indique si un basculement de mode TTS <-> Conversation est en cours de négociation.
     */
    bool isModeSwitchPending() const { return _modeSwitchPending; }

    /**
     * @brief Retourne la configuration actuelle de VoiceKit.
     */
    const VoiceKitConfig& getConfig() const { return _config; }

    String getVoice() const { return _config.voice; }
    String getLanguage() const { return _config.language; }
    VoiceStyle getStyle() const { return _config.style; }
    VoiceTone getTone() const { return _config.tone; }
    String getAccent() const { return _config.accent; }

    // =========================================================================
    // API STT (Speech-to-Text)
    // =========================================================================

    /**
     * @brief Démarre l'acquisition microphone et le streaming audio PCM 16 kHz vers Gemini Live.
     * @return true si la capture a démarré avec succès.
     */
    bool startListening();

    /**
     * @brief Termine l'acquisition microphone et transmet le signal activityEnd.
     *        Bascule en attente de la transcription finale de Gemini.
     * @return true si l'arrêt s'est déroulé proprement.
     */
    bool stopListening();

    /**
     * @brief Annule immédiatement l'acquisition microphone sans attendre de transcription.
     */
    void cancelListening();

    /**
     * @brief Indique si le microphone est actuellement en cours d'enregistrement.
     */
    bool isListening() const;

    /**
     * @brief Indique si la capture est achevée et en attente de la transcription finale.
     */
    bool isTranscribing() const;

    /**
     * @brief Enregistre le callback de réception des transcriptions (partielles et finales).
     * @param cb Callback recevant le texte transcrit et le flag isFinal.
     */
    void onTranscription(TranscriptionCallback cb) { _onTranscription = std::move(cb); }

    /**
     * @brief Callback déclenché au début effectif de l'écoute microphone.
     */
    void onListeningStarted(VoidCallback cb) { _onListeningStarted = std::move(cb); }

    /**
     * @brief Callback déclenché à la fin de l'acquisition microphone (relâchement bouton).
     */
    void onListeningFinished(VoidCallback cb) { _onListeningFinished = std::move(cb); }

    /**
     * @brief Retourne le niveau RMS instantané du dernier bloc micro PCM 16 kHz capturé.
     */
    uint32_t getAudioLevel() const { return _streamer.getLastChunkRms(); }

    /**
     * @brief Enregistre un callback léger invoqué sur chaque bloc audio PCM 16 kHz acquis (32 ms).
     * @param cb Callback recevant les échantillons PCM, leur nombre et le niveau RMS calculé.
     */
    void onAudioChunk(LiveAudioStreamer::AudioChunkCallback cb) { _streamer.onAudioChunk(std::move(cb)); }

    // =========================================================================
    // API Conversation Bidirectionnelle (v0.4.0 / v0.4.1.1)
    // =========================================================================

    /**
     * @brief Active ou désactive le mode conversationnel bidirectionnel natif (Mode B).
     * 
     * Sémantique (v0.4.1.1) :
     * - Refuse tout basculement de mode pendant un tour actif (écoute micro, transcription,
     *   restitution sonore) ou pendant une connexion en cours.
     * - Si la session est connectée et au repos, applique le nouveau mode via une reconnexion
     *   maîtrisée pour négocier la nouvelle instruction système auprès de Gemini Live.
     * - Si la session est hors-ligne, configure le mode pour la future connexion.
     * - Préserve strictement les directives vocales (langue, style, ton, accent) et les callbacks.
     * 
     * @param enable true pour activer la conversation native Gemini Live, false pour le mode unitaire TTS/STT.
     * @return true si la demande est acceptée / appliquée, false si refusée.
     */
    bool enableConversation(bool enable = true);

    /**
     * @brief Indique si le mode conversationnel est actif.
     */
    bool isConversationEnabled() const { return _conversationEnabled; }

    /**
     * @brief Définit le callback recevant la transcription de ce que l'utilisateur dit (partielle ou finale).
     */
    void onUserText(TranscriptionCallback cb) { _onUserText = std::move(cb); }

    /**
     * @brief Définit le callback recevant la transcription de la réponse de l'assistant Gemini.
     */
    void onAssistantText(AssistantTextCallback cb) { _onAssistantText = std::move(cb); }

    /**
     * @brief Callback déclenché à la fin complète d'un tour de conversation (restitution audio achevée).
     */
    void onConversationFinished(VoidCallback cb) { _onConversationFinished = std::move(cb); }

    /**
     * @brief Callback avec métriques complètes du tour de conversation.
     */
    void onConversationTurnMetrics(TurnFinishedCallback cb) { _onConversationTurnMetrics = std::move(cb); }

    /**
     * @brief Retourne les métriques détaillées du dernier tour conversationnel.
     */
    const ConversationMetrics& getConversationMetrics() const { return _conversationController.getMetrics(); }

    /**
     * @brief Accès direct au ConversationController sous-jacent.
     */
    ConversationController& getConversationController() { return _conversationController; }
    const ConversationController& getConversationController() const { return _conversationController; }

    // Accès directs aux composants VoxCore sous-jacents (diagnostics / instrumentation)
    LiveAudioPlayer& getPlayer() { return _player; }
    const LiveAudioPlayer& getPlayer() const { return _player; }
    LiveAudioStreamer& getStreamer() { return _streamer; }
    const LiveAudioStreamer& getStreamer() const { return _streamer; }
    GeminiLiveClient& getClient() { return _client; }
    const GeminiLiveClient& getClient() const { return _client; }
    LiveSessionController& getSession() { return _session; }
    const LiveSessionController& getSession() const { return _session; }
    IAudioOutput* getAudioOutput() const { return _audioOutput; }
    IAudioInput* getAudioInput() const { return _audioInput; }

    /**
     * @brief Retourne les métriques instrumentées de démarrage et de connexion (v0.5.2).
     */
    const ConnectionMetrics& getConnectionMetrics() const { return _connectionMetrics; }

    /**
     * @brief Retourne les métriques instrumentées de latence vocale (v0.5.3).
     */
    const VoiceLatencyMetrics& getLatencyMetrics() const;

    /**
     * @brief Notifie l'instant exact de détection de fin de parole par un VAD local.
     */
    void notifySpeechEndDetected(uint32_t ms);

    /**
     * @brief Vérifie si l'horloge système UTC est synchronisée (timestamp >= 2024).
     */
    static bool isSystemTimeValid();

private:
    void updateConnectionMetrics();
    void setState(VoiceState newState);
    bool dispatchSpeech(const String& text);
    void handleIncomingTranscription(const String& text, bool isFinal);

    VoiceKitConfig _config;
    VoiceState _state = VoiceState::UNINITIALIZED;
    String _lastError = "";

    ConnectionMetrics _connectionMetrics;
    VoiceLatencyMetrics _latencyMetrics;
    uint32_t _initStartTimeMs = 0;
    uint32_t _totalReadyMs = 0;

    std::unique_ptr<M5AudioOutput> _internalAudioOutput;
    IAudioOutput* _audioOutput = nullptr;

    std::unique_ptr<M5AudioInput> _internalAudioInput;
    IAudioInput* _audioInput = nullptr;

    LiveAudioPlayer _player;
    LiveAudioStreamer _streamer;
    GeminiLiveClient _client;
    LiveSessionController _session;
    WiFiManager _wifi;
    ConversationController _conversationController;
    bool _conversationEnabled = false;

    std::queue<String> _textQueue;
    String _pendingText = "";

    uint32_t _connectingStartTimeMs = 0;
    uint32_t _waitingStartTimeMs = 0;

    // États STT internes
    bool _transcriptionPending = false;
    bool _finalTranscriptionReceived = false;
    uint32_t _stopListeningTimeMs = 0;
    String _lastInterimText = "";
    String _lastFinalText = "";

    // Suivi sécurisé des changements de profil vocal (v0.4.1.1)
    bool _profileChangePending = false;
    String _pendingVoice;
    String _pendingLanguage;
    VoiceStyle _pendingStyle = VoiceStyle::NATURAL;
    VoiceTone _pendingTone = VoiceTone::NEUTRAL;
    String _pendingAccent;

    // Suivi sécurisé des transitions de mode TTS <-> Conversation (v0.4.1.1)
    bool _modeSwitchPending = false;
    bool _previousConversationMode = false;

    // Callbacks utilisateurs
    VoidCallback _onReady;
    VoidCallback _onSpeechStarted;
    VoidCallback _onSpeechFinished;
    ErrorCallback _onError;
    StateCallback _onStateChanged;
    TranscriptionCallback _onTranscription;
    VoidCallback _onListeningStarted;
    VoidCallback _onListeningFinished;
    TranscriptionCallback _onUserText;
    AssistantTextCallback _onAssistantText;
    VoidCallback _onConversationFinished;
    TurnFinishedCallback _onConversationTurnMetrics;
};

