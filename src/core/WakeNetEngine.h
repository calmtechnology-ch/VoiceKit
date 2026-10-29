#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <vector>
#include "IWakeWordEngine.h"

#if defined(ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(VOICEKIT_ENABLE_WAKENET)
#define VOICEKIT_ENABLE_WAKENET 1
#endif

#if defined(VOICEKIT_ENABLE_WAKENET) && VOICEKIT_ENABLE_WAKENET && __has_include("../esp_sr/include/esp_wn_iface.h")
#define VOICEKIT_HAS_ESP_SR_WAKENET 1
#include "../esp_sr/include/esp_wn_iface.h"
#include "../esp_sr/include/esp_wn_models.h"
#include "../esp_sr/include/model_path.h"
#elif defined(VOICEKIT_ENABLE_WAKENET) && VOICEKIT_ENABLE_WAKENET && __has_include(<esp_wn_iface.h>) && __has_include(<esp_wn_models.h>) && __has_include(<model_path.h>)
#define VOICEKIT_HAS_ESP_SR_WAKENET 1
#include <esp_wn_iface.h>
#include <esp_wn_models.h>
#include <model_path.h>
#else
#define VOICEKIT_HAS_ESP_SR_WAKENET 0
#endif

#if VOICEKIT_HAS_ESP_SR_WAKENET
extern "C" void voicekit_wakenet_clean(const esp_wn_iface_t *wakenet, model_iface_data_t *model_data);
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
        _resolvedModelName = "";
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
    const char* getResolvedModelName() const {
        return _resolvedModelName.isEmpty() ? _modelName.c_str() : _resolvedModelName.c_str();
    }

    void setModelName(const String& modelName) override {
        String clean = modelName;
        clean.trim();
        if (clean.isEmpty()) {
            clean = DEFAULT_MODEL_NAME;
        }
        if (_modelName != clean) {
            if (_initialized) {
                end();
            }
            _modelName = clean;
            _resolvedModelName = "";
            _modelProbed = false;
            _modelPresent = false;
        }
    }

    /**
     * @brief Vérifie si la phrase configurée correspond acoustiquement au modèle WakeNet sélectionné (Section 3 — v0.6.3.1).
     *        Une simple modification de wakeWordPhrase n'entraîne jamais l'apprentissage d'une nouvelle phrase acoustique.
     */
    bool isPhraseConsistentWithModel() const {
        if (_modelName == DEFAULT_MODEL_NAME || _modelName == "bonjouresp" || _modelName == "wn9l_fr_bonjouresp_tts3") {
            String p = _targetWakeWord;
            p.trim();
            return p.equalsIgnoreCase("Bonjour ESP");
        }
        return true;
    }

    /**
     * @brief 1. Support compilé : indique si les en-têtes et l'intégration ESP-SR WakeNet sont compilés.
     *        La simple présence des en-têtes ne suffit pas à considérer le moteur comme opérationnel.
     */
    bool isCompiledSupportAvailable() const override {
#if VOICEKIT_HAS_ESP_SR_WAKENET
        return true;
#elif !defined(ESP32) && !defined(ARDUINO)
        return _simCompiledSupport;
#else
        return false;
#endif
    }

    /**
     * @brief 2. Modèle disponible : vérifie que la partition Flash 'model' (srmodels.bin)
     *        et le modèle cible (ex: wn10_fr_bonjouresp) sont effectivement présents.
     */
    bool isModelAvailable() const override {
        if (!isCompiledSupportAvailable()) {
            return false;
        }
        if (_initialized) {
            return true;
        }
#if VOICEKIT_HAS_ESP_SR_WAKENET
        if (_modelProbed) {
            return _modelPresent;
        }
        srmodel_list_t* probeModels = esp_srmodel_init("model");
        _modelProbed = true;
        if (!probeModels) {
            _modelPresent = false;
            strncpy(_lastError, "Partition Flash 'model' (srmodels.bin) introuvable pour ESP-SR", sizeof(_lastError) - 1);
            return false;
        }
        char* modelNamePtr = esp_srmodel_filter(probeModels, ESP_WN_PREFIX, (char*)_modelName.c_str());
        if (!modelNamePtr && _modelName == DEFAULT_MODEL_NAME) {
            // Repli autorisé uniquement pour l'alias officiel par défaut wn10_fr_bonjouresp -> wn9l_fr_bonjouresp_tts3
            modelNamePtr = esp_srmodel_filter(probeModels, ESP_WN_PREFIX, (char*)"bonjouresp");
        }
        _modelPresent = (modelNamePtr != nullptr);
        if (_modelPresent) {
            _resolvedModelName = modelNamePtr;
        } else {
            _resolvedModelName = "";
            snprintf(_lastError, sizeof(_lastError), "Modele WakeNet '%s' absent de srmodels.bin", _modelName.c_str());
        }
        esp_srmodel_deinit(probeModels);
        return _modelPresent;
#elif !defined(ESP32) && !defined(ARDUINO)
        if (!_simPartitionPresent) {
            strncpy(_lastError, "Partition Flash 'model' (srmodels.bin) introuvable pour ESP-SR", sizeof(_lastError) - 1);
            return false;
        }
        if (!isSimModelPresent(_modelName)) {
            snprintf(_lastError, sizeof(_lastError), "Modele WakeNet '%s' absent de srmodels.bin", _modelName.c_str());
            return false;
        }
        return true;
#else
        return false;
#endif
    }

    /**
     * @brief Indique si le moteur est réellement disponible (support compilé ET modèle binaire présent).
     */
    bool isAvailable() const override {
        return isCompiledSupportAvailable() && isModelAvailable();
    }

    bool isSimulatedEngine() const override {
        return false; // Moteur acoustique réel (jamais simulé)
    }

    size_t getPreferredFrameSamples() const override {
        return WAKENET_FRAME_SAMPLES; // 480 échantillons (30 ms @ 16 kHz)
    }

    bool begin(uint32_t sampleRateHz = 16000, uint8_t channels = 1) override {
        // Libérer systématiquement toute ressource antérieure avant réinitialisation (v0.6.2.1)
        end();

        if (sampleRateHz != 16000 || channels != 1) {
            strncpy(_lastError, "WakeNet10 requiert PCM 16000 Hz mono (1 canal)", sizeof(_lastError) - 1);
            _initialized = false;
            return false;
        }

#if VOICEKIT_HAS_ESP_SR_WAKENET
        _models = esp_srmodel_init("model");
        _modelProbed = true;
        if (!_models) {
            _modelPresent = false;
            strncpy(_lastError, "Partition Flash 'model' (srmodels.bin) introuvable pour ESP-SR", sizeof(_lastError) - 1);
            _initialized = false;
            return false;
        }
        char* modelNamePtr = esp_srmodel_filter(_models, ESP_WN_PREFIX, (char*)_modelName.c_str());
        if (!modelNamePtr && _modelName == DEFAULT_MODEL_NAME) {
            modelNamePtr = esp_srmodel_filter(_models, ESP_WN_PREFIX, (char*)"bonjouresp");
        }
        if (!modelNamePtr) {
            _modelPresent = false;
            _resolvedModelName = "";
            snprintf(_lastError, sizeof(_lastError), "Modele WakeNet '%s' absent de srmodels.bin", _modelName.c_str());
            esp_srmodel_deinit(_models);
            _models = nullptr;
            _initialized = false;
            return false;
        }
        _resolvedModelName = modelNamePtr;
        _modelPresent = true;
        _wakenet = (esp_wn_iface_t*)esp_wn_handle_from_name(modelNamePtr);
        if (!_wakenet) {
            snprintf(_lastError, sizeof(_lastError), "Echec esp_wn_handle_from_name('%s')", modelNamePtr);
            esp_srmodel_deinit(_models);
            _models = nullptr;
            _initialized = false;
            return false;
        }
        _modelData = _wakenet->create(modelNamePtr, DET_MODE_95);
        if (!_modelData) {
            _modelData = _wakenet->create(modelNamePtr, DET_MODE_90);
        }
        if (!_modelData) {
            snprintf(_lastError, sizeof(_lastError), "Echec creation instance WakeNet ('%s')", modelNamePtr);
            _wakenet = nullptr;
            esp_srmodel_deinit(_models);
            _models = nullptr;
            _initialized = false;
            return false;
        }
        if (_wakenet->get_samp_chunksize) {
            int chunkSz = _wakenet->get_samp_chunksize(_modelData);
            if (chunkSz > 0) {
                _modelChunkSamples = (size_t)chunkSz;
            }
        }
        _lastError[0] = '\0';
        _initialized = true;
        return true;
#elif !defined(ESP32) && !defined(ARDUINO)
        if (_simCompiledSupport) {
            if (!_simPartitionPresent) {
                strncpy(_lastError, "Partition Flash 'model' (srmodels.bin) introuvable pour ESP-SR", sizeof(_lastError) - 1);
                _initialized = false;
                return false;
            }
            _simResourcesAllocated = true; // Simule esp_srmodel_init("model")
            if (!isSimModelPresent(_modelName)) {
                snprintf(_lastError, sizeof(_lastError), "Modele WakeNet '%s' absent de srmodels.bin", _modelName.c_str());
                _simResourcesAllocated = false; // Libération garantie après échec filtre modèle
                _initialized = false;
                return false;
            }
            if (_simCreateFailure) {
                snprintf(_lastError, sizeof(_lastError), "Echec creation instance WakeNet ('%s')", _modelName.c_str());
                _simResourcesAllocated = false; // Libération garantie après échec create()
                _initialized = false;
                return false;
            }
            _resolvedModelName = (_modelName == DEFAULT_MODEL_NAME) ? String("wn9l_fr_bonjouresp_tts3") : _modelName;
            _lastError[0] = '\0';
            _initialized = true;
            return true;
        }
        strncpy(_lastError,
                "ESP-SR WakeNet10 (wn10_fr_bonjouresp) non disponible dans cet environnement Arduino-ESP32 (IDF 4.4.7)",
                sizeof(_lastError) - 1);
        _initialized = false;
        return false;
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
#elif !defined(ESP32) && !defined(ARDUINO)
        _simResourcesAllocated = false;
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
        memset(_accumulator, 0, sizeof(_accumulator));
#if VOICEKIT_HAS_ESP_SR_WAKENET
        if (_wakenet && _modelData) {
            voicekit_wakenet_clean(_wakenet, _modelData);
        }
#endif
    }

    /**
     * @brief Alimente l'adaptateur de cadrage 512 -> 480 échantillons (32 ms -> 30 ms).
     *        Permet de découper tout flux PCM 16 kHz (ex: blocs DMA de 512 échantillons)
     *        en fenêtres exactes de 480 échantillons (30 ms) attendues par WakeNet,
     *        sans aucune perte aux frontières des trames même lorsqu'une détection survient.
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
                    (void)onFrame480(_accumulator, WAKENET_FRAME_SAMPLES);
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
        res.confidence = 0.0f; // Ne jamais inventer un score de confiance non fourni par WakeNet (v0.6.2.1)

        if (!_initialized) {
            return res;
        }

#if VOICEKIT_HAS_ESP_SR_WAKENET
        if (_wakenet && _modelData && _modelChunkSamples == 512 && sampleCount >= 512) {
            size_t offset = 0;
            while (offset + 512 <= sampleCount) {
                wakenet_state_t state = _wakenet->detect(_modelData, (int16_t*)(samples + offset));
                offset += 512;
                if (state == WAKENET_DETECTED) {
                    res.detected = true;
                    res.word = _targetWakeWord;
                    res.confidence = 0.0f;
                    res.isSimulated = false;
                    break;
                }
            }
            res.processingTimeUs = micros() - startUs;
            return res;
        }
#endif

        feedFrameAdapter(samples, sampleCount, [&](const int16_t* frame480, size_t count) -> bool {
            (void)count;
#if VOICEKIT_HAS_ESP_SR_WAKENET
            if (_wakenet && _modelData) {
                wakenet_state_t state = _wakenet->detect(_modelData, (int16_t*)frame480);
                if (state == WAKENET_DETECTED) {
                    res.detected = true;
                    res.word = _targetWakeWord;
                    res.confidence = 0.0f; // WakeNet detect() ne fournit pas de score de probabilité flottant
                    res.isSimulated = false;
                    return true;
                }
            }
#elif !defined(ESP32) && !defined(ARDUINO)
            if (_simDetectCallback && _simDetectCallback(frame480, count)) {
                res.detected = true;
                res.word = _targetWakeWord;
                res.confidence = 0.0f;
                res.isSimulated = false;
                return true;
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
    const int16_t* getAccumulatorData() const { return _accumulator; }

#if !defined(ESP32) && !defined(ARDUINO)
    void setSimEnvironment(bool compiledSupport, bool partitionPresent, bool modelPresent, bool createFailure = false) {
        _simCompiledSupport = compiledSupport;
        _simPartitionPresent = partitionPresent;
        _simModelPresent = modelPresent;
        _simCreateFailure = createFailure;
        _modelProbed = false;
        _modelPresent = false;
        if (!compiledSupport) {
            strncpy(_lastError,
                    "ESP-SR WakeNet10 (wn10_fr_bonjouresp) non disponible dans cet environnement Arduino-ESP32 (IDF 4.4.7)",
                    sizeof(_lastError) - 1);
        }
    }
    void addSimAvailableModel(const String& modelName) {
        if (!modelName.isEmpty()) {
            _simAvailableModels.push_back(modelName);
        }
    }
    void clearSimAvailableModels() {
        _simAvailableModels.clear();
    }
    bool isSimModelPresent(const String& modelName) const {
        if (!_simModelPresent) {
            return false;
        }
        for (const auto& m : _simAvailableModels) {
            if (m == modelName) {
                return true;
            }
        }
        // Par défaut dans la partition embarquée srmodels_bonjouresp.c, seul wn10_fr_bonjouresp / wn9l_fr_bonjouresp_tts3 est présent
        if (modelName == DEFAULT_MODEL_NAME || modelName == "bonjouresp" || modelName == "wn9l_fr_bonjouresp_tts3") {
            return true;
        }
        return false;
    }
    bool hasSimResourcesAllocated() const { return _simResourcesAllocated; }
    void setSimDetectCallback(Frame480Callback cb) { _simDetectCallback = std::move(cb); }
#endif

private:
    mutable String _modelName;
    mutable String _resolvedModelName;
    String _targetWakeWord;
    bool _initialized = false;
    mutable bool _modelProbed = false;
    mutable bool _modelPresent = false;
    mutable char _lastError[160] = {};

    // Adaptateur de cadrage 512 échantillons (32 ms) -> 480 échantillons (30 ms @ 16 kHz)
    int16_t _accumulator[WAKENET_FRAME_SAMPLES];
    size_t _accumCount = 0;
    uint32_t _frames480Produced = 0;

#if VOICEKIT_HAS_ESP_SR_WAKENET
    srmodel_list_t* _models = nullptr;
    esp_wn_iface_t* _wakenet = nullptr;
    model_iface_data_t* _modelData = nullptr;
    size_t _modelChunkSamples = WAKENET_FRAME_SAMPLES;
#elif !defined(ESP32) && !defined(ARDUINO)
    bool _simCompiledSupport = false;
    bool _simPartitionPresent = false;
    bool _simModelPresent = false;
    bool _simCreateFailure = false;
    bool _simResourcesAllocated = false;
    std::vector<String> _simAvailableModels;
    Frame480Callback _simDetectCallback;
#endif
};

