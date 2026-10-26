# Exemple MinimalSTT — VoiceKit v0.5.3.1

Exemple autonome de reconnaissance vocale **STT (Speech-to-Text)** en temps réel sur **M5Stack CoreS3**, utilisant la bibliothèque **VoiceKit v0.5.3.1** connectée à l'API **Google Gemini Live**.

---

## 1. Fonctionnalités

- **Dictée vocale Push-to-Talk** : Appui maintenu sur le bouton tactile « DICTER » pour capturer la voix, relâchement pour finaliser la transcription.
- **Audio haute fidélité** : Capture microphone ES7210 stéréo matérielle downmixée en PCM 16 kHz mono 16 bits par double-buffering DMA non bloquant.
- **Streaming direct vers Gemini Live** : Envoi asynchrone des blocs audio vers l'API WebSocket bidirectionnelle avec signalisation officielle `activityStart` / `activityEnd`.
- **Transcriptions en direct** :
  - Affichage instantané des transcriptions **partielles** (intérim).
  - Affichage clair et validation de la transcription **finale**.
  - Sortie simultanée sur le moniteur série (115200 baud).
- **Zéro perturbation sonore** : Haut-parleur silencieux pendant l'écoute pour éliminer tout risque de larsen acoustique.
- **Interface graphique réactive** : Conçue directement sur M5Unified (sans LVGL requis).

---

## 2. États de l'Application

| État | Affichage Écran | Description |
| :--- | :--- | :--- |
| **INITIALISATION** | Orange | Connexion Wi-Fi et handshake WebSocket TLS Gemini Live |
| **PRÊT** | Vert | Session active, prêt à écouter la voix |
| **ÉCOUTE** | Rouge | Microphone actif, capture et envoi PCM 16 kHz en continu |
| **TRANSCRIPTION** | Cyan | Capture terminée, réception du texte final par Gemini Live |
| **TERMINÉ** | Vert | Transcription finale affichée et prête pour une nouvelle dictée |
| **ERREUR** | Magenta | Signalement d'anomalie réseau ou matérielle |

---

## 3. Configuration et Déploiement

### 3.1. Identifiants Wi-Fi et Clé Gemini
Copiez le fichier `include/secrets.example.h` vers `include/secrets.h` :
```cpp
#pragma once
#define WIFI_SSID     "VOTRE_BOX_WIFI"
#define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE"
#define GEMINI_KEY    "AIzaSy..." // Clé API Google Gemini Live
```

### 3.2. Compilation avec PlatformIO
```bash
pio run -d examples/MinimalSTT
```

### 3.3. Téléversement
```bash
pio run -d examples/MinimalSTT -t upload
pio device monitor -b 115200
```
