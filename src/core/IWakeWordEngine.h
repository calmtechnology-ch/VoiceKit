#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>

#ifdef DISABLED
#undef DISABLED
#endif

/**
 * @enum WakeWordState
 * @brief Machine à états interne du détecteur de mot de réveil (VoiceKit v0.6.2).
 *
 * Cycle nominal :
 * DISABLED -> INITIALIZING -> ARMED -> DETECTED -> SUSPENDED -> ARMED
 */
enum class WakeWordState : uint8_t {
    DISABLED = 0,     ///< Détecteur désactivé (coût CPU et mémoire minimal)
    INACTIVE = 0,     ///< Alias de DISABLED
    INITIALIZING,     ///< Initialisation du moteur et du pont audio en cours
    ARMED,            ///< Capture PCM 16 kHz active et détection en écoute
    DETECTED,         ///< Mot de réveil détecté (état transitoire avant suspension)
    SUSPENDED,        ///< Détecteur suspendu (pendant conversation/TTS/STT ou après détection)
    ERROR             ///< Erreur d'initialisation, moteur indisponible ou défaut d'exécution
};

inline const char* wakeWordStateToString(WakeWordState state) {
    switch (state) {
        case WakeWordState::INACTIVE:     return "DISABLED";
        case WakeWordState::INITIALIZING: return "INITIALIZING";
        case WakeWordState::ARMED:        return "ARMED";
        case WakeWordState::DETECTED:     return "DETECTED";
        case WakeWordState::SUSPENDED:    return "SUSPENDED";
        case WakeWordState::ERROR:        return "ERROR";
        default:                          return "UNKNOWN";
    }
}

/**
 * @enum WakeWordEngineType
 * @brief Types de moteurs de détection de mot de réveil pris en charge par l'abstraction.
 */
enum class WakeWordEngineType : uint8_t {
    NONE = 0,             ///< Aucun moteur sélectionné
    TEST = 1,             ///< Moteur de test déterministe (déclenchement simulé explicite, non vocal)
    WAKENET = 2,          ///< Espressif ESP-SR WakeNet (ex: WakeNet10 wn10_fr_bonjouresp)
    MICRO_WAKE_WORD = 3,  ///< Moteur TFLite Micro / microWakeWord (extensible)
    CUSTOM = 4            ///< Moteur externe personnalisé injecté via IWakeWordEngine*
};

inline const char* wakeWordEngineTypeToString(WakeWordEngineType type) {
    switch (type) {
        case WakeWordEngineType::NONE:            return "NONE";
        case WakeWordEngineType::TEST:            return "TEST";
        case WakeWordEngineType::WAKENET:         return "WAKENET";
        case WakeWordEngineType::MICRO_WAKE_WORD: return "MICRO_WAKE_WORD";
        case WakeWordEngineType::CUSTOM:          return "CUSTOM";
        default:                                  return "UNKNOWN";
    }
}

/**
 * @enum WakeWordOverflowPolicy
 * @brief Politique explicite lorsque la file bornée du pont audio Wake Word est pleine.
 *
 * Note de sécurité concurrente (v0.6.2.1) :
 * Dans une file circulaire SPSC sans verrou à zéro copie (peekFrame / consumeFrame),
 * seul le consommateur est autorisé à avancer `_tail` après achèvement de `IWakeWordEngine::process()`.
 * La politique `DROP_OLDEST` est par conséquent désactivée (repli sécurisé sur `DROP_NEWEST`)
 * afin de garantir qu'une trame en cours de consultation par le consommateur ne puisse jamais
 * être écrasée ni invalidée par le producteur audio.
 */
enum class WakeWordOverflowPolicy : uint8_t {
    DROP_NEWEST = 0,  ///< Abandonne la nouvelle trame entrante sans bloquer la tâche audio (défaut, lock-free SPSC)
    DROP_OLDEST = 1   ///< Désactivé en v0.6.2.1 pour protéger la durée de vie de peekFrame() (repli sur DROP_NEWEST)
};

inline const char* wakeWordOverflowPolicyToString(WakeWordOverflowPolicy policy) {
    switch (policy) {
        case WakeWordOverflowPolicy::DROP_NEWEST: return "DROP_NEWEST";
        case WakeWordOverflowPolicy::DROP_OLDEST: return "DROP_OLDEST";
        default:                                  return "DROP_NEWEST";
    }
}

/**
 * @struct WakeWordResult
 * @brief Résultat retourné par l'analyse d'une trame PCM dans IWakeWordEngine::process().
 */
struct WakeWordResult {
    bool detected = false;          ///< true si le mot de réveil a été détecté sur cette trame
    String word = "";               ///< Mot ou expression détectée (ex: "Bonjour ESP")
    float confidence = 0.0f;        ///< Score de confiance normalisé [0.0 .. 1.0]
    bool isSimulated = false;       ///< true si l'événement provient d'un déclenchement de test simulé
    uint32_t processingTimeUs = 0;  ///< Durée d'exécution du moteur sur cette trame (µs)
};

