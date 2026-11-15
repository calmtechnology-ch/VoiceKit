# TwoClickConversationLVGL — VoiceKit v0.6.7.1

Exemple de référence démontrant l'interaction vocale réutilisable à deux clics (PTT à bascule), la validation locale de parole par VAD holistique et la commande STOP prioritaire avec **LVGL 9** sur **M5Stack CoreS3**.

---

## 1. Objectif

Fournir un exemple autonome de référence pour l'interaction vocale conversationnelle stabilisée (v0.6.7.1) utilisant les composants réutilisables `VoiceInteractionController` et `VoiceCaptureValidator`, avec interface graphique tactile **LVGL 9**, en éliminant les captures silencieuses ou parasites avant toute requête réseau.

---

## 2. Fonctionnalités

1. **Paradigme Push-to-Talk à deux clics (PARLER / TERMINER) :**
   - **1er clic :** Démarre l'acquisition microphone (`startListening()`).
   - **2ème clic :** Clôture l'écoute, évalue localement le flux audio (`VoiceCaptureValidator`).
   - **Si parole confirmée :** Invoque `stopListening()` pour déclencher la synthèse et la réponse IA.
   - **Si silence ou bruit bref :** Invoque `cancelListening()` pour couper le micro immédiatement **sans générer de trame réseau**, économisant bande passante et tokens.
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

## 3. Matériel requis

- **Carte :** M5Stack CoreS3 (ESP32-S3 dual-core LX7 @ 240 MHz, 16 Mo Flash, 8 Mo PSRAM).
- **Audio :** Codec ES7210 (Microphone PCM 16 kHz) et AW88298 (Haut-parleur I2S 24 kHz).
- **Écran :** ILI9342C (320 × 240) avec dalle tactile capacitive FT6336U.
- **Alimentation :** Câble USB-C fournissant 5V / 1A.

---

## 4. Configuration

1. Copier le modèle d'identifiants :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Éditer `include/secrets.h` avec vos identifiants :
   ```cpp
   #pragma once

   #define WIFI_SSID     "VOTRE_WIFI_SSID"
   #define WIFI_PASSWORD "VOTRE_WIFI_PASSWORD"

   // Clé OpenAI Realtime
   #define OPENAI_API_KEY "sk-proj-..."

   // Clé Gemini Live (si USE_GEMINI est activé)
   #define GEMINI_API_KEY "AIzaSy..."
   ```
3. Pour basculer vers Gemini Live, modifiez `include/app_config.h` :
   ```cpp
   #define USE_OPENAI 0
   #define USE_GEMINI 1
   ```

---

## 5. Compilation

Compiler avec PlatformIO :
```bash
pio run -d examples/TwoClickConversationLVGL
```

---

## 6. Téléversement

1. Téléverser sur la carte connectée :
   ```bash
   pio run -d examples/TwoClickConversationLVGL -t upload
   ```
2. Ouvrir le moniteur série :
   ```bash
   pio device monitor -d examples/TwoClickConversationLVGL -b 115200
   ```

---

## 7. Mode d'utilisation

1. À la mise sous tension, l'en-tête affiche l'état Wi-Fi et WebSocket.
2. Dès que l'état indique **PRÊT**, toucher le bouton vert **PARLER**.
3. Le bouton devient ambre **TERMINER** et le statut indique **ÉCOUTE EN COURS**.
4. Parler clairement dans le microphone du CoreS3.
5. Toucher le bouton **TERMINER** pour clore la capture.
6. Le validateur évalue l'enregistrement :
   - Si parole détectée, statut **SYNTHÈSE EN COURS** puis élocution de l'assistant.
   - Si silence ou tapotement bref, rejet immédiat avec statut **SILENCE REJETÉ**.
7. Toucher le bouton rouge **STOP** à tout moment interrompt immédiatement l'action.

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| MAT-01 | Conversation normale | Cliquer PARLER, parler 2-3 s, cliquer TERMINER | Parole validée, réponse vocale fluide de l'assistant. |
| MAT-02 | Silence rejeté | Cliquer PARLER, rester silencieux, cliquer TERMINER | Rejet SILENCE, micro coupé localement, 0 requête IA. |
| MAT-03 | Bruit bref rejeté | Cliquer PARLER, tapoter 1 fois la table, cliquer TERMINER | Rejet BRIEF_NOISE, micro coupé, retour à PRÊT. |
| MAT-04 | Double clic accidentel | Cliquer deux fois très rapidement (< 120 ms) | Filtrage anti-rebond matériel actif, écoute maintenue. |
| MAT-05 | STOP pendant réponse | Toucher STOP pendant que l'assistant parle | Coupure audio instantanée, retour à PRÊT. |
| MAT-06 | STOP pendant écoute | Toucher STOP pendant l'écoute micro | Coupure micro immédiate, annulation locale. |
| MAT-07 | Rafale de clics | Toucher frénétiquement le bouton tactile | Aucune incohérence ni plantage, transitions stables. |
| MAT-08 | Timeout microphone | Cliquer PARLER, parler sans cliquer TERMINER pendant > 15 s | Clôture automatique à 15 s, évaluation VAD et transmission. |

---

## 9. Résultats attendus

- **8/8 PASS** sur les tests matériels CoreS3 réels.
- Affichage LVGL 9 fluide sans déchirement (> 30 FPS).
- Zéro trame IA envoyée sur silence ou tapotement parasite.
- Stabilité mémoire continue sans fuite de heap.

---

## 10. Limitations connues

- En environnement sonore extrêmement bruyant (> 85 dB RMS continu), le plancher adaptatif peut nécessiter quelques secondes d'ambiance pour se caler.
- L'utilisation de LVGL 9 requiert un modèle de carte avec PSRAM active (CoreS3 recommandé).
