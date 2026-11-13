/**
 * @file main.cpp
 * @brief Exemple MinimalTTS — Synthese vocale pure sur M5Stack CoreS3 avec VoiceKit v0.5.3.1
 * 
 * Base technique : Moteur VoxCore (Mode 2 DEDICATED_AUDIO_TASK, WebSocket TLS mbedTLS,
 * bundle officiel Google PKI, restitution PCM 24 kHz haute fluidite sans coupure).
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
#include <WiFi.h>

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

#ifndef GEMINI_KEY
    #ifdef GEMINI_API_KEY
        #define GEMINI_KEY GEMINI_API_KEY
    #else
        #define GEMINI_KEY ""
    #endif
#endif

#ifndef WIFI_PASSWORD
    #ifdef WIFI_PASS
        #define WIFI_PASSWORD WIFI_PASS
    #else
        #define WIFI_PASSWORD ""
    #endif
#endif

static VoxCoreVoice voice;
static bool s_welcomeAnnounced = false;
static bool s_voiceStarted = false;
static uint32_t s_phraseIndex = 0;

// Textes de test conformes aux exigences de test du cahier des charges
static const char* TEST_PHRASES[] = {
    // 1. Phrase courte
    "Bonjour, je suis VoiceKit.",
    // 2. Annonce confirmee
    "Ceci est un test de synthese vocale reussi sur M5Stack CoreS3.",
    // 3. Phrase moyenne (~10 secondes)
    "La synthese vocale haute fidelite fonctionne maintenant parfaitement sans aucune coupure grace au moteur audio stabilise de VoxCore.",
    // 4. Phrase longue (~20-25 secondes)
    "VoiceKit version zero point six point cinq est une bibliotheque autonome legere construite directement sur les composants stabilises de VoxCore. Les protocoles reseau et la restitution sonore vingt-quatre kilohertz sont desormais valides mot a mot jusqu au dernier echantillon sonore."
};
static constexpr size_t TEST_PHRASES_COUNT = sizeof(TEST_PHRASES) / sizeof(TEST_PHRASES[0]);

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  VoxCore VoiceKit v0.6.5.2 — Exemple MinimalTTS       ");
    Serial.println("  Synthese vocale unitaire haute fidelite               ");
    Serial.println("========================================================");

    // 1. Initialisation materielle CoreS3 (Ecran LCD, Alimentation, Haut-parleur)
    auto cfg = M5.config();
    cfg.internal_spk = true;
    M5.begin(cfg);

    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("VoiceKit v0.6.5.2", 10, 15);
    M5.Display.setTextSize(1);
    M5.Display.drawString("Moteur VoxCore (Mode 2 DMA)", 10, 45);
    M5.Display.drawString("Appuyez sur l'ecran pour parler", 10, 65);
    M5.Display.drawString("Statut: DEMARRAGE...", 10, 95);

    // 2. Configuration de VoiceKit
    VoiceKitConfig vConfig;
    vConfig.apiKey = GEMINI_KEY;
#ifdef GEMINI_MODEL
    vConfig.model = GEMINI_MODEL;
#endif
    vConfig.wifiMode = WiFiMode::MANAGED_WIFI; // VoiceKit gere Wi-Fi & synchronisation NTP
    vConfig.wifiSSID = WIFI_SSID;
    vConfig.wifiPassword = WIFI_PASSWORD;
    vConfig.volume = 70;
    vConfig.queuePolicy = QueuePolicy::REJECT_IF_BUSY;

    // 3. Enregistrement des callbacks essentiels
    voice.onReady([]() {
        Serial.println("[MinimalTTS] Callback onReady : Moteur vocal connecte et pret !");
        M5.Display.fillRect(0, 95, M5.Display.width(), 40, TFT_BLACK);
        M5.Display.drawString("Statut: PRET (Touchez l'ecran)", 10, 95);
    });

    voice.onSpeechStarted([]() {
        Serial.println("[MinimalTTS] Callback onSpeechStarted : Restitution vocale active");
        M5.Display.fillRect(0, 95, M5.Display.width(), 40, TFT_BLACK);
        M5.Display.drawString("Statut: PAROLE EN COURS...", 10, 95);
    });

    voice.onSpeechFinished([]() {
        Serial.println("[MinimalTTS] Callback onSpeechFinished : Fin complete de parole confirmee");
        M5.Display.fillRect(0, 95, M5.Display.width(), 40, TFT_BLACK);
        M5.Display.drawString("Statut: PRET (Attente touche)", 10, 95);
    });

    voice.onError([](const String& err) {
        Serial.printf("[MinimalTTS] Callback onError : %s\n", err.c_str());
        M5.Display.fillRect(0, 95, M5.Display.width(), 40, TFT_BLACK);
        M5.Display.drawString("Erreur: " + err, 10, 95);
    });

    // 4. Demarrage du moteur vocal
    s_voiceStarted = voice.begin(vConfig);
    if (!s_voiceStarted) {
        Serial.println("[MinimalTTS] FATAL: Echec demarrage VoiceKit");
        M5.Display.drawString("Statut: ECHEC INIT", 10, 95);
    }
}

void loop() {
    M5.update();

    if (s_voiceStarted) {
        // Traitement periodique du composant vocal
        voice.update();

        // 1. Annonce automatique d'accueil des que le moteur est pret
        if (!s_welcomeAnnounced && voice.isReady()) {
            s_welcomeAnnounced = true;
            Serial.println("[MinimalTTS] Premier lancement : prononciation de l'annonce d'accueil");
            voice.say("Bonjour, je suis VoiceKit.");
        }

        // 2. Declenchement tactile d'une nouvelle phrase de test
        auto detail = M5.Touch.getDetail();
        if (detail.wasClicked() || detail.wasPressed()) {
            if (voice.isReady()) {
                const char* phrase = TEST_PHRASES[s_phraseIndex % TEST_PHRASES_COUNT];
                s_phraseIndex++;
                Serial.printf("[MinimalTTS] Appui tactile -> Test #%u : \"%s\"\n",
                              (unsigned)s_phraseIndex, phrase);
                voice.say(phrase);
            } else if (voice.isSpeaking()) {
                Serial.println("[MinimalTTS] Appui tactile pendant parole -> Interruption STOP");
                voice.stop();
            } else {
                Serial.printf("[MinimalTTS] Touche ignoree : moteur non disponible (Etat=%s)\n",
                              voiceStateToString(voice.getState()));
            }
        }
    }

    // Diagnostics periodiques dans la console serie
    static uint32_t s_lastDiagMs = 0;
    const uint32_t now = millis();
    if (now - s_lastDiagMs >= 3000) {
        s_lastDiagMs = now;
        Serial.printf("[DIAG] WiFi=%s (%s) | NTP=%s | VoiceState=%s | Ready=%d | Busy=%d | Spk=%d\n",
                      (WiFi.status() == WL_CONNECTED) ? "CONNECTE" : "DECONNECTE",
                      WiFi.localIP().toString().c_str(),
                      VoxCoreVoice::isSystemTimeValid() ? "OK" : "ATTENTE",
                      voiceStateToString(voice.getState()),
                      voice.isReady(),
                      voice.isBusy(),
                      voice.isSpeaking());
    }

    delay(5);
}
