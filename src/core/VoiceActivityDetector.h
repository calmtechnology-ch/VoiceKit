#pragma once

#include <stdint.h>
#include <stddef.h>
#include <cmath>
#include <functional>

/**
 * @file VoiceActivityDetector.h
 * @brief Détecteur d'activité vocale (VAD) léger et non bloquant pour flux PCM 16 kHz.
 * 
 * Fonctionnalités :
 * - Analyse bloc par bloc (chunks PCM 16 kHz, typiquement 32 ms = 512 échantillons).
 * - Seuil adaptatif au bruit ambiant via lissage exponentiel (EMA).
 * - Hystérésis entre seuil d'attaque (détection de parole) et seuil de relâchement (maintien).
 * - Validation de durée minimale de parole (200 ms) pour rejeter les bruits transitoires (clics).
 * - Détection de fin de parole après une durée de silence consécutive configurable (1 200 ms).
 * - Durée maximale de segment bornée (30 secondes) pour éviter les flux infinis.
 * - Non-bloquant, sans allocation dynamique pendant le streaming, adapté aux tâches FreeRTOS et loop().
 */

enum class VadState {
    SILENCE,            ///< Bruit de fond ambiant, écoute passive, en attente de parole
    SPEECH_STARTING,    ///< Dépassement du seuil haut en cours de validation (durée min)
    SPEECH_ACTIVE,      ///< Parole confirmée et active
    SILENCE_STARTING,   ///< En cours de parole, niveau tombé sous seuil bas (compte à rebours silence)
    SPEECH_ENDED        ///< Fin de segment confirmée (silence atteint ou durée max atteinte)
};

enum class VadEndReason {
    NONE,
    SILENCE_TIMEOUT,    ///< Durée de silence consécutive atteinte (1 200 ms)
    MAX_DURATION        ///< Durée maximale de segment atteinte (30 s)
};

struct VadConfig {
    uint32_t minSpeechDurationMs = 200;      ///< Durée minimale de voix pour confirmer le début (ms)
    uint32_t silenceDurationMs = 1200;       ///< Durée de silence requise pour clore le segment (ms)
    uint32_t maxSegmentDurationMs = 30000;   ///< Durée maximale absolue d'un segment de parole (ms)
    
    // Suivi adaptatif du bruit de fond
    float noiseFloorAlpha = 0.05f;           ///< Facteur de lissage EMA du bruit en phase de silence
    uint32_t initialNoiseFloor = 200;        ///< Valeur RMS initiale du plancher de bruit
    uint32_t minNoiseFloor = 50;             ///< Plafond bas du bruit de fond
    uint32_t maxNoiseFloor = 3500;           ///< Plafond haut du bruit de fond
    
    // Hystérésis & Seuils dynamiques
    // Seuil parole  = (noiseFloor * speechSensitivityMultiplier) + speechOffset
    // Seuil silence = (noiseFloor * silenceSensitivityMultiplier) + silenceOffset
    float speechSensitivityMultiplier = 2.0f;
    uint32_t speechOffset = 250;
    
    float silenceSensitivityMultiplier = 1.3f;
    uint32_t silenceOffset = 120;
};

class VoiceActivityDetector {
public:
    using SpeechStartedCallback = std::function<void()>;
    using SpeechEndedCallback = std::function<void(VadEndReason)>;

    explicit VoiceActivityDetector(const VadConfig& config = VadConfig())
        : _config(config),
          _noiseFloor((float)config.initialNoiseFloor) {
        reset();
    }

    /**
     * @brief Réinitialise l'état interne pour un nouveau segment de dictée.
     *        Conserve l'estimation courante du bruit de fond.
     */
    void reset() {
        _state = VadState::SILENCE;
        _endReason = VadEndReason::NONE;
        _consecutiveSpeechMs = 0;
        _currentSilenceMs = 0;
        _segmentDurationMs = 0;
        _speechConfirmed = false;
        _lastRms = 0;
    }

    /**
     * @brief Réinitialise totalement le détecteur, y compris le plancher de bruit.
     */
    void fullReset() {
        _noiseFloor = (float)_config.initialNoiseFloor;
        reset();
    }

