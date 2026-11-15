# TwoClickConversationLVGL — VoiceKit v0.6.7.0 Experimental

Exemple de référence démontrant l'interaction vocale réutilisable à deux clics (PTT à bascule), la validation locale de parole par VAD holistique et la commande STOP prioritaire avec **LVGL 9** sur **M5Stack CoreS3**.

---

## 1. Fonctionnalités démontrées

1. **Paradigme à deux clics (PARLER / TERMINER) :**
   - **1er clic :** Démarre l'acquisition microphone (`startListening()`).
   - **2ème clic :** Clôture l'écoute, évalue localement le flux audio (`VoiceCaptureValidator`).
   - **Si parole confirmée :** Invoque `stopListening()` pour déclencher la synthèse et la réponse IA.
   - **Si silence ou bruit bref :** Invoque `cancelListening()` pour couper le micro immédiatement **sans générer de trame réseau vers OpenAI**, économisant bande passante et tokens.
2. **Filtrage anti-rebond matériel (120 ms) :**
   - Immunise contre les micro-rebonds de la dalle tactile capacitive du CoreS3.
3. **Commande STOP prioritaire :**
   - Bouton STOP dédié accessible à tout moment.
   - Un clic sur le bouton d'action pendant la lecture audio ou le traitement coupe instantanément le haut-parleur AW88298 et réaligne la session.
4. **Surveillance non bloquante (Watchdogs intégrés) :**
   - Timeout de capture micro (15 s) avec décision VAD automatique.
   - Timeout d'attente de réponse serveur (15 s).
   - Reprise automatique après anomalie.
5. **Indépendance architecturale :**
   - `VoiceInteractionController` et `VoiceCaptureValidator` sont totalement indépendants de LVGL et peuvent être pilotés par un bouton tactile, un bouton physique M5 ou une commande série.

---

## 2. Configuration matérielle et logicielle

- **Carte :** M5Stack CoreS3 (ESP32-S3, 16 Mo Flash, 8 Mo PSRAM).
- **Audio :** Codec ES7210 (Microphone PCM 16 kHz) et AW88298 (Haut-parleur I2S).
- **Écran :** ILI9342C (320x240) avec tactile capacitif FT6336U.
- **Framework :** Arduino / ESP-IDF via PlatformIO.
- **Bibliothèque graphique :** LVGL 9.2.2+.

---

## 3. Configuration des identifiants (`include/secrets.h`)

Créez ou modifiez `include/secrets.h` :

```cpp
#pragma once

#define WIFI_SSID     "VOTRE_WIFI_SSID"
#define WIFI_PASSWORD "VOTRE_WIFI_PASSWORD"

// Clé OpenAI Realtime
#define OPENAI_API_KEY "sk-proj-..."
```

Pour basculer vers Gemini Live, modifiez `include/app_config.h` :
```cpp
#define USE_OPENAI 0
#define USE_GEMINI 1
```

---

## 4. Compilation et téléversement

```bash
pio run -d examples/TwoClickConversationLVGL
pio run -d examples/TwoClickConversationLVGL -t upload
```