/**
 * @struct WakeWordEvent
 * @brief Événement de détection horodaté consultable depuis la boucle principale (compatible LVGL).
 *
 * Permet à une application graphique (ex: CoreS3-VoiceUI-Demo) de récupérer l'événement
 * dans sa propre boucle LVGL sans aucun appel LVGL depuis une tâche audio ou de reconnaissance.
 */
struct WakeWordEvent {
    bool valid = false;             ///< true si un événement est disponible
    String word = "";               ///< Mot détecté (ex: "Bonjour ESP")
    float confidence = 0.0f;        ///< Confiance [0.0 .. 1.0]
    bool isSimulated = false;       ///< true si déclenché par le moteur de test (non vocal)
    uint32_t timestampMs = 0;       ///< Horodatage millis() de la détection
    uint32_t detectionIndex = 0;    ///< Numéro séquentiel de la détection
    WakeWordEngineType engineType = WakeWordEngineType::NONE; ///< Moteur source
};

/**
 * @struct WakeWordMetrics
 * @brief Diagnostics et métriques de performance à faible coût (Section 11 — VoiceKit v0.6.2).
 */
struct WakeWordMetrics {
    // Diagnostics mémoire (octets)
    uint32_t freeInternalHeap = 0;          ///< RAM interne libre actuelle
    uint32_t minFreeInternalHeap = 0;       ///< RAM interne libre minimale observée
    uint32_t freePsram = 0;                 ///< PSRAM libre actuelle
    uint32_t minFreePsram = 0;              ///< PSRAM libre minimale observée

    // Temps de traitement et budget CPU boucle principale / LVGL (microsecondes — v0.6.2.1)
    uint32_t lastAudioPushDurationUs = 0;     ///< Dernier temps de copie dans le callback audio critique (µs)
    uint32_t maxAudioPushDurationUs = 0;      ///< Temps maximal passé dans le callback audio critique (µs)
    uint32_t lastWakeProcessDurationUs = 0;   ///< Dernier temps de traitement d'une trame par le moteur (µs)
    uint32_t maxWakeProcessDurationUs = 0;    ///< Temps maximal de traitement d'une trame par le moteur (µs)
    uint32_t lastUpdateProcessDurationUs = 0; ///< Temps cumulé de traitement Wake Word lors du dernier update() (µs)
    uint32_t maxUpdateProcessDurationUs = 0;  ///< Temps cumulé maximal de traitement lors d'un appel update() (µs)
    uint32_t processingBudgetUs = 5000;       ///< Budget temporel configuré par appel update() (µs, 0 = illimité)
    uint32_t processingBudgetExceeded = 0;    ///< Nombre de dépassements du budget de traitement observés
    size_t maxFramesPerUpdate = 2;            ///< Nombre maximal de trames traitées par appel update()
    size_t lastFramesProcessedInUpdate = 0;   ///< Nombre de trames traitées lors du dernier appel update() actif

    // Compteurs de trames et de file bornée
    uint32_t framesReceived = 0;            ///< Nombre total de trames PCM 16 kHz reçues par le pont
    uint32_t framesProcessed = 0;           ///< Nombre total de trames transmises au moteur Wake Word
    uint32_t framesDropped = 0;             ///< Nombre de trames abandonnées (file pleine ou invalide)
    uint32_t queueSaturations = 0;          ///< Nombre d'événements de saturation de la file audio
    size_t currentQueueOccupancy = 0;       ///< Nombre de trames actuellement en attente dans la file
    size_t maxQueueOccupancy = 0;           ///< Occupation maximale observée de la file bornée
    size_t queueCapacity = 0;               ///< Capacité totale de la file bornée (en trames)
    uint32_t lastRms = 0;                   ///< Dernier niveau RMS acquis sur le flux PCM 16 kHz
    uint32_t peakRms = 0;                   ///< Niveau RMS crête acquis sur le flux PCM 16 kHz

    // Compteurs d'événements, transitions micro et machine à états
    uint32_t detectionCount = 0;            ///< Nombre total de détections (réelles + simulées)
    uint32_t simulatedDetectionCount = 0;   ///< Nombre de détections issues du moteur TEST
    uint32_t stateTransitionCount = 0;      ///< Nombre de transitions d'état effectuées
    uint32_t errorCount = 0;                ///< Nombre d'erreurs d'initialisation ou d'exécution
    uint32_t captureStopFailures = 0;       ///< Nombre d'échecs d'arrêt de capture locale (stopLocalCapture)
    uint32_t lastDetectionTimestampMs = 0;  ///< Horodatage millis() de la dernière détection

