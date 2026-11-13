/**
 * @file MinimalTTS_Arduino.ino
 * @brief Exemple autonome VoiceKit v0.6.5.2 pour Arduino IDE (M5Stack CoreS3)
 * 
 * =============================================================================
 * Configuration requise dans l'Arduino IDE :
 * =============================================================================
 * 1. Outils -> Type de carte -> ESP32 Arduino -> "M5Stack-CoreS3" (ou "ESP32S3 Dev Module")
 * 2. Outils -> PSRAM -> "OPI PSRAM"  <-- [CRITIQUE : indispensable pour le buffer audio PSRAM]
 * 3. Outils -> Partition Scheme -> "Huge APP (3MB No OTA)" ou "16M Flash (3MB APP/9.9MB FATFS)"
 * 4. Outils -> Flash Size -> "16MB (128Mb)"
 * 5. Outils -> Upload Speed -> 460800 / 921600
 * 
 * Bibliothèques nécessaires (à installer via le Gestionnaire de bibliothèques Arduino) :
 * - M5Unified (par M5Stack, version >= 0.2.23)
 * - M5CoreS3  (par M5Stack, version >= 1.0.0)
 * =============================================================================
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>

// =============================================================================
// Identifiants Wi-Fi et Clé API Gemini
// Remplacez les valeurs ci-dessous par vos identifiants réels.
// NE JAMAIS PUBLIER VOS CLÉS DANS UN DÉPÔT PUBLIC !
// =============================================================================
static const char* WIFI_SSID     = "VOTRE_SSID_WIFI";
static const char* WIFI_PASSWORD = "VOTRE_MOT_DE_PASSE_WIFI";
static const char* GEMINI_KEY    = "VOTRE_CLE_API_GEMINI";

// Instance principale du moteur vocal VoiceKit
VoxCoreVoice voice;
bool speechTriggered = false;

void setup() {
    // Initialisation matérielle M5Stack CoreS3 (écran, bus I2C/AXP2101, audio AW88298)
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.6.5.2 — Exemple MinimalTTS (Arduino IDE)");
    Serial.println("========================================================");

    // Affichage initial sur l'écran CoreS3
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("VoiceKit v0.6.5.2", 10, 15);
    M5.Display.setTextSize(1);
    M5.Display.drawString("Connexion Wi-Fi & Initialisation...", 10, 50);

    // Configuration de VoiceKit
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.voice = "Aoede";    // Voix : Aoede, Charon, Fenrir, Kore, Puck
    config.volume = 70;        // Volume (0 - 100 %)

    // Callbacks d'état pour information visuelle / console
    voice.onStateChanged([](VoiceState state) {
        Serial.printf("[VoiceKit] Nouvel etat : %s\n", voiceStateToString(state));
        M5.Display.fillRect(10, 80, 300, 30, TFT_BLACK);
        M5.Display.drawString(voiceStateToString(state), 10, 80);
    });

    voice.onError([](const String& errorMsg) {
        Serial.printf("[VoiceKit] ERREUR : %s\n", errorMsg.c_str());
        M5.Display.fillRect(10, 120, 300, 30, TFT_BLACK);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString(errorMsg.c_str(), 10, 120);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    });

    // Démarrage du moteur
    if (!voice.begin(config)) {
        Serial.println("[VoiceKit] Echec de voice.begin()");
        M5.Display.drawString("Erreur initialisation VoiceKit", 10, 120);
        return;
    }

    Serial.println("[VoiceKit] Moteur vocal initialise.");
}

void loop() {
    M5.update();
    voice.update();

    // Déclenchement automatique dès que le moteur est prêt (Wi-Fi connecté, heure NTP synchronisée)
    if (voice.isReady() && !speechTriggered) {
        speechTriggered = true;
        Serial.println("[VoiceKit] Moteur pret -> Envoi de la synthese vocale.");
        M5.Display.drawString("Enonciation du message...", 10, 120);
        voice.say("Bonjour, VoiceKit version zero point six point cinq fonctionne parfaitement sur CoreS3.");
    }

    // Ré-énonciation par appui tactile sur l'écran
    auto touch = M5.Touch.getDetail();
    if (touch.wasPressed()) {
        Serial.println("[VoiceKit] Ecran touche -> Nouvelle synthese.");
        voice.say("VoiceKit est pret et repond au toucher.");
    }

    delay(10);
}
