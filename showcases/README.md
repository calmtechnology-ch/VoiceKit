# Applications Vitrines (Showcases) — VoiceKit v0.6.7.2

Ce répertoire regroupe les **trois applications applicatives complètes** construites exclusivement sur les API publiques de VoiceKit pour **M5Stack CoreS3**, sans modifier les exemples pédagogiques de base.

> **Note :** L'ancienne application `OrientationClockVoice` a été archivée dans `archive/examples/OrientationClockVoice/`. Elle ne fait plus partie des showcases actifs.

---

## Vue d'ensemble

| Application | Rôle | Matériel requis | Fournisseurs IA | Statut de maturité |
|---|---|---|---|---|
| **[`VoiceAssistant`](VoiceAssistant/)** | Assistant vocal conversationnel généraliste tactile (LVGL 9) | M5Stack CoreS3 | OpenAI Realtime & Google Gemini Live (bascule dynamique) | **Vitrine principale** — Tests matériels CoreS3 validés (bascule IA & STOP) |
| **[`VoiceSensorStation`](VoiceSensorStation/)** | Station environnementale vocale (température, humidité, pression, gaz brut) | M5Stack CoreS3 + **ENV Pro U169 (BME688)** | OpenAI Realtime & Google Gemini Live | **En développement** — Tests natifs et compilation validés ; tests matériels `NOT RUN` |
| **[`VoiceControl`](VoiceControl/)** | Centre de commandes vocales dédié à l'heure suisse et à la date | M5Stack CoreS3 | Google Gemini Live *(défaut)* ou OpenAI Realtime *(au démarrage)* | **En développement** — Tests natifs et compilation validés ; validation matérielle en cours |

---

## 1. VoiceAssistant (`showcases/VoiceAssistant/`)

**Candidat principal pour démontrer les capacités complètes de VoiceKit.**

- **Fonctionnalités :**
  - Assistant conversationnel généraliste avec interface tactile LVGL 9 (320 × 240, polices françaises accentuées, zone de réponse défilable).
  - Push-to-Talk à deux clics (`PARLER` / `TERMINER`) avec validation vocale locale (`VoiceCaptureValidator`) et bouton `STOP` prioritaire.
  - Bascule dynamique entre **OpenAI Realtime** et **Google Gemini Live**, changement de langue et de voix, sauvegarde du profil en NVS (hors clés API).
- **Configuration :** `include/secrets.example.h` → `include/secrets.h` et `include/app_config.h`.
- **État de validation réel :**
  - **Compilation PlatformIO :** Validée (`pio run -d showcases/VoiceAssistant -e m5stack-cores3`).
  - **Tests matériels CoreS3 effectués :**
    - Audit bascule fournisseur ([`tests/PROVIDER_SWITCH_AUDIT.md`](VoiceAssistant/tests/PROVIDER_SWITCH_AUDIT.md)) : **8/8 scénarios matériels PASS** sur CoreS3 (T1 démarrage OpenAI, T2 démarrage Gemini, T3 OpenAI→Gemini, T4 Gemini→OpenAI, T5 bascule après STOP, T6 bascule après reconnexion, T7 rejet clé manquante, T8 4 bascules successives sans redémarrage).
    - Audit STOP & récupération de session ([`tests/STOP_SESSION_AUDIT.md`](VoiceAssistant/tests/STOP_SESSION_AUDIT.md)) et rapport correctif ([`tests/CORRECTIVE_REPORT.md`](VoiceAssistant/tests/CORRECTIVE_REPORT.md)) : défauts identifiés et corrigés, avec vérification matérielle des correctifs de bascule et de STOP.
- **Documentation :** [showcases/VoiceAssistant/README.md](VoiceAssistant/README.md)

---

## 2. VoiceSensorStation (`showcases/VoiceSensorStation/`)

**Application spécialisée combinant mesures environnementales locales et dialogue IA.**

- **Matériel supplémentaire obligatoire :** Module **M5Stack Unit ENV Pro (U169 / Bosch BME688)** connecté sur le Port A (Grove I²C externe, GPIO 2 SDA / GPIO 1 SCL).
- **Fonctionnalités :**
  - Mesure non bloquante toutes les 2 s : température (°C), humidité (%), pression (hPa) et résistance de gaz brute (kOhm, sans IAQ/BSEC).
  - Commandes vocales locales déterministes (7 intentions environnementales) et questions analytiques vers Gemini Live ou OpenAI Realtime avec injection des mesures réelles.
  - Fonctionnement autonome des mesures et de l'affichage même hors ligne (sans Wi-Fi ni IA).
- **État de validation réel :**
  - **Tests natifs :** Suite dédiée présente (`tests/test_station_core.cpp`).
  - **Compilation PlatformIO :** Validée.
  - **Tests matériels CoreS3 + ENV Pro U169 :** **NON EXÉCUTÉS (`NOT RUN`)** dans le plan de validation ([`tests/TEST_PLAN.md`](VoiceSensorStation/tests/TEST_PLAN.md) : VSS-01 à VSS-16 et SW-01 à SW-10 marqués `NOT RUN`).
  - **Statut :** En développement (ne pas considérer comme validé matériellement tant que VSS-01..VSS-16 ne sont pas exécutés).
- **Documentation :** [showcases/VoiceSensorStation/README.md](VoiceSensorStation/README.md)

---

## 3. VoiceControl (`showcases/VoiceControl/`)

**Centre de commandes vocales déterministe limité exclusivement à l'heure suisse et à la date.**

- **Configuration réelle (`src/config/VoiceControlConfig.h`) :**
  - **Configuration centralisée en fichier unique** (aucun écran de paramètres embarqué sur le CoreS3).
  - **Langue unique (`VC_LANGUAGE`) :** `"fr"` par défaut (support `"fr"` ou `"en"`, appliqué à l'interface LVGL, au STT et au TTS ; surcharges optionnelles `VC_UI_LANGUAGE`, `VC_STT_LANGUAGE`, `VC_TTS_LANGUAGE`).
  - **Fournisseur IA au démarrage (`VC_AI_PROVIDER`) :** `VC_PROVIDER_GEMINI` par défaut (ou `VC_PROVIDER_OPENAI`, sans bascule dynamique à chaud).
  - **Modèles configurés :** `models/gemini-2.5-flash-native-audio-latest` (voix `Aoede`) pour Gemini ; `gpt-realtime-2.1-mini` (voix `alloy`) pour OpenAI.
  - **Instructions système :** `VC_SYSTEM_PROMPT_FR` / `VC_SYSTEM_PROMPT_EN` restreignant strictement l'assistant à l'heure et à la date (toute autre demande est rejetée poliment).
  - **Fuseau horaire :** `Europe/Zurich` (`CET-1CEST,M3.5.0,M10.5.0/3`) avec synchronisation NTP non bloquante.
  - **Scripts de compilation rapide :** `build_fr_gemini.bat`, `build_fr_openai.bat`, `build_en_gemini.bat`, `build_en_openai.bat`, `build_all.bat`, `build_menu.bat`.
- **État de validation réel :**
  - **Tests natifs :** Suite dédiée présente (`tests/test_voicecontrol_core.cpp`).
  - **Compilation PlatformIO :** Validée pour les variantes configurées.
  - **Tests matériels CoreS3 :** Non documentés dans un rapport matériel dédié (`NOT TESTED` pour la release v0.6.7.2).
  - **Statut :** En développement.
- **Documentation :** [showcases/VoiceControl/README.md](VoiceControl/README.md)
