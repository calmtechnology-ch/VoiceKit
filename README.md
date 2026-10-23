# VoxCore VoiceKit v0.2.0

Bibliothèque C++/Arduino indépendante de **synthèse vocale (Text-To-Speech / TTS)** haute fidélité pour **M5Stack CoreS3** (ESP32-S3), construite directement sur la base technique stable et validée de **[VoxCore](https://github.com/Philippe-AD/VoxCore)**.

---

## 1. Philosophie & Principes de la v0.2.0

La série v0.1.0.x a rencontré des instabilités (handshake TLS `-0x2700`, épuisement de heap, découpage de trames non-standard) dues à des réécritures partielles.  
La version **v0.2.0** applique une règle stricte : **réutiliser les composants originaux éprouvés de VoxCore sans réécriture fonctionnelle**, tout en offrant une **API publique minimale et élégante**.

### Piliers d'Architecture :
1. **Composant Invité Strict & Headless** : VoiceKit ne crée aucune interface graphique intrusive, ne dépend ni de LVGL ni d'`UIManager`, et respecte la rotation d'affichage, les capteurs IMU (BMI270) et l'horloge RTC de l'application hôte.
2. **Sanctuaire Audio Mode 2 de VoxCore** : Pipeline audio issu de VoxCore v0.2.1.3.6 / v0.2.2.0 :
   - Tâche FreeRTOS dédiée sur le Core 1 (priorité 4).
   - Tampon circulaire PSRAM de 120 000 échantillons 16 bits @ 24 kHz (~5 s de réserve).
   - Quadruple buffer DMA (4 x 960 échantillons = 160 ms de marge acoustique).
   - Vidage matériel calculé (`getHardwareDrainMs()`) garantissant la fin acoustique réelle du dernier mot sans coupure.
3. **Protocole Gemini Live & Sécurité TLS** :
   - Client WebSocket natif `esp_websocket_client` sur pile de 14 Ko.
   - Bundle complet officiel de 21 certificats racines Google Trust Services (`GoogleRoots.h`).
   - Format standard BidiGenerateContent avec `"automaticActivityDetection": {"disabled": true}`.
   - Synchronisation SNTP préalable garantissant la validité temporelle des certificats (`time >= 1700000000`).
4. **API Publique Minimale** : Une façade unique (`VoxCoreVoice`) permettant de faire parler le CoreS3 en quelques lignes de code.

---

## 2. Architecture de la Bibliothèque

```text
VoiceKit v0.2.0
├── API publique
│   ├── VoxCoreVoice (.h / .cpp)
│   └── VoiceKitConfig (.h)
├── Moteur VoxCore original
│   ├── GeminiLiveClient (WebSocket TLS & BidiGenerateContent)
│   ├── LiveSessionController (Machine à états & turnComplete)
│   ├── LiveAudioPlayer (Mode 2 DMA FreeRTOS & tampon PSRAM)
│   ├── Audio HAL (M5AudioOutput sur M5.Speaker)
│   ├── AudioTraceLog (Journalisation temps réel sans allocation)
│   ├── WiFiManager (Connexion Wi-Fi robuste & SNTP)
│   └── GoogleRoots.h (21 certificats racines Google PKI)
└── Exemples
    ├── MinimalTTS (Validation physique pure, sans LVGL)
    └── OrientationClockVoice (Horloge dynamique CoreS3 avec LVGL 9)
```

---

## 3. Guide de Démarrage Rapide

### 3.1. Code Minimal

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>

VoxCoreVoice voice;

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = true;
    M5.begin(cfg);

    VoiceKitConfig config;
    config.apiKey = "VOTRE_CLE_API_GEMINI";
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = "MonWiFi";
    config.wifiPassword = "MonMotDePasse";
    config.volume = 70;

    // Callbacks optionnels
    voice.onReady([]() {
        Serial.println("Moteur vocal prêt !");
        voice.say("Bonjour, je suis VoiceKit.");
    });

    voice.onSpeechFinished([]() {
        Serial.println("Émission terminée jusqu'au dernier échantillon sonore.");
    });

    voice.begin(config);
}

