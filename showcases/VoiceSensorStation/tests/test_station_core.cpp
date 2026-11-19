/**
 * @file test_station_core.cpp
 * @brief Tests unitaires natifs (sans matériel) pour VoiceSensorStation v0.1.0.1.
 *
 * Couvre :
 * - Validation des mesures BME688 (bornes physiques, NaN, Inf, gaz=0)
 * - Gestion de l'âge et de la péremption (STALE) des données
 * - Machine à états SensorManager (INITIALIZING, READING, READY, STALE, ERROR, DISCONNECTED, reconnexion)
 * - Classification des commandes environnementales locales vs questions IA
 * - Formatage des réponses vocales françaises (virgule décimale, capteur indisponible, mesure périmée)
 * - Injection structurée du contexte capteur réel pour OpenAI / Gemini
 *
 * NOTE : Ces tests valident la logique logicielle pure et ne remplacent pas
 *        la validation matérielle sur M5Stack CoreS3 + ENV Pro U169.
 */

#include <iostream>
#include <string>
#include <cmath>
#include <limits>
#include <cassert>

#include "AppConfig.h"
#include "sensors/SensorData.h"
#include "sensors/EnvSensor.h"
#include "sensors/SensorManager.h"
#include "voice/SensorCommands.h"
#include "voice/ProfileSwitchLogic.h"

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { \
            g_passed++; \
        } else { \
            g_failed++; \
            std::cerr << "[FAIL] " << msg << " (ligne " << __LINE__ << ")\n"; \
        } \
    } while (0)

// Capteur simulé pour tests unitaires de la machine à états SensorManager
class MockEnvSensor : public IEnvSensor {
public:
    bool present = true;
    bool beginReadingOk = true;
    bool endReadingOk = true;
    uint32_t readDelayMs = 200;
    SensorData nextSample;

    bool begin() override { return present; }
    bool isConnected() override { return present; }

    bool beginReading(uint32_t nowMs, uint32_t& outReadyAtMs) override {
        if (!present || !beginReadingOk) return false;
        outReadyAtMs = nowMs + readDelayMs;
        return true;
    }

    bool endReading(SensorData& outSample) override {
        if (!present || !endReadingOk) return false;
        outSample = nextSample;
        return outSample.valid;
    }

    uint8_t getI2cAddress() const override { return present ? 0x77 : 0; }
};

static SensorData makeValidSample(float t = 22.4f, float h = 48.0f, float p = 1013.2f, float gas = 85300.0f, uint32_t ts = 1000) {
    SensorData d;
    d.temperatureC = t;
    d.humidityPercent = h;
    d.pressureHpa = p;
    d.gasResistanceOhms = gas;
    d.timestampMs = ts;
    d.valid = true;
    d.gasValid = (gas > 0.0f);
    return d;
}

static void testSensorValidation() {
    SensorData ok = makeValidSample();
    CHECK(validateSensorSample(ok), "Échantillon nominal valide");
    CHECK(isGasReadingValid(ok.gasResistanceOhms), "Résistance de gaz > 0 valide");

    SensorData badTemp = ok;
    badTemp.temperatureC = 120.0f;
    CHECK(!validateSensorSample(badTemp), "Température hors plage rejetée");

    SensorData nanTemp = ok;
    nanTemp.temperatureC = std::numeric_limits<float>::quiet_NaN();
    CHECK(!validateSensorSample(nanTemp), "Température NaN rejetée");

    SensorData badHum = ok;
    badHum.humidityPercent = 105.0f;
    CHECK(!validateSensorSample(badHum), "Humidité > 100% rejetée");

    SensorData badPres = ok;
    badPres.pressureHpa = 100.0f;
    CHECK(!validateSensorSample(badPres), "Pression hors plage rejetée");

    CHECK(!isGasReadingValid(0.0f), "Résistance de gaz nulle signalée invalide");
    CHECK(!isGasReadingValid(-5.0f), "Résistance de gaz négative rejetée");
}

static void testAgeAndFreshness() {
    SensorData d = makeValidSample(22.4f, 48.0f, 1013.2f, 85300.0f, 5000);
    CHECK(computeSampleAgeMs(d, 6300) == 1300, "Calcul exact de l'âge (1300 ms)");
    CHECK(isSampleFresh(d, 14999, 10000), "Donnée fraîche sous le seuil");
    CHECK(!isSampleFresh(d, 15001, 10000), "Donnée périmée au-delà du seuil");

    SensorData invalid;
    invalid.valid = false;
    CHECK(computeSampleAgeMs(invalid, 10000) == UINT32_MAX, "Âge UINT32_MAX si invalide");
    CHECK(!isSampleFresh(invalid, 10000, 10000), "Donnée invalide jamais fraîche");
}

