# VoiceKit v0.4.0 — Bibliothèque Audio TTS + STT + Conversation Vocale pour M5Stack CoreS3

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![Arduino IDE](https://img.shields.io/badge/Arduino%20IDE-Compatible-00979C.svg)](https://www.arduino.cc/)
[![ESP32-S3](https://img.shields.io/badge/Target-M5Stack%20CoreS3%20(ESP32--S3)-blue.svg)](https://docs.m5stack.com/en/core/CoreS3)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Bibliothèque C++/Arduino installable de **synthèse vocale (Text-To-Speech / TTS)**, de **reconnaissance vocale (Speech-To-Text / STT)** et de **conversation vocale bidirectionnelle native (Mode B)** haute fidélité pour **M5Stack CoreS3** (ESP32-S3), propulsée par l'API **Gemini Live** (protocole bidirectionnel BidiGenerateContent) et construite sur le moteur audio et réseau éprouvé de **[VoxCore](https://github.com/Philippe-AD/VoxCore)** et **[VoxTranscribe](https://github.com/Philippe-AD/VoxTranscribe)**.

---

## Sommaire

1. [Fonctionnalités Principales](#1-fonctionnalités-principales)
2. [Structure de la Bibliothèque](#2-structure-de-la-bibliothèque)
3. [Méthodes d'Installation](#3-méthodes-dinstallation)
   - [Méthode A — PlatformIO depuis GitHub](#méthode-a--platformio-depuis-github)
   - [Méthode B — Arduino IDE](#méthode-b--arduino-ide)
   - [Méthode C — Développement Local](#méthode-c--développement-local)
4. [Référence de l'API Publique (`VoxCoreVoice`)](#4-référence-de-lapi-publique-voxcorevoice)
   - [API Synthèse Vocale (TTS)](#api-synthèse-vocale-tts)
   - [API Reconnaissance Vocale (STT)](#api-reconnaissance-vocale-stt)
   - [API Conversation Vocale Bidirectionnelle](#api-conversation-vocale-bidirectionnelle)
5. [Exemples Inclus](#5-exemples-inclus)
6. [Garanties de Non-Régression & Sécurité](#6-garanties-de-non-régression--sécurité)

---

## 1. Fonctionnalités Principales

- **Zéro dépendance intrusive** : VoiceKit est headless. Il ne requiert ni LVGL, ni UIManager, et n'interfère pas avec l'écran ou les capteurs de l'hôte.
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
├── library.json                 # Manifeste PlatformIO (v0.4.0)
├── library.properties           # Manifeste Arduino IDE (v0.4.0)
├── LICENSE                      # Licence MIT
├── README.md                    # Documentation complète
├── CHANGELOG.md                 # Historique des versions
├── src/                         # Sources et en-têtes (Standard Arduino 1.5)
│   ├── VoxCoreVoice.h           # En-tête public principal (TTS + STT + Conversation)
│   ├── VoxCoreVoice.cpp         # Implémentation de la façade publique
│   ├── VoiceKitConfig.h         # Configuration du moteur & métriques conversationnelles
│   ├── core/                    # Moteur interne VoxCore
│   │   ├── GeminiLiveClient (.h/.cpp)     # Client WebSocket BidiGenerateContent
│   │   ├── LiveSessionController (.h/.cpp)# Automate de session Gemini Live
│   │   ├── ConversationController (.h/.cpp)# Contrôleur de conversation bidirectionnelle
│   │   ├── LiveAudioPlayer (.h/.cpp)      # Lecteur DMA PCM 24 kHz (TTS / Réponse)
│   │   ├── LiveAudioStreamer (.h/.cpp)    # Streamer micro PCM 16 kHz (STT / Entrée)
│   │   ├── WiFiManager (.h/.cpp)          # Gestionnaire Wi-Fi & SNTP
│   │   ├── AudioTraceLog (.h/.cpp)        # Journaliseur d'événements audio
│   │   └── GoogleRoots.h                  # Bundle certificats Google Trust Services
│   └── hal/                     # Couche d'abstraction matérielle
│       ├── IAudioOutput.h                 # Interface de sortie audio
│       ├── IAudioInput.h                  # Interface d'entrée microphone
│       ├── M5AudioOutput (.h/.cpp)        # HAL haut-parleur M5.Speaker
│       ├── M5AudioInput (.h/.cpp)         # HAL microphone M5.Mic (ES7210)
│       └── HardwareManager.h              # Arbitrage I2S centralisé HP / Micro
├── examples/                    # Exemples d'application
│   ├── MinimalConversation/     # NOUVEAU : Application conversation bidirectionnelle
│   ├── MinimalTTS/              # Exemple PlatformIO TTS sans LVGL
│   ├── OrientationClockVoice/   # Exemple PlatformIO horloge LVGL 9 + TTS
│   ├── MinimalSTT/              # Exemple PlatformIO dictée Push-to-Talk
│   └── MinimalTTS_Arduino/      # Exemple autonome Arduino IDE (.ino)
└── tests/
    ├── run_tests.bat            # Lanceur des tests automatisés MinGW
    └── test_voicekit_all.cpp    # 35 suites de tests unitaires (279 validations)
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

### API Synthèse Vocale (TTS)

| Méthode | Rôle | Caractère |
| :--- | :--- | :---: |
| `bool begin(const VoiceKitConfig& config)` | Démarre les pilotes audio, le Wi-Fi, la synchronisation SNTP et la session Gemini Live. | Asynchrone / Non bloquant |
| `bool say(const String& text)` | Met en file et transmet la phrase à synthétiser en streaming audio PCM 24 kHz. | **Non bloquant** |
| `void update()` | Traite la machine d'état, le flux WebSocket et le streamer micro. À appeler à chaque `loop()`. | **Non bloquant** (< 1 ms) |
| `void stop()` | Interrompt immédiatement l'audio en cours ou l'écoute micro et purge les tampons. | Immédiat |
| `bool isReady() const` | Indique si le réseau et la session Gemini sont prêts pour une synthèse ou une dictée. | Non bloquant |
| `bool isBusy() const` | Indique si une requête, restitution audio ou écoute microphone est active. | Non bloquant |
| `bool isSpeaking() const` | Indique si des échantillons sont actuellement envoyés aux haut-parleurs via DMA. | Non bloquant |
| `void setVolume(uint8_t volume)` | Règle le volume sonore de 0 à 100 %. | Immédiat |

### API Reconnaissance Vocale (STT)

| Méthode | Rôle | Caractère |
| :--- | :--- | :---: |
| `bool startListening()` | Démarre la capture micro PCM 16 kHz et le streaming vers Gemini Live. Interrompt tout TTS en cours. | **Non bloquant** |
| `bool stopListening()` | Arrête l'acquisition micro, envoie `activityEnd` et attend la transcription finale. | **Non bloquant** |
| `void cancelListening()` | Annule immédiatement l'acquisition microphone sans attendre de transcription. | Immédiat |
| `bool isListening() const` | Indique si le microphone est activement en train de capturer la voix. | Non bloquant |
| `bool isTranscribing() const` | Indique que l'acquisition est finie et que la transcription finale est en attente. | Non bloquant |
| `void onTranscription(TranscriptionCallback cb)` | Enregistre le callback de réception des transcriptions : `void(const String& text, bool isFinal)`. | Immédiat |
| `void onListeningStarted(VoidCallback cb)` | Déclenché dès que la capture microphone et le streaming sont effectifs. | Immédiat |
| `void onListeningFinished(VoidCallback cb)` | Déclenché au relâchement du bouton microphone. | Immédiat |

### API Conversation Vocale Bidirectionnelle (v0.4.0)

| Méthode | Rôle | Caractère |
| :--- | :--- | :---: |
| `void enableConversation(bool enable)` | Active/désactive le mode conversationnel bidirectionnel natif (Mode B). Bascule l'instruction système. | Immédiat |
| `bool isConversationEnabled() const` | Indique si le mode conversationnel bidirectionnel est actif. | Non bloquant |
| `void onUserText(TranscriptionCallback cb)` | Callback de transcription pour la voix de l'utilisateur : `void(const String& text, bool isFinal)`. | Immédiat |
| `void onAssistantText(AssistantTextCallback cb)` | Callback textuel de la réponse générée par Gemini : `void(const String& text)`. | Immédiat |
| `void onConversationFinished(VoidCallback cb)` | Notifié à la fin complète d'un tour de conversation (vidage audio achevé). | Immédiat |
| `void onConversationTurnMetrics(TurnMetricsCallback cb)` | Notifie les métriques diagnostiques complètes du tour : `void(const ConversationMetrics& m)`. | Immédiat |
| `const ConversationMetrics& getConversationMetrics() const` | Retourne la structure des métriques détaillées du dernier tour. | Non bloquant |

### États Opérationnels (`VoiceState`)

L'état du moteur est accessible via `voice.getState()` :
- `VoiceState::INITIALIZING` : Démarrage des composants matériels et réseau.
- `VoiceState::CONNECTING_WIFI` : Négociation du lien Wi-Fi local.
- `VoiceState::CONNECTING_SERVICE` : Poignée de main TLS et ouverture de la session Gemini Live.
- `VoiceState::READY` : Moteur prêt pour une synthèse vocale, une dictée ou un tour conversationnel.
- `VoiceState::LISTENING` : Écoute active du microphone (bouton « PARLER » enfoncé).
- `VoiceState::PROCESSING` / `WAITING_RESPONSE` : Microphone relâché, traitement et attente de la réponse Gemini.
- `VoiceState::SPEAKING` : Restitution sonore du haut-parleur en cours (réponse vocale native).
- `VoiceState::STOPPED` : Tour interrompu par l'utilisateur (appui sur STOP).
- `VoiceState::DISCONNECTED` : Session réseau déconnectée.
- `VoiceState::ERROR_STATE` : Anomalie réseau ou API (erreur récupérable via nouvel essai).

### Métriques Diagnostiques de Tour (`ConversationMetrics`)

Chaque tour de conversation produit un ensemble de métriques précises :
- `turnId` : Numéro séquentiel du tour.
- `micCaptureDurationMs` : Durée d'acquisition du microphone en millisecondes.
- `audioBlocksSent` : Nombre de blocs audio PCM 16 kHz transmis vers Gemini Live.
- `firstAudioLatencyMs` : Temps écoulé jusqu'à la réception du premier échantillon audio de réponse.
- `pcmBlocksReceived` : Nombre de trames audio PCM 24 kHz reçues du serveur.
- `droppedSamples` : Nombre d'échantillons éventuellement perdus.
- `totalTurnDurationMs` : Durée totale du tour de conversation.
- `userText` : Transcription reconnue de la parole de l'utilisateur.
- `assistantText` : Texte correspondant à la réponse vocale de Gemini.

### Callbacks d'Événements Conversationnels

```cpp
voice.enableConversation(true);

voice.onUserText([](const String& text, bool isFinal) {
    if (isFinal) {
        Serial.printf("[UTILISATEUR] \"%s\"\n", text.c_str());
    }
});

voice.onAssistantText([](const String& text) {
    Serial.printf("[GEMINI] \"%s\"\n", text.c_str());
});

voice.onConversationFinished([]() {
    Serial.println("[CONVERSATION] Tour termine, pret pour une nouvelle question");
});

voice.onConversationTurnMetrics([](const ConversationMetrics& m) {
    Serial.printf("[METRIQUES] Tour #%llu | Micro: %u ms | Latence 1er son: %u ms | Duree totale: %u ms\n",
                  (unsigned long long)m.turnId, (unsigned)m.micCaptureDurationMs,
                  (unsigned)m.firstAudioLatencyMs, (unsigned)m.totalTurnDurationMs);
});
```

---

## 5. Exemples Inclus

1. **`examples/MinimalConversation/`** (PlatformIO) — **NOUVEAU v0.4.0** :  
   Application conversationnelle vocale bidirectionnelle pour M5Stack CoreS3. L'utilisateur appuie sur « PARLER », pose sa question, relâche le bouton, voit sa transcription, puis entend la réponse vocale de Gemini sur le haut-parleur CoreS3. Grand bouton tactile dynamique (« PARLER » / « STOP »), cartes d'affichage 320x240, contrôle parallèle via port série (`P`/`R`/`S`).
2. **`examples/MinimalTTS/`** (PlatformIO) :  
   Validation physique TTS minimale sans LVGL. Démonstration de `voice.say()` au démarrage puis déclenchement tactile sur l'écran.
3. **`examples/MinimalSTT/`** (PlatformIO) :  
   Démonstration de reconnaissance vocale Push-to-Talk STT pour M5Stack CoreS3. Bouton tactile « DICTER », affichage des états (PRÊT, ÉCOUTE, TRANSCRIPTION, TERMINÉ, ERREUR), affichage dynamique des résultats intérimaires et finaux à l'écran et sur le moniteur série.
4. **`examples/OrientationClockVoice/`** (PlatformIO) :  
   Application complète d'horloge dynamique intégrant **LVGL 9**, accéléromètre **BMI270** (rotation 360°), horloge RTC **BM8563**, et annonce de l'heure en français sur appui tactile.
5. **`examples/MinimalTTS_Arduino/`** (Arduino IDE) :  
   Croquis autonome `.ino` prêt à l'emploi avec documentation pas-à-pas des menus Arduino IDE.

---

## 6. Garanties de Non-Régression & Sécurité

La version **v0.4.0** garantit une non-régression absolue sur les trois applications validées (TTS v0.2.1 et STT v0.3.0) :
- **Intégrité des moteurs fondamentaux** : Le client Gemini Live (`GeminiLiveClient`), le lecteur DMA (`LiveAudioPlayer`), le streamer micro (`LiveAudioStreamer`) et la pile TLS Google (`GoogleRoots.h`) ne sont pas réécrits ni altérés.
- **Coordination légère sans duplication** : Le `ConversationController` réutilise la session Gemini Live existante sans introduire de second transport WebSocket ni de tâches concurrentes.
- **Arbitrage matériel garanti** : Pas de conflit I2S ni de larsen grâce au `HardwareManager` qui coupe automatiquement le haut-parleur lors de la prise de son micro et le réactive pour la réponse.
- **Suite de tests automatisés** : 35 suites de tests unitaires et 279 assertions validées avec 0 échec (`tests/test_voicekit_all.exe`).
- **Zéro identifiant en clair** : Aucun secret, SSID Wi-Fi ni clé API n'est publié dans le dépôt.

Consultez le [Rapport de Livraison v0.4.0](RAPPORT_LIVRAISON_v0.4.0.md) pour les détails d'architecture et les preuves de compilation.
