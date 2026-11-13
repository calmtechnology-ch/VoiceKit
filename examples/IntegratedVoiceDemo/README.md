# IntegratedVoiceDemo — VoiceKit v0.6.5.2

Exemple officiel principal et vitrine d'intégration de **VoiceKit v0.6.5.2** illustrant l'orchestration complète par `IntegratedVoicePipeline` avec interface tactile graphique **LVGL 9** sur **M5Stack CoreS3**.

---

## 1. Objectif

Démontrer l'intégration complète de la chaîne vocale sans recopier la machine à états ni l'orchestration dans l'application :
$$\text{PTT} \longrightarrow \text{STT} \longrightarrow \text{ROUTING} \longrightarrow \text{LOCAL\_TOOL / AI} \longrightarrow \text{TTS} \longrightarrow \text{READY}$$

L'exemple montre comment brancher `IntegratedVoicePipeline` avec le registre d'outils locaux sécurisés `ToolRegistry`, le routeur d'intentions locales en français `VoiceCommandDispatcher`, et le fournisseur conversationnel temps réel (Google Gemini Live ou OpenAI Realtime).

---

## 2. Fonctionnalités

- **Interface LVGL 9 moderne** : Conçue pour la résolution 320 × 240 du M5Stack CoreS3 avec badge d'état dynamique, zones de transcription/réponse avec retour à la ligne automatique et fenêtre modale de confirmation.
- **Bouton PTT fiable** : Gestion robuste de l'appui maintenu et du relâchement avec retour visuel immédiat (couleur et libellé synchronisés).
- **Bouton STOP prioritaire** : Interruption instantanée à tout moment du cycle (écoute, réflexion ou restitution vocale).
- **Routage vocal hybride** :
  - *Commandes locales immédiates* : Exécution sans latence réseau pour les outils enregistrés.
  - *Conversation IA en streaming* : Redirection automatique vers Gemini Live ou OpenAI Realtime pour les questions générales.
- **Gestion sécurisée des actions sensibles (`SENSITIVE`)** : Boîte de dialogue tactile demandant une confirmation explicite avant toute exécution irréversible.
- **Mémoire conversationnelle (`ReplyMemory`)** : Mémorisation des dernières réponses avec support natif des intentions locales de répétition (*« Répète »*).
- **Pause et reprise du pipeline** : Suspension du traitement des commandes ordinaires (`PAUSE` / `RESUME`) via commande vocale ou série. Le bouton PARLER reste utilisable lorsque les ressources audio sont disponibles pour capter les ordres de reprise (`RESUME`) ou d'arrêt prioritaire (`STOP`), tandis que les commandes ordinaires ne sont pas exécutées pendant la pause.
- **Contrôle série complet** : Injection de transcriptions simulées, pilotage PTT et rotation écran à chaud.

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3FN8, double cœur Xtensa LX7 à 240 MHz).
- **Mémoire** : 16 Mo Flash SPI, 8 Mo OPI PSRAM (obligatoire pour les tampons PCM).
- **Périphériques** : Écran LCD tactile capacitif 2.0" (320 × 240 ILI9342C + FT6336U), amplificateur audio I2S AW88298 et microphone double ES7210.
- **Alimentation** : Câble USB-C connecté à un port fournissant au moins 1A (5V).

---

## 4. Configuration

1. Copier le fichier d'exemple des identifiants :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Éditer `include/secrets.h` avec vos paramètres :
   ```cpp
   #define WIFI_SSID     "Votre_SSID_WiFi"
   #define WIFI_PASSWORD "Votre_Mot_De_Passe"

   // Décommenter une seule ligne selon le fournisseur souhaité :
   #define USE_GEMINI      // <-- Google Gemini Live (par défaut)
   // #define USE_OPENAI   // <-- OpenAI Realtime

   // Renseigner la clé de votre fournisseur sélectionné :
   #define GEMINI_KEY    "Votre_Cle_API_Google_Gemini"
   #define GEMINI_API_KEY GEMINI_KEY
   #define OPENAI_API_KEY "Votre_Cle_API_OpenAI"
   ```
   *Note : Le fichier `secrets.example.h` sert uniquement de modèle. Si `secrets.h` est absent ou si des clés d'exemple sont détectées, l'application démarre avec un badge explicite `ERR CONFIG` sans initier de connexion réseau.*

---

## 5. Compilation

Compiler l'exemple via PlatformIO Core CLI :
```bash
pio run -d examples/IntegratedVoiceDemo
```
Le projet utilise le fichier de partition `partitions_16MB.csv` pour allouer l'espace nécessaire à l'application et à LVGL 9.

---

## 6. Téléversement

1. Connecter le M5Stack CoreS3 en USB-C.
2. Téléverser le binaire compilé :
   ```bash
   pio run -d examples/IntegratedVoiceDemo -t upload
   ```
