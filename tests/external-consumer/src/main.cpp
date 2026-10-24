/**
 * @file main.cpp
 * @brief Projet de validation externe pour VoiceKit v0.2.1
 * 
 * Ce projet valide l'installation de la bibliotheque VoiceKit
 * directement depuis GitHub via PlatformIO (lib_deps),
 * sans aucune copie locale ni chemin d'inclusion manuel (-I).
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>

VoxCoreVoice voice;
bool speechTriggered = false;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.2.1 — External Consumer Test Project");
    Serial.println("========================================================");

    VoiceKitConfig config;
    config.apiKey = "TEST_API_KEY_MOCK";
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = "TEST_SSID";
    config.wifiPassword = "TEST_PASSWORD";
    config.voice = "Aoede";
    config.volume = 70;

    voice.onStateChanged([](VoiceState state) {
        Serial.printf("[ExternalConsumer] Etat : %s\n", voiceStateToString(state));
    });

    voice.onError([](const String& errorMsg) {
        Serial.printf("[ExternalConsumer] Erreur : %s\n", errorMsg.c_str());
    });

    if (!voice.begin(config)) {
        Serial.println("[ExternalConsumer] Echec voice.begin()");
        return;
    }

    Serial.println("[ExternalConsumer] Initialisation VoiceKit reussie !");
}

void loop() {
    M5.update();
    voice.update();

    if (voice.isReady() && !speechTriggered) {
        speechTriggered = true;
        voice.say("Bonjour, VoiceKit fonctionne.");
    }

    delay(10);
}
