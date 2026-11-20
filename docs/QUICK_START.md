# VoiceKit — Guide de Démarrage Rapide (Quick Start)

**Version :** VoiceKit v0.6.7.2  
**Matériel cible :** M5Stack CoreS3 (ESP32-S3)  
**Dépôt officiel :** https://github.com/calmtechnology-ch/VoiceKit

Ce guide vous permet de lancer une première démonstration vocale sur **M5Stack CoreS3**, soit à partir d'un exemple prêt à l'emploi (**Parcours A**, recommandé aux débutants), soit en intégrant VoiceKit dans votre propre projet PlatformIO (**Parcours B**).

---

## 1. Prérequis

### Matériel
- **1 × M5Stack CoreS3** (ESP32-S3, 16 Mo Flash, 8 Mo PSRAM, écran tactile, micros et haut-parleur intégrés).
- **1 × Câble USB Type-C** prenant en charge le transfert de données et fournissant au moins 5 V / 1 A.
- **1 × Réseau Wi-Fi 2.4 GHz** avec accès Internet (l'ESP32-S3 ne détecte pas les réseaux 5 GHz).
- **1 × Clé API** :
  - Google Gemini Live via [Google AI Studio](https://aistudio.google.com/), ou
  - OpenAI Realtime via [OpenAI Platform](https://platform.openai.com/).

### Logiciels
1. Installez **[Visual Studio Code](https://code.visualstudio.com/)**.
2. Installez l'extension **PlatformIO IDE** depuis le gestionnaire d'extensions de VS Code.
3. Installez **[Git](https://git-scm.com/)** si vous souhaitez cloner le dépôt.
4. Branchez le M5Stack CoreS3 en USB-C : le port série USB CDC est reconnu par le système (ex. `COM9` sous Windows, `/dev/ttyACM0` sous Linux, `/dev/cu.usbmodem*` sous macOS).

---

## 2. Parcours A — Tester un exemple fourni (Recommandé aux débutants)

Ce parcours utilise l'exemple le plus simple, `examples/MinimalTTS`, pour faire parler le M5Stack CoreS3.

### Étape 1 — Cloner le dépôt VoiceKit

```bash
git clone https://github.com/calmtechnology-ch/VoiceKit.git
cd VoiceKit
```

### Étape 2 — Ouvrir le dossier de l'exemple

Dans VS Code, ouvrez directement le dossier :

```text
VoiceKit/examples/MinimalTTS
```

*(Menu : **File → Open Folder...** puis sélectionnez `examples/MinimalTTS`.)*

### Étape 3 — Configurer le Wi-Fi et la clé API

Copiez le fichier modèle `include/secrets.example.h` vers `include/secrets.h` :

- **Sous Windows (PowerShell) :**
  ```powershell
  Copy-Item include\secrets.example.h include\secrets.h
  ```
- **Sous Linux / macOS :**
  ```bash
  cp include/secrets.example.h include/secrets.h
  ```

Ouvrez `examples/MinimalTTS/include/secrets.h` et renseignez vos valeurs :

```cpp
#pragma once

#define WIFI_SSID       "VOTRE_RESEAU_WIFI_2_4GHZ"
#define WIFI_PASSWORD   "VOTRE_MOT_DE_PASSE_WIFI"
#define GEMINI_API_KEY  "AIzaSy..."
```

> **Sécurité :** Le fichier `include/secrets.h` est ignoré par `.gitignore`. Ne le publiez jamais dans un dépôt Git.

### Étape 4 — Compiler et téléverser

Depuis un terminal ouvert dans `examples/MinimalTTS` (ou via les boutons PlatformIO en bas de VS Code) :

```bash
# Compiler le firmware
pio run

# Téléverser sur le M5Stack CoreS3
pio run -t upload

# Ouvrir le moniteur série (115 200 bauds)
pio device monitor -b 115200
```

### Étape 5 — Vérifier le résultat sur le CoreS3

1. Le moniteur série affiche la bannière `VoxCore VoiceKit v0.6.7.2`, la connexion au Wi-Fi et l'établissement de la session Gemini Live.
2. L'écran du M5Stack CoreS3 affiche l'état de connexion puis passe à l'état prêt.
3. Le haut-parleur du CoreS3 prononce la phrase de démonstration.

Pour passer ensuite à une vraie conversation tactile, poursuivez avec `examples/MinimalConversation` ou `examples/TwoClickConversationLVGL` (voir **[examples/README.md](../examples/README.md)**).

---

## 3. Parcours B — Utiliser VoiceKit dans un projet personnel

Ce parcours crée un projet PlatformIO autonome, indépendant du dépôt VoiceKit (sans `symlink://`).

### Étape 1 — Créer l'arborescence du projet

Créez un dossier `MonProjetVoiceKit/` contenant :

```text
MonProjetVoiceKit/
├── platformio.ini
├── no_ota.csv
├── .gitignore
├── include/
│   └── secrets.h
└── src/
    └── main.cpp
```

Copiez le fichier `no_ota.csv` depuis la racine du dépôt VoiceKit dans `MonProjetVoiceKit/no_ota.csv`, et ajoutez `include/secrets.h` dans `.gitignore`.

### Étape 2 — Configurer `platformio.ini`

```ini
[env:m5stack-cores3]
platform = espressif32@6.9.0
board = m5stack-cores3
framework = arduino
lib_ldf_mode = deep
board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
board_build.flash_mode = dio
monitor_speed = 115200
upload_speed = 460800
board_build.partitions = no_ota.csv
build_unflags = -std=gnu++11
build_flags =
    -std=gnu++17
    -DCORE_DEBUG_LEVEL=1
    -DARDUINO_RUNNING_CORE=1
    -DARDUINO_EVENT_RUNNING_CORE=1
    -DBOARD_M5STACK_CORES3=1
    -DVOXCORE_AUDIO_MODE=2
lib_deps =
    m5stack/M5CoreS3@1.0.0
    m5stack/M5Unified@0.2.23
    ; Pour le code de développement actuel (avant création du tag v0.6.7.2) :
    https://github.com/calmtechnology-ch/VoiceKit.git#main
    ; Pour la future release officielle une fois le tag publié :
    ; https://github.com/calmtechnology-ch/VoiceKit.git#v0.6.7.2
```

> **Important :** Le tag Git `v0.6.7.2` n'est disponible qu'après la publication officielle de la release. Avant sa création, utilisez la branche `#main` (ou une copie locale de VoiceKit dans `lib/VoiceKit/`).

### Étape 3 — Créer `include/secrets.h`

```cpp
#pragma once

#define WIFI_SSID     "VOTRE_RESEAU_WIFI_2_4GHZ"
#define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE_WIFI"

// Clé API Google Gemini Live (https://aistudio.google.com/)
#define GEMINI_KEY    "AIzaSy..."

// Optionnel : clé OpenAI Realtime si vous utilisez VoiceProviderType::OPENAI
#define OPENAI_KEY    "sk-proj-..."
```

### Étape 4 — Écrire `src/main.cpp` (API publique `<VoiceKit.h>`)

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceKit.h>

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #error "Veuillez creer include/secrets.h avec vos identifiants Wi-Fi et votre cle API."
#endif

static VoiceKit voice;
static bool welcomeSpoken = false;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("Connexion VoiceKit...", 160, 120);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- Demarrage VoiceKit Quick Start ---");

    VoiceKitConfig vcfg;
    vcfg.provider = VoiceProviderType::GEMINI;
    vcfg.apiKey = GEMINI_KEY;
    vcfg.wifiMode = WiFiMode::MANAGED_WIFI;
    vcfg.wifiSSID = WIFI_SSID;
    vcfg.wifiPassword = WIFI_PASSWORD;
    vcfg.voice = "Aoede";
    vcfg.volume = 75;

    voice.onReady([]() {
        Serial.println("[VoiceKit] Pret !");
        M5.Display.fillScreen(TFT_DARKGREEN);
        M5.Display.drawString("Connecte et Pret !", 160, 120);
        if (!welcomeSpoken) {
            welcomeSpoken = true;
            voice.say("Bonjour ! Bienvenue sur VoiceKit pour M5Stack CoreS3.");
        }
    });

    voice.onError([](const String& err) {
        Serial.printf("[VoiceKit Erreur] %s\n", err.c_str());
        M5.Display.fillScreen(TFT_RED);
        M5.Display.drawString("Erreur VoiceKit", 160, 120);
    });

    if (!voice.begin(vcfg)) {
        Serial.println("[VoiceKit] Echec de voice.begin()");
    }
}

void loop() {
    M5.update();
    voice.update();
    delay(10);
}
```

### Étape 5 — Compiler et téléverser

```bash
cd MonProjetVoiceKit
pio run
pio run -t upload
pio device monitor -b 115200
```

---

## 4. Dépannage des erreurs courantes

| Symptôme | Cause probable | Action corrective |
|---|---|---|
| **Échec Wi-Fi** (`WiFi begin(): connect failed`) | Réseau 5 GHz, SSID ou mot de passe erroné | Connectez le CoreS3 à un réseau **2.4 GHz** et vérifiez `WIFI_SSID` / `WIFI_PASSWORD` dans `include/secrets.h`. |
| **Clé API absente ou invalide** (`Close code 1007`, `1008`, `401`) | Clé manquante, révoquée ou contenant un espace | Vérifiez votre clé sur Google AI Studio ou OpenAI Platform et assurez-vous qu'elle correspond au `vcfg.provider` choisi. |
| **Erreur de compilation C++17** | Drapeau `-std=gnu++11` toujours actif | Vérifiez la présence de `build_unflags = -std=gnu++11` et `-std=gnu++17` dans `build_flags` de `platformio.ini`. |
| **Port USB incorrect ou introuvable** | Câble charge seule, mauvais port ou autre terminal ouvert | Utilisez un câble USB-C données, fermez tout autre moniteur série, listez les ports avec `pio device list`, puis passez `--upload-port COMx` si besoin. |
| **Partition Flash insuffisante** (`region 'app0' overflowed`) | Table de partitions par défaut trop petite | Utilisez `board_build.partitions = no_ota.csv` (2 Mo) pour les projets simples, ou `partitions_16MB.csv` (8 Mo) dès que vous utilisez LVGL 9. |
| **Absence de réponse du fournisseur** | Pare-feu, quota API dépassé ou modèle indisponible | Vérifiez la connexion Internet, le solde/quota API et les traces série `[LiveSession]` / `[Gemini]` / `[OpenAIClient]`. |
| **Microphone ou haut-parleur indisponible** | Volume à 0, alimentation USB trop faible ou bus audio occupé | Réglez `vcfg.volume` entre 60 et 85, utilisez une alimentation USB stable (≥ 5 V / 1 A) et évitez de réinitialiser `M5.Mic` / `M5.Speaker` manuellement après `voice.begin()`. |

---

## 5. Étapes suivantes

- Parcourez le **[catalogue des 12 exemples](../examples/README.md)** (de `MinimalTTS` à `IntegratedVoiceDemo`).
- Découvrez les **[applications showcases](../showcases/README.md)** (`VoiceAssistant`, `VoiceSensorStation`, `VoiceControl`).
- Consultez les **[limitations connues](KNOWN_LIMITATIONS.md)** et la **[politique de sécurité](../SECURITY.md)**.
