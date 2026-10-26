#pragma once

#include <Arduino.h>
#include <functional>
#include <atomic>
#include "VoiceActivityDetector.h"
#include "VoiceKitConfig.h"

// Déclaration anticipée
class VoxCoreVoice;

/**
 * @enum ContinuousState
 * @brief États fonctionnels du contrôleur de dictée vocale continue.
 */
enum class ContinuousState : uint8_t {
    INACTIVE = 0,       ///< Contrôleur arrêté / inactif
    IDLE,               ///< Actif mais en attente (session en connexion ou repos)
    LISTENING,          ///< Microphone ouvert, streaming actif, VAD en écoute
    SPEECH_DETECTED,    ///< Voix active détectée et confirmée (> 200 ms)
    WAITING_FINAL,      ///< Fin de parole détectée (silence 1 200 ms ou 30 s max), stopListening() émis, attente transcription finale
    SEGMENT_DONE,       ///< Segment finalisé (transcription reçue ou timeout), temporisation avant reprise
    PAUSED              ///< Dictée suspendue manuellement (pause)
};

inline const char* continuousStateToString(ContinuousState s) {
    switch (s) {
        case ContinuousState::INACTIVE:        return "INACTIF";
        case ContinuousState::IDLE:            return "EN_ATTENTE";
        case ContinuousState::LISTENING:       return "ECOUTE";
        case ContinuousState::SPEECH_DETECTED: return "PAROLE_DETECTEE";
        case ContinuousState::WAITING_FINAL:   return "ATTENTE_TRANSCRIPTION";
        case ContinuousState::SEGMENT_DONE:    return "SEGMENT_TERMINE";
        case ContinuousState::PAUSED:          return "EN_PAUSE";
        default:                               return "INCONNU";
    }
}

/**
 * @class ContinuousListeningController
 * @brief Contrôleur autonome et réutilisable de reconnaissance vocale continue (ContinuousSTT).
 * 
 * Responsabilités (v0.5.0.1) :
 * - Analyse synchrone et non bloquante des blocs PCM 16 kHz via VoiceActivityDetector.
 * - Transfert thread-safe et lock-free des valeurs RMS de la tâche audio FreeRTOS vers update().
 * - Détection automatique de début d'élocution (200 ms) et de fin de phrase (1 200 ms de silence).
 * - Clôture automatique des segments à 30 secondes maximum.
 * - Émission thread-safe de stopListening() depuis la boucle principale (update).
 * - Attente déterministe de la transcription finale Gemini Live authentique.
 * - Identifiant séquentiel de segment et unicité absolue de finalisation (zéro duplication).
 * - Gestion du timeout de fin de parole sans fabrication de faux texte (zéro hallucination).
 * - Reprise automatique fluide après temporisation (400 ms par défaut).
 * - Pause et reprise manuelles sans réinitialisation de la session réseau.
 * - Zéro capture concurrente du microphone : partage strict du flux audio avec VoxCoreVoice.
 * - Contexte d'exécution documenté et unifié : tous les callbacks d'événements s'exécutent dans la tâche principale.
 */
class ContinuousListeningController {
public:
    using SpeechStartedCallback = std::function<void()>;
    using SpeechEndedCallback = std::function<void(VadEndReason)>;
    using SegmentCallback = std::function<void(const String& text, bool confirmed)>;
    using StateCallback = std::function<void(ContinuousState state)>;

    ContinuousListeningController();
    ~ContinuousListeningController();

    // Empêcher la copie accidentelle en raison des membres std::atomic
    ContinuousListeningController(const ContinuousListeningController&) = delete;
    ContinuousListeningController& operator=(const ContinuousListeningController&) = delete;

    /**
     * @brief Associe le contrôleur à l'instance VoxCoreVoice sous-jacente.
     * @param voice Pointeur vers l'instance VoxCoreVoice.
     */
    void begin(VoxCoreVoice* voice);

    /**
     * @brief Démarre la reconnaissance vocale continue.
     * @return true si la demande est acceptée.
     */
    bool start();

    /**
     * @brief Arrête la reconnaissance continue.
     * @return true si arrêté avec succès.
     */
    bool stop();

    /**
     * @brief Suspend manuellement la dictée continue sans fermer la session.
     */
    void pause();

    /**
     * @brief Reprend la dictée continue après une pause.
     * @return true si la reprise a démarré.
     */
    bool resume();

    /**
     * @brief Traitement périodique obligatoire à appeler dans loop() ou VoiceKit::update().
     *        Tous les traitements VAD et callbacks s'exécutent dans ce contexte unifié.
     */
    void update();

