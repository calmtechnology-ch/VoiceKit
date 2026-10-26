# Exemple OpenAIConversation — VoiceKit v0.6.0

Démonstration de dialogue vocal bidirectionnel complet avec **OpenAI Realtime API** (`wss://api.openai.com/v1/realtime`) sur **M5Stack CoreS3**.

## Présentation

Cet exemple exploite l'architecture multi-fournisseurs introduite dans VoiceKit v0.6.0 pour établir une conversation vocale naturelle en français :
- **Connexion TLS WebSocket directe** depuis l'ESP32-S3 vers OpenAI Realtime.
- **Rééchantillonnage matériel transparent** : Microphone ES7210 16 kHz vers 24 kHz sans latence.
- **Restitution audio haute fidélité** : PCM 24 kHz natif via le DAC I2S AW88298 et le buffer PSRAM VoxCore.
- **Transcriptions simultanées** : Whisper en entrée (`input_audio_transcription`) et flux textuel de l'assistant en sortie.
- **Interruption STOP instantanée** : Annule la réponse en cours (`response.cancel`) et purge le buffer audio.
- **Instrumentation de latence** : Affichage temps réel des jalons chronologiques de chaque tour.

## Matériel requis

- M5Stack CoreS3 (ESP32-S3 dual-core 240 MHz, 16 Mo Flash, 8 Mo PSRAM)
- Microphone intégré ES7210 & Haut-parleur AW88298
- Réseau Wi-Fi 2.4 GHz avec accès Internet
- Clé API OpenAI avec accès à l'API Realtime

## Configuration

1. Copiez le fichier d'exemple des secrets :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Éditez `include/secrets.h` avec vos identifiants réels :
   ```cpp
   #define OPENAI_API_KEY "sk-proj-VOTRE_CLE_REELLE"
   #define WIFI_SSID     "VOTRE_RESEAU_WIFI"
   #define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE"
   ```

## Compilation et Téléversement

Avec PlatformIO CLI :
```bash
cd examples/OpenAIConversation
pio run -t upload -t monitor
```

## Utilisation

1. Au démarrage, l'écran CoreS3 affiche `INITIALISATION` puis `CONNEXION...`.
2. Lorsque la session est confirmée par OpenAI Realtime, le badge passe au vert : `PRET (OPENAI)`.
3. **Parler** : Maintenez le bouton bleu **PARLER** enfoncé et posez votre question en français.
4. **Envoyer** : Relâchez le bouton. L'état bascule en `REFLEXION...`.
5. **Écouter** : L'assistant répond oralement sur le haut-parleur.
6. **Interrompre (STOP)** : À tout moment pendant la réflexion ou la parole, touchez le bouton rouge **STOP** pour couper net.
7. **Moniteur série** : Vous pouvez également envoyer `P` (appui), `R` (relâchement), ou `S` (STOP) sur le port série à 115 200 bauds.
