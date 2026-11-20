# VoiceKit — Bibliothèque Vocale pour M5Stack CoreS3

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![ESP32-S3](https://img.shields.io/badge/Target-M5Stack%20CoreS3%20(ESP32--S3)-blue.svg)](https://docs.m5stack.com/en/core/CoreS3)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Version: v0.6.7.2](https://img.shields.io/badge/Version-v0.6.7.2-blue.svg)](CHANGELOG.md)

**Découvrir → Installer → Faire parler → Converser → Développer**

---

## 1. Présentation de VoiceKit

**VoiceKit** est une bibliothèque C++17 pour **M5Stack CoreS3** permettant de créer des applications vocales avec **Google Gemini Live** ou **OpenAI Realtime**. Elle gère les microphones, le haut-parleur, la connexion aux services IA et les interactions vocales, afin de simplifier le développement d'applications sur ESP32-S3.

---

## 2. Ce que l'on peut réaliser

Avec VoiceKit, vous pouvez créer rapidement :

- **Faire parler le CoreS3 (TTS)** : synthétiser et lire à voix haute des phrases en français, anglais, allemand, espagnol ou italien.
- **Reconnaître la parole (STT)** : capturer la voix en Push-to-Talk (PTT) et obtenir sa transcription.
- **Converser en temps réel** : dialoguer naturellement à la voix avec Google Gemini Live ou OpenAI Realtime, avec interaction tactile à deux clics (`PARLER` / `TERMINER`), rejet local du silence (VAD) et arrêt immédiat (`STOP`).
- **Exécuter des commandes locales** : router des commandes matérielles ou métier déterministes hors ligne via un registre sécurisé (`ToolRegistry`).
- **Changer de voix et de langue** : choisir parmi les profils vocaux Gemini et OpenAI avec sauvegarde locale en mémoire NVS (sans jamais enregistrer de clé API).
- **Détecter un mot de réveil hors ligne (expérimental)** : réveiller l'appareil localement avec « Bonjour ESP » (WakeNet 9 / ESP-SR).

---

## 3. Matériel nécessaire

| Élément | Requis | Détails |
|---|---|---|
| **Carte cible** | **M5Stack CoreS3** | ESP32-S3, 16 Mo Flash, 8 Mo PSRAM, écran tactile 320 × 240, double micro ES7210 et haut-parleur AW88298 intégrés |
| **Câble USB-C** | Données + alimentation | 5 V / 1 A minimum recommandé pour le haut-parleur |
| **Réseau Wi-Fi** | **2.4 GHz uniquement** | L'ESP32-S3 ne supporte pas les réseaux 5 GHz |
| **Clé API IA** | Gemini ou OpenAI | [Google AI Studio](https://aistudio.google.com/) (Gemini Live) ou [OpenAI Platform](https://platform.openai.com/) (OpenAI Realtime) |

> **Remarque :** Seul le **M5Stack CoreS3** est officiellement supporté. Les modèles Core Basic, Core2, Atom ou StickC ne sont pas compatibles.

---

## 4. Installation rapide

L'environnement recommandé est **[Visual Studio Code](https://code.visualstudio.com/)** avec l'extension **[PlatformIO IDE](https://platformio.org/)**.

### Option A — Tester un exemple fourni (recommandé pour débuter)

```bash
git clone https://github.com/calmtechnology-ch/VoiceKit.git
cd VoiceKit
```

1. Ouvrez un dossier d'exemple dans VS Code, par exemple `examples/MinimalTTS`.
2. Copiez `include/secrets.example.h` en `include/secrets.h`.
3. Renseignez votre Wi-Fi 2.4 GHz et votre clé API dans `include/secrets.h`.
4. Compilez et téléversez avec PlatformIO (`pio run -t upload`).

### Option B — Utiliser VoiceKit dans votre propre projet PlatformIO

Ajoutez VoiceKit dans votre `platformio.ini` (voir le parcours détaillé et complet dans **[docs/QUICK_START.md](docs/QUICK_START.md)**) :

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
    https://github.com/calmtechnology-ch/VoiceKit.git#main
```

> **Note de version :** Remplacez `#main` par `#v0.6.7.2` une fois la release officielle `v0.6.7.2` publiée sur GitHub. Placez également le fichier `no_ota.csv` (à la racine de ce dépôt) dans votre projet.

---

## 5. Première démonstration vocale

Pour un nouveau projet, le point d'entrée recommandé est la façade haut niveau `<VoiceKit.h>` :

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceKit.h>
#include "secrets.h"

static VoiceKit voice;
static bool spoken = false;

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
        if (!spoken) {
            spoken = true;
            voice.say("Bonjour ! VoiceKit fonctionne sur M5Stack CoreS3.");
        }
    });

    voice.onError([](const String& err) {
        Serial.printf("[VoiceKit Erreur] %s\n", err.c_str());
    });

    voice.begin(vcfg);
}

