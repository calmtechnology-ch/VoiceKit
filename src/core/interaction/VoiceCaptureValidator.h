#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @file VoiceCaptureValidator.h
 * @brief Validateur d'activité vocale (VAD) léger et non bloquant pour VoiceKit v0.6.7.0.
 *
 * Extrait et standardisé depuis VoiceKit-SmartUI v0.1.0.6 (voice_validator.h).
 * Évalue la présence d'activité vocale exploitable avant toute transmission vers le fournisseur IA :
 * - Suivi adaptatif du bruit ambiant par lissage exponentiel (EMA).
 * - Calcul des métriques sur les blocs audio PCM 16 kHz (durée nominale 32 ms).
 * - Mesure de la durée cumulée de parole (au-dessus du seuil dynamique).
 * - Mesure du segment continu de parole le plus long pour immuniser contre les bruits brefs (clics, chocs).
 * - Rejet catégorique des captures silencieuses ou transitoires sans appel à stopListening().
 * - Configuration personnalisable des seuils numériques sans imposer les valeurs du CoreS3.
 */

struct VoiceCaptureMetrics {
    bool isValid = false;
    const char* rejectReason = "OK";     ///< "OK", "SILENCE", "BRIEF_NOISE", "TOO_SHORT", "LOW_ENERGY"
    uint32_t captureDurationMs = 0;      ///< Durée totale de la capture (ms)
    uint32_t cumulativeSpeechMs = 0;     ///< Durée cumulée d'activité vocale détectée (ms)
    uint32_t maxConsecutiveSpeechMs = 0; ///< Plus longue séquence continue au-dessus du seuil (ms)
    uint32_t peakRms = 0;                ///< Niveau RMS crête observé
    uint32_t averageRms = 0;             ///< Niveau RMS moyen sur l'ensemble de la capture
    uint32_t noiseFloor = 0;             ///< Estimation du plancher de bruit ambiant (RMS)
    uint32_t speechThreshold = 0;        ///< Seuil dynamique de détection de parole (RMS)
    uint32_t speechChunksCount = 0;      ///< Nombre de blocs classés voix
    uint32_t totalChunksCount = 0;       ///< Nombre total de blocs reçus
};

// Alias de rétrocompatibilité avec SmartUI
using VadMetrics = VoiceCaptureMetrics;

struct VoiceCaptureConfig {
    uint32_t minCaptureDurationMs = 350;     ///< Durée minimale de capture (ms) [défaut CoreS3: 350]
    uint32_t minSpeechDurationMs = 250;      ///< Durée cumulée minimale de parole (ms) [défaut: 250]
    uint32_t minConsecutiveSpeechMs = 120;   ///< Segment continu minimal (ms) [défaut: 120]
    uint32_t speechThresholdFloorRms = 380;  ///< Seuil RMS minimal plancher [défaut ES7210: 380]
    float noiseMultiplier = 1.8f;            ///< Multiplicateur du bruit ambiant [défaut: 1.8]
    uint32_t noiseOffsetRms = 150;           ///< Décalage RMS [défaut: +150]
    uint32_t chunkDurationMs = 32;           ///< Durée nominale d'un bloc (ms) [défaut: 32]
    float initialNoiseFloor = 180.0f;        ///< Plancher de bruit initial [défaut: 180]
    float minNoiseFloor = 50.0f;             ///< Borne basse plancher de bruit [défaut: 50]
    float maxNoiseFloor = 3000.0f;           ///< Borne haute plancher de bruit [défaut: 3000]
    float noiseAlpha = 0.05f;                ///< Coefficient EMA pour lissage du bruit [défaut: 0.05]
    uint32_t lowEnergyMarginRms = 30;        ///< Marge RMS minimale au-dessus du bruit [défaut: 30]
    uint32_t minChunksCount = 10;            ///< Nombre minimal de blocs reçus [défaut: 10]
};

class VoiceCaptureValidator {
public:
    explicit VoiceCaptureValidator(const VoiceCaptureConfig& config = VoiceCaptureConfig());
    VoiceCaptureValidator(uint32_t minSpeechMs,
                          uint32_t minConsecutiveMs,
                          uint32_t minCaptureMs,
                          float initialNoiseFloor = 180.0f);

    void reset();
    void start(uint32_t nowMs = 0);
    void stop();

    void feedChunk(uint32_t rms);
    void updateIdleRms(uint32_t rms);

    uint32_t getSpeechThreshold() const;
    VoiceCaptureMetrics evaluate(uint32_t nowMs = 0);

    const VoiceCaptureMetrics& getLastMetrics() const { return _lastMetrics; }
    bool isActive() const { return _active; }
    uint32_t getNoiseFloor() const { return static_cast<uint32_t>(_noiseFloor); }
    uint32_t getPeakRms() const { return _peakRms; }

    const VoiceCaptureConfig& getConfig() const { return _config; }
    void setConfig(const VoiceCaptureConfig& config);

    void setMinSpeechDurationMs(uint32_t ms) { _config.minSpeechDurationMs = ms; }
    void setMinConsecutiveSpeechMs(uint32_t ms) { _config.minConsecutiveSpeechMs = ms; }
    void setMinCaptureDurationMs(uint32_t ms) { _config.minCaptureDurationMs = ms; }
    void setSpeechThresholdFloorRms(uint32_t rms) { _config.speechThresholdFloorRms = rms; }
    void setNoiseFloor(float floor) { _noiseFloor = floor; }

private:
    VoiceCaptureConfig _config;
    VoiceCaptureMetrics _lastMetrics;

    float _noiseFloor = 180.0f;
    uint32_t _peakRms = 0;
    uint64_t _sumRms = 0;
    uint32_t _totalChunks = 0;
    uint32_t _speechChunks = 0;
    uint32_t _consecutiveSpeechChunks = 0;
    uint32_t _maxConsecutiveSpeechChunks = 0;
    uint32_t _startTimeMs = 0;
    bool _active = false;
};

// Alias de rétrocompatibilité avec SmartUI
using VoiceActivityValidator = VoiceCaptureValidator;