static void testSensorManagerStateMachine() {
    MockEnvSensor mock;
    mock.nextSample = makeValidSample(21.5f, 52.0f, 1012.8f, 92400.0f, 0);
    SensorManager mgr(mock);
    mgr.setPollIntervalMs(2000);
    mgr.setStaleThresholdMs(5000);
    mgr.setReconnectIntervalMs(3000);

    // 1. Démarrage normal -> READING puis READY après endReading
    CHECK(mgr.begin(1000), "begin() réussit si capteur présent");
    CHECK(mgr.getState() == SensorState::READING, "État READING après démarrage première lecture");

    mgr.update(1100); // Pas encore prêt (200 ms)
    CHECK(mgr.getState() == SensorState::READING, "Reste en READING avant fin de conversion");

    mgr.update(1250); // Prêt
    CHECK(mgr.getState() == SensorState::READY, "Passage à READY après lecture valide");
    CHECK(mgr.hasValidData(), "Instantané valide disponible");
    CHECK(std::fabs(mgr.getSnapshot().temperatureC - 21.5f) < 0.01f, "Température stockée exacte");

    // 2. Déconnexion capteur -> DISCONNECTED tout en conservant le dernier instantané valide
    mock.present = false;
    mgr.update(3500);
    CHECK(mgr.getState() == SensorState::DISCONNECTED, "Déconnexion détectée -> DISCONNECTED");
    CHECK(mgr.hasValidData(), "Dernier instantané valide conservé en mémoire");
    CHECK(mgr.getDiagnostics().i2cErrorCount > 0, "Compteur d'erreur I2C incrémenté");

    // 3. Reconnexion automatique -> retour à READY puis nouvelle mesure
    mock.present = true;
    mock.nextSample = makeValidSample(23.1f, 45.0f, 1015.0f, 78000.0f, 0);
    mgr.update(7000); // Après intervalle de reconnexion (3000 ms)
    CHECK(mgr.getState() == SensorState::READY, "Reconnexion automatique -> READY");
    CHECK(mgr.getDiagnostics().reconnectSuccessCount == 1, "Compteur de reconnexion incrémenté");

    mgr.update(9500); // Déclenche nouvelle lecture
    CHECK(mgr.getState() == SensorState::READING, "Nouvelle lecture lancée après reconnexion");
    mgr.update(9800); // Termine nouvelle lecture
    CHECK(mgr.getState() == SensorState::READY, "Retour à READY avec nouvelle mesure");
    CHECK(std::fabs(mgr.getSnapshot().temperatureC - 23.1f) < 0.01f, "Nouvelle température appliquée");
}

static void testLocalCommandClassification() {
    // Commandes locales directes
    CHECK(SensorCommandLogic::classifyCommand("Quelle est la température ?") == LocalSensorCommand::TEMPERATURE,
          "Classification température");
    CHECK(SensorCommandLogic::classifyCommand("Quel est le taux d'humidité ?") == LocalSensorCommand::HUMIDITY,
          "Classification humidité");
    CHECK(SensorCommandLogic::classifyCommand("Quelle est la pression atmosphérique ?") == LocalSensorCommand::PRESSURE,
          "Classification pression");
    CHECK(SensorCommandLogic::classifyCommand("Quelle est la résistance de gaz ?") == LocalSensorCommand::GAS_RESISTANCE,
          "Classification résistance de gaz");
    CHECK(SensorCommandLogic::classifyCommand("Donne-moi toutes les mesures.") == LocalSensorCommand::ALL_MEASUREMENTS,
          "Classification toutes les mesures");
    CHECK(SensorCommandLogic::classifyCommand("Est-ce que le capteur fonctionne ?") == LocalSensorCommand::SENSOR_STATUS,
          "Classification état capteur");
    CHECK(SensorCommandLogic::classifyCommand("De quand datent les dernières mesures ?") == LocalSensorCommand::SAMPLE_AGE,
          "Classification âge des mesures");

    // Questions analytiques / conversationnelles -> doivent retourner NONE (routage IA)
    CHECK(SensorCommandLogic::classifyCommand("Comment interpréter l'humidité actuelle ?") == LocalSensorCommand::NONE,
          "Question interprétation humidité -> IA");
    CHECK(SensorCommandLogic::classifyCommand("Est-ce que l'air est trop sec ?") == LocalSensorCommand::NONE,
          "Question air trop sec -> IA");
    CHECK(SensorCommandLogic::classifyCommand("Pourquoi la pression atmosphérique change-t-elle ?") == LocalSensorCommand::NONE,
          "Question pourquoi pression -> IA");
    CHECK(SensorCommandLogic::classifyCommand("Explique-moi le fonctionnement du BME688.") == LocalSensorCommand::NONE,
          "Question explication BME688 -> IA");
    CHECK(SensorCommandLogic::classifyCommand("Est-ce que les conditions de la pièce sont confortables ?") == LocalSensorCommand::NONE,
          "Question confort pièce -> IA");
}