void loop() {
    M5.update();
    voice.update();
    delay(10);
}
```

Guide pas à pas complet et dépannage : **[docs/QUICK_START.md](docs/QUICK_START.md)**.

---

## 6. Choisir Gemini ou OpenAI

VoiceKit prend en charge les deux fournisseurs temps réel avec la même API :

```cpp
// Google Gemini Live
vcfg.provider = VoiceProviderType::GEMINI;
vcfg.apiKey = GEMINI_KEY;

// OpenAI Realtime
vcfg.provider = VoiceProviderType::OPENAI;
vcfg.apiKey = OPENAI_KEY;
```

| Critère | Google Gemini Live | OpenAI Realtime |
|---|---|---|
| **Modèle par défaut** | `models/gemini-2.5-flash-native-audio-latest` | `gpt-4o-realtime-preview` (configurable) |
| **Clé API** | Google AI Studio (`AIza...`) | OpenAI Platform (`sk-proj-...`) |
| **Changement dynamique** | Supporté via `applyVoiceProfile()` / `setProvider()` | Supporté via `applyVoiceProfile()` / `setProvider()` |

> **Coûts API :** L'utilisation des API cloud Gemini et OpenAI peut engendrer des frais selon votre forfait et votre consommation. Surveillez vos quotas sur les consoles des fournisseurs.

---

## 7. Exemples disponibles

Le dépôt fournit **12 exemples officiels** (11 projets PlatformIO et 1 sketch Arduino IDE), classés par niveau de difficulté dans **[examples/README.md](examples/README.md)** :

| Niveau | Exemples | Objectif |
|---|---|---|
| **Niveau 1 — Débutant** | [`MinimalTTS`](examples/MinimalTTS), [`MinimalSTT`](examples/MinimalSTT), [`MinimalConversation`](examples/MinimalConversation), [`TwoClickConversationLVGL`](examples/TwoClickConversationLVGL) | Faire parler, reconnaître la voix, première conversation Gemini, conversation tactile PTT 2 clics |
| **Niveau 2 — Intermédiaire** | [`OpenAIConversationLVGL`](examples/OpenAIConversationLVGL), [`VoiceSettingsDemo`](examples/VoiceSettingsDemo), [`VoiceDiagnostics`](examples/VoiceDiagnostics), [`WakeWordFoundation`](examples/WakeWordFoundation) | OpenAI + LVGL 9, profils vocaux multilingues NVS, diagnostics, mot de réveil hors ligne |
| **Niveau 3 — Avancé** | [`IntegratedVoiceDemo`](examples/IntegratedVoiceDemo), [`WakeWordConversation`](examples/WakeWordConversation), [`LocalToolRegistryDemo`](examples/LocalToolRegistryDemo) | Orchestrateur complet, conversation mains libres par mot de réveil, outils matériels locaux |
| **Arduino IDE** | [`MinimalTTS_Arduino`](examples/MinimalTTS_Arduino) | Démonstration TTS autonome au format `.ino` |

---

## 8. Applications showcases

Le répertoire **[showcases/](showcases/README.md)** présente trois applications complètes construites sur les API publiques de VoiceKit :

- **[`VoiceAssistant`](showcases/VoiceAssistant)** : assistant vocal conversationnel tactile (LVGL 9, Gemini Live & OpenAI Realtime, PTT 2 clics, VAD, STOP, réglages multilingues). Vitrine principale pour démontrer VoiceKit.
- **[`VoiceSensorStation`](showcases/VoiceSensorStation)** : station environnementale vocale utilisant le capteur externe **M5Stack ENV Pro U169 (Bosch BME688)** (application spécialisée).
- **[`VoiceControl`](showcases/VoiceControl)** : centre de commandes vocales dédié à l'heure suisse et à la date, avec configuration centralisée en fichier unique (FR/EN, Gemini/OpenAI).

Consultez **[showcases/README.md](showcases/README.md)** pour leur état exact de validation.

---

## 9. Documentation avancée

- **[Guide de démarrage rapide](docs/QUICK_START.md)** — Installation pas à pas (exemples et projet personnel) et dépannage.
- **[Catalogue pédagogique des exemples](examples/README.md)** — Choix de l'exemple adapté et comparaison `VoiceKit.h` vs `VoxCoreVoice.h`.
- **[Index complet de la documentation](docs/README.md)** — Architecture, rapports techniques et guides spécialisés.
- **[Interaction vocale 2 clics & VAD](docs/VOICE_INTERACTION.md)** — `VoiceInteractionController` et `VoiceCaptureValidator`.
- **[Registre d'outils locaux & MCP](docs/architecture_tools_registry_ai_mcp.md)** — Commandes matérielles sécurisées.
- **[Politique de sécurité](SECURITY.md)** — Gestion des clés API, masquage série, NVS et signalement de vulnérabilités.
- **[Journal des modifications](CHANGELOG.md)** — Historique détaillé des versions.

---

## 10. Fonctionnalités et limitations

### Stables
- Synthèse vocale (TTS 24 kHz), reconnaissance vocale Push-to-Talk (STT 16 kHz), conversation bidirectionnelle temps réel (Gemini Live & OpenAI Realtime).
- Interaction vocale à deux clics (`VoiceInteractionController`), validation locale VAD (`VoiceCaptureValidator`) et `STOP` prioritaire.
- Profils vocaux multilingues avec persistance NVS (hors clés API) et registre d'outils locaux (`ToolRegistry`).

### Expérimentales
- Détection locale de mot de réveil hors ligne (`WakeWordFoundation` / `WakeWordConversation`, modèle « Bonjour ESP »).
- Mode d'écoute continue adaptative (`ContinuousSTT`).

### Principales limitations
- **M5Stack CoreS3** et **Wi-Fi 2.4 GHz** requis.
- Standard **C++17** (`-std=gnu++17`) obligatoire.
- Partition Flash 8 Mo (`partitions_16MB.csv`) nécessaire pour les projets combinant LVGL 9 et polices UTF-8.
- Détail complet dans **[docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md)**.

---

## 11. Tests et stabilité

VoiceKit dispose d'une validation automatisée reproductible :

- **Tests natifs C++17** (`tests/run_tests.bat`) : validation de la logique métier, des machines à états, du VAD, du PTT 2 clics, du `STOP` prioritaire et de la sécurité NVS.
- **Validation des exemples** (`python tools/validation/validate_examples.py`) : contrôle de structure et compilation PlatformIO des 11 exemples PlatformIO.
- **Projet consommateur externe** : compilation vérifiée hors dépôt.
- **Validation matérielle sur M5Stack CoreS3** : référence historique v0.6.7.1 sur les 8 scénarios MAT-01 à MAT-08 et les 8 scénarios de bascule fournisseur ; voir les résultats détaillés de la release dans **[docs/release/v0.6.7.2/TEST_RESULTS.md](docs/release/v0.6.7.2/TEST_RESULTS.md)**.

---

## 12. Licence et contributions

- **Licence VoiceKit :** [MIT License](LICENSE) — Copyright (c) 2026 Philippe AD / VoxCore Project.
- **Composants tiers :**
  - `cJSON` (tests natifs) : licence MIT.
  - **ESP-SR / WakeNet** (`src/esp_sr/`, incluant `libesp_sr_wakenet_s3.a` et le modèle `wn9l_fr_bonjouresp_tts3`) : fourni par Espressif Systems sous les conditions de licence ESP-SR (utilisation destinée au matériel Espressif ; non couvert par la licence MIT de VoiceKit). Voir **[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)**.
- **Contributions & sécurité :** Les Pull Requests et retours sont les bienvenus. Pour toute question de sécurité, consultez **[SECURITY.md](SECURITY.md)**.
