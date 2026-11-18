# VoiceAssistant v0.1.0 — Application Showcase VoiceKit

**VoiceAssistant** est un assistant vocal conversationnel complet pour **M5Stack CoreS3 (ESP32-S3)**, conçu comme l'application vitrine officielle de la bibliothèque [VoiceKit v0.6.7.1](https://github.com/calmtechnology-ch/VoiceKit).

Il permet à l'utilisateur d'engager un dialogue vocal naturel, de poser des questions oralement, de recevoir des réponses parlées fluides et de suivre les transcriptions sur l'écran tactile capacitif (320 × 240).

---

## 1. Présentation

VoiceAssistant démontre l'intégration native et modulaire de VoiceKit sans nécessiter de code complexe :
- **Architecture applicative légère** articulée autour de `VoiceInteractionController` et `VoxCoreVoice`.
- **Interface tactile moderne sous LVGL 9** : thème clair, lisible, avec cartes de dialogue et indicateurs d'état en français.
- **Double compatibilité IA temps réel** : Google Gemini Live et OpenAI Realtime (`gpt-realtime-2.1-mini`).
- **Validation locale de parole (VAD)** : élimination catégorique des requêtes serveurs inutiles lors de silences ou tapotements.
- **Bouton STOP prioritaire** : interruption instantanée du micro ou du haut-parleur.

---

## 2. Fonctionnalités

- **Push-to-Talk (PTT) à 2 clics** : un 1er clic sur `PARLER` pour ouvrir le micro, un 2nd clic sur `TERMINER` pour clôturer. Bouton automatiquement désactivé (`LV_STATE_DISABLED`) lorsque la session IA ou le réseau n'est pas opérationnel.
- **Anti-rebond matériel** : filtrage de 120 ms pour éviter tout double déclenchement tactile intempestif.
- **Analyse locale VAD** : analyse d'énergie RMS sur blocs PCM 16 kHz (plancher garanti 380 RMS pour le codec ES7210).
- **Interruption instantanée STOP** : coupe la restitution sonore sur l'amplificateur AW88298 ou la capture micro, avec purge des callbacks tardifs et protection de l'historique.
- **Choix du fournisseur IA** : bascule dynamique et asynchrone entre Google Gemini Live et OpenAI Realtime depuis l'écran Paramètres via les API de haut niveau VoiceKit.
- **Gestion multilingue** : sélection parmi le Français (`fr-FR`), l'Anglais (`en-US`), l'Allemand (`de-DE`), l'Italien (`it-IT`) et l'Espagnol (`es-ES`).
- **Catalogue de voix compatibles** : affichage exclusif des voix supportées par le fournisseur choisi.
- **Persistance des préférences (NVS)** : sauvegarde du fournisseur, de la langue et de la voix dans la mémoire non volatile (sans stocker de clés API). *Note : le volume sonore est géré en mémoire vive pour la session et réinitialisé à sa valeur nominale au démarrage.*
- **Affichage des réponses longues** : carte ASSISTANT équipée d'un conteneur défilable verticalement au toucher permettant de consulter plus de 10 lignes sans altérer l'agencement 320 × 240.
- **Historique local en RAM** : conservation structurée des derniers échanges complets (sans duplication de fragments), avec réinitialisation visuelle immédiate via le bouton `Effacer`.

---

## 3. Matériel Nécessaire

- **M5Stack CoreS3** (ESP32-S3 Dual Core 240 MHz, 16 Mo Flash, 8 Mo PSRAM).
- **Microphone intégré** : double micro ES7210 (I2S DMA 16 kHz mono).
- **Haut-parleur intégré** : amplificateur AW88298 (I2S DMA 24 kHz).
- **Écran** : LCD ILI9342C 320 × 240 pixels avec dalle tactile capacitive FT6336U.
- **Câble USB-C** pour l'alimentation et la programmation série.
- **Connexion Wi-Fi 2.4 GHz**.
- Clé d'API **Google Gemini** (Gemini Live) et/ou **OpenAI** (Realtime API).

---

## 4. Aperçu de l'Interface Graphique (LVGL 9)

Organisation en thème clair (fond `#F4F6F9`, cartes blanches `#FFFFFF`, accents bleus `#1976D2`) :

### Écran Conversation
```text
+--------------------------------------------------+
| VoiceAssistant            Gemini         WiFi OK |
+--------------------------------------------------+
| [ Assistant prêt (2 clics) ]          [VAD: PRÊT]|
+--------------------------------------------------+
| VOUS                                             |
| Quelle est la capitale du Japon ?                |
+--------------------------------------------------+
| ASSISTANT                                        |
| La capitale du Japon est Tokyo.                  |
+--------------------------------------------------+
|      [   PARLER   ]            [   STOP   ]      |
+--------------------------------------------------+
| [  Conversation  ]          [   Paramètres   ]   |
+--------------------------------------------------+
```

*(Emplacement réservé pour photographies réelles sur CoreS3)*

---

## 5. Installation avec PlatformIO

Clonez le dépôt et placez-vous dans le dossier de l'application :

```bash
cd showcases/VoiceAssistant
```

Le projet utilise directement la bibliothèque locale VoiceKit via `symlink://../..` configuré dans `platformio.ini`.

Pour compiler :

```bash
pio run
```

Pour téléverser et ouvrir le moniteur série :

```bash
pio run --target upload --target monitor
```

---

## 6. Configuration Wi-Fi et Clés API