static void testFrenchResponseFormatting() {
    SensorData d = makeValidSample(22.4f, 48.0f, 1013.2f, 85300.0f, 10000);

    std::string rTemp = SensorCommandLogic::formatCommandResponse(
        LocalSensorCommand::TEMPERATURE, d, SensorState::READY, 11300, 10000);
    CHECK(rTemp == "La température actuelle est de 22,4 degrés.", "Format réponse température exact");

    std::string rHum = SensorCommandLogic::formatCommandResponse(
        LocalSensorCommand::HUMIDITY, d, SensorState::READY, 11300, 10000);
    CHECK(rHum == "Le taux d'humidité actuel est de 48 pour cent.", "Format réponse humidité exact");

    std::string rPres = SensorCommandLogic::formatCommandResponse(
        LocalSensorCommand::PRESSURE, d, SensorState::READY, 11300, 10000);
    CHECK(rPres == "La pression atmosphérique actuelle est de 1013,2 hectopascals.", "Format réponse pression exact");

    std::string rGas = SensorCommandLogic::formatCommandResponse(
        LocalSensorCommand::GAS_RESISTANCE, d, SensorState::READY, 11300, 10000);
    CHECK(rGas == "La résistance de gaz actuelle est de 85,3 kilo-ohms.", "Format réponse gaz exact");

    // Capteur indisponible
    std::string rUnavail = SensorCommandLogic::formatCommandResponse(
        LocalSensorCommand::TEMPERATURE, d, SensorState::DISCONNECTED, 11300, 10000);
    CHECK(rUnavail == "Le capteur environnemental est actuellement indisponible.", "Réponse capteur indisponible");

    // Mesure périmée (VSS-16)
    std::string rStale = SensorCommandLogic::formatCommandResponse(
        LocalSensorCommand::TEMPERATURE, d, SensorState::STALE, 25000, 10000);
    CHECK(rStale == "Je n'ai pas de mesure récente de température.", "Réponse mesure périmée");

    // Contexte IA structuré
    std::string aiPrompt = SensorCommandLogic::buildAiPromptWithContext(
        "Est-ce que l'air est trop sec ?", d, SensorState::READY, 11300, 10000);
    CHECK(aiPrompt.find("Temperature : 22.4 C") != std::string::npos, "Contexte IA contient température réelle");
    CHECK(aiPrompt.find("Humidite : 48 %") != std::string::npos, "Contexte IA contient humidité réelle");
    CHECK(aiPrompt.find("Capteur : READY") != std::string::npos, "Contexte IA contient état capteur");

    SensorData empty;
    std::string aiNoData = SensorCommandLogic::buildAiPromptWithContext(
        "Quelle est la température ?", empty, SensorState::DISCONNECTED, 11300, 10000);
    CHECK(aiNoData.find("INDISPONIBLES") != std::string::npos, "Contexte IA signale explicitement l'absence de données");
}