    /**
     * @brief Traite un bloc audio PCM 16 kHz (appelé depuis le callback audio FreeRTOS Core 1).
     *        Strictement non bloquant, sans mutex, sans allocation ni manipulation de String.
     * @param samples Pointeur vers les échantillons PCM.
     * @param count Nombre d'échantillons.
     * @param rms Valeur RMS calculée du bloc.
     */
    void processAudioChunk(const int16_t* samples, size_t count, uint32_t rms);

    /**
     * @brief Traite la réception d'une transcription Gemini Live.
     * @param text Texte transcrit.
     * @param isFinal Indique s'il s'agit du résultat définitif authentique.
     */
    void handleTranscription(const String& text, bool isFinal);

    // États & Requêtes (Thread-safe)
    ContinuousState getState() const { return _state.load(std::memory_order_relaxed); }
    bool isActive() const { return _isActive.load(std::memory_order_relaxed); }
    bool isPaused() const { return _isPaused.load(std::memory_order_relaxed); }
    bool isSpeechActive() const;
    bool isBusy() const;

    uint32_t getSegmentCount() const { return _segmentCount; }
    uint32_t getCurrentSegmentId() const { return _currentSegmentId; }
    const String& getLastFinalText() const { return _lastFinalText; }
    bool wasLastFinalConfirmed() const { return _lastFinalConfirmed; }
    uint32_t getLastLevelRms() const { return _lastChunkRms.load(std::memory_order_relaxed); }
    uint32_t getNoiseFloor() const { return _vad.getNoiseFloor(); }
    uint32_t getCurrentSilenceMs() const { return _vad.getCurrentSilenceMs(); }
    uint32_t getSegmentDurationMs() const { return _vad.getSegmentDurationMs(); }
    float getSilenceProgressRatio() const { return _vad.getSilenceProgressRatio(); }
    VadEndReason getLastEndReason() const { return _vad.getEndReason(); }
    uint32_t getSpeechEndDetectedMs() const { return _speechEndDetectedMs; }

    // Configuration VAD
    VoiceActivityDetector& getVad() { return _vad; }
    const VoiceActivityDetector& getVad() const { return _vad; }
    const VadConfig& getVadConfig() const { return _vad.getConfig(); }
    void setVadConfig(const VadConfig& config) { _vad.setConfig(config); }
    void setSilenceDurationMs(uint32_t ms) { _vad.setSilenceDurationMs(ms); }
    void setMinSpeechDurationMs(uint32_t ms) { _vad.setMinSpeechDurationMs(ms); }
    void setMaxSegmentDurationMs(uint32_t ms) { _vad.setMaxSegmentDurationMs(ms); }
    void setAutoResumeDelayMs(uint32_t delayMs) { _autoResumeDelayMs = delayMs; }
    uint32_t getAutoResumeDelayMs() const { return _autoResumeDelayMs; }

    // Callbacks — Contexte garanti : Tâche principale (loop / update)
    void onSpeechStarted(SpeechStartedCallback cb) { _onSpeechStarted = std::move(cb); }
    void onSpeechEnded(SpeechEndedCallback cb) { _onSpeechEnded = std::move(cb); }
    void onSegmentComplete(SegmentCallback cb) { _onSegmentComplete = std::move(cb); }
    void onStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }

private:
    void setState(ContinuousState newState);
    void clearRmsQueue();
    bool pushRms(uint32_t rms);
    bool popRms(uint32_t& rms);

    VoxCoreVoice* _voice = nullptr;
    VoiceActivityDetector _vad;
    std::atomic<ContinuousState> _state{ContinuousState::INACTIVE};

    std::atomic<bool> _isActive{false};
    std::atomic<bool> _isPaused{false};
    std::atomic<bool> _speechEndRequested{false};
    std::atomic<uint32_t> _lastChunkRms{0};

    // File SPSC (Single-Producer Single-Consumer) bornée et lock-free pour transfert audio -> update (128 blocs = 4,096 s)
    static constexpr size_t RMS_QUEUE_CAPACITY = 128;
    std::atomic<uint32_t> _rmsQueue[RMS_QUEUE_CAPACITY];
    std::atomic<size_t> _rmsHead{0};
    std::atomic<size_t> _rmsTail{0};

    uint32_t _currentSegmentId = 0;
    bool _segmentFinalized = false;
    uint32_t _segmentCount = 0;
    String _lastFinalText = "";
    bool _lastFinalConfirmed = true;

    uint32_t _stopListeningTimeMs = 0;
    uint32_t _speechEndDetectedMs = 0;
    uint32_t _lastCompletedTimeMs = 0;
    uint32_t _autoResumeDelayMs = 400;

    SpeechStartedCallback _onSpeechStarted;
    SpeechEndedCallback _onSpeechEnded;
    SegmentCallback _onSegmentComplete;
    StateCallback _onStateChanged;
};
