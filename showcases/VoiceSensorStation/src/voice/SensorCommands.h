#pragma once

#include <stdint.h>
#include <string>
#include "sensors/SensorData.h"
#include "sensors/SensorManager.h"

/**
 * @file SensorCommands.h
 * @brief Commandes environnementales locales, formatage vocal français et contexte IA structuré.
 *
 * Sépare clairement :
 * - SensorCommandLogic : logique pure C++17 (normalisation, classification local vs IA,
 *   formatage vocal en français, construction du contexte IA), testable nativement sans matériel.
 * - SensorCommands (ESP32 / Arduino) : intégration avec ToolRegistry et VoiceCommandDispatcher
 *   de VoiceKit v0.6.7.1 pour exécuter les 7 outils locaux sur SensorManager.
 */

enum class LocalSensorCommand : uint8_t {
    NONE = 0,
    TEMPERATURE,
    HUMIDITY,
    PRESSURE,
    GAS_RESISTANCE,
    ALL_MEASUREMENTS,
    SENSOR_STATUS,
    SAMPLE_AGE
};

class SensorCommandLogic {
public:
    /**
     * @brief Normalise un texte français UTF-8 (minuscules, sans accents, sans ponctuation).
     */
    static std::string normalizeFrench(const std::string& input);

    /**
     * @brief Détecte si la phrase demande une analyse/interprétation/explication destinée à l'IA.
     */
    static bool isInterpretiveAiQuery(const std::string& normalizedText);

    /**
     * @brief Classifie une transcription brute en commande environnementale locale (ou NONE -> IA).
     */
    static LocalSensorCommand classifyCommand(const std::string& rawText);

    /**
     * @brief Retourne l'identifiant d'outil ToolRegistry associé à une commande locale.
     */
    static const char* commandToToolId(LocalSensorCommand cmd);

    /**
     * @brief Formate un nombre avec virgule décimale française (ex: 22.4 -> "22,4").
     */
    static std::string formatFrenchDecimal(float value, int decimals);

    /**
     * @brief Formate la réponse vocale française exacte à partir d'un instantané réel.
     *        N'invente jamais aucune valeur ; gère explicitement l'indisponibilité et la péremption.
     */
    static std::string formatCommandResponse(LocalSensorCommand cmd,
                                             const SensorData& snapshot,
                                             SensorState state,
                                             uint32_t nowMs,
                                             uint32_t staleThresholdMs = AppConfig::SENSOR_STALE_THRESHOLD_MS);

    /**
     * @brief Construit le bloc de contexte environnemental réel + question pour OpenAI / Gemini.
     */
    static std::string buildAiPromptWithContext(const std::string& userQuestion,
                                                const SensorData& snapshot,
                                                SensorState state,
                                                uint32_t nowMs,
                                                uint32_t staleThresholdMs = AppConfig::SENSOR_STALE_THRESHOLD_MS);
};

#ifdef ARDUINO

#include <Arduino.h>
#include <ToolRegistry.h>
#include <core/pipeline/VoiceCommandDispatcher.h>

struct SensorCommandDispatchResult {
    bool handledLocally = false;
    LocalSensorCommand command = LocalSensorCommand::NONE;
    String toolId;
    String spokenResponse;
};

class SensorCommands {
public:
    SensorCommands() = default;
    ~SensorCommands() = default;

    /**
     * @brief Enregistre les 7 outils locaux dans ToolRegistry et leurs motifs dans VoiceCommandDispatcher.
     */
    bool begin(const SensorManager& sensorManager);

    /**
     * @brief Tente de résoudre et d'exécuter localement une transcription utilisateur.
     *        Si la phrase est une question d'interprétation/conversation, retourne handledLocally = false.
     */
    SensorCommandDispatchResult tryExecuteLocal(const String& rawText, uint32_t nowMs);

    /**
     * @brief Construit la requête IA enrichie avec les vraies mesures du capteur.
     */
    String buildAiPrompt(const String& userQuestion, uint32_t nowMs) const;

    ToolRegistry& getRegistry() { return m_registry; }
    const ToolRegistry& getRegistry() const { return m_registry; }
    VoiceCommandDispatcher& getDispatcher() { return m_dispatcher; }

private:
    const SensorManager* m_sensorManager = nullptr;
    ToolRegistry m_registry;
    VoiceCommandDispatcher m_dispatcher;
    uint32_t m_evalNowMs = 0;

    ToolResult executeLocalTool(LocalSensorCommand cmd) const;
};

#endif // ARDUINO
