# WakeWordConversation — VoiceKit v0.6.3.2

Exemple de conversation vocale entièrement mains libres déclenchée exclusivement par le mot de réveil acoustique local **« Bonjour ESP »** (`wn10_fr_bonjouresp`) sur **M5Stack CoreS3**, avec affichage direct **M5GFX** (sans LVGL).

## Cycle fonctionnel

1. **EN ATTENTE — « Bonjour ESP »** (`WAKE_ARMED`) : WakeNet (`wn10_fr_bonjouresp`) écoute localement en hors ligne sur le microphone ES7210.
2. **MOT DE RÉVEIL DÉTECTÉ** (`WAKE_DETECTED` / `PREPARING_SESSION`) : Suspension contrôlée de WakeNet et libération vérifiée du microphone.
3. **CONNEXION GEMINI / CONNEXION OPENAI** (`CONNECTING`) : Établissement de la session WebSocket TLS et confirmation `setupComplete`.
4. **PRÊT — PARLEZ** / **ÉCOUTE** (`CONVERSATION_LISTENING`) : Capture vocale automatique avec détection locale de fin de phrase (VAD) et timeout d'inactivité configurable (30 s par défaut).
5. **RÉPONSE VOCALE** (`CONVERSATION_SPEAKING`) : Restitution intégrale de la réponse PCM 24 kHz via VoxCore Mode 2, puis retour automatique en écoute pour permettre plusieurs tours sans répéter « Bonjour ESP ».
6. **FIN DE CONVERSATION** (`ENDING_SESSION`) : Clôture par timeout de silence (30 s), durée maximale, arrêt manuel (`STOP SESSION`) ou commande vocale confirmée par transcription finale (« Au revoir »), puis déconnexion réseau.
7. **RETOUR EN ÉCOUTE** (`RETURNING_TO_WAKE`) : Réactivation contrôlée de WakeNet et retour à `WAKE_ARMED`.

## Configuration

Copiez `include/secrets.example.h` vers `include/secrets.h` et renseignez vos identifiants Wi-Fi et clé API (`GEMINI_KEY` ou `OPENAI_API_KEY`).

## Compilation et téléversement

```bash
pio run -e m5stack-cores3
pio run -e m5stack-cores3 -t upload
```
