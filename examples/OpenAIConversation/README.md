# Exemple OpenAIConversation — VoiceKit v0.6.3.2.1
## Modern Voice UI & Corrective Edition — M5Stack CoreS3

Démonstration de dialogue vocal bidirectionnel temps réel haute performance avec **OpenAI Realtime API** (`wss://api.openai.com/v1/realtime`, modèle `gpt-realtime-2.1-mini`) sur **M5Stack CoreS3** (320 × 240 pixels M5GFX).

---

## 1. Sécurité des Identifiants (Avertissement Impératif)

> [!CAUTION]
> **Révocation obligatoire des secrets exposés** : Si une clé API OpenAI ou des identifiants Wi-Fi ont été précédemment committés ou rendus publics dans l'historique du projet, ils doivent être **immédiatement révoqués et remplacés** depuis le tableau de bord OpenAI et les paramètres de votre box Internet.

1. Le fichier `include/secrets.example.h` contient **exclusivement des valeurs fictives** (placeholders).
2. Vos identifiants réels doivent résider dans `include/secrets.h`, strictement ignoré par Git (`.gitignore`).
3. Aucun secret ni en-tête d'authentification n'est jamais affiché sur le port série ou à l'écran.

---

## 2. Nouveautés et Correctifs de la Version v0.6.3.2.1

La version **v0.6.3.2.1** stabilise et consolide l'expérience vocale sur M5Stack CoreS3 en résolvant 4 anomalies démontrées lors des tests matériels réels :

### Correction A — Filtrage des Control Frames WebSocket & Résilience JSON
- **Symptôme résolu** : Erreurs répétées `[OpenAIClient] Erreur parsing JSON recu` survenant toutes les 10 secondes même en période d'inactivité.
- **Cause identifiée** : La couche de transport WebSocket (`EspWebSocketTransport`) transmettait au client OpenAI les trames de contrôle keepalive PING (`0x09`) et PONG (`0x0A`) émises par `esp_websocket_client`. Ces données non-JSON provoquaient un échec de parsing `cJSON` et pouvaient réinitialiser prématurément le tampon de réassemblage `_rxAccum` lors de la réception de fragments audio multi-paquets.
- **Correctif** : Filtrage strict des trames de contrôle RFC 6455 (`op >= 0x08`), réassemblage protégé, traitement sans faille des trames vides/espaces et journalisation d'erreurs sécurisée sans fuite de secrets.

### Correction B — Conservation Intégrale de la Transcription Assistant & Découpage UTF-8
- **Symptôme résolu** : Le bilan final affichait `[Conversation] OpenAI : "."` au lieu de la phrase complète, et certains mots français étaient scindés avec un espace indésirable (`agré ables`, `t ’aider`, `réflé chir`).
- **Cause identifiée** : Le contrôleur conversationnel réassignait la métrique finale au dernier fragment reçu (`"."`), tandis que l'algorithme d'assemblage insérait un espace arbitraire entre des morceaux coupant des mots, des élisions ou des séquences multi-octets UTF-8.
- **Correctif** : Accumulation rigoureuse de tous les deltas textuels sans perte, soudure propre des morceaux de mots et des apostrophes françaises (`t’aider`, `agréables`, `réfléchir`), et capture prioritaire du transcript officiel complet (`response.output_audio_transcript.done` et `response.done`).

### Correction C — Optimisation des Performances Graphiques (Pics à 51,7 ms)
- **Symptôme analysé** : Pic mesuré de rendu graphique `[DIAG][UI] Rendu graphique : Max = 51744 us`.
- **Cause identifiée** : Exécution combinée du traçage du fond d'écran (`fillScreen`, 153,6 Ko transférés par SPI) et de l'ensemble des composants lors du premier cycle de boucle après l'initialisation.
- **Correctif** : Découplage de `drawStaticBackground()` lors du `setup()`, limitation à 10 Hz du rafraîchissement textuel lors du streaming vocal, et instrumentation fine des 6 sous-composants (`[DIAG][UI_SUB]`).

### Correction D — Normalisation des Libellés Série Dynamiques
- **Symptôme résolu** : Présence du préfixe hérité `[GEMINI]` lors de l'exécution avec le fournisseur OpenAI Realtime.
- **Correctif** : Génération dynamique des tags de journalisation (`[OPENAI]` / `[GEMINI]`) indexée fidèlement sur le fournisseur configuré (`VoiceProviderType`).

---

## 3. Interface Graphique Modern Voice UI

- **Thème Dark Mode Premium** : Fond bleu nuit profond (`#080E14`), contrastes doux, cartes aux coins arrondis et bordures subtiles.
- **Orbe Vocal Lumineux Animé (VoiceOrb)** :
  - Élément central situé à Y = 35..108, diamètre ~65 px.
  - Double-buffering `M5Canvas` (zéro scintillement, pushSprite ~1 ms, sans LVGL).
  - Cadence cible non-bloquante de 18 fps (intervalle ~55 ms, `millis()`, zéro `delay()`).
  - **5 comportements visuels** :
    1. **PRÊT** : Cyan doux, respiration lente et calme (période ~3,6 s).
    2. **ÉCOUTE** : Corail / orange vif, pulsation rythmée et **réaction visuelle dynamique au niveau sonore RMS réel** (`VoxCoreVoice::getAudioLevel()`).
    3. **ANALYSE** : Violet / pourpre profond avec 3 particules orbitales en rotation continue.
    4. **RÉPONSE** : Turquoise éclatant avec ondes sonores circulaires en expansion.
    5. **ERREUR** : Rouge d'alerte statique avec point d'exclamation.
