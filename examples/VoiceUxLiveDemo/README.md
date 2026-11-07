# VoiceKit v0.6.4.2 — VoiceUxLiveDemo

Application de démonstration du **Voice UX Live Bridge** pour M5Stack CoreS3 (ESP32-S3).

Cette application relie pour la première fois la reconnaissance vocale en streaming STT de VoiceKit, le filtrage et l'arbitrage d'intentions locales `VoiceUxController`, l'historique conversationnel léger `ReplyMemory` et la synthèse vocale TTS haute fluidité de VoiceKit.

---

## 1. Caractéristiques de l'exemple

- **Capture vocale réelle sur M5Stack CoreS3** : Enregistrement micro 16 kHz par maintien de la touche tactile `PARLER` (ou commande série `P` / `R`).
- **Transcription STT en direct** : Transcription unitaire via VoiceKit STT (Google Gemini Live ou OpenAI Realtime).
- **Arbitrage d'intentions 100% local** :
  - **REPEAT** : « Répète » prononcé au micro déclenche immédiatement la répétition TTS locale de la dernière réponse sans appel à un modèle conversationnel distant.
  - **PREVIOUS** : « Réponse précédente » restitue l'avant-dernière réponse mémorisée.
  - **STOP** : « Arrête » ou appui sur le bouton tactile rouge `STOP` prioritaire.
  - **PAUSE / RESUME** : « Mets en pause » suspend les requêtes ordinaires ; « Continue » reprend la session.
- **Réponses déterministes de test pré-chargées** :
  1. « La température actuelle est de vingt-deux degrés. »
  2. « L'humidité relative est de quarante-huit pour cent. »
- **Sanctuaire audio respecté** : Aucune restitution TTS pendant une capture micro, micro libéré avant restitution sonore, dédoublonnage des transcriptions finales et rejet strict des événements tardifs ou annulés.
- **Interface graphique M5Unified / M5GFX (320 × 240)** : Sans dépendance LVGL 9, affichage clair des statuts, de la transcription et de la mémoire.
- **Console de diagnostic série (115 200 bauds)** : Mode hybride permettant d'alterner voix réelle et injections simulées.

---

## 2. Configuration (`secrets.h`)

Copiez le fichier d'exemple et renseignez vos identifiants Wi-Fi et votre clé API :

```bash
cp include/secrets.example.h include/secrets.h
```

Éditez `include/secrets.h` :

```cpp
#pragma once
#define WIFI_SSID     "Votre_Box_WiFi"
#define WIFI_PASSWORD "Votre_Mot_De_Passe"
#define GEMINI_KEY    "AIzaSy..."
```

---

## 3. Compilation et Téléversement

Avec PlatformIO CLI :

```bash
pio run -d examples/VoiceUxLiveDemo -t upload
pio device monitor -d examples/VoiceUxLiveDemo -b 115200
```

---

## 4. Commandes du Moniteur Série (115 200 bauds)

```text
P          : Démarrer la capture microphone
R          : Terminer la capture microphone
S          : STOP prioritaire
T <texte>  : Injecter une transcription simulée (ex: T Répète)
M <texte>  : Mémoriser manuellement une réponse
H          : Afficher l'historique ReplyMemory
C          : Effacer l'historique
D          : Activer/désactiver Voice UX
?          : Afficher l'aide
```

---

## 5. Exemple de séquence de test

1. À la mise sous tension, le CoreS3 se connecte au Wi-Fi et initialise VoiceKit et VoiceUxLiveBridge.
2. Les deux phrases locales déterministes sont insérées dans `ReplyMemory`.
3. Maintenez le bouton tactile **PARLER** sur l'écran et dites à voix haute :
   > « Répète »
4. Relâchez le bouton.
5. La transcription STT finale `[STT][REAL] Transcription finale : Répète` est analysée localement.
6. Le haut-parleur restitue immédiatement à voix haute :
   > « L'humidité relative est de quarante-huit pour cent. »
7. Dites ensuite :
   > « Réponse précédente »
8. Le haut-parleur restitue :
   > « La température actuelle est de vingt-deux degrés. »

