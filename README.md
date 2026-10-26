# VoiceKit v0.5.3 — Optimisation de Latence Vocale & Diagnostic Avancé

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![Arduino IDE](https://img.shields.io/badge/Arduino%20IDE-Compatible-00979C.svg)](https://www.arduino.cc/)
[![ESP32-S3](https://img.shields.io/badge/Target-M5Stack%20CoreS3%20(ESP32--S3)-blue.svg)](https://docs.m5stack.com/en/core/CoreS3)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Bibliothèque C++/Arduino installable de **synthèse vocale (Text-To-Speech / TTS)**, de **reconnaissance vocale (Speech-To-Text / STT)**, de **dictée vocale continue mains libres (ContinuousSTT)** et de **conversation vocale bidirectionnelle native (Mode B)** haute fidélité pour **M5Stack CoreS3** (ESP32-S3), propulsée par l'API **Gemini Live** (protocole bidirectionnel BidiGenerateContent) et construite sur le moteur audio et réseau éprouvé de **[VoxCore](https://github.com/Philippe-AD/VoxCore)** et **[VoxTranscribe](https://github.com/Philippe-AD/VoxTranscribe)**.

La version **v0.5.3** apporte l'optimisation de la réactivité des interactions vocales (**Voice Latency Optimization**), l'instrumentation détaillée de chaque tour vocal et l'élimination des allocations superflues :
- **Instrumentation Précise de la Latence Vocale (7 Jalons / 6 Durées)** : Mesure à la milliseconde de chaque jalon d'un tour vocal (`speechEndDetectedMs`, `micStoppedMs`, `activityEndSentMs`, `firstResponseReceivedMs`, `firstAudioReceivedMs`, `firstAudioPlayedMs`, `playbackFinishedMs`) et calcul strict des durées dérivées. Toute valeur non observée renvoie explicitement `-1` (`N/D`), sans aucun faux zéro.
- **Suppression Totale des Allocations Tas en Streaming** : Encodage Base64 direct sur la pile (`encodeBase64Direct`) sans instanciation d'objets `String` toutes les 32 ms. Élimine la fragmentation mémoire du tas FreeRTOS et les micro-saccades processeur.
- **Contrôle Dynamique du Silence VAD** : Paramétrage du seuil de silence VAD local (`vadSilenceDurationMs`, 1 200 ms par défaut) avec validation de réactivité à **800 ms**, **1 000 ms** et **1 200 ms** via l'API `setVadSilenceDurationMs(ms)`.
- **Démonstrateur `VoiceDiagnostics` v0.5.3** : Ajout d'un 6e onglet tactile dédié `LATENCY` (`LAT`) affichant la latence perçue, le délai Gemini, le délai local DMA, la durée de capture, les statistiques de session (min, moy, max, médiane) et le réglage interactif du VAD, complété d'une sortie série `[LATENCY_CSV]`.
- **Sanctuaire Audio Intact & Anti-Régression** : Préservation absolue du buffer circulaire PSRAM 120 000 échantillons (~5 s) et du quadruple buffer DMA (160 ms). Aucune coupure de syllabe ni de dernier mot (validation stricte sur la phrase repère : *« alpha tango terminé »*).
- **Consolidation Complète** : Conservation intégrale des optimisations FastConnect v0.5.2, 912 tests unitaires réussis (+79 assertions v0.5.3) et 100% des exemples compilés sous PlatformIO.

---

## Sommaire

1. [Fonctionnalités Principales](#1-fonctionnalités-principales)
2. [Structure de la Bibliothèque](#2-structure-de-la-bibliothèque)
3. [Méthodes d'Installation](#3-méthodes-dinstallation)
   - [Méthode A — PlatformIO depuis GitHub](#méthode-a--platformio-depuis-github)
   - [Méthode B — Arduino IDE](#méthode-b--arduino-ide)
   - [Méthode C — Développement Local](#méthode-c--développement-local)
4. [Nouvelle API Simplifiée (`VoiceKit.h`)](#4-nouvelle-api-simplifiée-voicekith)
5. [Contrôleur Réutilisable `ContinuousListeningController`](#5-contrôleur-réutilisable-continuouslisteningcontroller)
6. [Référence de l'API Historique (`VoxCoreVoice`)](#6-référence-de-lapi-historique-voxcorevoice)
7. [Exemples Inclus](#7-exemples-inclus)
8. [Garanties de Non-Régression & Sécurité](#8-garanties-de-non-régression--sécurité)

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

## 2. Structure de la Bibliothèque

```text
VoiceKit/
├── library.json                 # Manifeste PlatformIO (v0.5.0)
├── library.properties           # Manifeste Arduino IDE (v0.5.0)
├── LICENSE                      # Licence MIT
├── README.md                    # Documentation complète
├── CHANGELOG.md                 # Historique des versions
├── src/                         # Sources et en-têtes (Standard Arduino 1.5)
│   ├── VoiceKit.h               # NOUVEAU v0.5.0 : Façade simplifiée unifiée (TTS, STT, Continuous, Conversation)
│   ├── VoiceKit.cpp             # Implémentation de la façade simplifiée
│   ├── VoxCoreVoice.h           # En-tête public historique préservé
│   ├── VoxCoreVoice.cpp         # Moteur principal VoiceKit / VoxCore
│   ├── VoiceKitConfig.h         # Configuration, profils vocaux & métriques
│   ├── Utf8Utils.h              # Utilitaires UTF-8 & échappement JSON RFC 8259
│   ├── core/                    # Moteur interne VoxCore
│   │   ├── ContinuousListeningController (.h/.cpp) # NOUVEAU v0.5.0 : Contrôleur autonome de dictée continue
│   │   ├── VoiceActivityDetector.h                # Détecteur VAD local 16 kHz
│   │   ├── GeminiLiveClient (.h/.cpp)     # Client WebSocket BidiGenerateContent
│   │   ├── LiveSessionController (.h/.cpp)# Automate de session Gemini Live
│   │   ├── ConversationController (.h/.cpp)# Contrôleur de conversation bidirectionnelle
│   │   ├── LiveAudioPlayer (.h/.cpp)      # Lecteur DMA PCM 24 kHz (TTS / Réponse)
│   │   ├── LiveAudioStreamer (.h/.cpp)    # Streamer micro PCM 16 kHz (STT / Entrée)
│   │   ├── WiFiManager (.h/.cpp)          # Gestionnaire Wi-Fi & SNTP
│   │   ├── Utf8Utils (.h/.cpp)            # Validation & sanitisation UTF-8 RFC 3629
│   │   ├── AudioTraceLog (.h/.cpp)        # Journaliseur d'événements audio
│   │   └── GoogleRoots.h                  # Bundle certificats Google Trust Services
│   └── hal/                     # Couche d'abstraction matérielle
│       ├── IAudioOutput.h                 # Interface de sortie audio
│       ├── IAudioInput.h                  # Interface d'entrée microphone
│       ├── M5AudioOutput (.h/.cpp)        # HAL haut-parleur M5.Speaker
│       ├── M5AudioInput (.h/.cpp)         # HAL microphone M5.Mic (ES7210)
│       └── HardwareManager.h              # Arbitrage I2S centralisé HP / Micro
├── examples/                    # Exemples d'application (9 exemples)
│   ├── VoiceDiagnostics/        # NOUVEAU v0.5.3 : Outil officiel de diagnostic (6 onglets dont LATENCY) & CSV
│   ├── MinimalVoiceKit/         # Démonstrateur façade simplifiée multi-modes
│   ├── ContinuousSTT/           # Dictée continue mains libres avec interface M5Unified
│   ├── VoiceSettingsDemo/       # Démonstrateur tactile profils & UTF-8
│   ├── MinimalConversation/     # Application conversation bidirectionnelle (Mode B)
│   ├── MinimalTTS/              # Exemple PlatformIO TTS sans LVGL
│   ├── OrientationClockVoice/   # Exemple PlatformIO horloge LVGL 9 + TTS
│   ├── MinimalSTT/              # Exemple PlatformIO dictée Push-to-Talk
│   └── MinimalTTS_Arduino/      # Exemple autonome Arduino IDE (.ino)
└── tests/
    ├── run_tests.bat            # Lanceur des tests automatisés MinGW
    └── test_voicekit_all.cpp    # 65 suites de tests unitaires (912 validations, 0 échec)
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
    https://github.com/Philippe-AD/VoiceKit.git#v0.5.0
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

## 4. Nouvelle API Simplifiée (`VoiceKit.h`)

La version **v0.5.0** introduit une façade unifiée et haut niveau optionnelle `VoiceKit`, accessible via `#include <VoiceKit.h>`. Elle permet de piloter l'intégralité des fonctionnalités vocales à l'aide d'un unique objet :

```cpp
#include <VoiceKit.h>

VoiceKit voice;
```

### 4.1 Vue d'ensemble des méthodes

| Méthode | Description | Retour | Caractère |
| :--- | :--- | :---: | :---: |
| `bool begin(config)` | Initialise le matériel, le Wi-Fi, SNTP et la session Gemini Live. | `bool` | Asynchrone / Non bloquant |
| `void update()` | Traite la machine d'état et les contrôleurs audio. À appeler dans `loop()`. | `void` | Non bloquant (< 1 ms) |
| `bool say(text)` | Déclenche la synthèse vocale (TTS) de la phrase donnée. | `bool` | Non bloquant |
| `bool listen()` | Démarre la reconnaissance vocale manuelle (STT Push-to-Talk). | `bool` | Non bloquant |
| `bool stopListening()` | Arrête l'acquisition micro et attend la transcription finale. | `bool` | Non bloquant |
| `void cancelListening()` | Annule immédiatement l'écoute sans attendre de transcription. | `void` | Immédiat |
| `bool startContinuousListening()` | Active la dictée continue mains libres avec détection automatique de fin de parole. | `bool` | Non bloquant |
| `bool stopContinuousListening()` | Désactive la dictée continue et replace la façade en mode `IDLE`. | `bool` | Immédiat |
| `bool pauseContinuousListening()` | Met en pause temporaire la dictée continue. | `bool` | Immédiat |
| `bool resumeContinuousListening()` | Reprend la dictée continue après une pause. | `bool` | Non bloquant |
| `bool startConversation()` | Active le mode conversationnel bidirectionnel natif Gemini Live. | `bool` | Sécurisé / Non bloquant |
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

### 4.2 Événements & Callbacks Unifiés

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

## 5. Contrôleur Réutilisable `ContinuousListeningController`

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

## 6. Référence de l'API Historique (`VoxCoreVoice`)

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

### API Profil Vocal & Langue
- `bool setVoiceProfile(voice, lang, style, tone, accent)`
- `bool setVoice(const String& voice)`, `bool setLanguage(const String& lang)`
- `bool setStyle(VoiceStyle / const String&)`, `bool setTone(VoiceTone / const String&)`, `bool setAccent(const String& accent)`
- `bool isConnecting() const`, `bool isProfileChangePending() const`

---

## 7. Exemples Inclus

La bibliothèque fournit 9 exemples complets prêts à l'emploi :

1. **`examples/VoiceDiagnostics/`** (PlatformIO) — **MIS À JOUR v0.5.3** :  
   Outil officiel de diagnostic et de télémétrie vocale pour M5Stack CoreS3. Interface tactile M5Unified (320 × 240) à 6 onglets (`SYS`, `AUDIO`, `LAT`, `NET`, `CONN`, `EVT`). L'onglet `LAT` affiche la latence complète de réponse, le délai Gemini, le délai local DMA, la durée de capture microphone, les statistiques de session (min/moy/max/médiane) et le contrôle interactif du VAD (800 / 1 000 / 1 200 ms). Inclut la journalisation série structurée `[LATENCY]` et l'export CSV `[LATENCY_CSV]`.
2. **`examples/MinimalVoiceKit/`** (PlatformIO) :  
   Démonstrateur de référence de la façade simplifiée `VoiceKit`. Permet de tester successivement les 4 modes vocaux (TTS, STT manuel, dictée continue et conversation) sur l'écran tactile du M5Stack CoreS3 (320 × 240) avec diagnostic en temps réel sur le moniteur Série.
3. **`examples/ContinuousSTT/`** (PlatformIO) :  
   Application autonome de dictée vocale mains libres avec interface graphique complète M5Unified (7 états, VU-mètre RMS, compte à rebours de silence, bouton Pause/Reprendre, accents français UTF-8).
4. **`examples/VoiceSettingsDemo/`** (PlatformIO) :  
   Démonstrateur tactile multi-profils et validation UTF-8 pour M5Stack CoreS3. Permet de faire défiler et tester 5 scénarios complets avec accents français, allemand et anglais.
5. **`examples/MinimalConversation/`** (PlatformIO) :  
   Application conversationnelle vocale bidirectionnelle native (Mode B) pour M5Stack CoreS3. Bouton tactile dynamique (« PARLER » / « STOP ») et cartes d'affichage.
6. **`examples/MinimalTTS/`** (PlatformIO) :  
   Validation physique TTS minimale sans LVGL. Démonstration de synthèse vocale au démarrage puis déclenchement tactile sur l'écran.
7. **`examples/MinimalSTT/`** (PlatformIO) :  
   Démonstration de reconnaissance vocale Push-to-Talk STT pour M5Stack CoreS3 avec transcriptions intérimaires et finales.
8. **`examples/OrientationClockVoice/`** (PlatformIO) :  
   Horloge dynamique complète avec **LVGL 9**, accéléromètre **BMI270** (rotation 360°), horloge RTC **BM8563**, et annonce vocale de l'heure en français.
9. **`examples/MinimalTTS_Arduino/`** (Arduino IDE) :  
   Croquis autonome `.ino` prêt à l'emploi avec documentation pas-à-pas des menus Arduino IDE.

---

## 8. Garanties de Non-Régression & Sécurité

La version **v0.5.3** garantit une non-régression absolue sur l'ensemble des modules validés en v0.5.2 :
- **Intégrité absolue du moteur audio VoxCore** : Aucune réduction arbitraire du buffer circulaire PSRAM (120 000 échantillons @ 24 kHz) ni des marges DMA (160 ms). Préservation intégrale de la tâche FreeRTOS Mode 2 et du vidage exact sans coupure de syllabe.
- **Validation Repère Acoustique (« alpha tango terminé »)** : Vérification rigoureuse que les derniers mots émis par Gemini Live sont intégralement restitués sans troncature.
- **Suppression du Heap Churn** : Remplacement des allocations `String` lors du streaming microphone Base64 par un tamponnage direct sur la pile (`encodeBase64Direct`), assurant une fluidité audio maximale.
- **Politique Anti-Faux Zéro** : Toute métrique temporelle non observée renvoie explicitement `-1` (`N/D`), sans création artificielle de durées nulles.
- **Suite de tests automatisés** : 65 suites de tests unitaires et 912 assertions validées avec 0 échec (`tests/test_voicekit_all.exe`).
- **Compilations d'exemples validées à 100%** : Les 9 exemples compilent sans aucune erreur sous PlatformIO et Arduino IDE (M5Stack CoreS3).
- **Zéro identifiant en clair** : Aucun secret, SSID Wi-Fi ni clé API n'est publié dans le dépôt.

Consultez le [Rapport de Livraison v0.5.3](RAPPORT_LIVRAISON_v0.5.3.md) pour les détails d'architecture, les métriques exposées et les mesures comparatives.