3. Ouvrir le moniteur série à 115 200 baud :
   ```bash
   pio device monitor -d examples/IntegratedVoiceDemo -b 115200
   ```

---

## 7. Mode d'utilisation

### 7.1 Utilisation tactile

1. Attendre l'apparition du badge vert **PRÊT** dans le bandeau supérieur.
2. **Parler** : Maintenir le bouton bleu **Parler (PTT)** enfoncé. Parler distinctement face au CoreS3.
3. **Envoyer** : Relâcher le bouton. La transcription utilisateur apparaît en haut de l'écran.
4. **Interrompre** : Appuyer à tout moment sur le bouton rouge **STOP** pour annuler le tour en cours.
5. **Confirmer** : Lors d'une commande sensible, une fenêtre modale apparaît. Appuyer sur **Confirmer** ou **Refuser**.

### 7.2 Commandes console série

La console série accepte les commandes interactives suivantes :
- `p` : Démarrer l'écoute vocale (PTT ON).
- `r` : Arrêter l'écoute vocale (PTT OFF -> transcription).
- `s` : Interruption STOP prioritaire.
- `k` : Basculer l'état Pause / Reprise du pipeline.
- `c` : Valider la confirmation sensible en attente.
- `x` : Refuser la confirmation sensible en attente.
- `t <phrase>` : Injecter une phrase de test simulée (ex: `t statut`, `t repete`).
- `o <0..3>` : Régler la rotation de l'écran LCD.

---

## 8. Tests à effectuer

| # | Type | Action / Phrase prononcée | Résultat attendu |
|---|---|---|---|
| 1 | Outil Local (Read-Only) | *« Statut »* ou `t statut` | Annonce vocale et textuelle du niveau de batterie et de l'état système. |
| 2 | Outil Local (Read-Only) | *« Mémoire »* ou `t memoire` | Annonce de la RAM interne et de la PSRAM disponible en Ko. |
| 3 | Outil Local (Write) | *« Règle la luminosité à 50 »* | Modification effective du rétroéclairage LCD et confirmation vocale. |
| 4 | Outil Local (Sensible) | *« Réinitialise l'écran »* | Apparition de la fenêtre modale de confirmation. Pas d'exécution tant que non confirmé. |
| 5 | Confirmation Acceptée | Appui sur bouton *Confirmer* ou touche `c` | Effacement de l'écran, rafraîchissement graphique, et confirmation vocale. |
| 6 | Intention Locale (Mémoire) | *« Répète »* ou `t repete` | Restitution vocale immédiate de la dernière réponse générée. |
| 7 | Conversation IA | *« Pourquoi le ciel est-il bleu ? »* | Routage vers l'IA, réponse textuelle complète et synthèse audio PCM 24 kHz. |
| 8 | Interruption STOP | Appui sur *STOP* pendant la réponse IA | Coupure audio immédiate et retour à l'état `READY`. |
| 9 | Pause / Reprise | Touche `k` (ou *« Pause »*), puis *« Statut »* ou *« Reprends »* | Suspension des commandes ordinaires (*« Statut »* rejeté sans exécution). Le bouton PARLER reste utilisable lorsque les ressources audio sont disponibles ; *« Reprends »* quitte la pause (RESUME) et STOP reste prioritaire. |

---

## 9. Résultats attendus

- **Transitions d'état** :
  $$\text{READY} \longrightarrow \text{LISTENING} \longrightarrow \text{TRANSCRIBING} \longrightarrow \begin{cases} \text{LOCAL\_TOOL} \longrightarrow \text{SPEAKING} \\ \text{WAITING\_CONFIRMATION} \\ \text{AI\_PROCESSING} \longrightarrow \text{SPEAKING} \end{cases} \longrightarrow \text{READY}$$
- **Vérification d'exécution des outils** : Aucune commande locale n'est validée tant que son exécution effective n'a pas produit de `ToolResult::success()`.
- **Gestion PAUSE / RESUME** : En pause, le traitement des commandes ordinaires est suspendu. Le bouton PARLER reste utilisable lorsque les ressources audio sont disponibles afin de recevoir la commande de reprise (`RESUME`) ou d'arrêt prioritaire (`STOP`). Les commandes ordinaires ne sont jamais exécutées pendant la pause.
- **Mémoire & fluidité** : Aucun blocage de la boucle graphique LVGL (maintien à ~50 FPS), aucune fuite de mémoire après des dizaines de tours consécutifs.

---

## 10. Limitations connues

- Requiert une connexion Wi-Fi stable à 2.4 GHz pour la partie conversationnelle en streaming.
- Le microphone interne du CoreS3 requiert de parler à moins de 40 cm dans un environnement calme pour une transcription optimale.
- Les polices embarquées (`FrenchFonts`) couvrent l'intégralité du jeu de caractères français ISO-8859-1 / UTF-8 mais n'embarquent pas les glyphes asiatiques CJK afin de limiter l'empreinte Flash.

