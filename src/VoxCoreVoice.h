#pragma once
#include <Arduino.h>
#include <functional>
#include <queue>
#include <memory>
#include "VoiceKitConfig.h"
#include "hal/IAudioOutput.h"
#include "hal/M5AudioOutput.h"
#include "LiveAudioPlayer.h"
#include "core/GeminiLiveClient.h"
#include "core/LiveSessionController.h"
#include "WiFiManager.h"

/**
 * @class VoxCoreVoice
 * @brief Bibliotheque autonome de synthese vocale TTS pour M5Stack CoreS3.
 * 
 * Construit directement sur le moteur stabilise de VoxCore :
 * - WebSocket TLS esp_websocket_client & racines Google PKI
 * - Audio natif PCM 24 kHz haute fluidite
 * - Quadruple buffer DMA & tache FreeRTOS dediee (Mode 2)
 * - Restitution sans coupure de fin de syllabe
 * - Zero couplage avec LVGL ni UIManager
 */
class VoxCoreVoice {
public:
    using VoidCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const String& error)>;
    using StateCallback = std::function<void(VoiceState state)>;

    VoxCoreVoice();
    ~VoxCoreVoice();

    /**
     * @brief Initialise la bibliotheque et ses sous-systemes VoxCore.
     * @param config Configuration VoiceKit (cle API, Wi-Fi, volume, etc.).
     * @return true si l'initialisation a reussi.
     */
    bool begin(const VoiceKitConfig& config);

    /**
     * @brief Lance la synthese vocale asynchrone d'un texte via Gemini Live.
     * @param text Texte a prononcer mot a mot.
     * @return true si la requete a ete acceptee ou mise en file, false si rejetee.
     */
    bool say(const String& text);

    /**
     * @brief Traitement periodique obligatoire. A appeler regulierement dans loop().
     */
    void update();

    /**
     * @brief Interrompt immediatement toute generation ou restitution sonore en cours.
     */
    void stop();

    /**
     * @brief Indique si le service vocal est pret a accepter un nouvel appel say().
     */
    bool isReady() const;

    /**
     * @brief Indique si le moteur est occupe (requete en vol, generation ou parole active).
     */
    bool isBusy() const;

    /**
     * @brief Indique si le haut-parleur emet actuellement du son.
     */
    bool isSpeaking() const;

    /**
     * @brief Retourne le dernier message d'erreur.
     */
    String getLastError() const { return _lastError; }

    /**
     * @brief Retourne l'etat operationnel courant.
     */
    VoiceState getState() const { return _state; }

    /**
     * @brief Regle le volume sonore (0 a 100 %).
     */
    void setVolume(uint8_t percent);

    /**
     * @brief Retourne le volume sonore actuel (0 a 100 %).
     */
    uint8_t getVolume() const;

    // Enregistrement des callbacks essentiels
    void onReady(VoidCallback cb) { _onReady = std::move(cb); }
    void onSpeechStarted(VoidCallback cb) { _onSpeechStarted = std::move(cb); }
    void onSpeechFinished(VoidCallback cb) { _onSpeechFinished = std::move(cb); }
    void onError(ErrorCallback cb) { _onError = std::move(cb); }
    void onStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }

    // Accesseur direct aux composants VoxCore sous-jacents (pour diagnostics)
    LiveAudioPlayer& getPlayer() { return _player; }
    const LiveAudioPlayer& getPlayer() const { return _player; }
    GeminiLiveClient& getClient() { return _client; }
    const GeminiLiveClient& getClient() const { return _client; }
    LiveSessionController& getSession() { return _session; }
    const LiveSessionController& getSession() const { return _session; }

    /**
     * @brief Verifie si l'horloge systeme UTC est synchronisee (timestamp >= 2024).
     */
    static bool isSystemTimeValid();

private:
    void setState(VoiceState newState);
    bool dispatchSpeech(const String& text);

    VoiceKitConfig _config;
    VoiceState _state = VoiceState::UNINITIALIZED;
    String _lastError = "";

    std::unique_ptr<M5AudioOutput> _internalAudioOutput;
    IAudioOutput* _audioOutput = nullptr;

    LiveAudioPlayer _player;
    GeminiLiveClient _client;
    LiveSessionController _session;
    WiFiManager _wifi;

    std::queue<String> _textQueue;
    String _pendingText = "";

    uint32_t _connectingStartTimeMs = 0;
    uint32_t _waitingStartTimeMs = 0;

    // Callbacks utilisateurs
    VoidCallback _onReady;
    VoidCallback _onSpeechStarted;
    VoidCallback _onSpeechFinished;
    ErrorCallback _onError;
    StateCallback _onStateChanged;
};
