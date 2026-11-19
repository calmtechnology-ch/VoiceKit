#include "SensorCommands.h"
#include <cstdio>
#include <cmath>
#include <cstring>

// =============================================================================
// SensorCommandLogic — Logique pure C++17 (ESP32 + tests natifs)
// =============================================================================

std::string SensorCommandLogic::normalizeFrench(const std::string& input) {
    if (input.empty()) return "";

    std::string raw;
    raw.reserve(input.size());

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(input.data());
    const size_t len = input.size();

    size_t i = 0;
    while (i < len) {
        unsigned char b0 = bytes[i];
        if (b0 <= 0x7F) {
            char c = static_cast<char>(b0);
            if (c >= 'A' && c <= 'Z') {
                raw.push_back(static_cast<char>(c + 32));
            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                raw.push_back(c);
            } else {
                raw.push_back(' ');
            }
            ++i;
            continue;
        }

        // Accents UTF-8 français (bloc Latin-1 0xC3)
        if (b0 == 0xC3 && i + 1 < len) {
            unsigned char b1 = bytes[i + 1];
            switch (b1) {
                case 0xA0: case 0xA2: case 0xA4:
                case 0x80: case 0x82: case 0x84:
                    raw.push_back('a'); break;
                case 0xA9: case 0xA8: case 0xAA: case 0xAB:
                case 0x89: case 0x88: case 0x8A: case 0x8B:
                    raw.push_back('e'); break;
                case 0xAE: case 0xAF:
                case 0x8E: case 0x8F:
                    raw.push_back('i'); break;
                case 0xB4: case 0xB6:
                case 0x94: case 0x96:
                    raw.push_back('o'); break;
                case 0xB9: case 0xBB: case 0xBC:
                case 0x99: case 0x9B: case 0x9C:
                    raw.push_back('u'); break;
                case 0xA7: case 0x87:
                    raw.push_back('c'); break;
                default:
                    raw.push_back(' '); break;
            }
            i += 2;
            continue;
        }

        // Ligature œ / Œ (0xC5 0x93 / 0x92)
        if (b0 == 0xC5 && i + 1 < len && (bytes[i + 1] == 0x93 || bytes[i + 1] == 0x92)) {
            raw.append("oe");
            i += 2;
            continue;
        }

        raw.push_back(' ');
        ++i;
    }

    // Réduction des espaces multiples
    std::string clean;
    clean.reserve(raw.size());
    bool lastSpace = true;
    for (char ch : raw) {
        if (ch == ' ') {
            if (!lastSpace) {
                clean.push_back(' ');
                lastSpace = true;
            }
        } else {
            clean.push_back(ch);
            lastSpace = false;
        }
    }
    if (!clean.empty() && clean.back() == ' ') {
        clean.pop_back();
    }
    return clean;
}

static bool containsWordOrPhrase(const std::string& padded, const char* phrase) {
    std::string needle = std::string(" ") + phrase + " ";
    return padded.find(needle) != std::string::npos;
}

bool SensorCommandLogic::isInterpretiveAiQuery(const std::string& norm) {
    if (norm.empty()) return false;
    std::string padded = " " + norm + " ";

    static const char* const kAiCues[] = {
        "pourquoi",
        "explique",
        "expliquez",
        "expliquer",
        "interpreter",
        "interpretation",
        "fonctionnement",
        "comment fonctionne",
        "comment marche",
        "comment interpreter",
        "trop sec",
        "trop humide",
        "trop chaud",
        "trop froid",
        "confortable",
        "confortables",
        "confort",
        "aerer",
        "ouvrir la fenetre",
        "conseil",
        "conseils",
        "recommande",
        "recommandes",
        "normal",
        "normale",
        "bon",
        "bonne",
        "mauvais",
        "mauvaise",
        "sain",
        "saine",
        "qualite de l air",
        "co2",
        "bme688",
        "signifie",
        "veut dire",
        "difference",
        "change t elle",
        "varie"
    };

    for (const char* cue : kAiCues) {
        if (containsWordOrPhrase(padded, cue)) {
            return true;
        }
    }
    return false;
}