    void reset() {
        freeInternalHeap = 0;
        minFreeInternalHeap = 0;
        freePsram = 0;
        minFreePsram = 0;
        lastAudioPushDurationUs = 0;
        maxAudioPushDurationUs = 0;
        lastWakeProcessDurationUs = 0;
        maxWakeProcessDurationUs = 0;
        lastUpdateProcessDurationUs = 0;
        maxUpdateProcessDurationUs = 0;
        processingBudgetExceeded = 0;
        lastFramesProcessedInUpdate = 0;
        framesReceived = 0;
        framesProcessed = 0;
        framesDropped = 0;
        queueSaturations = 0;
        currentQueueOccupancy = 0;
        maxQueueOccupancy = 0;
        queueCapacity = 0;
        lastRms = 0;
        peakRms = 0;
        detectionCount = 0;
        simulatedDetectionCount = 0;
        stateTransitionCount = 0;
        errorCount = 0;
        captureStopFailures = 0;
        lastDetectionTimestampMs = 0;
    }
};

/**
 * @class IWakeWordEngine
 * @brief Interface abstraite pure pour les moteurs de détection locale de mot de réveil.
 *
 * Permet de découpler totalement l'architecture audio et la machine à états de VoiceKit
 * du moteur de reconnaissance sous-jacent (WakeNet10 ESP-SR, microWakeWord, ou moteur de test).
 */
class IWakeWordEngine {
public:
    virtual ~IWakeWordEngine() = default;

    /**
     * @brief Nom lisible du moteur (ex: "TEST", "WAKENET", "MICRO_WAKE_WORD").
     */
    virtual const char* getName() const = 0;

    /**
     * @brief Identifiant énuméré du type de moteur.
     */
    virtual WakeWordEngineType getType() const = 0;

    /**
     * @brief Nom du modèle chargé ou ciblé (ex: "wn10_fr_bonjouresp", "test_manual").
     */
    virtual const char* getModelName() const { return ""; }

    /**
     * @brief 1. Support compilé : indique si le support logiciel/bibliothèque du moteur est compilé.
     */
    virtual bool isCompiledSupportAvailable() const { return isAvailable(); }

    /**
     * @brief 2. Modèle disponible : indique si le modèle acoustique binaire est présent et accessible.
     */
    virtual bool isModelAvailable() const { return isAvailable(); }

    /**
     * @brief Indique si le moteur et son modèle binaire sont réellement disponibles
     *        dans l'environnement de compilation et sur la cible matérielle.
     */
    virtual bool isAvailable() const = 0;

    /**
     * @brief Indique s'il s'agit d'un moteur de test simulé (true) ou d'un véritable
     *        moteur de reconnaissance acoustique (false).
     */
    virtual bool isSimulatedEngine() const = 0;

    /**
     * @brief Initialise le moteur de détection.
     * @param sampleRateHz Fréquence d'échantillonnage PCM (doit être 16000 Hz).
     * @param channels Nombre de canaux (doit être 1 = mono).
     * @return true si l'initialisation a réussi, false sinon.
     */
    virtual bool begin(uint32_t sampleRateHz = 16000, uint8_t channels = 1) = 0;

    /**
     * @brief Libère les ressources mémoire du moteur.
     */
    virtual void end() = 0;

    /**
     * @brief Indique si le moteur est initialisé et prêt à traiter des trames PCM.
     */
    virtual bool isInitialized() const = 0;

    /**
     * @brief Réinitialise l'état interne du détecteur (lors d'une suspension ou reprise).
     */
    virtual void reset() = 0;

    /**
     * @brief Traite un bloc d'échantillons PCM 16 kHz mono 16 bits signé.
     *        Appelé hors du callback audio critique (dans le contexte de WakeWordManager::update()).
     * @param samples Pointeur vers les échantillons PCM 16 bits signés.
     * @param sampleCount Nombre d'échantillons dans le bloc (ex: 512 ou 480).
     * @param rms Niveau RMS précalculé du bloc.
     * @return WakeWordResult Résultat de détection pour ce bloc.
     */
    virtual WakeWordResult process(const int16_t* samples, size_t sampleCount, uint32_t rms) = 0;

    /**
     * @brief Taille de trame optimale attendue par le moteur (en échantillons à 16 kHz).
     *        Ex: 512 (32 ms, natif DMA VoiceKit) ou 480 (30 ms, natif ESP-SR WakeNet).
     */
    virtual size_t getPreferredFrameSamples() const { return 512; }

    /**
     * @brief Définit le mot de réveil cible (ex: "Bonjour ESP").
     */
    virtual void setTargetWakeWord(const String& word) { (void)word; }

    /**
     * @brief Retourne le mot de réveil cible configuré.
     */
    virtual String getTargetWakeWord() const { return "Bonjour ESP"; }

    /**
     * @brief Retourne le dernier message d'erreur explicite du moteur.
     */
    virtual const char* getLastError() const = 0;
};