    /**
     * @brief Traite un bloc d'échantillons PCM 16 bits.
     * @param samples Pointeur vers les échantillons PCM mono.
     * @param count Nombre d'échantillons dans le bloc.
     * @param sampleRateHz Fréquence d'échantillonnage (défaut 16 000 Hz).
     * @return true si le segment de parole vient de se terminer ou est terminé.
     */
    bool processSamples(const int16_t* samples, size_t count, uint32_t sampleRateHz = 16000) {
        if (!samples || count == 0) return isSpeechEnded();
        uint64_t sumSq = 0;
        for (size_t i = 0; i < count; ++i) {
            int32_t s = samples[i];
            sumSq += (uint64_t)(s * s);
        }
        uint32_t rms = static_cast<uint32_t>(sqrt((double)sumSq / count));
        uint32_t durationMs = (count * 1000UL) / sampleRateHz;
        if (durationMs == 0) durationMs = 1;
        return processRms(rms, durationMs);
    }

    /**
     * @brief Traite une valeur RMS précalculée pour un bloc audio.
     * @param rms Valeur RMS du bloc.
     * @param chunkDurationMs Durée temporelle du bloc en millisecondes (ex: 32 ms pour 512 éch. à 16 kHz).
     * @return true si le segment de parole vient de se terminer ou est terminé.
     */
    bool processRms(uint32_t rms, uint32_t chunkDurationMs = 32) {
        _lastRms = rms;

        uint32_t speechThresh = getSpeechThreshold();
        uint32_t silenceThresh = getSilenceThreshold();

        switch (_state) {
            case VadState::SILENCE: {
                if (rms >= speechThresh) {
                    _state = VadState::SPEECH_STARTING;
                    _consecutiveSpeechMs = chunkDurationMs;
                } else {
                    // Adaptation douce du niveau de bruit de fond pendant le silence
                    _noiseFloor = _noiseFloor * (1.0f - _config.noiseFloorAlpha) + ((float)rms) * _config.noiseFloorAlpha;
                    if (_noiseFloor < (float)_config.minNoiseFloor) _noiseFloor = (float)_config.minNoiseFloor;
                    if (_noiseFloor > (float)_config.maxNoiseFloor) _noiseFloor = (float)_config.maxNoiseFloor;
                }
                break;
            }

            case VadState::SPEECH_STARTING: {
                if (rms >= speechThresh) {
                    _consecutiveSpeechMs += chunkDurationMs;
                    if (_consecutiveSpeechMs >= _config.minSpeechDurationMs) {
                        // Début de parole validé !
                        _state = VadState::SPEECH_ACTIVE;
                        _speechConfirmed = true;
                        _segmentDurationMs = _consecutiveSpeechMs;
                        _currentSilenceMs = 0;
                        if (_onSpeechStarted) {
                            _onSpeechStarted();
                        }
                    }
                } else {
                    // Rejet du bruit transitoire (durée insuffisante < minSpeechDurationMs)
                    _state = VadState::SILENCE;
                    _consecutiveSpeechMs = 0;
                }
                break;
            }

            case VadState::SPEECH_ACTIVE: {
                _segmentDurationMs += chunkDurationMs;

                // Contrôle de la durée maximale absolue
                if (_segmentDurationMs >= _config.maxSegmentDurationMs) {
                    _state = VadState::SPEECH_ENDED;
                    _endReason = VadEndReason::MAX_DURATION;
                    if (_onSpeechEnded) {
                        _onSpeechEnded(VadEndReason::MAX_DURATION);
                    }
                    return true;
                }

                if (rms < silenceThresh) {
                    // Relâchement sous le seuil bas : amorce du silence
                    _state = VadState::SILENCE_STARTING;
                    _currentSilenceMs = chunkDurationMs;
                } else {
                    _currentSilenceMs = 0;
                }
                break;
            }

            case VadState::SILENCE_STARTING: {
                _segmentDurationMs += chunkDurationMs;

                // Contrôle de la durée maximale absolue
                if (_segmentDurationMs >= _config.maxSegmentDurationMs) {
                    _state = VadState::SPEECH_ENDED;
                    _endReason = VadEndReason::MAX_DURATION;
                    if (_onSpeechEnded) {
                        _onSpeechEnded(VadEndReason::MAX_DURATION);
                    }
                    return true;
                }

                if (rms >= speechThresh) {
                    // L'utilisateur reprend la parole avant expiration du silence (pause inter-mots)
                    _state = VadState::SPEECH_ACTIVE;
                    _currentSilenceMs = 0;
                } else {
                    _currentSilenceMs += chunkDurationMs;
                    if (_currentSilenceMs >= _config.silenceDurationMs) {
                        // Fin de parole validée après 1 200 ms de silence !
                        _state = VadState::SPEECH_ENDED;
                        _endReason = VadEndReason::SILENCE_TIMEOUT;
                        if (_onSpeechEnded) {
                            _onSpeechEnded(VadEndReason::SILENCE_TIMEOUT);
                        }
                        return true;
                    }
                }
                break;
            }

            case VadState::SPEECH_ENDED: {
                return true;
            }
        }

        return (_state == VadState::SPEECH_ENDED);
    }

