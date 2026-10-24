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
#include "WiFiManager.h"

/**
 * @class VoxCoreVoice
 * @brief Bibliothèque autonome TTS (Synthèse vocale) et STT (Reconnaissance vocale)
 *        pour M5Stack CoreS3 avec Google Gemini Live.
 * 
 * Construit directement sur le moteur stabilisé de VoxCore :
 * - TTS : WebSocket TLS, PCM 24 kHz haute fluidité, buffer DMA, zéro coupure.
 * - STT : Microphone ES7210 16 kHz mono, double-buffering DMA, tâches FreeRTOS découplées,
 *         transcriptions partielles et finales Gemini Live, arbitrage matériel strict.
 * - Zéro couplage avec LVGL ni UIManager.
 */
class VoxCoreVoice {
public:
    using VoidCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const String& error)>;
    using StateCallback = std::function<void(VoiceState state)>;
    using TranscriptionCallback = std::function<void(const String& text, bool isFinal)>;

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
     * @brief Vérifie si l'horloge système UTC est synchronisée (timestamp >= 2024).
     */
    static bool isSystemTimeValid();

private:
    void setState(VoiceState newState);
    bool dispatchSpeech(const String& text);
    void handleIncomingTranscription(const String& text, bool isFinal);

    VoiceKitConfig _config;
    VoiceState _state = VoiceState::UNINITIALIZED;
    String _lastError = "";

    std::unique_ptr<M5AudioOutput> _internalAudioOutput;
    IAudioOutput* _audioOutput = nullptr;

    std::unique_ptr<M5AudioInput> _internalAudioInput;
    IAudioInput* _audioInput = nullptr;

    LiveAudioPlayer _player;
    LiveAudioStreamer _streamer;
    GeminiLiveClient _client;
    LiveSessionController _session;
    WiFiManager _wifi;

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

    // Callbacks utilisateurs
    VoidCallback _onReady;
    VoidCallback _onSpeechStarted;
    VoidCallback _onSpeechFinished;
    ErrorCallback _onError;
    StateCallback _onStateChanged;
    TranscriptionCallback _onTranscription;
    VoidCallback _onListeningStarted;
    VoidCallback _onListeningFinished;
};
