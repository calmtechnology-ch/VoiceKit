/**
 * @file main.cpp
 * @brief Projet de validation externe pour VoiceKit v0.6.4.3
 * 
 * Ce projet valide l'installation et l'intégration de la bibliothèque VoiceKit
 * directement en tant que consommateur externe via PlatformIO (lib_deps),
 * sans aucune copie locale manuelle ni chemin d'inclusion forcé (-I).
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
    Serial.println("  VoiceKit v0.6.4.3 — External Consumer Test Project    ");
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
