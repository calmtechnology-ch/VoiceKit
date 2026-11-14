# VoiceKit — Guide de Démarrage Rapide (Quick Start)
**Version :** v0.6.6.2  
**Cible :** M5Stack CoreS3 (ESP32-S3)

Ce guide pas à pas permet à tout développeur disposant d'un M5Stack CoreS3 de créer, compiler et téléverser son premier projet exploitant **VoiceKit** en moins de 10 minutes.

---

## 1. Matériel Requis

- **1× M5Stack CoreS3** (avec écran tactile 320 × 240, double microphone et haut-parleur intégrés).
- **1× Câble USB Type-C** supportant le transfert de données.
- **1× Réseau Wi-Fi 2.4 GHz** avec accès Internet (l'ESP32-S3 ne supporte pas le 5 GHz).
- **1× Clé d'API** Google Gemini Live (gratuite sur [Google AI Studio](https://aistudio.google.com/)) ou OpenAI Realtime.

---

## 2. Prérequis Logiciels

1. Installez **[Visual Studio Code](https://code.visualstudio.com/)**.
2. Installez l'extension **PlatformIO IDE** depuis l'onglet Extensions de VS Code.
3. Branchez votre M5Stack CoreS3 en USB : le pilote USB CDC est automatiquement reconnu (ex. `COM3` sous Windows, `/dev/ttyUSB0` sous Linux).

---

## 3. Création d'un Nouveau Projet PlatformIO

### Option A : Cloner directement le dépôt VoiceKit (Recommandé pour tester les exemples)

```bash
git clone https://github.com/Philippe-AD/VoiceKit.git
cd VoiceKit
```
Ouvrez le dossier dans VS Code. Vous pouvez compiler et flasher immédiatement n'importe quel exemple du dossier `examples/` (ex. `examples/MinimalTTS` ou `examples/IntegratedVoiceDemo`).

---

### Option B : Intégrer VoiceKit dans votre propre projet existant

1. Créez un nouveau projet PlatformIO avec la carte `m5stack-cores3`.
2. Configurez votre fichier `platformio.ini` comme suit :

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
    -DBOARD_M5STACK_CORES3=1
    -DVOXCORE_AUDIO_MODE=2
lib_deps =
    m5stack/M5CoreS3@1.0.0
    m5stack/M5Unified@0.2.23
    https://github.com/Philippe-AD/VoiceKit.git#v0.6.6.2
```

3. Téléchargez et placez le fichier de table de partition `no_ota.csv` (disponible à la racine du dépôt VoiceKit) dans le répertoire racine de votre projet.

---

## 4. Configuration des Identifiants Réseau et Clé API

Dans le sous-dossier `include/` de votre projet, créez le fichier `secrets.h` à partir du modèle :

```cpp
#pragma once

// Identifiants Wi-Fi (réseau 2.4 GHz obligatoire)
#define WIFI_SSID     "MonReseauWiFi"
#define WIFI_PASSWORD "MonMotDePasse"

// Clé API Google Gemini Live (depuis aistudio.google.com)
#define GEMINI_KEY    "AIzaSy..."

// Optionnel : Clé OpenAI si vous utilisez OpenAI Realtime
#define OPENAI_KEY    "sk-proj-..."
```

> **Attention sécurité :** Le fichier `secrets.h` ne doit jamais être commité sur un dépôt public. Ajoutez `secrets.h` à votre fichier `.gitignore`.

---

## 5. Votre Premier Code : Synthèse Vocale (TTS)

Écrivez le code suivant dans `src/main.cpp` :

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceKit.h>

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #error "Veuillez creer le fichier include/secrets.h a partir du modele"
#endif

static VoiceKit voice;

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
    vcfg.volume = 75;

    // Callbacks d'etat
    voice.onReady([]() {
        Serial.println("[VoiceKit] Pret ! Envoi de la phrase vocale...");
        M5.Display.fillScreen(TFT_DARKGREEN);
        M5.Display.drawString("Connecte et Pret !", 160, 120);
        voice.say("Bonjour ! Bienvenue sur VoiceKit pour M5Stack CoreS3.");
    });

    voice.onError([](const char* err) {
        Serial.printf("[VoiceKit Erreur] %s\n", err);
        M5.Display.fillScreen(TFT_RED);
        M5.Display.drawString("Erreur connexion", 160, 120);
    });

    if (!voice.begin(vcfg)) {
        Serial.println("[VoiceKit] Echec de demarrage");
    }
}

void loop() {
    M5.update();
    voice.update();
    delay(10);
}
```

---

## 6. Compilation et Téléversement

Dans le terminal PlatformIO de VS Code :

```bash
# Compilation
pio run

# Téléversement sur le CoreS3
pio run -t upload

# Ouverture du moniteur série (115 200 bauds)
pio device monitor -b 115200
```

---

## 7. Résultat Attendu

1. L'écran du M5Stack CoreS3 affiche d'abord `Connexion VoiceKit...`.
2. Le moniteur série affiche :
   - La bannière `VoxCore VoiceKit v0.6.6.2`.
   - La connexion au réseau Wi-Fi local avec attribution d'adresse IP en DHCP.
   - La négociation sécurisée TLS WebSocket avec Google Gemini Live.
   - Le message `[VoiceKit] Pret ! Envoi de la phrase vocale...`.
3. L'écran devient vert et affiche `Connecte et Pret !`.
4. Le haut-parleur du M5Stack CoreS3 énonce clairement : *« Bonjour ! Bienvenue sur VoiceKit pour M5Stack CoreS3. »*.

---

## 8. Dépannage Minimal

| Problème rencontré | Cause fréquente | Solution |
|---|---|---|
| `WiFi begin(): connect failed` | Réseau 5 GHz ou identifiants incorrects | Vérifiez que votre box diffuse un réseau 2.4 GHz et contrôlez `WIFI_SSID` / `WIFI_PASSWORD` dans `secrets.h`. |
| `Close code 1007` ou `Close code 1008` | Clé API invalide ou refusée | Vérifiez que votre clé d'API est active sur Google AI Studio sans espaces superflus. |
| Erreur de compilation `c++17` | Standard C++ par défaut trop ancien | Assurez-vous que `build_flags = -std=gnu++17` et `build_unflags = -std=gnu++11` sont présents dans votre `platformio.ini`. |
| Erreur `region 'app0' overflowed` | Partition Flash insuffisante | Utilisez `no_ota.csv` (2 Mo) pour les exemples simples, ou `partitions_16MB.csv` pour les exemples avec interface LVGL 9. |
| Pas de son audible | Volume à 0 ou amplificateur non alimenté | Vérifiez que `vcfg.volume` est réglé entre 60 et 85 et que la batterie ou l'alimentation USB fournit au moins 5V/1A. |

---

## 9. Pour Aller Plus Loin

Consultez les 11 exemples officiels du dossier `examples/` :
- **Dictée Push-to-Talk (STT) :** `examples/MinimalSTT`
- **Conversation Bidirectionnelle :** `examples/MinimalConversation`
- **Mot de Réveil Hors Ligne (« Bonjour ESP ») :** `examples/WakeWordFoundation`
- **Orchestration Complète & Commandes Locales :** `examples/IntegratedVoiceDemo`
- **Profils Vocaux & Interface Tactile LVGL 9 :** `examples/VoiceSettingsDemo`
