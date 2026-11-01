# Exemple OpenAIConversation — VoiceKit v0.6.3.1

Démonstration de dialogue vocal bidirectionnel temps réel haute performance avec **OpenAI Realtime API** (`wss://api.openai.com/v1/realtime`) sur **M5Stack CoreS3**.

---

## 1. Sécurité des Identifiants (Avertissement Impératif)

> [!CAUTION]
> **Révocation obligatoire des secrets exposés** : Si une clé API OpenAI ou des identifiants Wi-Fi ont été précédemment committés ou rendus publics dans l'historique du projet, ils doivent être **immédiatement révoqués et remplacés** depuis le tableau de bord OpenAI et les paramètres de votre box Internet.

1. Le fichier `include/secrets.example.h` contient **exclusivement des valeurs fictives** (placeholders).
2. Vos identifiants réels doivent résider dans `include/secrets.h`, strictement ignoré par Git (`.gitignore`).
3. Aucun secret n'est jamais affiché sur le port série ou à l'écran.

---

## 2. Présentation Technique

Cet exemple exploite l'architecture multi-fournisseurs de VoiceKit v0.6.3.1 :
- **Connexion TLS WebSocket directe** : Négociation sécurisée depuis l'ESP32-S3 vers `wss://api.openai.com/v1/realtime` sans serveur intermédiaire.
- **Propriétaire unique du microphone** : Acquisition PCM 16 kHz mono 16-bit pilotée par `M5AudioInput` sans double démarrage I2S.
- **Rééchantillonnage matériel transparent** : Conversion temps réel 16 kHz vers 24 kHz via `AudioResampler`.
- **Restitution audio haute fidélité** : PCM 24 kHz natif via le DAC I2S AW88298 et le buffer PSRAM VoxCore (Mode 2 `DEDICATED_AUDIO_TASK`), avec vidage exact garantissant l'absence de coupure de la dernière syllabe.
- **Transcriptions simultanées** : Whisper en entrée (`input_audio_transcription`) et flux textuel de l'assistant OpenAI en sortie.
- **Gestion tactile robuste** : Moteur d'événements inspiré de `CoreS3-TouchDiagnostics v0.1.0.1` assurant une reconnaissance fiable de `PRESS` et `RELEASE`, un anti-rebond matériel, la gestion des glissements hors zone et des contacts perdus, et l'absence totale de blocage.
- **Interruption STOP prioritaire** : Coupe immédiatement la réflexion ou la restitution en cours (`response.cancel`), purge les tampons audio et libère le haut-parleur.
- **Interface graphique optimisée** : M5GFX 320×240 avec rafraîchissement partiel ciblé des zones modifiées (zéro scintillement), support complet des caractères français UTF-8 (accents `é, è, ê, ë, à, â, ç, î, ï, ô, ù, û, ü`, majuscules `É, È, Ê, À, Ç`, typographie « », ’, —, …) et affichage multi-lignes sans découpage de séquences d'octets.
- **Diagnostics structurés et instrumentés** : Mesure continue des cycles de `voice.update()`, de la mémoire libre (Heap et PSRAM) et des jalons de latence.

---

## 3. Matériel Requis

- **M5Stack CoreS3** (ESP32-S3 dual-core 240 MHz, 16 Mo Flash, 8 Mo PSRAM, écran LCD ILI9342C 320×240, contrôleur tactile capacitif FT6336U).
- Microphone intégré ES7210 & amplificateur / haut-parleur AW88298.
- Réseau Wi-Fi 2.4 GHz avec accès Internet.
- Clé API OpenAI avec accès à l'API Realtime (`gpt-realtime-2.1-mini`).

---

## 4. Configuration

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

3. Vérifiez la configuration du port dans `platformio.ini` :
   - Par défaut, PlatformIO détecte automatiquement le port COM (`upload_port` commenté).
   - Pour forcer un port particulier, vous pouvez décommenter `upload_port = COM4` ou passer l'option `--upload-port COM4` lors du téléversement.

---

## 5. Compilation et Téléversement

Depuis la racine du projet ou depuis le dossier de l'exemple :

```bash
# Compilation du firmware
pio run -d examples/OpenAIConversation

# Téléversement vers le M5Stack CoreS3
pio run -d examples/OpenAIConversation -t upload

# Ouverture du moniteur série à 115 200 bauds
pio device monitor -d examples/OpenAIConversation -b 115200
```

---

## 6. Guide d'Utilisation

### Cycle Push-to-Talk (PARLER / RELÂCHER)

1. **Démarrage** : L'écran affiche l'état `CONNEXION...` (badge orange). Dès la confirmation de la session WebSocket OpenAI, le badge bascule au vert : `PRÊT (OPENAI)`.
2. **Parler** : Appuyez et maintenez votre doigt sur le bouton bleu **PARLER**.
   - Le bouton devient rouge avec le libellé **ENREGISTREMENT...**.
   - Le badge affiche **ÉCOUTE EN COURS**.
   - Parlez naturellement en français face au microphone du CoreS3.
3. **Envoyer** : Relâchez le bouton.
   - Le statut passe à **TRAITEMENT** (`REFLEXION...`, badge magenta).
   - La transcription Whisper utilisateur s'affiche dans le cadre supérieur.
4. **Écouter** : L'assistant vocal OpenAI répond oralement sur le haut-parleur (`RÉPONSE VOCALE`, badge cyan), et sa transcription textuelle s'affiche dans le cadre inférieur avec retour à la ligne automatique.
5. **Fin de tour** : Lorsque la restitution audio s'achève complètement (tampon I2S vidé jusqu'au dernier échantillon), le système revient automatiquement à l'état `PRÊT` sans blocage. Le cycle peut être répété indéfiniment.

### Interruption Prioritaire (STOP)

- À tout moment pendant la réflexion (**TRAITEMENT**) ou pendant que l'assistant parle (**RÉPONSE**), le bouton bascule en mode **STOP** (fond rouge-orangé).
- Un appui sur **STOP** coupe net la réponse côté OpenAI (`response.cancel`), purge les tampons audio et remet instantanément le système en état `PRÊT`.

### Récupération après Erreur

- En cas de perte de connectivité ou d'erreur réseau, le badge bascule en **ERREUR** (badge rouge) et le message d'erreur est affiché.
- Un appui sur le bouton tactile lance une réinitialisation propre et ordonnée sans nécessiter de redémarrage matériel.

### Commandes Série de Contrôle

Depuis le terminal série (115 200 bauds) :
- `P` ou `p` : Déclencher la prise de parole (simule l'appui PARLER).
- `R` ou `r` : Terminer la prise de parole (simule le relâchement).
- `S` ou `s` : Interruption prioritaire STOP.
- `D` ou `d` : Exporter immédiatement le rapport complet des diagnostics (mémoire Heap/PSRAM, durées de `voice.update()`, tours complétés, statut Wi-Fi et latences).