1. Copiez le fichier d'exemple des secrets :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Éditez `include/secrets.h` avec vos identifiants :
   ```cpp
   #pragma once

   // Réseau Wi-Fi local (2.4 GHz)
   #define WIFI_SSID "Mon_Reseau_WiFi"
   #define WIFI_PASSWORD "Mon_Mot_De_Passe"

   // Clé Google Gemini (Gemini Live API)
   #define GEMINI_API_KEY "AIzaSy..."

   // Clé OpenAI (Realtime API gpt-realtime-2.1-mini)
   #define OPENAI_API_KEY "sk-proj-..."
   ```

> [!IMPORTANT]
> Le fichier `include/secrets.h` est ignoré par Git. Ne committez jamais vos clés d'API.

---

## 7. Première Conversation

1. Mettez le M5Stack CoreS3 sous tension.
2. L'écran affiche l'initialisation, puis la barre d'état passe à **"WiFi OK"** et **"Assistant prêt (2 clics)"**.
3. Appuyez sur **[ PARLER ]** : le bouton devient rouge et affiche **"TERMINER"**, indiquant que le microphone est ouvert.
4. Posez votre question distinctement (ex: *"Explique-moi le fonctionnement des marées en deux phrases"*).
5. Appuyez sur **[ TERMINER ]**.
6. L'indicateur affiche **"Validation de la parole..."**, puis **"Réflexion en cours..."**.
7. L'assistant répond vocalement sur le haut-parleur et le texte complet s'affiche dans la carte **ASSISTANT**.

---

## 8. Utilisation des Boutons

- **Bouton PARLER / TERMINER** :
  - **Prêt** (`PARLER`, bleu) : déclenche l'écoute microphone.
  - **Écoute** (`TERMINER`, rouge) : finalise la prise de parole et soumet la capture au VAD.
  - **Traitement** (`TRAITEMENT...`, bleu clair) : attente de la synthèse de l'IA.
  - **Parole** (`STOP PAROLE`, violet) : clic possible pour interrompre la lecture vocale.
- **Bouton STOP** :
  - Bouton rouge permanent.
  - Annule instantanément une capture en cours, coupe le haut-parleur AW88298 et remet la session à l'état `READY` sans redémarrer le système.
- **Onglets Conversation / Paramètres** :
  - Basculent instantanément entre le dialogue et les options sans impacter la connexion audio/réseau.

---

## 9. Modification de la Voix et de la Langue

Depuis l'écran **Paramètres** :
1. **Langue** : Appuyez sur le bouton de langue pour alterner parmi *Français (fr-FR)*, *English (en-US)*, *Deutsch (de-DE)*, *Italiano (it-IT)* ou *Español (es-ES)*.
2. **Voix** : Appuyez sur le bouton de voix pour cycler entre les timbres compatibles du fournisseur sélectionné.
3. **Volume** : Ajustez par pas de 10 % avec `[-]` et `[+]`.
4. **Sauvegarde** : Appuyez sur **Sauver** pour mémoriser ces réglages en NVS (Flash). Ils seront rechargés automatiquement au prochain démarrage.

---

## 10. Changement Gemini / OpenAI

1. Ouvrez l'écran **Paramètres**.
2. Cliquez sur **Gemini Live** ou **OpenAI Realtime**.
3. L'application interrompt toute interaction active, ferme la session précédente et initialise la connexion avec le nouveau fournisseur.
4. L'indicateur signale la transition puis confirme la disponibilité.
5. Revenez à l'onglet **Conversation** : la pilule de l'en-tête reflète immédiatement le nouveau fournisseur actif.

---

## 11. Limitations Connues

- **Réseau Wi-Fi 5 GHz** : Non supporté par l'ESP32-S3 (utiliser un réseau Wi-Fi 2.4 GHz).
- **Transcription OpenAI** : La transcription textuelle dépend des événements Whisper/Realtime transmis par l'API ; si l'API ne renvoie pas la transcription partielle, seul le texte final validé est affiché.
- **Historique local** : Conservé uniquement en RAM pour préserver la Flash (les échanges sont effacés au redémarrage).
- **Pas de wake word** : La v0.1.0 utilise exclusivement le PTT tactile à deux clics (le mot de réveil local fait partie des évolutions futures de la vitrine).

---

## 12. Dépannage

- **"WiFi NO" dans l'en-tête** : Vérifiez le SSID et le mot de passe dans `secrets.h`. Vérifiez que le point d'accès émet en 2.4 GHz.
- **"Clé manquante"** : Assurez-vous d'avoir renseigné la clé d'API correspondant au fournisseur sélectionné (`GEMINI_API_KEY` ou `OPENAI_API_KEY`).
- **"VAD: REJET (SILENCE)"** : Le niveau sonore n'a pas atteint le plancher requis (380 RMS) ou aucun mot n'a été détecté. Parlez plus près du micro ou d'une voix plus ferme.
- **Pas de son** : Vérifiez que le volume n'est pas à 0 % dans les Paramètres.

---

## 13. Tests de Validation

L'application intègre le protocole complet de validation matérielle **VA-01 à VA-20** détaillé dans [TEST_PLAN.md](tests/TEST_PLAN.md), couvrant :
- Rejet de silence et bruit bref sans appel réseau (zéro trame parasite).
- Protection anti-rebond et rafales tactiles.
- Interruption STOP en cours d'écoute ou de parole.
- Reprise après timeout microphone (15 s) ou coupure Wi-Fi.

---

## 14. Relation avec VoiceKit

VoiceAssistant v0.1.0 est **strictement une application cliente** construite au-dessus de VoiceKit v0.6.7.1 :
- Elle n'implémente aucun VAD propriétaire (utilise `VoiceCaptureValidator`).
- Elle n'orchestre aucune tâche concurrente pour le micro ou l'I2S (délégués à `VoxCoreVoice`).
- Elle utilise `VoiceInteractionController` pour toute la logique d'état et de synchronisation non bloquante.