- **En-tête compact (Y = 0..30)** : Titre, signal Wi-Fi graphique et badge d'état court.
- **Message d'état centré (Y = 110..132)** : Instructions claires en français naturel sous l'orbe.
- **Cartes conversationnelles (Y = 136..196)** :
  - Carte utilisateur (`Vous`) et carte assistant (`Assistant`).
  - Moteur `FrenchUI` avec calcul du retour à la ligne basé sur la largeur en pixels (`display.textWidth()`).
  - Restitution géométrique haute fidélité des accents français (`é, è, ê, ë, à, â, ç, î, ï, ô, ù, û, ü`), ligatures (`œ, Œ`) et typographie (`« », ’, —, …`).
- **Grand bouton principal (Y = 200..235)** :
  - Hauteur 35 px, très visible et réactif.
  - États : `PARLER` (bleu vif), `RELÂCHER` (orange vif Push-to-Talk), `STOP` (rouge avec icône carré prioritaire).
- **Gestion tactile robuste** : Anti-rebond 40 ms, suivi monotone `contact_id`, zone active du bouton Y = 200..235, priorité immédiate STOP sur front montant PRESS.

---

## 4. Spécifications Techniques et Matériel

- **Cible matérielle** : M5Stack CoreS3 (ESP32-S3FN8 dual-core 240 MHz, 16 Mo Flash, 8 Mo PSRAM OPI, LCD ILI9342C 320×240, tactile capacitif FT6336U).
- **Audio HAL** : Microphone intégré ES7210 (PCM 16 kHz) & amplificateur AW88298 (PCM 24 kHz natif).
- **Bibliothèque vocale** : VoiceKit v0.6.3.2.1 (moteur `VoxCoreVoice`).
- **Fournisseur IA** : OpenAI Realtime API (`wss://api.openai.com/v1/realtime`).
- **Modèle** : `gpt-realtime-2.1-mini`.
- **Voix & Langue** : `alloy`, `fr-FR`.

---

## 5. Configuration et Compilation

1. Copiez le modèle de secrets :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```

2. Renseignez vos identifiants réels dans `include/secrets.h` :
   ```cpp
   #pragma once

   #define OPENAI_API_KEY "sk-proj-VOTRE_CLE_REELLE"
   #define WIFI_SSID     "VOTRE_RESEAU_WIFI"
   #define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE"
   ```

3. Compilation et téléversement avec PlatformIO :
   ```bash
   # Compiler le projet
   pio run -d examples/OpenAIConversation

   # Téléverser sur le CoreS3
   pio run -d examples/OpenAIConversation -t upload

   # Ouvrir le moniteur série (115 200 bauds)
   pio device monitor -d examples/OpenAIConversation -b 115200
   ```

---

## 6. Guide d'Utilisation

### Cycle Push-to-Talk (PARLER / RELÂCHER)

1. **Initialisation** : L'écran affiche l'état `CONNEXION`. Dès que la session WebSocket OpenAI est négociée, l'écran passe en `PRÊT` (bouton bleu `PARLER`, badge cyan `PRÊT`).
2. **Maintenir pour parler** : Appuyez et maintenez votre doigt sur le bouton `PARLER`.
   - Le bouton devient orange vif `RELÂCHER`.
   - L'orbe bascule en mode corail pulsant et réagit en temps réel aux variations du volume de votre voix.
   - Le texte d'état indique *« Je vous écoute… »*.
3. **Relâcher pour envoyer** : Retirez votre doigt du bouton.
   - Le système passe en `ANALYSE` (badge violet, orbe avec particules orbitales en rotation).
   - Le texte d'état indique *« Je réfléchis… »*.
   - Votre transcription Whisper apparaît dans la carte `Vous`.
4. **Restitution de la réponse** : L'assistant répond oralement sur le haut-parleur.
   - L'orbe turquoise émet des ondes sonores animées.
   - Le texte d'état indique *« Je vous réponds »*.
   - La réponse textuelle s'affiche dans la carte `Assistant`.
5. **Clôture du tour** : Une fois la dernière syllabe prononcée et le buffer I2S vidé, l'interface revient automatiquement à `PRÊT`.

### Interruption Prioritaire (STOP)

- Pendant la réflexion ou pendant que l'assistant parle, le bouton affiche `STOP` (fond rouge vif avec icône carré).
- Dès le premier contact physique (front montant PRESS), VoiceKit coupe la session OpenAI (`response.cancel`), purge les tampons audio et libère instantanément le système.

### Commandes Série

Depuis le terminal série (115 200 bauds) :
- `P` : Simule l'appui `PARLER`.
- `R` : Simule le relâchement `RELÂCHER`.
- `S` : Déclenche l'interruption prioritaire `STOP`.
- `D` : Affiche instantanément les diagnostics instrumentés globaux et sous-composants :
  - `[DIAG][VOICE]` : Pic et durée de `voice.update()`.
  - `[DIAG][UI]` : Pic et durée de rafraîchissement d'affichage.
  - `[DIAG][UI_SUB]` : Détail des temps par composant (`Orbe`, `Header`, `State`, `UserCard`, `AsstCard`, `MainBtn`).
  - `[DIAG][HEAP]` : Mémoire interne et PSRAM disponibles.