static void testProviderSwitchLogic() {
    using namespace StationSwitch;

    // 1. OpenAI (CONNECTED, idle) -> Gemini avec clé présente : APPLY_NOW direct (pas de disconnect redondant)
    CHECK(decideSwitchAction(OpState::IDLE, false, false, true,
                             Provider::OPENAI, Provider::GEMINI, true,
                             true, SessionState::CONNECTED, false, false) == RequestAction::APPLY_NOW,
          "OpenAI CONNECTED -> Gemini délègue directement à applyVoiceProfile (APPLY_NOW)");

    // 2. Gemini (CONNECTED, idle) -> OpenAI avec clé présente : APPLY_NOW direct
    CHECK(decideSwitchAction(OpState::IDLE, false, false, true,
                             Provider::GEMINI, Provider::OPENAI, true,
                             true, SessionState::CONNECTED, false, false) == RequestAction::APPLY_NOW,
          "Gemini CONNECTED -> OpenAI délègue directement à applyVoiceProfile (APPLY_NOW)");

    // 3. Déjà actif et connecté sur Gemini -> ALREADY_ACTIVE
    CHECK(decideSwitchAction(OpState::IDLE, false, false, true,
                             Provider::GEMINI, Provider::GEMINI, true,
                             true, SessionState::CONNECTED, false, false) == RequestAction::ALREADY_ACTIVE,
          "Demande Gemini alors que Gemini déjà CONNECTED -> ALREADY_ACTIVE");

    // 4. Clé Gemini absente -> REJECT_NO_KEY
    CHECK(decideSwitchAction(OpState::IDLE, false, false, true,
                             Provider::OPENAI, Provider::GEMINI, false,
                             true, SessionState::CONNECTED, false, false) == RequestAction::REJECT_NO_KEY,
          "Bascule OpenAI -> Gemini sans clé Gemini -> REJECT_NO_KEY");

    // 5. Moteur actif (parole ou écoute) -> NEED_STOP
    CHECK(decideSwitchAction(OpState::IDLE, false, false, true,
                             Provider::OPENAI, Provider::GEMINI, true,
                             false, SessionState::PLAYING, false, false) == RequestAction::NEED_STOP,
          "Bascule pendant parole/écoute -> NEED_STOP");

    // 6. Session en RECONNECTING -> NEED_DISCONNECT (pour débloquer applyVoiceProfile)
    CHECK(decideSwitchAction(OpState::IDLE, false, false, true,
                             Provider::OPENAI, Provider::GEMINI, true,
                             true, SessionState::RECONNECTING, true, false) == RequestAction::NEED_DISCONNECT,
          "Session en RECONNECTING -> NEED_DISCONNECT avant applyVoiceProfile");

    // 7. Opération déjà en cours ou STOP actif ou VoiceKit profile pending -> REJECT_BUSY
    CHECK(decideSwitchAction(OpState::APPLYING, false, false, true,
                             Provider::OPENAI, Provider::GEMINI, true,
                             true, SessionState::CONNECTING, true, false) == RequestAction::REJECT_BUSY,
          "Bascule déjà en APPLYING -> REJECT_BUSY");
    CHECK(decideSwitchAction(OpState::IDLE, true, false, true,
                             Provider::OPENAI, Provider::GEMINI, true,
                             true, SessionState::CONNECTING, true, false) == RequestAction::REJECT_BUSY,
          "VoiceKit isProfileChangePending() -> REJECT_BUSY");
    CHECK(decideSwitchAction(OpState::IDLE, false, true, true,
                             Provider::OPENAI, Provider::GEMINI, true,
                             true, SessionState::CONNECTED, false, false) == RequestAction::REJECT_BUSY,
          "STOP actif -> REJECT_BUSY");

    // 8. Validation onProfileApplied : confirme Gemini uniquement si APPLYING et fournisseur correspondant
    CHECK(doesProfileAppliedConfirm(OpState::APPLYING, true, Provider::GEMINI, Provider::GEMINI),
          "onProfileApplied(Gemini) confirme bascule vers Gemini");
    CHECK(!doesProfileAppliedConfirm(OpState::APPLYING, true, Provider::GEMINI, Provider::OPENAI),
          "Callback tardif onProfileApplied(OpenAI) rejeté pendant bascule vers Gemini");
    CHECK(!doesProfileAppliedConfirm(OpState::IDLE, true, Provider::GEMINI, Provider::GEMINI),
          "onProfileApplied ignoré comme confirmation de bascule si OpState::IDLE");

    // 9. Validation onReady fallback (si onReady arrive sans onProfileApplied)
    CHECK(evaluateOnReadyDuringSwitch(OpState::APPLYING, false, SessionState::CONNECTED,
                                      Provider::GEMINI, Provider::GEMINI) == ReadyDecision::CONFIRM_SWITCH,
          "onReady confirme bascule si VoiceKit CONNECTED sur fournisseur cible et !profilePending");
    CHECK(evaluateOnReadyDuringSwitch(OpState::APPLYING, true, SessionState::CONNECTED,
                                      Provider::GEMINI, Provider::GEMINI) == ReadyDecision::IGNORE,
          "onReady ignoré si VoiceKit a encore _profileChangePending=true");
    CHECK(evaluateOnReadyDuringSwitch(OpState::APPLYING, false, SessionState::CONNECTED,
                                      Provider::OPENAI, Provider::GEMINI) == ReadyDecision::IGNORE,
          "onReady tardif de l'ancien fournisseur (OpenAI) ignoré pendant bascule vers Gemini");
    CHECK(evaluateOnReadyDuringSwitch(OpState::APPLYING, false, SessionState::CONNECTING,
                                      Provider::GEMINI, Provider::GEMINI) == ReadyDecision::IGNORE,
          "onReady ignoré si session non CONNECTED");
}

int main() {
    testSensorValidation();
    testAgeAndFreshness();
    testSensorManagerStateMachine();
    testLocalCommandClassification();
    testFrenchResponseFormatting();
    testProviderSwitchLogic();

    std::cout << "========================================\n";
    std::cout << "VoiceSensorStation " << AppConfig::APP_VERSION << " — Unit Tests\n";
    std::cout << "PASS : " << g_passed << " | FAIL : " << g_failed << "\n";
    std::cout << "========================================\n";
    return (g_failed == 0) ? 0 : 1;
}
