# Notes de Publication — VoiceKit v0.6.7.2 (Public Release & Easy Onboarding)

**Date de préparation :** 19 novembre 2026  
**Dépôt :** https://github.com/calmtechnology-ch/VoiceKit  
**Matériel cible :** M5Stack CoreS3 (ESP32-S3)  
**Licence :** MIT *(composants tiers ESP-SR / WakeNet soumis aux conditions Espressif)*

---

## 1. Présentation de la bibliothèque

**VoiceKit** est une bibliothèque C++17 pour **M5Stack CoreS3** permettant de concevoir des applications vocales temps réel avec **Google Gemini Live** ou **OpenAI Realtime**. Elle prend en charge l'acquisition microphonique (ES7210), la restitution haut-parleur (AW88298), le transport WebSocket sécurisé TLS et la coordination des interactions vocales, afin de permettre aux développeurs de se concentrer sur leur application.

La version **v0.6.7.2** consolide la sécurité, la documentation et le parcours de prise en main pour la première diffusion publique de la bibliothèque, sans modification des API publiques ni refonte architecturale.

---

## 2. Public cible

- Développeurs C++/Arduino et PlatformIO souhaitant créer rapidement un assistant vocal, un afficheur parlant ou un objet connecté vocal sur **M5Stack CoreS3**.
- Makers, enseignants et intégrateurs recherchant une base structurée combinant interaction vocale cloud (Gemini Live / OpenAI Realtime) et commandes locales déterministes sur ESP32-S3.

---

## 3. Fonctionnalités disponibles

### Stables
- **Synthèse vocale (TTS 24 kHz)** : énonciation fluide via `voice.say(...)`.
- **Reconnaissance vocale Push-to-Talk (STT 16 kHz)** : capture et transcription via `voice.listen()` / `voice.stopListening()`.
- **Conversation vocale bidirectionnelle temps réel** avec Google Gemini Live ou OpenAI Realtime.
- **Interaction vocale stabilisée à 2 clics** (`VoiceInteractionController`) + **validation locale VAD** (`VoiceCaptureValidator`) rejetant silence et bruits brefs avant tout envoi réseau.
- **Arrêt prioritaire `STOP`** non réentrant et retour garanti à `READY`.
- **Profils vocaux multilingues** (`VoiceProfileManager` : français, anglais, allemand, espagnol, italien ; catalogue Gemini & OpenAI ; persistance NVS sans clé API).
- **Registre local d'outils sécurisé** (`ToolRegistry` : `READ_ONLY`, `WRITE`, `SENSITIVE` avec confirmation obligatoire).
- **Pipeline vocal intégré** (`IntegratedVoicePipeline`).

### Expérimentales
- **Détection locale de mot de réveil hors ligne** (`WakeWordFoundation` / `WakeWordConversation`, modèle WakeNet 9 « Bonjour ESP »).
- **Écoute continue adaptative** (`ContinuousSTT`).

---

## 4. Matériel supporté

- **Exclusivement M5Stack CoreS3** (ESP32-S3 dual-core 240 MHz, 16 Mo Flash, 8 Mo PSRAM, écran tactile IPS 320 × 240, double micro ES7210, haut-parleur AW88298).
- **Wi-Fi 2.4 GHz** requis (le 5 GHz n'est pas supporté par l'ESP32-S3).
- Câble USB-C données avec alimentation stable (≥ 5 V / 1 A recommandé).

---

## 5. Installation

1. Installez **VS Code** et l'extension **PlatformIO IDE**.
2. **Débutants (Parcours A) :** clonez le dépôt, ouvrez `examples/MinimalTTS`, copiez `include/secrets.example.h` en `include/secrets.h`, renseignez votre Wi-Fi 2.4 GHz et votre clé API, puis lancez `pio run -t upload`.
3. **Projet personnel (Parcours B) :** configurez `platformio.ini` (C++17 `-std=gnu++17`, `no_ota.csv`, `m5stack/M5CoreS3@1.0.0`, `m5stack/M5Unified@0.2.23`, et dépendance Git VoiceKit).

Guide complet et dépannage : **[`docs/QUICK_START.md`](../../QUICK_START.md)**.

---

## 6. Exemples conseillés pour démarrer

1. [`examples/MinimalTTS`](../../../examples/MinimalTTS/) — Faire parler le CoreS3 (premier test).
2. [`examples/MinimalSTT`](../../../examples/MinimalSTT/) — Reconnaissance vocale Push-to-Talk.
3. [`examples/MinimalConversation`](../../../examples/MinimalConversation/) — Première conversation avec Gemini Live.
4. [`examples/TwoClickConversationLVGL`](../../../examples/TwoClickConversationLVGL/) — Conversation tactile LVGL 9 avec PTT 2 clics, VAD local et `STOP`.
5. [`showcases/VoiceAssistant`](../../../showcases/VoiceAssistant/) — Application vitrine complète (OpenAI Realtime ↔ Gemini Live).

Catalogue complet des 12 exemples : **[`examples/README.md`](../../../examples/README.md)**.

---

## 7. Fournisseurs IA & Avertissement sur les coûts API

VoiceKit prend en charge **Google Gemini Live** et **OpenAI Realtime** (modèles audio temps réel via WebSocket sécurisé TLS).

> **Avertissement coûts API cloud :** L'utilisation des API Google Gemini Live et OpenAI Realtime est soumise aux quotas et aux grilles tarifaires de chaque fournisseur (facturation potentielle par jeton ou seconde audio). L'utilisateur est responsable de la gestion de ses clés API, de ses plafonds de dépenses et de la surveillance de sa consommation.

---

## 8. Limitations connues

- **M5Stack CoreS3 uniquement** et **Wi-Fi 2.4 GHz obligatoire**.
- **Standard C++17 obligatoire** (`-std=gnu++17`).
- **Partition Flash 8 Mo** (`partitions_16MB.csv`) requise pour les applications utilisant LVGL 9 avec polices UTF-8.
- **Clés API jamais stockées en NVS** (politique de sécurité stricte INV-10).
- Détail complet : **[`docs/KNOWN_LIMITATIONS.md`](../../KNOWN_LIMITATIONS.md)**.

---

## 9. Tests réalisés

- **Tests natifs C++17 (`tests/run_tests.bat`) :** exécutés sur compilateur `g++` C++17 — voir le bilan exact et transparent dans **[`TEST_RESULTS.md`](TEST_RESULTS.md)**.
- **Compilation des exemples (`tools/validation/validate_examples.py`) :** validation de structure des 12 exemples et compilation PlatformIO des 11 exemples PlatformIO (l'exemple Arduino IDE `MinimalTTS_Arduino` est distingué séparément) — voir **[`EXAMPLES_VALIDATION.md`](EXAMPLES_VALIDATION.md)**.
- **Installation externe autonome :** validée par compilation PlatformIO hors dépôt sans `symlink://` — voir **[`INSTALLATION_VALIDATION.md`](INSTALLATION_VALIDATION.md)**.
- **Tests matériels CoreS3 :** référence v0.6.7.1 (8/8 MAT-01..MAT-08 + 8/8 bascule fournisseur dans `VoiceAssistant`) ; essais physiques spécifiques v0.6.7.2 planifiés manuellement (`NOT TESTED` lors de la préparation automatisée) — voir **[`TEST_RESULTS.md`](TEST_RESULTS.md)**.
