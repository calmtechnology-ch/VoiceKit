# VoiceKit v0.6.0 — Architecture Multi-Fournisseurs (Gemini Live & OpenAI Realtime)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![Arduino IDE](https://img.shields.io/badge/Arduino%20IDE-Compatible-00979C.svg)](https://www.arduino.cc/)
[![ESP32-S3](https://img.shields.io/badge/Target-M5Stack%20CoreS3%20(ESP32--S3)-blue.svg)](https://docs.m5stack.com/en/core/CoreS3)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Bibliothèque C++/Arduino installable de **synthèse vocale (Text-To-Speech / TTS)**, de **reconnaissance vocale (Speech-To-Text / STT)**, de **dictée vocale continue mains libres (ContinuousSTT)** et de **conversation vocale bidirectionnelle native (Mode B)** haute fidélité pour **M5Stack CoreS3** (ESP32-S3).

Propulsée par une **architecture multi-fournisseurs unifiée (`IVoiceProvider`)** supportant à la fois **Google Gemini Live** (moteur par défaut, 100% rétrocompatible) et **OpenAI Realtime API** (`wss://api.openai.com/v1/realtime`), construite sur le moteur audio et réseau éprouvé de **[VoxCore](https://github.com/Philippe-AD/VoxCore)** et **[VoxTranscribe](https://github.com/Philippe-AD/VoxTranscribe)**.

La version **v0.6.0** fait évoluer VoiceKit vers un framework vocal universel :
- **Architecture Multi-Fournisseurs (`IVoiceProvider`)** : Interface pure unifiée pour l'authentification, la négociation de session, le streaming audio bidirectionnel, les transcriptions STT/TTS et les interruptions STOP.
- **Intégration Native OpenAI Realtime** : Client WebSocket TLS direct ESP32-S3 (`OpenAIRealtimeClient`) avec support du modèle `gpt-realtime-2.1-mini`, authentification Bearer, négociation `session.update`, streaming PCM 24 kHz Base64 et transcriptions Whisper intégrées.
- **Sélection par Configuration Zéro-Code** : Bascule instantanée entre Gemini et OpenAI par simple configuration (`config.provider = VoiceProviderType::OPENAI;`). Aucune modification requise dans le code métier de l'application !
- **Rééchantillonneur Dédié 16 kHz $\to$ 24 kHz (`AudioResampler`)** : Algorithme linéaire ultra-rapide en virgule fixe entière (512 in $\to$ 768 out en moins de 15 µs sur Core 1), n'impactant aucunement la tâche audio critique.
- **Sanctuaire Audio VoxCore Intact** : Conservation intégrale de la tâche FreeRTOS Mode 2, du buffer circulaire PSRAM 120 000 échantillons (~5 s), des buffers DMA et du protocole de vidage exact sans coupure de la syllabe finale (*« alpha tango terminé »*).
- **Nouvel Exemple `OpenAIConversation`** : Démonstration headless complète de conversation vocale bidirectionnelle en français avec OpenAI Realtime.
- **Validation Exhaustive & Zéro Régression** : **1 047 tests unitaires réussis avec 0 échec** (79 suites de tests) et 100% des exemples PlatformIO compilés avec succès.

---

## Sommaire

- [**Apprendre VoiceKit — Tutoriel progressif**](#apprendre-voicekit--tutoriel-progressif)
1. [Fonctionnalités Principales](#1-fonctionnalités-principales)
2. [Architecture Multi-Fournisseurs](#2-architecture-multi-fournisseurs)
3. [Structure de la Bibliothèque](#3-structure-de-la-bibliothèque)
4. [Méthodes d'Installation](#4-méthodes-dinstallation)
   - [Méthode A — PlatformIO depuis GitHub](#méthode-a--platformio-depuis-github)
   - [Méthode B — Arduino IDE](#méthode-b--arduino-ide)
   - [Méthode C — Développement Local](#méthode-c--développement-local)
5. [Configuration & Choix du Fournisseur](#5-configuration--choix-du-fournisseur)
6. [Nouvelle API Simplifiée (`VoiceKit.h`)](#6-nouvelle-api-simplifiée-voicekith)
7. [Contrôleur Réutilisable `ContinuousListeningController`](#7-contrôleur-réutilisable-continuouslisteningcontroller)
8. [Référence de l'API Historique (`VoxCoreVoice`)](#8-référence-de-lapi-historique-voxcorevoice)
9. [Exemples Inclus](#9-exemples-inclus)
10. [Garanties de Non-Régression & Sécurité](#10-garanties-de-non-régression--sécurité)

---

## Apprendre VoiceKit — Tutoriel progressif

Vous découvrez VoiceKit ou la programmation vocale sur ESP32-S3 ? Un **[parcours d'apprentissage progressif en français](tutorials/README.md)** est disponible dans le dossier `tutorials/`.

Conçu selon le principe de *la simplicité avant la sophistication*, il vous guide pas à pas à travers 12 leçons thématiques, depuis l'installation et votre premier programme jusqu'à la synthèse vocale, la reconnaissance vocale et le dialogue interactif :
- **[Leçon 00 — Préparer son environnement VoiceKit](tutorials/00-Installation/README.md)** : Prise en main du CoreS3, installation de PlatformIO, Wi-Fi et clé API Gemini Live.
- **[Leçon 01 — Mon premier programme VoiceKit](tutorials/01-HelloVoiceKit/README.md)** : Initialisation de la façade `VoiceKit`, configuration et connexion à Gemini Live.
- **[Leçon 02 — Faire parler le CoreS3](tutorials/02-TextToSpeech/README.md)** : Synthèse vocale asynchrone (`say`), suivi d'événements (`onSpeechStarted`, `onSpeechFinished`).
- **[Leçon 03 — Contrôler la synthèse vocale](tutorials/03-SpeechControl/README.md)** : Pilotage interactif série, volume sonore (0-100 %), arrêt d'urgence (`stop`) et diagnostic.

Consultez le **[sommaire complet du parcours (Leçons 00 à 11)](tutorials/README.md)** pour découvrir l'ensemble des leçons.

---

## 1. Fonctionnalités Principales

- **Zéro dépendance intrusive** : VoiceKit est headless. Il ne requiert ni LVGL, ni UIManager, et n'interfère pas avec l'écran ou les capteurs de l'hôte.
- **Configuration Vocale Complète (v0.4.1)** :
  - **Voix Gemini** : Sélection parmi les voix officielles (ex. `Aoede`, `Puck`, `Charon`, `Fenrir`, `Kore`).
  - **Langue BCP-47** : Paramétrable (`fr-FR` par défaut, `en-US`, `de-DE`, etc.). Orientation intelligente : instruction système systématique, plus paramètre de setup `speechConfig.languageCode` si le modèle le supporte.
  - **Styles d'élocution** : `natural`, `professional`, `expressive`, `narrative`.
  - **Tonalités expressives** : `neutral`, `calm`, `warm`, `cheerful`, `serious`.
  - **Accentuation optionnelle** : Directives de prononciation transmises sans altérer les instructions métier (TTS, STT, conversation).
  - **Reconnexion sécurisée** : Changement de profil dynamique appliqué uniquement à chaud lorsque le moteur est au repos (`!isBusy()`).
- **Chaîne UTF-8 & Français Intégrale (v0.4.1)** :
  - Préservation stricte de tous les caractères accentués français (`é`, `è`, `ê`, `ë`, `à`, `â`, `ù`, `û`, `î`, `ï`, `ô`, `ö`, `ç`, `É`, `À`, `Ç`).
  - Prise en charge des ligatures (`œ`, `æ`) et ponctuations typographiques (`’`, `«`, `»`).
  - Zéro translittération, suppression ou altération d'accent lors du transport audio et des requêtes/réponses Gemini.
  - Validation UTF-8 conforme RFC 3629 et échappement JSON centralisé conforme RFC 8259 (`Utf8Utils`).
- **Conversation Vocale Bidirectionnelle Native (Mode B)** :
  - Flux continu sans latence : Micro ES7210 (PCM 16 kHz) $\rightarrow$ Gemini Live $\rightarrow$ Haut-parleur AW88298 (PCM 24 kHz).
  - Élimination des allers-retours textuels superflus : Gemini génère directement sa réponse vocale native en streaming audio.
  - Coordination légère de session via `ConversationController` : gestion fine des tours sans second transport WebSocket ni duplication de tâches.
  - Callbacks de transcription intégrés pour afficher à la fois la question prononcée (`inputTranscription`) et la réponse de l'assistant (`modelTurn`).
- **Sanctuaire Audio TTS Mode 2 (FreeRTOS)** : 
  - Tâche audio dédiée sur le Core 1 (priorité 4).
  - Tampon circulaire en PSRAM de 120 000 échantillons 16 bits @ 24 kHz (~5 s de pré-tamponnage).
  - Quadruple buffer DMA (160 ms de marge acoustique).
  - Algorithme de vidage matériel exact : aucune coupure de syllabe en fin d'énonciation.
- **Reconnaissance Vocale STT Haute Fidélité (PCM 16 kHz)** :
  - Capture matérielle microphone CoreS3 (double micro ES7210).
  - Double-buffering ping-pong DMA 512 échantillons @ 16 kHz.
  - Streaming audio asynchrone découplé vers Gemini Live avec signalisation officielle `activityStart` / `activityEnd`.
  - Transcriptions partielles (interim) et finales avec flag `isFinal`.
  - Règle stricte anti-hallucination : aucune invention de transcription finale à partir de fragments partiels ; gestion d'un timeout de silence paramétrable.
  - Arbitrage matériel automatique : coupure automatique du haut-parleur pendant l'écoute pour éliminer le larsen.
- **Sécurité TLS & Google PKI** :
  - Connexion WebSocket native `esp_websocket_client`.
  - Bundle officiel de 21 certificats Google Trust Services (`GoogleRoots.h`).
  - Validation temporelle SNTP préalable automatique.
- **Conditionnement standard** : Compatible PlatformIO Registry / Git et Arduino IDE 1.5+ specification.

---

## 2. Architecture Multi-Fournisseurs

VoiceKit v0.6.0 introduit une architecture ouverte découplée reposant sur l'interface pure `IVoiceProvider` :

```text
                        ┌───────────────────────────────────────────────┐
                        │      Application Utilisateur / Exemples        │
                        └───────────────────────┬───────────────────────┘
                                                │
                                    ┌───────────▼───────────┐
                                    │   VoiceKit / VoxCore   │
                                    └─────┬───────────┬─────┘
                                          │           │
                 ┌────────────────────────┘           └────────────────────────┐
                 ▼                                                             ▼
    ┌─────────────────────────┐                                   ┌─────────────────────────┐
    │   GeminiVoiceProvider   │                                   │   OpenAIRealtimeClient  │
    │   (Google Gemini Live)  │                                   │  (OpenAI Realtime API)  │
    └────────────┬────────────┘                                   └────────────┬────────────┘
                 │ PCM 16k in / 24k out                                        │ PCM 24k in / 24k out
                 │                                                ┌────────────▼────────────┐
                 │                                                │      AudioResampler     │
                 │                                                │   (16 kHz -> 24 kHz)    │
                 │                                                └────────────┬────────────┘
                 ▼                                                             ▼
    ┌─────────────────────────┐                                   ┌─────────────────────────┐
    │  Google Live Transport  │                                   │  OpenAI TLS Transport   │
    │  (wss://generativelanguage...)                              │  (wss://api.openai.com) │
    └─────────────────────────┘                                   └─────────────────────────┘
```

- **`IVoiceProvider`** : Contrat standardisé pour la connexion, l'authentification Bearer, la configuration de session, l'envoi de chunks audio PCM, la réception de flux audio descendants, la transmission de texte, l'interruption STOP et les callbacks d'événements.
- **`GeminiVoiceProvider`** : Adaptateur sans surcoût enveloppant le client `GeminiLiveClient` existant (100% de rétrocompatibilité).
- **`OpenAIRealtimeClient`** : Client natif WebSocket TLS direct pour `wss://api.openai.com/v1/realtime`, gestion des trames `session.update`, Base64 streaming, et transcriptions Whisper.
- **`AudioResampler`** : Algorithme linéaire ultra-rapide à virgule fixe entière convertissant les blocs 16 kHz du micro CoreS3 vers les 24 kHz requis par OpenAI, en moins de 15 µs par bloc, sans blocage de la boucle audio.

---

## 3. Structure de la Bibliothèque

```text
VoiceKit/
├── library.json                 # Manifeste PlatformIO (v0.6.0)
├── library.properties           # Manifeste Arduino IDE (v0.6.0)
├── LICENSE                      # Licence MIT
├── README.md                    # Documentation complète
├── CHANGELOG.md                 # Historique des versions
├── src/                         # Sources et en-têtes (Standard Arduino 1.5)
│   ├── VoiceKit.h               # Façade simplifiée unifiée (TTS, STT, Continuous, Conversation)
│   ├── VoiceKit.cpp             # Implémentation de la façade simplifiée
│   ├── VoxCoreVoice.h           # En-tête public historique préservé
│   ├── VoxCoreVoice.cpp         # Moteur principal VoiceKit / VoxCore
│   ├── VoiceKitConfig.h         # Configuration, choix du fournisseur & profils vocaux
│   ├── Utf8Utils.h              # Utilitaires UTF-8 & échappement JSON RFC 8259
│   ├── core/                    # Moteur interne VoxCore & Fournisseurs
│   │   ├── IVoiceProvider.h               # Interface unifiée des fournisseurs vocaux
│   │   ├── GeminiVoiceProvider (.h/.cpp)  # Adaptateur Gemini Live -> IVoiceProvider
│   │   ├── OpenAIRealtimeClient (.h/.cpp) # Client natif OpenAI Realtime WebSocket TLS
│   │   ├── AudioResampler (.h/.cpp)       # Rééchantillonneur linéaire 16k -> 24k
│   │   ├── OpenAIRoots.h                  # Bundle certificats TLS DigiCert pour OpenAI
│   │   ├── GoogleRoots.h                  # Bundle certificats Google Trust Services
│   │   ├── ContinuousListeningController (.h/.cpp) # Contrôleur autonome de dictée continue
│   │   ├── VoiceActivityDetector.h        # Détecteur VAD local 16 kHz
│   │   ├── GeminiLiveClient (.h/.cpp)     # Client WebSocket BidiGenerateContent
│   │   ├── LiveSessionController (.h/.cpp)# Automate de session
│   │   ├── ConversationController (.h/.cpp)# Contrôleur de conversation bidirectionnelle
│   │   ├── LiveAudioPlayer (.h/.cpp)      # Lecteur DMA PCM 24 kHz (TTS / Réponse)
│   │   ├── LiveAudioStreamer (.h/.cpp)    # Streamer micro PCM 16 kHz (STT / Entrée)
│   │   ├── WiFiManager (.h/.cpp)          # Gestionnaire Wi-Fi & SNTP
│   │   ├── Utf8Utils (.h/.cpp)            # Validation & sanitisation UTF-8 RFC 3629
│   │   └── AudioTraceLog (.h/.cpp)        # Journaliseur d'événements audio
│   └── hal/                     # Couche d'abstraction matérielle
│       ├── IAudioOutput.h                 # Interface de sortie audio
│       ├── IAudioInput.h                  # Interface d'entrée microphone
│       ├── M5AudioOutput (.h/.cpp)        # HAL haut-parleur M5.Speaker
│       ├── M5AudioInput (.h/.cpp)         # HAL microphone M5.Mic (ES7210)
│       └── HardwareManager.h              # Arbitrage I2S centralisé HP / Micro
├── examples/                    # Exemples d'application (10 exemples)
│   ├── OpenAIConversation/      # NOUVEAU v0.6.0 : Conversation avec OpenAI Realtime
│   ├── VoiceDiagnostics/        # Outil officiel de diagnostic (6 onglets dont LATENCY) & CSV
│   ├── MinimalVoiceKit/         # Démonstrateur façade simplifiée multi-modes
│   ├── ContinuousSTT/           # Dictée continue mains libres avec interface M5Unified
│   ├── VoiceSettingsDemo/       # Démonstrateur tactile profils & UTF-8
│   ├── MinimalConversation/     # Application conversation bidirectionnelle (Gemini Mode B)
│   ├── MinimalTTS/              # Exemple PlatformIO TTS sans LVGL
│   ├── OrientationClockVoice/   # Exemple PlatformIO horloge LVGL 9 + TTS
│   ├── MinimalSTT/              # Exemple PlatformIO dictée Push-to-Talk
│   └── MinimalTTS_Arduino/      # Exemple autonome Arduino IDE (.ino)
└── tests/
    ├── run_tests.bat            # Lanceur des tests automatisés MinGW
    └── test_voicekit_all.cpp    # 79 suites de tests unitaires (1 047 validations, 0 échec)
```

---

## 4. Méthodes d'Installation

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
    https://github.com/Philippe-AD/VoiceKit.git#v0.6.0
```

> **Note de partitionnement** : Le firmware VoiceKit et ses dépendances occupent environ 1,15 à 1,5 Mo. Utilisez une table de partitions comme `no_ota.csv` (fournie dans le dépôt) offrant au moins 2 à 3 Mo pour la partition application (`app0`).

#### Programme minimal (`src/main.cpp`)

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceKit.h>

VoiceKit voice;

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

    voice.onReady([]() {
        Serial.println("VoiceKit est prêt !");
    });

    voice.onSpeechFinished([]() {
        Serial.println("Élocution terminée.");
    });
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
  `https://github.com/Philippe-AD/VoiceKit/archive/refs/tags/v0.6.0.zip`

#### Étapes d'installation
1. Ouvrez Arduino IDE.
2. Menu : **Croquis** → **Inclure une bibliothèque** → **Ajouter la bibliothèque .ZIP...**
3. Sélectionnez le fichier `VoiceKit-0.6.0.zip` téléchargé.
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

Grâce à la structure `src/` conforme au standard Arduino 1.5, **aucun paramètre `-I../../include` n'est nécessaire**. PlatformIO découvre automatiquement les en-têtes et compile les sources du moteur.

---

## 5. Configuration & Choix du Fournisseur

VoiceKit permet de sélectionner le fournisseur vocal désiré directement via `VoiceKitConfig` :

### 5.1 Configuration Google Gemini Live (Fournisseur par défaut)
```cpp
VoiceKitConfig config;
config.provider = VoiceProviderType::GEMINI; // Optionnel : GEMINI est la valeur par défaut
config.apiKey = GEMINI_API_KEY;
config.model = "models/gemini-3.8-live";      // Modèle officiel Gemini Live
config.voice = "Aoede";                      // Aoede, Puck, Charon, Fenrir, Kore
config.language = "fr-FR";
config.wifiSSID = WIFI_SSID;
config.wifiPassword = WIFI_PASS;
```

### 5.2 Configuration OpenAI Realtime API
```cpp
VoiceKitConfig config;
config.provider = VoiceProviderType::OPENAI; // Sélection OpenAI Realtime
config.apiKey = OPENAI_API_KEY;              // Clé secrète sk-...
config.model = "gpt-realtime-2.1-mini";       // Modèle officiel Realtime
config.voice = "alloy";                      // alloy, echo, shimmer, etc.
config.language = "fr-FR";                   // Instructions et transcriptions en français
config.wifiSSID = WIFI_SSID;
config.wifiPassword = WIFI_PASS;
```

> **Sécurité absolue des clés** : Ne committez jamais vos clés secrètes d'API dans votre dépôt Git. Utilisez un fichier séparé `secrets.h` ignoré par `.gitignore`. VoiceKit masque automatiquement toutes les clés dans les journaux console.

---

## 6. Nouvelle API Simplifiée (`VoiceKit.h`)

La version **v0.5.0** introduit une façade unifiée et haut niveau optionnelle `VoiceKit`, accessible via `#include <VoiceKit.h>`. Elle permet de piloter l'intégralité des fonctionnalités vocales à l'aide d'un unique objet :

```cpp
#include <VoiceKit.h>

VoiceKit voice;
```

### 6.1 Vue d'ensemble des méthodes

| Méthode | Description | Retour | Caractère |
| :--- | :--- | :---: | :---: |
| `bool begin(config)` | Initialise le matériel, le Wi-Fi, SNTP et la session du fournisseur choisi. | `bool` | Asynchrone / Non bloquant |
| `void update()` | Traite la machine d'état et les contrôleurs audio. À appeler dans `loop()`. | `void` | Non bloquant (< 1 ms) |
| `bool say(text)` | Déclenche la synthèse vocale (TTS) de la phrase donnée. | `bool` | Non bloquant |
| `bool listen()` | Démarre la reconnaissance vocale manuelle (STT Push-to-Talk). | `bool` | Non bloquant |
| `bool stopListening()` | Arrête l'acquisition micro et attend la transcription finale. | `bool` | Non bloquant |
| `void cancelListening()` | Annule immédiatement l'écoute sans attendre de transcription. | `void` | Immédiat |
| `bool startContinuousListening()` | Active la dictée continue mains libres avec détection automatique de fin de parole. | `bool` | Non bloquant |
| `bool stopContinuousListening()` | Désactive la dictée continue et replace la façade en mode `IDLE`. | `bool` | Immédiat |
| `bool pauseContinuousListening()` | Met en pause temporaire la dictée continue. | `bool` | Immédiat |
| `bool resumeContinuousListening()` | Reprend la dictée continue après une pause. | `bool` | Non bloquant |
| `bool startConversation()` | Active le mode conversationnel bidirectionnel natif (Gemini Live ou OpenAI Realtime). | `bool` | Sécurisé / Non bloquant |
| `bool stopConversation()` | Désactive le mode conversationnel et replace la façade en mode `IDLE`. | `bool` | Immédiat |
| `void stop()` | Interrompt immédiatement toute opération en cours (TTS, écoute, conversation). | `void` | Immédiat |
| `bool isReady() const` | Indique si le système est prêt à recevoir une commande. | `bool` | Non bloquant |
| `bool isBusy() const` | Indique si une opération vocale (son, micro, réseau) est en cours. | `bool` | Non bloquant |
| `bool isSpeaking() const` | Indique si le haut-parleur restitue du son. | `bool` | Non bloquant |
| `bool isListening() const` | Indique si le microphone est en cours d'enregistrement. | `bool` | Non bloquant |
| `bool isTranscribing() const` | Indique que le micro est coupé et que la transcription finale est en attente. | `bool` | Non bloquant |
| `bool isContinuousListening() const` | Indique si le mode dictée continue est actif. | `bool` | Non bloquant |
| `bool isContinuousListeningPaused() const`| Indique si la dictée continue est en pause. | `bool` | Non bloquant |
| `bool isConversationActive() const` | Indique si le mode conversationnel est actif. | `bool` | Non bloquant |
| `VoiceState getState() const` | Retourne l'état opérationnel interne du moteur VoxCore. | `VoiceState` | Non bloquant |
| `VoiceKitMode getMode() const` | Retourne le mode de la façade (`IDLE`, `CONTINUOUS_LISTENING`, `CONVERSATION`). | `VoiceKitMode` | Non bloquant |
| `const ConnectionMetrics& getConnectionMetrics() const` | Retourne les métriques et temps de connexion mesurés (Wi-Fi, SNTP, WS, Setup, Total READY). | `const ConnectionMetrics&` | Non bloquant |
| `const VoiceLatencyMetrics& getLatencyMetrics() const` | Retourne les métriques de latence vocale détaillées du dernier tour vocal (7 jalons, 6 durées). | `const VoiceLatencyMetrics&` | Non bloquant |
| `void setVadSilenceDurationMs(uint32_t ms)` | Configure le seuil de silence VAD local (800, 1000, 1200 ms). Alias : `setSilenceDurationMs()`. | `void` | Immédiat |
| `uint32_t getVadSilenceDurationMs() const` | Retourne le seuil de silence VAD actuel en ms. Alias : `getSilenceDurationMs()`. | `uint32_t` | Non bloquant |
| `String getLastError() const` | Retourne le dernier message d'erreur survenu. | `String` | Non bloquant |

### 6.2 Événements & Callbacks Unifiés

Les callbacks de la façade `VoiceKit` sont exécutés dans le contexte de la boucle principale `loop()` lors des appels à `voice.update()`, garantissant l'absence de blocage ou d'accès concurrent :

```cpp
// 1. Session prête
voice.onReady([]() {
    Serial.println("[EVENT] VoiceKit est prêt");
});

// 2. Erreur survenue
voice.onError([](const String& err) {
    Serial.printf("[EVENT] Erreur : %s\n", err.c_str());
});

// 3. Début de restitution vocale (TTS ou Conversation)
voice.onSpeechStarted([]() {
    Serial.println("[EVENT] Restitution vocale démarrée");
});

// 4. Fin de restitution vocale (émis exactement une fois par énonciation)
voice.onSpeechFinished([]() {
    Serial.println("[EVENT] Restitution vocale achevée");
});

// 5. Transcription unifiée (STT manuel, ContinuousSTT et Conversation)
voice.onTranscription([](const String& text, bool isFinal) {
    if (isFinal) {
        Serial.printf("[TRANSCRIPTION FINALE] \"%s\"\n", text.c_str());
    } else {
        Serial.printf("[TRANSCRIPTION INTERIM] \"%s\"\n", text.c_str());
    }
});

// 6. Fin d'un tour de conversation
voice.onConversationFinished([]() {
    Serial.println("[EVENT] Tour conversationnel achevé");
});
```

---

## 7. Contrôleur Réutilisable `ContinuousListeningController`

Intégré au cœur de la bibliothèque (`src/core/ContinuousListeningController.h` / `.cpp`), ce contrôleur abstrait toute la complexité d'une dictée vocale continue mains libres :
- **Détection VAD Intégrée** : Analyse en direct les blocs PCM 16 kHz capturés par le microphone matériel.
- **Paramètres Temporels Déterministes** :
  - **Attaque parole** : 200 ms consécutifs au-dessus du seuil haut.
  - **Silence de clôture** : 1 200 ms de silence consécutif post-parole.
  - **Durée maximale de segment** : 30 secondes (bornage strict anti-blocage).
  - **Délai de réarmement automatique** : 400 ms entre deux segments.
- **Zéro Double Capture Microphone** : Se branche directement sur les chunks audio du streamer existant via `_voice->onAudioChunk()`.
- **Règle Anti-Hallucination Absolue** : Si le délai de transcription finale expire sans réponse du serveur, aucun texte factice n'est généré.
- **Support des Pauses & Reprises** : `pause()` et `resume()` permettent de suspendre la dictée sans déconnecter la session WebSocket TLS.

---

## 8. Référence de l'API Historique (`VoxCoreVoice`)

Pour assurer une rétrocompatibilité ascendante stricte à 100%, la classe `VoxCoreVoice` (`VoxCoreVoice.h`) reste intégralement disponible avec toutes ses méthodes publiques historiques :

### API Synthèse Vocale (TTS)
- `bool begin(const VoiceKitConfig& config)`
- `bool say(const String& text)`
- `void update()`
- `void stop()`
- `bool isReady() const`, `bool isBusy() const`, `bool isSpeaking() const`
- `void setVolume(uint8_t volume)`

### API Reconnaissance Vocale (STT)
- `bool startListening()`, `bool stopListening()`, `void cancelListening()`
- `bool isListening() const`, `bool isTranscribing() const`
- `void onTranscription(TranscriptionCallback cb)`
- `void onListeningStarted(VoidCallback cb)`, `void onListeningFinished(VoidCallback cb)`
- `uint32_t getAudioLevel() const`, `void onAudioChunk(AudioChunkCallback cb)`

### API Conversation Vocale Bidirectionnelle
- `bool enableConversation(bool enable)`
- `bool isConversationEnabled() const`, `bool isModeSwitchPending() const`
- `void onUserText(TranscriptionCallback cb)`, `void onAssistantText(AssistantTextCallback cb)`
- `void onConversationFinished(VoidCallback cb)`, `void onConversationTurnMetrics(TurnMetricsCallback cb)`
- `const ConversationMetrics& getConversationMetrics() const`
- `const VoiceLatencyMetrics& getLatencyMetrics() const`
- `void setVadSilenceDurationMs(uint32_t ms)`, `uint32_t getVadSilenceDurationMs() const`

### API Fournisseurs & Modèles (v0.6.0)
- `IVoiceProvider* getActiveProvider() const`
- `VoiceProviderType getProviderType() const`
- `GeminiLiveClient* getClient()` *(rétrocompatibilité)*

### API Profil Vocal & Langue
- `bool setVoiceProfile(voice, lang, style, tone, accent)`
- `bool setVoice(const String& voice)`, `bool setLanguage(const String& lang)`
- `bool setStyle(VoiceStyle / const String&)`, `bool setTone(VoiceTone / const String&)`, `bool setAccent(const String& accent)`
- `bool isConnecting() const`, `bool isProfileChangePending() const`

---

## 9. Exemples Inclus

La bibliothèque fournit 10 exemples complets prêts à l'emploi :

1. **`examples/OpenAIConversation/`** (PlatformIO) — **NOUVEAU v0.6.0** :  
   Démonstration épurée et headless de conversation vocale bidirectionnelle en français avec l'API **OpenAI Realtime** (`gpt-realtime-2.1-mini`). Acquisition microphone ES7210 16 kHz rééchantillonnée à chaud en 24 kHz via `AudioResampler`, streaming Base64, VAD local, restitution haut-parleur AW88298 24 kHz et interruption instantanée STOP sur appui tactile.
2. **`examples/VoiceDiagnostics/`** (PlatformIO) :  
   Outil officiel de diagnostic et de télémétrie vocale pour M5Stack CoreS3. Interface tactile M5Unified (320 × 240) à 6 onglets (`SYS`, `AUDIO`, `LAT`, `NET`, `CONN`, `EVT`). Journalisation série `[LATENCY]` et export CSV `[LATENCY_CSV]`.
3. **`examples/MinimalVoiceKit/`** (PlatformIO) :  
   Démonstrateur de référence de la façade simplifiée `VoiceKit`. Permet de tester successivement les 4 modes vocaux (TTS, STT manuel, dictée continue et conversation) sur l'écran tactile du M5Stack CoreS3 (320 × 240) avec diagnostic en temps réel.
4. **`examples/ContinuousSTT/`** (PlatformIO) :  
   Application autonome de dictée vocale mains libres avec interface graphique complète M5Unified (7 états, VU-mètre RMS, compte à rebours de silence, bouton Pause/Reprendre, accents français UTF-8).
5. **`examples/VoiceSettingsDemo/`** (PlatformIO) :  
   Démonstrateur tactile multi-profils et validation UTF-8 pour M5Stack CoreS3. Permet de faire défiler et tester 5 scénarios complets avec accents français, allemand et anglais.
6. **`examples/MinimalConversation/`** (PlatformIO) :  
   Application conversationnelle vocale bidirectionnelle native (Mode B) avec Gemini Live pour M5Stack CoreS3. Bouton tactile dynamique (« PARLER » / « STOP ») et cartes d'affichage.
7. **`examples/MinimalTTS/`** (PlatformIO) :  
   Validation physique TTS minimale sans LVGL. Démonstration de synthèse vocale au démarrage puis déclenchement tactile sur l'écran.
8. **`examples/MinimalSTT/`** (PlatformIO) :  
   Démonstration de reconnaissance vocale Push-to-Talk STT pour M5Stack CoreS3 avec transcriptions intérimaires et finales.
9. **`examples/OrientationClockVoice/`** (PlatformIO) :  
   Horloge dynamique complète avec **LVGL 9**, accéléromètre **BMI270** (rotation 360°), horloge RTC **BM8563**, et annonce vocale de l'heure en français.
10. **`examples/MinimalTTS_Arduino/`** (Arduino IDE) :  
    Croquis autonome `.ino` prêt à l'emploi avec documentation pas-à-pas des menus Arduino IDE.

---

## 10. Garanties de Non-Régression & Sécurité

La version **v0.6.0** garantit une non-régression absolue sur l'ensemble des modules préexistants :
- **Intégrité absolue du moteur audio VoxCore** : Aucune réduction arbitraire du buffer circulaire PSRAM (120 000 échantillons @ 24 kHz) ni des marges DMA (160 ms). Préservation intégrale de la tâche FreeRTOS Mode 2 et du vidage exact sans coupure de syllabe.
- **Validation Repère Acoustique (« alpha tango terminé »)** : Vérification rigoureuse que les derniers mots émis (par Gemini Live comme par OpenAI) sont intégralement restitués sans troncature, y compris lors de la réception de `response.done`.
- **Rééchantillonnage 16k $\to$ 24k en arithmétique entière** : Exécution en < 15 µs sur ESP32-S3 Core 1 sans blocage de la boucle de capture audio.
- **Zéro identifiant en clair** : Aucun secret, SSID Wi-Fi ni clé API n'est publié dans le dépôt. Masquage systématique des clés dans les logs.
- **Suite de tests automatisés** : 79 suites de tests unitaires et 1 047 assertions validées avec 0 échec (`tests/test_voicekit_all.exe`).
- **Compilations d'exemples validées à 100%** : Tous les exemples cibles PlatformIO compilent sans aucune erreur.

Consultez le [Rapport de Livraison v0.6.0](RAPPORT_LIVRAISON_v0.6.0.md) pour les détails d'architecture, les métriques exposées et les mesures comparatives.
