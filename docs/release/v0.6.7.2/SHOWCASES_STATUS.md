# VoiceKit v0.6.7.2 — État des Applications Showcases (`SHOWCASES_STATUS.md`)

**Date :** 19 novembre 2026  
**Bibliothèque :** VoiceKit v0.6.7.2  
**Répertoire analysé :** `showcases/`

---

## 1. Synthèse Comparative

| Application | Matériel requis | Tests natifs | Compilation PlatformIO | Validation matérielle CoreS3 | Statut recommandé |
|---|---|---|---|---|---|
| **`VoiceAssistant`** | M5Stack CoreS3 | N/A (validation via audits) | ✅ Validée (`m5stack-cores3`) | ✅ **8/8 PASS** sur bascule OpenAI↔Gemini & STOP (`PROVIDER_SWITCH_AUDIT.md`) | **Vitrine principale** (recommandée pour démonstration) |
| **`VoiceSensorStation`** | M5Stack CoreS3 + **ENV Pro U169 (BME688)** | Suite présente (`test_station_core.cpp`) | ✅ Validée | ⚠️ **NOT RUN** (`TEST_PLAN.md` : VSS-01..16 & SW-01..10 non exécutés) | **En développement** (application spécialisée) |
| **`VoiceControl`** | M5Stack CoreS3 | Suite présente (`test_voicecontrol_core.cpp`) | ✅ Validée | ⚠️ **NOT TESTED** (aucun rapport matériel dédié en v0.6.7.2) | **En développement** (périmètre heure/date) |

> **Ancienne référence nettoyée :** `OrientationClockVoice` a été archivée dans `archive/examples/OrientationClockVoice/` et retirée de `showcases/README.md`.

---

## 2. Détail par Application

### 2.1 `showcases/VoiceAssistant`
- **Rôle :** Assistant conversationnel généraliste avec interface tactile LVGL 9 (320 × 240), PTT à deux clics (`VoiceInteractionController`), VAD local (`VoiceCaptureValidator`), bouton `STOP` prioritaire, bascule dynamique OpenAI Realtime ↔ Google Gemini Live et persistance NVS des profils vocaux.
- **Rapports analysés :**
  - `showcases/VoiceAssistant/tests/PROVIDER_SWITCH_AUDIT.md` (19 nov. 2026) : 6 causes racines identifiées et corrigées ; **8/8 scénarios matériels PASS** sur CoreS3 (`COM9`), incluant T1 (démarrage OpenAI), T2 (démarrage Gemini), T3 (OpenAI→Gemini), T4 (Gemini→OpenAI), T5 (bascule après STOP sans fuite `cJSON`), T6 (bascule pendant reconnexion), T7 (rejet clé manquante), T8 (4 bascules successives O→G→O→G sans redémarrage).
  - `showcases/VoiceAssistant/tests/STOP_SESSION_AUDIT.md` (18 nov. 2026) : analyse détaillée des interruptions STOP et du verrouillage `esp_websocket_client`, complétée par les correctifs dans `AppController` et `VoiceKit/src/`.
  - `showcases/VoiceAssistant/tests/CORRECTIVE_REPORT.md` (18 nov. 2026) : conformité LVGL 9, zone de réponse défilable, historique atomique thread-safe et verrouillage du bouton `PARLER`.
- **Évaluation :** Candidat principal solide pour démontrer les capacités complètes de VoiceKit.

### 2.2 `showcases/VoiceSensorStation`
- **Rôle :** Station environnementale vocale mesurant température, humidité, pression atmosphérique et résistance de gaz brute (kOhm, sans BSEC/IAQ) via le module **M5Stack ENV Pro U169 (Bosch BME688)** sur Port A (GPIO 2 SDA / GPIO 1 SCL).
- **Rapports analysés :**
  - `showcases/VoiceSensorStation/README.md` et `showcases/VoiceSensorStation/tests/TEST_PLAN.md`.
- **Évaluation :** Architecture propre (mesures non bloquantes toutes les 2 s, commandes locales déterministes + questions analytiques IA, fonctionnement hors ligne). Toutefois, les 16 scénarios matériels (`VSS-01` à `VSS-16`) et les 10 scénarios de bascule (`SW-01` à `SW-10`) sont explicitement marqués **`NOT RUN`**. L'application est donc maintenue mais clairement identifiée comme **en développement**.

### 2.3 `showcases/VoiceControl`
- **Rôle :** Centre de commandes vocales déterministe limité exclusivement à l'heure suisse (`Europe/Zurich`, CET/CEST) et à la date, avec configuration centralisée dans `src/config/VoiceControlConfig.h` (sans écran de réglages embarqué).
- **Configuration réelle vérifiée dans `src/config/VoiceControlConfig.h` :**
  - Langue par défaut : `VC_LANGUAGE = "fr"` (support `"fr"` et `"en"`, appliqué à UI/STT/TTS).
  - Fournisseur IA par défaut : `VC_AI_PROVIDER = VC_PROVIDER_GEMINI` (note : `showcases/VoiceControl/README.md` a été corrigé pour refléter ce défaut réel au lieu de `VC_PROVIDER_OPENAI`).
  - Modèles : `models/gemini-2.5-flash-native-audio-latest` (`Aoede`) et `gpt-realtime-2.1-mini` (`alloy`).
  - Instructions système : `VC_SYSTEM_PROMPT_FR` / `VC_SYSTEM_PROMPT_EN` limitant strictement les réponses à l'heure et à la date.
- **Évaluation :** Tests natifs (`test_voicecontrol_core.cpp`) et scripts de build Windows présents, mais aucun rapport d'essais matériels formalisé pour v0.6.7.2 (`NOT TESTED`). Identifiée comme **en développement**.