void loop() {
    M5.update();
    voice.update();

    if (M5.Touch.getDetail().wasClicked()) {
        if (voice.isReady()) {
            voice.say("Vous venez de toucher l'écran.");
        }
    }

    delay(5);
}
```

---

## 4. Référence de l'API Publique (`VoxCoreVoice`)

### Méthodes Principales

| Méthode | Description | Bloquant ? |
| :--- | :--- | :---: |
| `bool begin(const VoiceKitConfig& config)` | Initialise les pilotes audio, le réseau et démarre la session Gemini Live. | Non |
| `bool say(const String& text)` | Envoie le texte pour synthèse vocale haute fidélité mot à mot. | **Non** |
| `void update()` | Traitement périodique indispensable à placer dans la boucle `loop()`. | **Non** (< 1 ms) |
| `void stop()` | Interrompt immédiatement l'audio en cours et purge les tampons. | Non |
| `bool isReady() const` | Indique si le moteur est connecté et prêt à recevoir une nouvelle phrase. | Non |
| `bool isBusy() const` | Indique si une requête, une synthèse ou une restitution est en cours. | Non |
| `bool isSpeaking() const` | Indique si des échantillons audio sont émis par le haut-parleur. | Non |
| `String getLastError() const` | Retourne la dernière erreur textuelle survenue. | Non |
| `void setVolume(uint8_t percent)` | Règle le volume sonore de 0 à 100 %. | Non |
| `uint8_t getVolume() const` | Retourne le volume courant configuré. | Non |

### Callbacks d'Événements

```cpp
voice.onReady([]() {
    // Déclenché lorsque le handshake TLS et setupComplete sont validés
});

voice.onSpeechStarted([]() {
    // Déclenché à la sortie du premier échantillon sonore par le DMA
});

voice.onSpeechFinished([]() {
    // Déclenché UNIQUEMENT après vidage complet de la mémoire DMA matérielle
});

voice.onError([](const String& err) {
    // Signalement des erreurs réseau, TLS ou Gemini
});
```

---

## 5. Exemples Inclus

### 5.1. MinimalTTS (`examples/MinimalTTS`)
Exemple autonome de référence, sans interface graphique lourde :
* Initialisation matérielle CoreS3.
* Connexion Wi-Fi et synchronisation NTP horloge.
* Établissement de la session Gemini Live sécurisée.
* Prononciation automatique de « Bonjour, je suis VoiceKit. ».
* Déclenchement tactile sur l'écran d'une suite de phrases de test (courte, 10s, 30s) ou interruption.

### 5.2. OrientationClockVoice (`examples/OrientationClockVoice`)
Intégration complète dans l'application CoreS3 Orientation Clock :
* Affichage minimaliste sous **LVGL 9**.
* Détection de rotation matérielle continue via l'accéléromètre **BMI270**.
* Horloge matérielle temps réel **BM8563 (RTC)**.
* Annonce de l'heure en français naturel sur toucher écran : `voice.say(formatTimeInFrench(hour, minute));`.

---

## 6. Procédure de Test et Validation Matérielle

Conformément à la méthodologie comparative, les composants sont testés sur le **M5Stack CoreS3** physique :

```powershell
# 1. Compiler et flasher MinimalTTS sur le CoreS3
pio run -d examples/MinimalTTS -t upload

# 2. Ouvrir la console série
pio device monitor -d examples/MinimalTTS -b 115200
```

Pour consulter l'analyse comparative détaillée et le rapport de conformité :
* [`COMPARAISON_VOXCORE_VOICEKIT.md`](COMPARAISON_VOXCORE_VOICEKIT.md)
* [`RAPPORT_LIVRAISON_v0.2.0.md`](RAPPORT_LIVRAISON_v0.2.0.md)
