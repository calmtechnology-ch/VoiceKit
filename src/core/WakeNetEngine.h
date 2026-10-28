#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include "IWakeWordEngine.h"

#if defined(VOICEKIT_ENABLE_WAKENET) && __has_include(<esp_wn_iface.h>) && __has_include(<esp_wn_models.h>) && __has_include(<model_path.h>)
#define VOICEKIT_HAS_ESP_SR_WAKENET 1
#include <esp_wn_iface.h>
#include <esp_wn_models.h>
#include <model_path.h>
#else
#define VOICEKIT_HAS_ESP_SR_WAKENET 0
#endif

/**
 * @class WakeNetEngine
 * @brief Adaptateur IWakeWordEngine pour Espressif ESP-SR WakeNet9 / WakeNet10
 *        ciblant le modèle français "wn10_fr_bonjouresp" (« Bonjour ESP »).
 *
 * Particularités techniques ESP-SR WakeNet10 :
 * - Cadence native WakeNet : trames de 30 ms à 16 kHz mono 16 bits signé = 480 échantillons.
 * - Capture DMA VoiceKit : blocs de 32 ms à 16 kHz mono 16 bits signé = 512 échantillons.
 * - Ce moteur intègre un adaptateur de cadrage sans perte (512 -> 480 échantillons) garantissant
 *   la continuité exacte des échantillons (ex: 15 blocs de 512 = 7680 éch = exactement 16 trames de 480).
 *
 * Compatibilité de compilation :
 * - Dans l'environnement actuel de VoiceKit (PlatformIO espressif32@6.9.0 / Arduino-ESP32 2.0.17 /
 *   ESP-IDF v4.4.7), les bibliothèques précompilées ESP-SR v2.x (WakeNet10, esp-dl, AFE) et la
 *   partition Flash srmodels.bin ne sont pas incluses (elles exigent ESP-IDF >= 5.0 et CMake idf_component).
 * - Lorsque VOICEKIT_HAS_ESP_SR_WAKENET vaut 0, isAvailable() retourne false et begin() échoue
 *   explicitement sans jamais simuler ni inventer de détection.
 */
class WakeNetEngine : public IWakeWordEngine {
public:
    static constexpr size_t WAKENET_FRAME_SAMPLES = 480; // 30 ms @ 16 000 Hz
    static constexpr const char* DEFAULT_MODEL_NAME = "wn10_fr_bonjouresp";

    using Frame480Callback = std::function<bool(const int16_t* frame480, size_t count)>;

    WakeNetEngine() {
        _modelName = DEFAULT_MODEL_NAME;
        _targetWakeWord = "Bonjour ESP";
        memset(_accumulator, 0, sizeof(_accumulator));
#if !VOICEKIT_HAS_ESP_SR_WAKENET
        strncpy(_lastError,
                "ESP-SR WakeNet10 (wn10_fr_bonjouresp) non disponible dans cet environnement Arduino-ESP32 (IDF 4.4.7)",
                sizeof(_lastError) - 1);
#endif
    }

    ~WakeNetEngine() override {
        end();
    }

    const char* getName() const override { return "WAKENET"; }
    WakeWordEngineType getType() const override { return WakeWordEngineType::WAKENET; }
    const char* getModelName() const override { return _modelName.c_str(); }

    void setModelName(const String& modelName) {
        if (!modelName.isEmpty()) {
            _modelName = modelName;
        }
    }

    bool isAvailable() const override {
#if VOICEKIT_HAS_ESP_SR_WAKENET
        return true;
#else
        return false;
#endif
    }

    bool isSimulatedEngine() const override {
        return false; // Moteur acoustique réel (jamais simulé)
    }

    size_t getPreferredFrameSamples() const override {
        return WAKENET_FRAME_SAMPLES; // 480 échantillons (30 ms @ 16 kHz)
    }

    bool begin(uint32_t sampleRateHz = 16000, uint8_t channels = 1) override {
        reset();
        if (sampleRateHz != 16000 || channels != 1) {
            strncpy(_lastError, "WakeNet10 requiert PCM 16000 Hz mono (1 canal)", sizeof(_lastError) - 1);
            _initialized = false;
            return false;
        }

#if VOICEKIT_HAS_ESP_SR_WAKENET
        _models = esp_srmodel_init("model");
        if (!_models) {
            strncpy(_lastError, "Partition Flash 'model' (srmodels.bin) introuvable pour ESP-SR", sizeof(_lastError) - 1);
            _initialized = false;
            return false;
        }
        char* modelNamePtr = esp_srmodel_filter(_models, ESP_WN_PREFIX, (char*)_modelName.c_str());
        if (!modelNamePtr) {
            snprintf(_lastError, sizeof(_lastError), "Modele WakeNet '%s' absent de srmodels.bin", _modelName.c_str());
            _initialized = false;
            return false;
        }
        _wakenet = (esp_wn_iface_t*)esp_wn_handle_from_name(modelNamePtr);
        if (!_wakenet) {
            snprintf(_lastError, sizeof(_lastError), "Echec esp_wn_handle_from_name('%s')", modelNamePtr);
            _initialized = false;
            return false;
        }
        _modelData = _wakenet->create(modelNamePtr, DET_MODE_90);
        if (!_modelData) {
            snprintf(_lastError, sizeof(_lastError), "Echec creation instance WakeNet ('%s')", modelNamePtr);
            _initialized = false;
            return false;
        }
        _lastError[0] = '\0';
        _initialized = true;
        return true;
#else
        strncpy(_lastError,
                "ESP-SR WakeNet10 (wn10_fr_bonjouresp) non disponible dans cet environnement Arduino-ESP32 (IDF 4.4.7)",
                sizeof(_lastError) - 1);
        _initialized = false;
        return false;
#endif
    }

