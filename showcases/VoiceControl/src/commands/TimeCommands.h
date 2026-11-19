#pragma once

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <string>
#include "config/VoiceControlConfig.h"

/**
 * @file TimeCommands.h
 * @brief Moteur de commandes horaires suisses (Europe/Zurich) pour VoiceControl v0.1.0.
 *
 * Sépare clairement :
 * - SwissTime : gestion du fuseau horaire suisse (CET/CEST, POSIX CET-1CEST,M3.5.0,M10.5.0/3)
 *   et conversion sûre depuis un timestamp UTC vers l'heure locale suisse.
 * - TimeCommandLogic : logique pure C++17 (normalisation, classification FR/EN, calculs sur
 *   timestamps et génération des réponses vocales), testable nativement sans matériel.
 * - TimeCommands (ESP32 / Arduino) : intégration avec ToolRegistry de VoiceKit v0.6.7.1.
 */

enum class TimeCommandType : uint8_t {
    UNKNOWN = 0,
    GET_TIME,
    GET_DATE,
    GET_DAY,
    GET_YEAR,
    GET_TIME_PLUS_1H,
    GET_TIME_MINUS_1H
};

namespace SwissTime {

/**
 * @brief Applique la règle POSIX suisse (CET-1CEST,M3.5.0,M10.5.0/3) au processus ESP32.
 *        Ne déclenche jamais de synchronisation NTP.
 */
void applyTimezone();

/**
 * @brief Détermine de façon exacte si un timestamp UTC tombe en heure d'été suisse (CEST, UTC+2)
 *        ou en heure d'hiver (CET, UTC+1) selon la règle Europe/Zurich (dernier dimanche de mars
 *        01:00 UTC -> dernier dimanche d'octobre 01:00 UTC).
 */
bool isSwissDstUtc(time_t utcEpoch);

/**
 * @brief Convertit un timestamp Unix UTC en heure locale suisse (struct tm).
 *        Ne rajoute jamais artificiellement une heure à une heure déjà locale.
 */
bool utcEpochToSwissLocal(time_t utcEpoch, struct tm& outLocal, bool* outIsDst = nullptr);

/**
 * @brief Indique si le timestamp UTC fourni est valide (postérieur au seuil NTP).
 */
inline bool isValidEpoch(time_t utcEpoch) {
    return utcEpoch >= static_cast<time_t>(VC_NTP_MIN_VALID_EPOCH);
}

/**
 * @brief Formate l'heure locale en 24h ("HH:MM:SS").
 */
void formatTimeHHMMSS(const struct tm& localTm, char* dst, size_t dstSize);

/**
 * @brief Formate la date au format suisse ("JJ.MM.AAAA").
 */
void formatDateDDMMYYYY(const struct tm& localTm, char* dst, size_t dstSize);

/**
 * @brief Formate date et heure pour les diagnostics ("JJ.MM.AAAA HH:MM:SS").
 */
void formatDateTimeLog(const struct tm& localTm, char* dst, size_t dstSize);

} // namespace SwissTime

struct TimeCommandEvalResult {
    TimeCommandType command = TimeCommandType::UNKNOWN;
    bool recognized = false;
    bool clockSynced = false;
    std::string spokenResponse;
};

class TimeCommandLogic {
public:
    static const char* commandToLogName(TimeCommandType cmd);
    static const char* commandToToolId(TimeCommandType cmd);

    /**
     * @brief Normalise une transcription UTF-8 (minuscules, sans accents français, sans ponctuation).
     */
    static std::string normalizeText(const std::string& input);

    /**
     * @brief Identifie une commande horaire autorisée en français ou en anglais.
     *        Toute commande hors périmètre retourne TimeCommandType::UNKNOWN.
     */
    static TimeCommandType classifyCommand(
        const std::string& rawText,
        VoiceControlConfig::LanguageCode lang = VoiceControlConfig::getSystemLanguage());

    /**
     * @brief Formate une heure (24h) en français parlé naturel (ex: "dix heures et quinze minutes").
     */
    static std::string formatSpokenTimeFr(int hours, int minutes);

    /**
     * @brief Formate une heure (24h) en anglais parlé naturel (ex: "ten fifteen").
     */
    static std::string formatSpokenTimeEn(int hours, int minutes);

    /**
     * @brief Génère la réponse vocale complète à partir de la commande et du timestamp UTC réel.
     *        Tous les calculs (+1h / -1h) sont effectués sur le timestamp UTC avant conversion suisse.
     */
    static TimeCommandEvalResult evaluateCommand(
        TimeCommandType cmd,
        time_t utcEpoch,
        bool clockSynced,
        VoiceControlConfig::LanguageCode lang = VoiceControlConfig::getSystemLanguage());

    /**
     * @brief Pipeline complet : classification + calcul sur horloge réelle + réponse vocale.
     */
    static TimeCommandEvalResult processTranscription(
        const std::string& rawText,
        time_t utcEpoch,
        bool clockSynced,
        VoiceControlConfig::LanguageCode lang = VoiceControlConfig::getSystemLanguage());
};

#ifdef ARDUINO

#include <Arduino.h>
#include <ToolRegistry.h>

struct TimeCommandDispatchResult {
    TimeCommandType command = TimeCommandType::UNKNOWN;
    bool recognized = false;
    bool clockSynced = false;
    String toolId;
    String spokenResponse;
};

class TimeCommands {
public:
    TimeCommands() = default;
    ~TimeCommands() = default;

    /**
     * @brief Enregistre les 6 outils horaires locaux en lecture seule dans ToolRegistry.
     */
    bool begin(VoiceControlConfig::LanguageCode lang = VoiceControlConfig::getSystemLanguage());

    /**
     * @brief Identifie et exécute une commande horaire à partir de la transcription STT
     *        et de l'horloge UTC réelle du CoreS3.
     */
    TimeCommandDispatchResult process(const String& rawText, time_t utcEpoch, bool clockSynced);

    ToolRegistry& getRegistry() { return m_registry; }
    const ToolRegistry& getRegistry() const { return m_registry; }

private:
    ToolRegistry m_registry;
    VoiceControlConfig::LanguageCode m_lang = VoiceControlConfig::LanguageCode::FR;
    time_t m_evalUtcEpoch = 0;
    bool m_evalClockSynced = false;

    ToolResult executeRegisteredTool(TimeCommandType cmd) const;
};

#endif // ARDUINO
