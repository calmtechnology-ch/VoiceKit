# MinimalConversation — Exemple de Conversation Vocale Bidirectionnelle

Exemple autonome de conversation vocale bidirectionnelle native (Mode B) pour **M5Stack CoreS3** utilisant **VoiceKit v0.4.0** et l'API WebSocket **Google Gemini Live** (`models/gemini-3.8-live`).

---

## 1. Fonctionnalités

- **Push-to-Talk fluide** : Appui maintenu sur « **PARLER** » pour capturer la voix en PCM 16 kHz.
- **Streaming audio temps réel** : Transmission continue des fragments PCM Base64 vers Gemini Live.
- **Transcription en direct** : Affichage de la question reconnue (`Vous : ...`).
- **Réponse vocale native** : Restitution sonore PCM 24 kHz haute fidélité par le haut-parleur AW88298 du CoreS3.
- **Anti-larsen strict** : Commutation exclusive du bus I2S via `HardwareManager`.
- **Bouton STOP interactif** : Interruption instantanée possible pendant la réflexion ou la restitution sonore.
- **Pilotage série** : Commandes clavier dans le terminal (`P` pour Parler, `R` pour Relâcher, `S` pour Stop).

---

## 2. Configuration (`secrets.h`)

Créez le fichier `include/secrets.h` à partir de `include/secrets.example.h` :

```cpp
#pragma once
#define WIFI_SSID     "VOTRE_WIFI_SSID"
#define WIFI_PASSWORD "VOTRE_WIFI_PASSWORD"
#define GEMINI_API_KEY "VOTRE_CLE_GEMINI_AI_STUDIO"
```

---

## 3. Compilation et Téléversement

```bash
# Compilation du firmware
pio run -d examples/MinimalConversation

# Téléversement vers le M5Stack CoreS3
pio run -d examples/MinimalConversation -t upload

# Ouverture du moniteur série
pio device monitor -d examples/MinimalConversation -b 115200
```
