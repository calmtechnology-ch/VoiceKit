# Exemple MinimalVoiceKit — VoiceKit v0.5.0

Cet exemple démontre l'utilisation de l'**API simplifiée `VoiceKit`** (façade optionnelle de niveau supérieur introduite dans VoiceKit v0.5.0) pour **M5Stack CoreS3**.

---

## 1. Fonctionnalités Démontrées

Avec **une seule instance de classe `VoiceKit voice;`**, cet exemple permet de tester successivement les 4 modes vocaux majeurs :

1. **TTS (Synthèse vocale mot à mot)** :
   ```cpp
   voice.say("Bonjour ! Bienvenue sur VoiceKit v0.5.0.");
   ```
2. **STT Manuel (Push-to-Talk)** :
   ```cpp
   voice.listen();         // Au début de l'appui tactile
   voice.stopListening();  // Au relâchement du bouton
   ```
3. **ContinuousSTT (Dictée continue mains libres avec VAD)** :
   ```cpp
   voice.startContinuousListening(); // Détection automatique de parole et fin de silence
   voice.pauseContinuousListening(); // Pause temporaire
   voice.resumeContinuousListening(); // Reprise
   voice.stopContinuousListening();  // Arrêt
   ```
4. **Conversation (Gemini Live Mode B)** :
   ```cpp
   voice.startConversation(); // Dialogue bidirectionnel oral
   voice.stopConversation();  // Retour au mode unitaire
   ```
5. **Arrêt d'urgence instantané** :
   ```cpp
   voice.stop();
   ```

---

## 2. Configuration Requise

### Identifiants Wi-Fi et Clé API Gemini Live
Copiez `include/secrets.example.h` vers `include/secrets.h` et renseignez votre Wi-Fi et votre clé API Google Gemini :
```cpp
#define WIFI_SSID     "VOTRE_WIFI_SSID"
#define WIFI_PASSWORD "VOTRE_WIFI_PASSWORD"
#define GEMINI_KEY    "VOTRE_CLE_API_GEMINI"
```

---

## 3. Compilation et Téléversement (PlatformIO)

```bash
# Compilation
pio run -d examples/MinimalVoiceKit

# Téléversement vers le CoreS3 (port COM auto ou spécifié)
pio run -d examples/MinimalVoiceKit -t upload

# Moniteur série (115200 baud)
pio device monitor -d examples/MinimalVoiceKit -b 115200
```

---

## 4. Commandes Série de Diagnostic

En complément de l'écran tactile, vous pouvez envoyer des commandes sur le port série (115200 baud) :
- `1` : Basculer vers l'onglet TTS et énoncer la phrase de test.
- `2` : Basculer vers l'onglet STT (Push-to-Talk).
- `3` : Activer / Désactiver la dictée continue (ContinuousSTT).
- `4` : Activer / Désactiver la conversation bidirectionnelle.
- `P` : Début d'écoute microphone (Parler).
- `R` : Fin d'écoute microphone (Relâcher).
- `S` : Commande STOP prioritaire immédiate.