    // Accesseurs d'état
    VadState getState() const { return _state; }
    VadEndReason getEndReason() const { return _endReason; }

    const char* getStateStr() const {
        switch (_state) {
            case VadState::SILENCE:          return "SILENCE";
            case VadState::SPEECH_STARTING:   return "AMORCE";
            case VadState::SPEECH_ACTIVE:     return "PAROLE";
            case VadState::SILENCE_STARTING:  return "PAUSE";
            case VadState::SPEECH_ENDED:      return "TERMINE";
            default:                         return "INCONNU";
        }
    }

    const char* getEndReasonStr() const {
        switch (_endReason) {
            case VadEndReason::SILENCE_TIMEOUT: return "SILENCE_1200MS";
            case VadEndReason::MAX_DURATION:    return "DUREE_MAX_30S";
            default:                            return "AUCUN";
        }
    }

    bool isSpeechConfirmed() const { return _speechConfirmed; }
    bool isSpeechActive() const { return (_state == VadState::SPEECH_ACTIVE || _state == VadState::SILENCE_STARTING); }
    bool isSpeechEnded() const { return (_state == VadState::SPEECH_ENDED); }
    bool isSilenceCounting() const { return (_state == VadState::SILENCE_STARTING); }

    uint32_t getNoiseFloor() const { return (uint32_t)_noiseFloor; }
    uint32_t getLastRms() const { return _lastRms; }
    uint32_t getConsecutiveSpeechMs() const { return _consecutiveSpeechMs; }
    uint32_t getCurrentSilenceMs() const { return _currentSilenceMs; }
    uint32_t getSegmentDurationMs() const { return _segmentDurationMs; }

    uint32_t getSpeechThreshold() const {
        return (uint32_t)(_noiseFloor * _config.speechSensitivityMultiplier) + _config.speechOffset;
    }

    uint32_t getSilenceThreshold() const {
        return (uint32_t)(_noiseFloor * _config.silenceSensitivityMultiplier) + _config.silenceOffset;
    }

    /**
     * @brief Retourne la progression relative du silence avant clôture [0.0 - 1.0].
     */
    float getSilenceProgressRatio() const {
        if (_config.silenceDurationMs == 0) return 0.0f;
        if (_state != VadState::SILENCE_STARTING && _state != VadState::SPEECH_ENDED) return 0.0f;
        float r = (float)_currentSilenceMs / (float)_config.silenceDurationMs;
        return (r > 1.0f) ? 1.0f : r;
    }

    // Configuration dynamique
    const VadConfig& getConfig() const { return _config; }
    void setConfig(const VadConfig& config) { _config = config; }
    void setMinSpeechDurationMs(uint32_t ms) { _config.minSpeechDurationMs = ms; }
    void setSilenceDurationMs(uint32_t ms) {
        if (ms > 0) {
            _config.silenceDurationMs = ms;
        }
    }
    void setMaxSegmentDurationMs(uint32_t ms) { _config.maxSegmentDurationMs = ms; }
    void setNoiseFloor(uint32_t floorRms) { _noiseFloor = (float)floorRms; }

    void setThresholds(float speechMul, uint32_t speechOff, float silenceMul, uint32_t silenceOff) {
        _config.speechSensitivityMultiplier = speechMul;
        _config.speechOffset = speechOff;
        _config.silenceSensitivityMultiplier = silenceMul;
        _config.silenceOffset = silenceOff;
    }

    // Callbacks optionnels
    void onSpeechStarted(SpeechStartedCallback cb) { _onSpeechStarted = std::move(cb); }
    void onSpeechEnded(SpeechEndedCallback cb) { _onSpeechEnded = std::move(cb); }

private:
    VadConfig _config;
    VadState _state = VadState::SILENCE;
    VadEndReason _endReason = VadEndReason::NONE;

    float _noiseFloor = 200.0f;
    uint32_t _lastRms = 0;
    uint32_t _consecutiveSpeechMs = 0;
    uint32_t _currentSilenceMs = 0;
    uint32_t _segmentDurationMs = 0;
    bool _speechConfirmed = false;

    SpeechStartedCallback _onSpeechStarted;
    SpeechEndedCallback _onSpeechEnded;
};