LocalSensorCommand SensorCommandLogic::classifyCommand(const std::string& rawText) {
    std::string norm = normalizeFrench(rawText);
    if (norm.empty()) {
        return LocalSensorCommand::NONE;
    }

    // Les questions d'interprétation ou d'explication vont toujours vers l'IA
    if (isInterpretiveAiQuery(norm)) {
        return LocalSensorCommand::NONE;
    }

    std::string padded = " " + norm + " ";

    // 1. Âge des mesures
    if (containsWordOrPhrase(padded, "de quand datent") ||
        containsWordOrPhrase(padded, "de quand date") ||
        containsWordOrPhrase(padded, "quand datent") ||
        containsWordOrPhrase(padded, "age des mesures") ||
        containsWordOrPhrase(padded, "age de la mesure") ||
        containsWordOrPhrase(padded, "dernieres mesures datent") ||
        containsWordOrPhrase(padded, "derniere mesure date") ||
        containsWordOrPhrase(padded, "fraicheur des mesures") ||
        containsWordOrPhrase(padded, "depuis quand")) {
        return LocalSensorCommand::SAMPLE_AGE;
    }

    // 2. État / fonctionnement du capteur
    if (containsWordOrPhrase(padded, "capteur fonctionne") ||
        containsWordOrPhrase(padded, "capteur marche") ||
        containsWordOrPhrase(padded, "etat du capteur") ||
        containsWordOrPhrase(padded, "statut du capteur") ||
        containsWordOrPhrase(padded, "capteur est il") ||
        containsWordOrPhrase(padded, "capteur est connecte") ||
        containsWordOrPhrase(padded, "capteur est disponible") ||
        containsWordOrPhrase(padded, "capteur ok") ||
        containsWordOrPhrase(padded, "diagnostic capteur")) {
        return LocalSensorCommand::SENSOR_STATUS;
    }

    // 3. Toutes les mesures
    if (containsWordOrPhrase(padded, "toutes les mesures") ||
        containsWordOrPhrase(padded, "tous les parametres") ||
        containsWordOrPhrase(padded, "bilan environnemental") ||
        containsWordOrPhrase(padded, "resume des mesures") ||
        containsWordOrPhrase(padded, "donne moi les mesures") ||
        containsWordOrPhrase(padded, "quelles sont les mesures") ||
        containsWordOrPhrase(padded, "conditions actuelles") ||
        containsWordOrPhrase(padded, "mesures actuelles")) {
        return LocalSensorCommand::ALL_MEASUREMENTS;
    }

    // 4. Température
    if (containsWordOrPhrase(padded, "temperature") ||
        containsWordOrPhrase(padded, "combien fait il") ||
        containsWordOrPhrase(padded, "il fait combien") ||
        containsWordOrPhrase(padded, "quel degre") ||
        containsWordOrPhrase(padded, "combien de degres")) {
        return LocalSensorCommand::TEMPERATURE;
    }

    // 5. Humidité
    if (containsWordOrPhrase(padded, "humidite") ||
        containsWordOrPhrase(padded, "hygrometrie")) {
        return LocalSensorCommand::HUMIDITY;
    }

    // 6. Pression atmosphérique
    if (containsWordOrPhrase(padded, "pression")) {
        return LocalSensorCommand::PRESSURE;
    }

    // 7. Résistance de gaz
    if (containsWordOrPhrase(padded, "resistance de gaz") ||
        containsWordOrPhrase(padded, "resistance gaz") ||
        containsWordOrPhrase(padded, "mesure de gaz") ||
        containsWordOrPhrase(padded, "valeur de gaz") ||
        containsWordOrPhrase(padded, "gaz")) {
        return LocalSensorCommand::GAS_RESISTANCE;
    }

    return LocalSensorCommand::NONE;
}

const char* SensorCommandLogic::commandToToolId(LocalSensorCommand cmd) {
    switch (cmd) {
        case LocalSensorCommand::TEMPERATURE:      return "sensor.get_temperature";
        case LocalSensorCommand::HUMIDITY:         return "sensor.get_humidity";
        case LocalSensorCommand::PRESSURE:         return "sensor.get_pressure";
        case LocalSensorCommand::GAS_RESISTANCE:   return "sensor.get_gas";
        case LocalSensorCommand::ALL_MEASUREMENTS: return "sensor.get_all";
        case LocalSensorCommand::SENSOR_STATUS:    return "sensor.get_status";
        case LocalSensorCommand::SAMPLE_AGE:       return "sensor.get_age";
        case LocalSensorCommand::NONE:
        default:                                   return "";
    }
}

