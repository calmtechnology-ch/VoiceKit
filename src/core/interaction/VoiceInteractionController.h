#pragma once

#include <Arduino.h>
#include <functional>
#include <stdint.h>
#include "VoxCoreVoice.h"
#include "VoiceCaptureValidator.h"

/**
 * @file VoiceInteractionController.h
 * @brief Machine à états réutilisable à deux clics (PTT à bascule), validation locale et STOP prioritaire.
 *        Composant autonome, modulaire et indépendant de LVGL pour VoiceKit v0.6.7.1.
 */

enum class VoiceInteractionState : uint8_t {
    DISABLED = 0,      ///< Bouton/interaction désactivé (Wi-Fi déconnecté ou session non établie)
    READY,             ///< Prêt à engager une prise de parole
    STARTING,          ///< Demande de capture initiée, attente confirmation moteur
    LISTENING,         ///< Microphone actif, capture en cours
    STOPPING_CAPTURE,  ///< Fin de parole demandée, validation locale et transmission serveur
    PROCESSING,        ///< Traitement et synthèse serveur en cours
    SPEAKING,          ///< Assistant en cours d'élocution sur haut-parleur
    INTERRUPTING,      ///< Arrêt prioritaire STOP en cours d'exécution
    RECOVERING,        ///< Nettoyage et reprise contrôlée après anomalie
    ERROR_STATE        ///< État d'erreur transport ou service
};

const char* voiceInteractionStateToString(VoiceInteractionState st);

// Alias de rétrocompatibilité avec SmartUI
using PTTState = VoiceInteractionState;
const char* pttStateToString(VoiceInteractionState st);

enum class VoiceInteractionIntent : uint8_t {
    NONE = 0,
    START_CAPTURE,     ///< 1er clic : engager l'écoute micro
    FINISH_CAPTURE,    ///< 2ème clic : clôturer et valider l'activité vocale
    CANCEL_CAPTURE,    ///< Commande STOP : abandon inconditionnel immédiat
    RECOVER            ///< Demande de réinitialisation contrôlée suite à erreur
};

// Alias de rétrocompatibilité avec SmartUI
using PTTIntent = VoiceInteractionIntent;
const char* voiceInteractionIntentToString(VoiceInteractionIntent intent);
const char* pttIntentToString(VoiceInteractionIntent intent);

struct VoiceInteractionConfig {
    uint32_t debounceMs = 120;                  ///< Filtrage anti-rebond matériel (ms)
    uint32_t micTimeoutMs = 15000;              ///< Timeout d'écoute microphone (ms)
    uint32_t serverResponseTimeoutMs = 15000;   ///< Timeout attente réponse serveur (ms)
    uint32_t stoppedRecoveryMs = 1000;          ///< Délai avant retour READY après STOPPED (ms)
    uint32_t interruptingRecoveryMs = 1500;     ///< Stabilisation maximale après INTERRUPTING (ms)
    uint32_t errorRecoveryMs = 15000;           ///< Tentative automatique de reprise après erreur (ms)

    // Paramètres VAD délégués à VoiceCaptureValidator
    uint32_t minCaptureDurationMs = 350;        ///< Durée minimale de capture (ms) [défaut CoreS3: 350]
    uint32_t minSpeechDurationMs = 250;         ///< Durée cumulée minimale de parole (ms) [défaut: 250]
    uint32_t minConsecutiveSpeechMs = 120;      ///< Segment continu minimal de parole (ms) [défaut: 120]
    uint32_t speechThresholdFloorRms = 380;     ///< Seuil RMS minimal plancher pour ES7210 [défaut: 380]
    float noiseMultiplier = 1.8f;               ///< Multiplicateur du bruit ambiant [défaut: 1.8]
    uint32_t noiseOffsetRms = 150;              ///< Décalage RMS [défaut: +150]
    float initialNoiseFloor = 180.0f;           ///< Plancher de bruit initial [défaut: 180]
};

class VoiceInteractionController {
public:
    using SilenceDetectedCallback = std::function<void(const char* reason)>;
    using StateChangedCallback = std::function<void(VoiceInteractionState oldState, VoiceInteractionState newState)>;
    using CaptureValidatedCallback = std::function<void(const VoiceCaptureMetrics& metrics)>;

    explicit VoiceInteractionController(
        VoxCoreVoice& voiceEngine,
        const VoiceInteractionConfig& config = VoiceInteractionConfig()
    );

    // Initialisation
    bool begin();

    // Interaction utilisateur universelle (tactile LVGL, bouton physique CoreS3, GPIO, série)
    void onTalkClick();
    void triggerStop();
    void recover();

