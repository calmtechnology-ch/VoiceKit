# VoiceKit v0.2.1 — Bibliothèque Audio TTS pour M5Stack CoreS3

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![Arduino IDE](https://img.shields.io/badge/Arduino%20IDE-Compatible-00979C.svg)](https://www.arduino.cc/)
[![ESP32-S3](https://img.shields.io/badge/Target-M5Stack%20CoreS3%20(ESP32--S3)-blue.svg)](https://docs.m5stack.com/en/core/CoreS3)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Bibliothèque C++/Arduino installable de **synthèse vocale (Text-To-Speech / TTS)** haute fidélité pour **M5Stack CoreS3** (ESP32-S3), propulsée par l'API **Gemini Live** (protocole bidirectionnel BidiGenerateContent) et construite sur le moteur audio et réseau éprouvé de **[VoxCore](https://github.com/Philippe-AD/VoxCore)**.

---

## Sommaire

1. [Fonctionnalités Principales](#1-fonctionnalités-principales)
2. [Structure de la Bibliothèque](#2-structure-de-la-bibliothèque)
3. [Méthodes d'Installation](#3-méthodes-dinstallation)
   - [Méthode A — PlatformIO depuis GitHub](#méthode-a--platformio-depuis-github)
   - [Méthode B — Arduino IDE](#méthode-b--arduino-ide)
   - [Méthode C — Développement Local](#méthode-c--développement-local)
4. [Référence de l'API Publique (`VoxCoreVoice`)](#4-référence-de-lapi-publique-voxcorevoice)
5. [Exemples Inclus](#5-exemples-inclus)
6. [Garanties de Non-Régression & Sécurité](#6-garanties-de-non-régression--sécurité)

---

## 1. Fonctionnalités Principales

- **Zéro dépendance intrusive** : VoiceKit est headless. Il ne requiert ni LVGL, ni UIManager, et n'interfère pas avec l'écran ou les capteurs de l'hôte.
- **Sanctuaire Audio Mode 2 (FreeRTOS)** : 
  - Tâche audio dédiée sur le Core 1 (priorité 4).
  - Tampon circulaire en PSRAM de 120 000 échantillons 16 bits @ 24 kHz (~5 s de pré-tamponnage).
  - Quadruple buffer DMA (160 ms de marge acoustique).
  - Algorithme de vidage matériel exact : aucune coupure de syllabe en fin d'énonciation.
- **Sécurité TLS & Google PKI** :
  - Connexion WebSocket native `esp_websocket_client`.
  - Bundle officiel de 21 certificats Google Trust Services (`GoogleRoots.h`).
  - Validation temporelle SNTP préalable automatique.
- **Conditionnement standard** : Compatible PlatformIO Registry / Git et Arduino IDE 1.5+ specification.

---

## 2. Structure de la Bibliothèque

```text
VoiceKit/
├── library.json                 # Manifeste PlatformIO
├── library.properties           # Manifeste Arduino IDE
├── LICENSE                      # Licence MIT
├── README.md                    # Documentation complète
├── CHANGELOG.md                 # Historique des versions
├── src/                         # Sources et en-têtes (Standard Arduino 1.5)
│   ├── VoxCoreVoice.h           # En-tête public principal
│   ├── VoxCoreVoice.cpp         # Implémentation de la façade publique
│   ├── VoiceKitConfig.h         # Configuration du moteur
│   ├── core/                    # Moteur interne VoxCore
│   │   ├── GeminiLiveClient (.h/.cpp)
│   │   ├── LiveSessionController (.h/.cpp)
│   │   ├── LiveAudioPlayer (.h/.cpp)
│   │   ├── WiFiManager (.h/.cpp)
│   │   ├── AudioTraceLog (.h/.cpp)
│   │   └── GoogleRoots.h
│   └── hal/                     # Couche d'abstraction matérielle
│       ├── IAudioOutput.h
│       └── M5AudioOutput (.h/.cpp)
├── examples/                    # Exemples d'application
│   ├── MinimalTTS/              # Exemple PlatformIO sans LVGL
│   ├── OrientationClockVoice/   # Exemple PlatformIO horloge LVGL 9
│   └── MinimalTTS_Arduino/      # Exemple autonome Arduino IDE (.ino)
└── tests/
    └── external-consumer/       # Projet de validation externe sans copie locale
```

---

## 3. Méthodes d'Installation

### Méthode A — PlatformIO depuis GitHub

Cette méthode est recommandée pour les projets professionnels ou basés sur VS Code / PlatformIO.

#### Prérequis
- PlatformIO Core (CLI) ou extension PlatformIO dans Visual Studio Code.
- Câble USB-C connecté à un M5Stack CoreS3.

#### Configuration `platformio.ini`
Ajoutez VoiceKit dans `lib_deps` en ciblant la version Git :

```ini
[env:m5stack-cores3]
platform = espressif32@6.9.0
board = m5stack-cores3
framework = arduino
lib_ldf_mode = deep
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
    https://github.com/Philippe-AD/VoiceKit.git#v0.2.1
```

> **Note de partitionnement** : Le firmware VoiceKit et ses dépendances occupent environ 1,15 Mo. Utilisez une table de partitions comme `no_ota.csv` (fournie dans le dépôt) offrant au moins 2 à 3 Mo pour la partition application (`app0`).

#### Programme minimal (`src/main.cpp`)

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>

VoxCoreVoice voice;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    VoiceKitConfig config;
    config.apiKey = "VOTRE_CLE_API_GEMINI";
    config.wifiSSID = "VOTRE_SSID";
    config.wifiPassword = "VOTRE_MOT_DE_PASSE";
    config.voice = "Aoede";
    config.volume = 180;

    voice.begin(config);
}

void loop() {
    M5.update();
    voice.update();

    if (voice.isReady()) {
        static bool done = false;
        if (!done) {
            voice.say("Bonjour, VoiceKit fonctionne avec succès.");
            done = true;
        }
    }
}
```

#### Commande de compilation
```bash
pio run
# Pour téléverser et afficher la console :
pio run -t upload && pio device monitor -b 115200
```

#### Erreurs courantes & Solutions
- **`fatal error: VoxCoreVoice.h: No such file or directory`** : Vérifiez que `lib_ldf_mode = deep` est défini dans `platformio.ini`.
- **`Echec initialisation memoire LiveAudioPlayer`** : Le CoreS3 requiert la PSRAM active. Assurez-vous que le profil de carte est bien `m5stack-cores3`.

---

### Méthode B — Arduino IDE

Cette méthode permet d'utiliser VoiceKit avec l'environnement classique Arduino IDE 2.x.

#### Prérequis
- Arduino IDE 2.3+ avec le paquet de cartes **esp32** par Espressif (version 2.0.14+ ou 3.x).
- Télécharger l'archive ZIP de VoiceKit depuis GitHub :  
  `https://github.com/Philippe-AD/VoiceKit/archive/refs/tags/v0.2.1.zip`

#### Étapes d'installation
1. Ouvrez Arduino IDE.
2. Menu : **Croquis** → **Inclure une bibliothèque** → **Ajouter la bibliothèque .ZIP...**
3. Sélectionnez le fichier `VoiceKit-0.2.1.zip` téléchargé.
4. Ouvrez le Gestionnaire de bibliothèques (**Outils** → **Gérer les bibliothèques**) et installez :
   - `M5Unified` (version >= 0.2.23)
   - `M5CoreS3` (version >= 1.0.0)

#### Configuration indispensable de l'IDE
Dans le menu **Outils**, configurez impérativement :
- **Type de carte** : `M5Stack-CoreS3` (ou `ESP32S3 Dev Module`)
- **PSRAM** : `OPI PSRAM`  *(CRITIQUE : indispensable pour allouer le buffer audio 24 kHz en PSRAM)*
- **Partition Scheme** : `Huge APP (3MB No OTA)` ou `16M Flash (3MB APP/9.9MB FATFS)`
- **Flash Size** : `16MB (128Mb)`

#### Exemple prêt à l'emploi
Ouvrez directement dans Arduino IDE :  
**Fichier** → **Exemples** → **VoiceKit** → **MinimalTTS_Arduino** (ou `examples/MinimalTTS_Arduino/MinimalTTS_Arduino.ino`).

Renseignez votre SSID Wi-Fi et votre clé API Gemini, puis cliquez sur **Téléverser**.

---

### Méthode C — Développement Local

Cette méthode permet de modifier VoiceKit et de tester immédiatement vos évolutions dans une application hôte sans passer par GitHub.

#### Prérequis
- Dépôt VoiceKit cloné en local sur votre poste.
- Votre projet consommateur PlatformIO situé sur la même machine.

#### Configuration `platformio.ini`
Utilisez la directive `symlink://` de PlatformIO :

```ini
[env:m5stack-cores3]
platform = espressif32@6.9.0
board = m5stack-cores3
framework = arduino
lib_ldf_mode = deep
board_build.partitions = no_ota.csv
build_flags =
    -std=gnu++17
    -DVOXCORE_AUDIO_MODE=2
lib_deps =
    m5stack/M5CoreS3@1.0.0
    m5stack/M5Unified@0.2.23
    symlink://../../VoiceKit    # Chemin relatif vers la racine de VoiceKit
```

Grâce à la structure `src/` de VoiceKit v0.2.1, **aucun paramètre `-I../../include` n'est nécessaire**. PlatformIO découvre automatiquement les en-têtes et compile les sources du moteur.

---

## 4. Référence de l'API Publique (`VoxCoreVoice`)

### Méthodes Principales

| Méthode | Rôle | Caractère |
| :--- | :--- | :---: |
| `bool begin(const VoiceKitConfig& config)` | Démarre les pilotes audio, le Wi-Fi, la synchronisation SNTP et la session Gemini Live. | Asynchrone / Non bloquant |
| `bool say(const String& text)` | Met en file et transmet la phrase à synthétiser en streaming audio PCM 24 kHz. | **Non bloquant** |
| `void update()` | Traite la machine d'état et le flux WebSocket. À appeler à chaque itération de `loop()`. | **Non bloquant** (< 1 ms) |
| `void stop()` | Interrompt immédiatement l'audio en cours et purge le buffer PSRAM. | Immédiat |
| `bool isReady() const` | Indique si le réseau et la session Gemini sont prêts pour une nouvelle synthèse. | Non bloquant |
| `bool isBusy() const` | Indique si une requête ou une restitution audio est active. | Non bloquant |
| `bool isSpeaking() const` | Indique si des échantillons sont actuellement envoyés aux haut-parleurs via DMA. | Non bloquant |
| `void setVolume(uint8_t volume)` | Règle le volume sonore de 0 à 255. | Immédiat |

### Callbacks d'Événements

```cpp
voice.onStateChanged([](VoiceState state) {
    // Changement d'état (INITIALIZING, CONNECTING_WIFI, TIME_SYNC, READY, SPEAKING, ERROR)
});

voice.onReady([]() {
    // Prêt à recevoir une commande say()
});

voice.onSpeechStarted([]() {
    // Le premier échantillon sonore franchit le DMA
});

voice.onSpeechFinished([]() {
    // Garanti déclenché uniquement après vidage physique complet du haut-parleur
});

voice.onError([](const String& message) {
    // Diagnostic textuel précis en cas d'anomalie réseau ou API
});
```

---

## 5. Exemples Inclus

1. **`examples/MinimalTTS/`** (PlatformIO) :  
   Validation physique minimale sans LVGL. Démonstration de `voice.say()` au démarrage puis déclenchement tactile sur l'écran.
2. **`examples/OrientationClockVoice/`** (PlatformIO) :  
   Application complète d'horloge dynamique intégrant **LVGL 9**, accéléromètre **BMI270** (rotation 360°), horloge RTC **BM8563**, et annonce de l'heure en français sur appui tactile.
3. **`examples/MinimalTTS_Arduino/`** (Arduino IDE) :  
   Croquis autonome `.ino` prêt à l'emploi avec documentation pas-à-pas des menus Arduino IDE.

---

## 6. Garanties de Non-Régression & Sécurité

La version **v0.2.1** préserve strictement la totalité du moteur vocal validé sur matériel en **v0.2.0** :
- **WebSocket TLS & Poignée de main** : Identique (pile 14 Ko, `esp_websocket_client`).
- **Certificats officiels Google Trust Services** : Intacts (empreinte SHA-256 de `GoogleRoots.h` inchangée).
- **Lecteur Audio DMA & PSRAM** : Intact (`LiveAudioPlayer.cpp` et `M5AudioOutput.cpp` inchangés au bit près).
- **Zéro identifiant en clair** : Aucun secret, SSID Wi-Fi ni clé API n'est publié dans le dépôt.

Consultez le [Rapport de Livraison v0.2.1](RAPPORT_LIVRAISON_v0.2.1.md) pour les empreintes SHA-256 et les preuves de compilation.
