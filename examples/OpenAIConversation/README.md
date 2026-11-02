# Exemple OpenAIConversation — VoiceKit v0.6.3.2
## Modern Voice UI Edition — M5Stack CoreS3

Démonstration de dialogue vocal bidirectionnel temps réel haute performance avec **OpenAI Realtime API** (`wss://api.openai.com/v1/realtime`, modèle `gpt-realtime-2.1-mini`) sur **M5Stack CoreS3** (320 × 240 pixels M5GFX).

---

## 1. Sécurité des Identifiants (Avertissement Impératif)

> [!CAUTION]
> **Révocation obligatoire des secrets exposés** : Si une clé API OpenAI ou des identifiants Wi-Fi ont été précédemment committés ou rendus publics dans l'historique du projet, ils doivent être **immédiatement révoqués et remplacés** depuis le tableau de bord OpenAI et les paramètres de votre box Internet.

1. Le fichier `include/secrets.example.h` contient **exclusivement des valeurs fictives** (placeholders).
2. Vos identifiants réels doivent résider dans `include/secrets.h`, strictement ignoré par Git (`.gitignore`).
3. Aucun secret n'est jamais affiché sur le port série ou à l'écran.

---

## 2. Nouveautés de l'Édition v0.6.3.2 (Modern Voice UI)

Cette version remplace l'ancienne interface par une interface vocale moderne, fluide, élégante et intuitive :

- **Thème Dark Mode Premium** : Fond bleu nuit profond (`#080E14`), contrastes doux, cartes aux coins arrondis et bordures subtiles.
- **Orbe Vocal Lumineux Animé (VoiceOrb)** :
  - Élément central situé à Y = 35..108, diamètre ~65 px.
  - Double-buffering `M5Canvas` (zéro scintillement, pushSprite ~1 ms, sans LVGL).
  - Cadence cible non-bloquante de 18 fps (intervalle ~55 ms, `millis()`, zéro `delay()`).
  - Auto-dégradation de cadence si `voice.update()` subit une forte charge.
  - **5 comportements visuels** :
    1. **PRÊT** : Cyan doux, respiration lente et calme (période ~3,6 s).
    2. **ÉCOUTE** : Corail / orange vif, pulsation rythmée et **réaction visuelle dynamique au niveau sonore RMS réel** (`VoxCoreVoice::getAudioLevel()`).
    3. **ANALYSE** : Violet / pourpre profond avec 3 particules orbitales en rotation continue.
    4. **RÉPONSE** : Turquoise éclatant avec ondes sonores circulaires en expansion.
    5. **ERREUR** : Rouge d'alerte statique avec point d'exclamation (0 % CPU en continu).
- **En-tête compact (Y = 0..30)** : Titre de l'application à gauche, indicateur graphique de signal Wi-Fi et badge d'état court à droite (`PRÊT`, `ÉCOUTE`, `ANALYSE`, `RÉPONSE`, `ERREUR`).
- **Message d'état centré (Y = 110..132)** : Instructions claires en français naturel sous l'orbe (*« Appuyez pour parler »*, *« Je vous écoute… »*, *« Je réfléchis… »*, *« Je vous réponds »*).
- **Cartes conversationnelles (Y = 136..196)** :
  - Carte utilisateur (`Vous`) et carte assistant (`Assistant`).
  - Moteur `FrenchUI` amélioré avec calcul du retour à la ligne basé sur la **largeur réelle en pixels** (`display.textWidth()`).
  - Restitution géométrique haute fidélité des accents français (`é, è, ê, ë, à, â, ç, î, ï, ô, ù, û, ü`), majuscules (`É, È, Ê, À, Ç`), ligatures (`œ, Œ`) et typographie (`« », ’, —, …`).
  - Troncature propre `...` aux frontières de codepoints sans coupure d'octet.
  - Conservation intégrale des transcriptions en mémoire (aucun texte masqué ni rogné sauvagement).
- **Grand bouton principal (Y = 200..235)** :
  - Hauteur 35 px, très visible et réactif.
  - États : `PARLER` (bleu vif avec icône micro), `RELÂCHER` (orange vif Push-to-Talk), `STOP` (rouge avec icône carré prioritaire), `RÉESSAYER` (rouge sombre) et `CONNEXION` (gris désactivé).
- **Gestion tactile robuste** : Héritée des travaux de stabilisation `CoreS3-TouchDiagnostics v0.1.0.1` (anti-rebond 40 ms, suivi monotone `contact_id`, gestion des glissements hors zone 15 px, gestion des contacts perdus, priorité absolue STOP sur front montant PRESS).

---

## 3. Architecture Logicielle Modulaire

```text
examples/OpenAIConversation/
├── include/
│   ├── secrets.example.h   <- Modèle de configuration sécurisé (placeholders)
│   ├── FrenchUI.h          <- Moteur UTF-8 français, calcul de largeurs en pixels et diacritiques
│   ├── VoiceUI.h           <- Gestionnaire de composition UI (cartes, en-tête, bouton, texte d'état)
│   └── VoiceOrb.h          <- Orbe vocal animé double-buffering M5Canvas
├── src/
│   ├── main.cpp            <- Orchestration VoiceKit, boucle non-bloquante, tactile, diagnostics
│   ├── VoiceUI.cpp         <- Rendu partiel par zones sales (dirty flags) sans scintillement
│   └── VoiceOrb.cpp        <- Rendu géométrique de l'orbe (5 états, 18 fps)
├── platformio.ini          <- Configuration PlatformIO m5stack-cores3 C++17
├── README.md               <- Documentation utilisateur et technique
└── RAPPORT_v0.6.3.2.md     <- Rapport exhaustif d'implémentation et de validation
```

---

## 4. Spécifications Techniques et Matériel

- **Cible matérielle** : M5Stack CoreS3 (ESP32-S3FN8 dual-core 240 MHz, 16 Mo Flash, 8 Mo PSRAM OPI, LCD ILI9342C 320×240, tactile capacitif FT6336U).
- **Audio HAL** : Microphone intégré ES7210 (PCM 16 kHz) & amplificateur AW88298 (PCM 24 kHz natif).
- **Bibliothèque vocale** : VoiceKit v0.6.3.1 (moteur `VoxCoreVoice`).
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

1. **Initialisation** : L'écran affiche l'état `CONNEXION` (bouton gris, badge `ATTENTE`, orbe en respiration calme). Dès que la session WebSocket OpenAI est négociée, l'écran passe en `PRÊT` (bouton bleu `PARLER`, badge cyan `PRÊT`).
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
- `D` : Affiche instantanément les diagnostics instrumentés (durée de `voice.update()`, durée de rendu graphique, Heap/PSRAM libres, nombre de tours, statut Wi-Fi).