    void end() override {
#if VOICEKIT_HAS_ESP_SR_WAKENET
        if (_wakenet && _modelData) {
            _wakenet->destroy(_modelData);
            _modelData = nullptr;
        }
        _wakenet = nullptr;
        if (_models) {
            esp_srmodel_deinit(_models);
            _models = nullptr;
        }
#endif
        _initialized = false;
        reset();
    }

    bool isInitialized() const override {
        return _initialized;
    }

    void reset() override {
        _accumCount = 0;
        _frames480Produced = 0;
    }

    /**
     * @brief Alimente l'adaptateur de cadrage 512 -> 480 échantillons (32 ms -> 30 ms).
     *        Permet de découper tout flux PCM 16 kHz (ex: blocs DMA de 512 échantillons)
     *        en fenêtres exactes de 480 échantillons (30 ms) attendues par WakeNet.
     * @param samples Échantillons PCM entrants.
     * @param sampleCount Nombre d'échantillons entrants.
     * @param onFrame480 Callback invoqué sur chaque trame complète de 480 échantillons.
     * @return Nombre de trames de 480 échantillons produites lors de cet appel.
     */
    size_t feedFrameAdapter(const int16_t* samples, size_t sampleCount, const Frame480Callback& onFrame480) {
        if (!samples || sampleCount == 0) return 0;

        size_t offset = 0;
        size_t produced = 0;
        while (offset < sampleCount) {
            size_t needed = WAKENET_FRAME_SAMPLES - _accumCount;
            size_t available = sampleCount - offset;
            size_t toCopy = (available < needed) ? available : needed;

            memcpy(&_accumulator[_accumCount], &samples[offset], toCopy * sizeof(int16_t));
            _accumCount += toCopy;
            offset += toCopy;

            if (_accumCount == WAKENET_FRAME_SAMPLES) {
                _frames480Produced++;
                produced++;
                _accumCount = 0;
                if (onFrame480) {
                    if (onFrame480(_accumulator, WAKENET_FRAME_SAMPLES)) {
                        break;
                    }
                }
            }
        }
        return produced;
    }

    WakeWordResult process(const int16_t* samples, size_t sampleCount, uint32_t rms) override {
        (void)rms;
        const uint32_t startUs = micros();
        WakeWordResult res;
        res.isSimulated = false;

        if (!_initialized) {
            return res;
        }

        feedFrameAdapter(samples, sampleCount, [&](const int16_t* frame480, size_t count) -> bool {
            (void)count;
#if VOICEKIT_HAS_ESP_SR_WAKENET
            if (_wakenet && _modelData) {
                wakenet_state_t state = _wakenet->detect(_modelData, (int16_t*)frame480);
                if (state == WAKENET_DETECTED) {
                    res.detected = true;
                    res.word = _targetWakeWord;
                    res.confidence = 0.95f;
                    res.isSimulated = false;
                    return true; // Arrêter le découpage sur détection
                }
            }
#else
            (void)frame480;
#endif
            return false;
        });

        res.processingTimeUs = micros() - startUs;
        return res;
    }

    void setTargetWakeWord(const String& word) override {
        if (!word.isEmpty()) {
            _targetWakeWord = word;
        }
    }

    String getTargetWakeWord() const override {
        return _targetWakeWord;
    }

    const char* getLastError() const override {
        return _lastError;
    }

    size_t getAccumulatorCount() const { return _accumCount; }
    uint32_t getFrames480Produced() const { return _frames480Produced; }

private:
    String _modelName;
    String _targetWakeWord;
    bool _initialized = false;
    char _lastError[160] = {};

    // Adaptateur de cadrage 512 échantillons (32 ms) -> 480 échantillons (30 ms @ 16 kHz)
    int16_t _accumulator[WAKENET_FRAME_SAMPLES];
    size_t _accumCount = 0;
    uint32_t _frames480Produced = 0;

#if VOICEKIT_HAS_ESP_SR_WAKENET
    srmodel_list_t* _models = nullptr;
    esp_wn_iface_t* _wakenet = nullptr;
    model_iface_data_t* _modelData = nullptr;
#endif
};