    // Cycle principal non bloquant (obligatoire dans loop())
    void update(uint32_t nowMs = 0);

    // Callbacks d'observation IHM
    void onSilenceDetected(SilenceDetectedCallback cb) { _onSilence = std::move(cb); }
    void onStateChanged(StateChangedCallback cb) { _onStateChanged = std::move(cb); }
    void onCaptureValidated(CaptureValidatedCallback cb) { _onCaptureValidated = std::move(cb); }

    // API explicite de notification / relais (depuis callbacks VoxCoreVoice de l'application)
    void notifyVoiceStateChanged(VoiceState newState);
    void notifyListeningStarted();
    void notifyListeningFinished();
    void notifySpeechStarted();
    void notifySpeechFinished();
    void notifyError(const String& err);
    void notifyReady();

    // Acheminement des blocs audio pour VAD
    void processAudioChunk(const int16_t* samples, size_t count, uint32_t rms);
    void feedChunkRms(uint32_t rms);

    // Liaison optionnelle directe aux callbacks de VoxCoreVoice
    void attachVoiceCallbacks();

    // Accesseurs
    VoiceInteractionState getState() const { return _state; }
    bool isListening() const { return (_state == VoiceInteractionState::LISTENING || _state == VoiceInteractionState::STARTING); }
    bool isReady() const { return (_state == VoiceInteractionState::READY); }
    bool isActionAllowed() const;
    bool isLastTurnCancelled() const { return _lastTurnCancelled; }
    const char* getRejectCause() const;
    const VoiceCaptureMetrics& getLastMetrics() const { return _validator.getLastMetrics(); }

    const VoiceCaptureValidator& getValidator() const { return _validator; }
    VoiceCaptureValidator& getValidator() { return _validator; }

    const VoiceInteractionConfig& getConfig() const { return _config; }
    void setConfig(const VoiceInteractionConfig& config);

    // Compteurs d'observabilité et instrumentation pour tests
    uint32_t getStartListeningCallsCount() const { return _startListeningCallsCount; }
    uint32_t getStopListeningCallsCount() const { return _stopListeningCallsCount; }
    uint32_t getCancelListeningCallsCount() const { return _cancelListeningCallsCount; }
    uint32_t getStopInterruptsCount() const { return _stopInterruptsCount; }
    uint32_t getResponseRequestsSentCount() const { return _responseRequestsSentCount; }
    uint32_t getSilenceRejectionsCount() const { return _silenceRejectionsCount; }
    void resetCallCounters();

    // Compatibilité descendante SmartUI (PTTController)
    void onParlerClick() { onTalkClick(); }
    void onButtonPress(uint32_t = 0, int32_t = 0, int32_t = 0) {}
    void onButtonRelease(uint32_t = 0, int32_t = 0, int32_t = 0) {}
    void onButtonPressLost(uint32_t = 0, int32_t = 0, int32_t = 0) {}
    void triggerSerialPress() { onTalkClick(); }
    void triggerSerialRelease();

private:
    VoxCoreVoice& _voice;
    VoiceCaptureValidator _validator;
    VoiceInteractionConfig _config;

    SilenceDetectedCallback _onSilence;
    StateChangedCallback _onStateChanged;
    CaptureValidatedCallback _onCaptureValidated;

    VoiceInteractionState _state = VoiceInteractionState::DISABLED;
    VoiceInteractionState _prevState = VoiceInteractionState::DISABLED;
    volatile VoiceInteractionIntent _pendingIntent = VoiceInteractionIntent::NONE;

    bool _startListeningCalled = false;
    bool _stopListeningCalled = false;
    bool _isListeningActive = false;
    bool _lastTurnCancelled = false;
    const char* _lastRejectCause = "OK";

    uint32_t _lastClickTimeMs = 0;
    uint32_t _captureStartTimeMs = 0;
    uint32_t _listeningStartTimeMs = 0;
    uint32_t _processingStartTimeMs = 0;
    uint32_t _stoppedStartTimeMs = 0;
    uint32_t _stopStartTimeMs = 0;
    uint32_t _errorStartTimeMs = 0;

    // Compteurs de suivi
    uint32_t _startListeningCallsCount = 0;
    uint32_t _stopListeningCallsCount = 0;
    uint32_t _cancelListeningCallsCount = 0;
    uint32_t _stopInterruptsCount = 0;
    uint32_t _responseRequestsSentCount = 0;
    uint32_t _silenceRejectionsCount = 0;

    void setState(VoiceInteractionState newState);
    void processIntents(uint32_t nowMs);
    void checkWatchdogs(uint32_t nowMs);
};
