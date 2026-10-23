#pragma once
#include <Arduino.h>
#include <functional>
#include <queue>
#include <memory>
#include "VoiceKitConfig.h"
#include "hal/IAudioOutput.h"
#include "hal/M5AudioOutput.h"
#include "audio/LiveAudioPlayer.h"
#include "gemini/GeminiLiveClient.h"

/**
 * @class VoxCoreVoice
 * @brief Composant invité indépendant de synthèse vocale TTS pour M5Stack CoreS3.
 * 
 * Permet d'intégrer la parole haute fidélité (PCM 24 kHz via Gemini Live)
 * dans toute application hôte (ex: horloges, tableaux de bord, capteurs)
 * sans modifier son affichage, sans bloquer sa boucle principale,
 * et sans couplage avec LVGL.
 */
class VoxCoreVoice {
public:
    using VoidCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const String& error)>;
    using StateCallback = std::function<void(VoiceState state)>;

    VoxCoreVoice();
    ~VoxCoreVoice();

    /**
     * @brief Initialise le moteur vocal VoiceKit.
     * @param config Paramètres de configuration (clé API, mode Wi-Fi, volume, etc.).
     * @return true si l'initialisation a réussi.
     */
    bool begin(const VoiceKitConfig& config);

    /**
     * @brief Lance la synthèse vocale d'un texte de manière asynchrone et non-bloquante.
     * Accepte ou refuse la demande sans immobiliser la boucle principale loop().
     * @param text Texte à prononcer mot à mot.
     * @return true si la demande a été acceptée (ou mise en file), false si refusée.
     */
    bool say(const String& text);

    /**
     * @brief Traitement périodique non-bloquant du moteur vocal.
     * Doit être appelé régulièrement dans la boucle loop() de l'application hôte.
     */
    void update();

    /**
     * @brief Arrête immédiatement toute synthèse ou restitution vocale en cours.
     * Vide les tampons matériels et la file d'attente.
     */
    void stop();

    /**
     * @brief Indique si le haut-parleur est actuellement en train d'émettre du son.
     */
    bool isSpeaking() const;

    /**
     * @brief Indique si le service est prêt à recevoir une demande de parole immédiate.
     */
    bool isReady() const;

    /**
     * @brief Indique si le moteur est occupé (génération réseau ou restitution active).
     */
    bool isBusy() const;

    /**
     * @brief Retourne le dernier message d'erreur survenu.
     */
    String getLastError() const { return _lastError; }

    /**
     * @brief Retourne l'état opérationnel courant.
     */
    VoiceState getState() const { return _state; }

    /**
     * @brief Règle le volume de restitution sonore (0 à 100 %).
     */
    void setVolume(uint8_t percent);

    /**
     * @brief Retourne le volume de restitution actuel (0 à 100 %).
     */
    uint8_t getVolume() const;

    // Enregistrement des callbacks
    void onReady(VoidCallback cb) { _onReady = std::move(cb); }
    void onSpeechStarted(VoidCallback cb) { _onSpeechStarted = std::move(cb); }
    void onSpeechFinished(VoidCallback cb) { _onSpeechFinished = std::move(cb); }
    void onError(ErrorCallback cb) { _onError = std::move(cb); }
    void onStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }

    // Accès aux sous-composants pour diagnostics
    LiveAudioPlayer& getPlayer() { return _player; }
    const LiveAudioPlayer& getPlayer() const { return _player; }
    GeminiLiveClient& getClient() { return _client; }
    const GeminiLiveClient& getClient() const { return _client; }

    /**
     * @brief Vérifie si l'heure système courante est valide (>= 2024), condition requise pour TLS.
     */
    static bool isSystemTimeValid();

private:
    void setState(VoiceState newState);
    void handleClientConnected();
    void handleClientSetupComplete();
    void handleClientDisconnected();
    void handleAudioData(const int16_t* pcm, size_t samples);
    void handleFirstAudio();
    void handleTurnComplete();
    void handlePlaybackFinished();
    void handleClientError(const String& err);
    bool dispatchSpeechRequest(const String& text);

    VoiceKitConfig _config;
    VoiceState _state = VoiceState::UNINITIALIZED;
    String _lastError = "";

    std::unique_ptr<M5AudioOutput> _internalAudioOutput;
    IAudioOutput* _audioOutput = nullptr;

    LiveAudioPlayer _player;
    GeminiLiveClient _client;

    std::queue<String> _textQueue;
    String _pendingTextOnConnect = "";
    uint32_t _lastActivityTimeMs = 0;
    uint32_t _lastWiFiRetryMs = 0;

    // Callbacks utilisateurs
    VoidCallback _onReady;
    VoidCallback _onSpeechStarted;
    VoidCallback _onSpeechFinished;
    ErrorCallback _onError;
    StateCallback _onStateChanged;
};
