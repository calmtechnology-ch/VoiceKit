# VoiceKit v0.6.6.2 — Public Release Readiness

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![ESP32-S3](https://img.shields.io/badge/Target-M5Stack%20CoreS3%20(ESP32--S3)-blue.svg)](https://docs.m5stack.com/en/core/CoreS3)
[![Tests: 3716 PASS](https://img.shields.io/badge/Tests-3716%20Passed-brightgreen.svg)](tests/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

## 1. Présentation de VoiceKit

**VoiceKit** est une bibliothèque logicielle C++17 pour microcontrôleur **ESP32-S3**, spécialement conçue pour transformer le boîtier **M5Stack CoreS3** en terminal vocal intelligent, interactif et temps réel.

VoiceKit propose une architecture audio et réseau unifiée supportant les deux principaux moteurs d'IA conversationnelle multimodale :
- **Google Gemini Live** (`wss://generativelanguage.googleapis.com/...`)
- **OpenAI Realtime API** (`wss://api.openai.com/v1/realtime`)

La bibliothèque gère l'ensemble de la chaîne technique : acquisition microphonique I2S 16 kHz, streaming bidirectionnel chiffré TLS, décodage et restitution audio haute fidélité 24 kHz sur le haut-parleur interne, gestion des tours conversationnels, exécution de commandes matérielles locales hors ligne (`ToolRegistry`) et détection locale de mot de réveil (Wake Word).

---

## 2. Matériel Officiellement Pris en Charge

| Composant | Référence / Spécification | Rôle dans VoiceKit |
|---|---|---|
| **Boîtier cible** | **M5Stack CoreS3** | Plateforme matérielle de référence exclusive |
| **Microcontrôleur** | **ESP32-S3** (dual-core Xtensa LX7 @ 240 MHz) | Traitement audio FreeRTOS & chiffrement TLS |
| **Mémoire** | **16 Mo Flash + 8 Mo PSRAM** (Octal SPI) | Tampons circulaires audio & interfaces graphiques |
| **Microphones** | **Double micro numérique ES7210** | Acquisition I2S DMA 16 kHz haute précision |
| **Haut-parleur** | **Amplificateur AW88298 & HP 1 W** | Restitution sonore claire PCM 24 kHz |
| **Écran** | **IPS LCD 320 × 240 (2 pouces) tactile** | Interface utilisateur visuelle (LVGL 9 ou M5GFX) |
| **Connectivité** | **Wi-Fi 2.4 GHz (802.11 b/g/n)** | Streaming WebSocket sécurisé (TLS 1.2/1.3) |

---

## 3. Fonctionnalités Stables

- **Synthèse Vocale (TTS 24 kHz) :** Énonciation fluide de phrases textuelles avec préservation de la dernière syllabe.
- **Reconnaissance Vocale Push-to-Talk (STT) :** Capture vocale manuelle fiable (`startListening` / `stopListening`) sans duplication ni transcription fantôme.
- **Conversation Bidirectionnelle Temps Réel :** Échange vocal continu avec Google Gemini Live ou OpenAI Realtime.
- **Pipeline Vocal Intégré (`IntegratedVoicePipeline`) :** Orchestrateur autonome coordonnant l'écoute, la transcription, la décision locale, l'IA et la réponse vocale.
- **Gestionnaire de Profils Vocaux Universels (`VoiceProfileManager`) :** Catalogue de 20 voix (10 Gemini, 10 OpenAI), gestion du multilingue (français, anglais, allemand, espagnol, italien) et persistance sécurisée en mémoire NVS (CRC32, sans clé API).
- **Registre Local d'Outils Sécurisé (`ToolRegistry`) :** Exécution déterministe de commandes locales avec niveaux de privilèges (`READ_ONLY`, `WRITE`, `SENSITIVE`) et confirmation obligatoire pour les actions sensibles.
- **STOP Prioritaire Universel :** Interruption matérielle et logicielle immédiate (< 50 ms), arrêt du décodage audio et retour garanti à l'état `READY`.

---

## 4. Fonctionnalités Expérimentales

- **Détection Locale de Mot de Réveil (`WakeWordFoundation`) :** Reconnaissance vocale locale hors ligne de l'amorce « Bonjour ESP » basée sur WakeNet 9 (ESP-SR).
- **Écoute Continue Adaptative (`ContinuousSTT`) :** Détection automatique d'activité vocale (VAD) par énergie acoustique sans appui sur un bouton.

---

## 5. Prérequis Logiciels

- **Environnement recommandé :** [VS Code](https://code.visualstudio.com/) avec l'extension **PlatformIO IDE**.
- **Framework :** Arduino pour ESP32 (`espressif32@6.9.0` ou version compatible).
- **Standard de compilation :** **C++17** obligatoire (`-std=gnu++17`).
- **Dépendances matérielles officielles :**
  - `m5stack/M5CoreS3@1.0.0`
  - `m5stack/M5Unified@0.2.23`
  - Optionnel : `lvgl/lvgl@^9.2.2` (uniquement pour les exemples graphiques tactiles).

---

## 6. Installation

### Dans un projet PlatformIO existant

Ajoutez VoiceKit et ses dépendances dans la section `lib_deps` de votre `platformio.ini` :

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
    https://github.com/Philippe-AD/VoiceKit.git#v0.6.6.2
```

Copiez également le fichier de partition `no_ota.csv` (présent à la racine du dépôt VoiceKit) dans votre répertoire de projet.

Pour un guide pas à pas complet, consultez le **[Guide de Démarrage Rapide (QUICK_START.md)](docs/QUICK_START.md)**.

---

## 7. Premier Exemple : Synthèse Vocale (TTS)

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceKit.h>
#include "secrets.h"

static VoiceKit voice;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    VoiceKitConfig vcfg;
    vcfg.provider = VoiceProviderType::GEMINI;
    vcfg.apiKey = GEMINI_KEY;
    vcfg.wifiMode = WiFiMode::MANAGED_WIFI;
    vcfg.wifiSSID = WIFI_SSID;
    vcfg.wifiPassword = WIFI_PASSWORD;
    vcfg.volume = 75;

    voice.onReady([]() {
        Serial.println("VoiceKit prêt ! Énonciation de la phrase...");
        voice.say("Bonjour ! VoiceKit est opérationnel sur M5Stack CoreS3.");
    });

    voice.begin(vcfg);
}

void loop() {
    M5.update();
    voice.update();
    delay(10);
}
```

---

## 8. Configuration Wi-Fi et Clés API

Créez votre fichier `secrets.h` dans le dossier `include/` à partir du fichier modèle `secrets.example.h` :

```cpp
#pragma once

#define WIFI_SSID     "VOTRE_SSID_WIFI_2.4GHZ"
#define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE"

// Clé d'API Google Gemini Live (gratuite sur https://aistudio.google.com/)
#define GEMINI_KEY    "AIzaSy..."

// Optionnel : Clé OpenAI Realtime (si fournisseur OPENAI sélectionné)
#define OPENAI_KEY    "sk-proj-..."
```

> **Sécurité :** Ne commitez jamais `secrets.h` dans un dépôt public. Ce fichier est exclu par défaut par le `.gitignore`. Consultez **[SECURITY.md](SECURITY.md)** pour les bonnes pratiques.

---

## 9. Choix du Fournisseur : Gemini Live vs OpenAI Realtime

VoiceKit permet de choisir le moteur lors de l'initialisation ou dynamiquement à chaud :

```cpp
// Option 1 : Google Gemini Live (par défaut)
vcfg.provider = VoiceProviderType::GEMINI;
vcfg.apiKey = GEMINI_KEY;

// Option 2 : OpenAI Realtime
vcfg.provider = VoiceProviderType::OPENAI;
vcfg.apiKey = OPENAI_KEY;
```

---

## 10. Liste des 11 Exemples Officiels

| Répertoire | Type | Description |
|---|---|---|
| [`examples/MinimalTTS`](examples/MinimalTTS) | PlatformIO | Synthèse vocale unitaire simple (TTS 24 kHz) |
| [`examples/MinimalSTT`](examples/MinimalSTT) | PlatformIO | Reconnaissance vocale Push-to-Talk fiabilisée |
| [`examples/MinimalConversation`](examples/MinimalConversation) | PlatformIO | Dialogue bidirectionnel natif avec Google Gemini Live |
| [`examples/MinimalTTS_Arduino`](examples/MinimalTTS_Arduino) | Arduino IDE | Compatibilité autonome Arduino IDE (.ino) |
| [`examples/WakeWordFoundation`](examples/WakeWordFoundation) | PlatformIO | Détection locale hors ligne « Bonjour ESP » (WakeNet) |
| [`examples/WakeWordConversation`](examples/WakeWordConversation) | PlatformIO | Cycle mains libres : Mot de Réveil -> Conversation |
| [`examples/LocalToolRegistryDemo`](examples/LocalToolRegistryDemo) | PlatformIO | Registre local d'outils matériels sécurisés hors ligne |
| [`examples/VoiceDiagnostics`](examples/VoiceDiagnostics) | PlatformIO | Tableau de bord de télémétrie et métriques système |
| [`examples/VoiceSettingsDemo`](examples/VoiceSettingsDemo) | PlatformIO | Démonstrateur multilingue, profils vocaux et LVGL 9 |
| [`examples/OpenAIConversationLVGL`](examples/OpenAIConversationLVGL) | PlatformIO | Conversation OpenAI Realtime avec interface graphique LVGL |
| [`examples/IntegratedVoiceDemo`](examples/IntegratedVoiceDemo) | PlatformIO | **Exemple de référence principal** : orchestrateur complet |

---

## 11. Limitations Connues

Pour une vue complète et détaillée des contraintes opérationnelles, consultez **[docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md)** :
- Bande Wi-Fi 2.4 GHz obligatoire (ESP32-S3 ne gère pas le 5 GHz).
- Standard C++17 requis pour la compilation.
- Partition Flash de 8 Mo (`partitions_16MB.csv`) requise pour les projets avec interface tactile LVGL 9.
- Aucune clé d'API n'est sauvegardée en mémoire NVS (politique de sécurité stricte).

---

## 12. Tests et Validation

VoiceKit dispose d'une infrastructure de validation logicielle et matérielle à double niveau :

- **3 716 tests unitaires et d'intégration natifs (100 % PASS)** exécutés sans régression (`tests/run_tests.bat`).
- **Compilation automatisée des 10 exemples PlatformIO (100 % PASS)** validée via `tools/validation/validate_examples.py`.
- **Validation matérielle sur M5Stack CoreS3 physique (19/20 PASS)** documentée dans **[docs/release/v0.6.6.2/VALIDATION_CORES3.md](docs/release/v0.6.6.2/VALIDATION_CORES3.md)**.

---

## 13. Dépannage

- **Erreur de compilation C++17 :** Vérifiez que `-std=gnu++11` est retiré via `build_unflags = -std=gnu++11` et remplacé par `build_flags = -std=gnu++17`.
- **Dépassement de partition (`overflowed by ...`) :** Utilisez `partitions_16MB.csv` dans `board_build.partitions`.
- **Erreur de connexion Wi-Fi :** Assurez-vous que votre box diffuse sur le canal 2.4 GHz et que vos identifiants dans `secrets.h` sont exacts.
- **Fermeture de session WebSocket (`code 1007` ou `code 1008`) :** Vérifiez la validité de votre clé d'API sur Google AI Studio ou OpenAI Platform.

---

## 14. Licence et Contributions

- **Licence principale :** [MIT License](LICENSE) — Copyright (c) 2026 Philippe AD / VoxCore Project.
- **Composants tiers :** ESP-SR / WakeNet sous licence Apache 2.0 (Espressif Systems), cJSON sous licence MIT.
- **Contributions :** Les contributions sont les bienvenues via Pull Requests sur la branche de développement. Pour tout signalement de faille de sécurité, consultez **[SECURITY.md](SECURITY.md)**.