std::string SensorCommandLogic::formatFrenchDecimal(float value, int decimals) {
    char buf[32];
    if (decimals <= 0) {
        std::snprintf(buf, sizeof(buf), "%.0f", std::round(value));
    } else if (decimals == 1) {
        std::snprintf(buf, sizeof(buf), "%.1f", value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f", value);
    }
    std::string s(buf);
    for (char& ch : s) {
        if (ch == '.') ch = ',';
    }
    return s;
}

static std::string formatAgeFrench(uint32_t ageMs) {
    if (ageMs < 1000) {
        return "moins d'une seconde";
    }
    if (ageMs < 60000) {
        float secs = static_cast<float>(ageMs) / 1000.0f;
        std::string s = SensorCommandLogic::formatFrenchDecimal(secs, 1);
        return s + (secs < 2.0f ? " seconde" : " secondes");
    }
    uint32_t totalSec = ageMs / 1000;
    uint32_t mins = totalSec / 60;
    uint32_t remSec = totalSec % 60;
    char buf[64];
    if (remSec == 0) {
        std::snprintf(buf, sizeof(buf), "%u %s",
                      static_cast<unsigned>(mins), mins <= 1 ? "minute" : "minutes");
    } else {
        std::snprintf(buf, sizeof(buf), "%u %s et %u %s",
                      static_cast<unsigned>(mins), mins <= 1 ? "minute" : "minutes",
                      static_cast<unsigned>(remSec), remSec <= 1 ? "seconde" : "secondes");
    }
    return std::string(buf);
}

std::string SensorCommandLogic::formatCommandResponse(LocalSensorCommand cmd,
                                                      const SensorData& snapshot,
                                                      SensorState state,
                                                      uint32_t nowMs,
                                                      uint32_t staleThresholdMs) {
    const bool unavailable = (state == SensorState::DISCONNECTED ||
                              state == SensorState::ERROR ||
                              state == SensorState::INITIALIZING);
    const bool hasValid = snapshot.valid && validateSensorSample(snapshot);
    const bool fresh = hasValid && isSampleFresh(snapshot, nowMs, staleThresholdMs) && (state != SensorState::STALE);

    // Commandes d'état et d'âge : traitées avant les commandes de valeurs
    if (cmd == LocalSensorCommand::SENSOR_STATUS) {
        switch (state) {
            case SensorState::READY:
            case SensorState::READING:
                if (fresh) {
                    return "Le capteur environnemental fonctionne normalement.";
                }
                return "Le capteur environnemental est connecté, en attente d'une mesure récente.";
            case SensorState::STALE:
                return "Le capteur environnemental ne fournit plus de mesure récente.";
            case SensorState::INITIALIZING:
                return "Le capteur environnemental est en cours d'initialisation.";
            case SensorState::ERROR:
            case SensorState::DISCONNECTED:
            default:
                return "Le capteur environnemental est actuellement indisponible.";
        }
    }

    if (cmd == LocalSensorCommand::SAMPLE_AGE) {
        if (!hasValid) {
            return "Aucune mesure environnementale valide n'a encore été enregistrée.";
        }
        uint32_t ageMs = computeSampleAgeMs(snapshot, nowMs);
        std::string resp = "Les dernières mesures datent d'il y a " + formatAgeFrench(ageMs) + ".";
        if (!fresh || unavailable) {
            resp += " Elles ne sont plus récentes.";
        }
        return resp;
    }

    // Pour les commandes de mesures : vérifier d'abord disponibilité et fraîcheur
    if (unavailable && !hasValid) {
        return "Le capteur environnemental est actuellement indisponible.";
    }
    if (unavailable) {
        return "Le capteur environnemental est actuellement indisponible.";
    }
    if (!hasValid || !fresh) {
        switch (cmd) {
            case LocalSensorCommand::TEMPERATURE:
                return "Je n'ai pas de mesure récente de température.";
            case LocalSensorCommand::HUMIDITY:
                return "Je n'ai pas de mesure récente d'humidité.";
            case LocalSensorCommand::PRESSURE:
                return "Je n'ai pas de mesure récente de pression atmosphérique.";
            case LocalSensorCommand::GAS_RESISTANCE:
                return "Je n'ai pas de mesure récente de résistance de gaz.";
            case LocalSensorCommand::ALL_MEASUREMENTS:
            default:
                return "Je n'ai pas de mesures environnementales récentes.";
        }
    }

    // Mesures valides et fraîches
    switch (cmd) {
        case LocalSensorCommand::TEMPERATURE:
            return "La température actuelle est de " +
                   formatFrenchDecimal(snapshot.temperatureC, 1) + " degrés.";

        case LocalSensorCommand::HUMIDITY:
            return "Le taux d'humidité actuel est de " +
                   formatFrenchDecimal(snapshot.humidityPercent, 0) + " pour cent.";

        case LocalSensorCommand::PRESSURE:
            return "La pression atmosphérique actuelle est de " +
                   formatFrenchDecimal(snapshot.pressureHpa, 1) + " hectopascals.";

        case LocalSensorCommand::GAS_RESISTANCE:
            if (!snapshot.gasValid || !isGasReadingValid(snapshot.gasResistanceOhms)) {
                return "La mesure de résistance de gaz n'est pas disponible pour le moment.";
            }
            return "La résistance de gaz actuelle est de " +
                   formatFrenchDecimal(snapshot.gasResistanceOhms / 1000.0f, 1) + " kilo-ohms.";

        case LocalSensorCommand::ALL_MEASUREMENTS: {
            std::string resp =
                "Température " + formatFrenchDecimal(snapshot.temperatureC, 1) + " degrés, "
                "humidité " + formatFrenchDecimal(snapshot.humidityPercent, 0) + " pour cent, "
                "pression " + formatFrenchDecimal(snapshot.pressureHpa, 1) + " hectopascals";
            if (snapshot.gasValid && isGasReadingValid(snapshot.gasResistanceOhms)) {
                resp += ", résistance de gaz " +
                        formatFrenchDecimal(snapshot.gasResistanceOhms / 1000.0f, 1) + " kilo-ohms.";
            } else {
                resp += ", résistance de gaz indisponible.";
            }
            return resp;
        }

        default:
            return "";
    }
}

std::string SensorCommandLogic::buildAiPromptWithContext(const std::string& userQuestion,
                                                         const SensorData& snapshot,
                                                         SensorState state,
                                                         uint32_t nowMs,
                                                         uint32_t staleThresholdMs) {
    char buf[384];
    const bool hasValid = snapshot.valid && validateSensorSample(snapshot);
    const bool fresh = hasValid && isSampleFresh(snapshot, nowMs, staleThresholdMs) && (state != SensorState::STALE);

    if (hasValid) {
        uint32_t ageMs = computeSampleAgeMs(snapshot, nowMs);
        float ageSec = static_cast<float>(ageMs) / 1000.0f;
        char gasBuf[32];
        if (snapshot.gasValid && isGasReadingValid(snapshot.gasResistanceOhms)) {
            std::snprintf(gasBuf, sizeof(gasBuf), "%.1f kOhm", snapshot.gasResistanceOhms / 1000.0f);
        } else {
            std::snprintf(gasBuf, sizeof(gasBuf), "INDISPONIBLE");
        }

        std::snprintf(buf, sizeof(buf),
                      "[MESURES CAPTEUR EN TEMPS REEL]\n"
                      "Temperature : %.1f C\n"
                      "Humidite : %.0f %%\n"
                      "Pression : %.1f hPa\n"
                      "Resistance de gaz : %s\n"
                      "Age des mesures : %.1f s%s\n"
                      "Capteur : %s\n\n"
                      "[QUESTION UTILISATEUR]\n",
                      snapshot.temperatureC,
                      snapshot.humidityPercent,
                      snapshot.pressureHpa,
                      gasBuf,
                      ageSec,
                      fresh ? "" : " (PERIMEE)",
                      sensorStateToString(state));
    } else {
        std::snprintf(buf, sizeof(buf),
                      "[MESURES CAPTEUR EN TEMPS REEL]\n"
                      "Mesures : INDISPONIBLES (aucune donnee valide)\n"
                      "Capteur : %s\n\n"
                      "[QUESTION UTILISATEUR]\n",
                      sensorStateToString(state));
    }

    return std::string(buf) + userQuestion;
}

// =============================================================================
// SensorCommands — Intégration VoiceKit ToolRegistry + VoiceCommandDispatcher
// =============================================================================

#ifdef ARDUINO

bool SensorCommands::begin(const SensorManager& sensorManager) {
    m_sensorManager = &sensorManager;
    m_registry.clear();
    m_dispatcher.clear();
    m_dispatcher.setRegistry(&m_registry);

    auto reg = [this](LocalSensorCommand cmd, const char* desc, const std::vector<String>& patterns) -> bool {
        const char* toolId = SensorCommandLogic::commandToToolId(cmd);
        bool okTool = m_registry.registerTool(
            toolId,
            desc,
            [this, cmd]() -> ToolResult { return executeLocalTool(cmd); },
            ToolSecurityPolicy::READ_ONLY
        );
        if (!okTool) return false;
        return m_dispatcher.registerMapping(toolId, patterns);
    };

    bool ok = true;
    ok &= reg(LocalSensorCommand::TEMPERATURE,
              "Lit la temperature reelle du capteur BME688",
              {"quelle est la temperature", "temperature", "temperature actuelle", "combien fait il", "il fait combien"});

    ok &= reg(LocalSensorCommand::HUMIDITY,
              "Lit le taux d'humidite reel du capteur BME688",
              {"quel est le taux d humidite", "taux d humidite", "humidite", "humidite actuelle"});

    ok &= reg(LocalSensorCommand::PRESSURE,
              "Lit la pression atmospherique reelle du capteur BME688",
              {"quelle est la pression atmospherique", "pression atmospherique", "pression", "pression actuelle"});

    ok &= reg(LocalSensorCommand::GAS_RESISTANCE,
              "Lit la resistance de gaz brute du capteur BME688",
              {"quelle est la resistance de gaz", "resistance de gaz", "resistance gaz", "gaz"});

    ok &= reg(LocalSensorCommand::ALL_MEASUREMENTS,
              "Lit l'ensemble des mesures environnementales reelles",
              {"toutes les mesures", "quelles sont les mesures", "bilan environnemental", "resume des mesures"});

    ok &= reg(LocalSensorCommand::SENSOR_STATUS,
              "Verifie l'etat de fonctionnement du capteur BME688",
              {"est ce que le capteur fonctionne", "etat du capteur", "statut du capteur", "capteur fonctionne"});

    ok &= reg(LocalSensorCommand::SAMPLE_AGE,
              "Indique l'anciennete des dernieres mesures du capteur",
              {"de quand datent les dernieres mesures", "age des mesures", "derniere mesure"});

    return ok;
}

ToolResult SensorCommands::executeLocalTool(LocalSensorCommand cmd) const {
    if (!m_sensorManager) {
        return ToolResult::executionFailed("Gestionnaire de capteur non initialise");
    }
    SensorData snap = m_sensorManager->getSnapshot();
    SensorState st = m_sensorManager->getState();
    std::string text = SensorCommandLogic::formatCommandResponse(
        cmd, snap, st, m_evalNowMs, m_sensorManager->getStaleThresholdMs());
    return ToolResult::success(String(text.c_str()));
}

SensorCommandDispatchResult SensorCommands::tryExecuteLocal(const String& rawText, uint32_t nowMs) {
    SensorCommandDispatchResult out;
    if (rawText.isEmpty() || !m_sensorManager) {
        return out;
    }

    m_evalNowMs = nowMs;

    std::string rawStd(rawText.c_str());
    std::string norm = SensorCommandLogic::normalizeFrench(rawStd);
    if (SensorCommandLogic::isInterpretiveAiQuery(norm)) {
        return out; // Question analytique -> routage vers IA
    }

    // 1. Correspondance via VoiceCommandDispatcher (VoiceKit)
    VoiceCommandMatch match = m_dispatcher.match(rawText);
    if (match.matched && !match.toolId.isEmpty()) {
        ToolResult res = m_registry.execute(match.toolId, match.args);
        if (res.isSuccess()) {
            out.handledLocally = true;
            out.toolId = match.toolId;
            out.spokenResponse = res.output;
            out.command = SensorCommandLogic::classifyCommand(rawStd);
            return out;
        }
    }

    // 2. Correspondance déterministe complémentaire (variantes naturelles françaises)
    LocalSensorCommand cmd = SensorCommandLogic::classifyCommand(rawStd);
    if (cmd != LocalSensorCommand::NONE) {
        String toolId = SensorCommandLogic::commandToToolId(cmd);
        ToolResult res = m_registry.execute(toolId);
        if (res.isSuccess()) {
            out.handledLocally = true;
            out.command = cmd;
            out.toolId = toolId;
            out.spokenResponse = res.output;
            return out;
        }
    }

    return out;
}

String SensorCommands::buildAiPrompt(const String& userQuestion, uint32_t nowMs) const {
    if (!m_sensorManager) {
        return userQuestion;
    }
    std::string prompt = SensorCommandLogic::buildAiPromptWithContext(
        std::string(userQuestion.c_str()),
        m_sensorManager->getSnapshot(),
        m_sensorManager->getState(),
        nowMs,
        m_sensorManager->getStaleThresholdMs()
    );
    return String(prompt.c_str());
}

#endif // ARDUINO
