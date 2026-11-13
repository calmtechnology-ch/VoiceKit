# MinimalConversation — VoiceKit v0.6.5.2

Exemple officiel de dialogue vocal bidirectionnel fluide (Full-Duplex / Mode B) avec **Google Gemini Live** sur **M5Stack CoreS3**.

---

## 1. Objectif

Démontrer un cycle conversationnel simple, réactif et complet :
$$\text{Écoute (PTT)} \longrightarrow \text{Transcription utilisateur} \longrightarrow \text{Réflexion IA} \longrightarrow \text{Réponse vocale (TTS)} \longrightarrow \text{Prêt}$$
L'exemple reste délibérément épuré : il n'intègre pas d'orchestrateur complexe ni de registre d'outils, permettant de tester isolément les capacités d'échange naturel avec Gemini Live.

---

## 2. Fonctionnalités

- **Conversation bidirectionnelle avec Gemini Live** : Transmission continue PCM 16 kHz et réception de flux PCM 24 kHz via WebSocket TLS.
- **Acquisition microphone optimisée** : Microphone ES7210 piloté sans interférence avec l'amplificateur de sortie.
- **Affichage synchronisé** : Transcription en temps réel de ce que dit l'utilisateur et du texte de réponse de l'assistant.
- **Restitution TTS haute fidélité** : Son clair via le haut-parleur AW88298 intégré.
- **Interruption STOP prioritaire** : Possibilité de stopper la réponse vocale en cours à tout moment par appui tactile ou commande série.
- **Contrôle clavier série** : Commandes `P` (Parler), `R` (Relâcher) et `S` (Stop) pour automatiser les tests.
- **Gestion des erreurs et reconnexion** : Remontée explicite des statuts réseau et moteur vocal.

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3).
- **Mémoire** : 8 Mo OPI PSRAM requise pour les flux audio simultanés.
- **Audio** : Microphone ES7210 et haut-parleur AW88298.
- **Alimentation** : USB-C 5V / 1A stable.

---

## 4. Configuration

1. Créer le fichier d'identifiants :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Compléter `include/secrets.h` :
   ```cpp
   #define WIFI_SSID     "Votre_SSID_WiFi"
   #define WIFI_PASSWORD "Votre_Mot_De_Passe"
   #define GEMINI_KEY    "Votre_Cle_API_Google_Gemini"
   ```

---

## 5. Compilation

Compiler avec PlatformIO :
```bash
pio run -d examples/MinimalConversation
```

---

## 6. Téléversement

1. Téléverser sur le M5Stack CoreS3 :
   ```bash
   pio run -d examples/MinimalConversation -t upload
   ```
2. Lancer le moniteur série :
   ```bash
   pio device monitor -d examples/MinimalConversation -b 115200
   ```

---

## 7. Mode d'utilisation

1. Attendre la fin de l'initialisation (badge **PRET** en haut à droite).
2. **Poser une question** : Maintenir le doigt appuyé sur le bouton bleu **PARLER**.
3. Poser votre question (ex: *« Quelle est la capitale de l'Islande ? »*).
4. **Relâcher** le bouton : l'état passe à **REFLEXION...** (orange).
5. L'assistant commence à répondre vocalement (état **REPONSE EN COURS**, bouton devient **STOP**).
6. Pour interrompre la réponse, appuyer sur le bouton rouge **STOP**.

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| 1 | Échange court | Poser une question factuelle brève | Réponse rapide, voix naturelle, texte transcrit affiché. |
| 2 | Échange multi-tours | Poser 3 questions consécutives | Le contexte est maintenu, aucun redémarrage du moteur. |
| 3 | Interruption STOP | Toucher STOP dès les premiers mots de l'assistant | Coupure audio immédiate, retour à l'état PRET. |
| 4 | Commande série | Envoyer `P` dans la console, parler, envoyer `R` | Déclenchement et terminaison équivalents à l'appui tactile. |
| 5 | Silence | Maintenir PARLER 3 s sans parler puis relâcher | Pas d'hallucination, retour propre à l'état initial. |

---

## 9. Résultats attendus

- Cycle conversationnel respecté :
  $$\text{READY} \longrightarrow \text{LISTENING} \longrightarrow \text{PROCESSING} \longrightarrow \text{SPEAKING} \longrightarrow \text{READY}$$
- Clarté acoustique sans sifflement ni souffle excessif.
- Absence de latence accumulée après plusieurs tours de conversation.

---

## 10. Limitations connues

- Dépendant de la latence du réseau Wi-Fi et de la disponibilité du serveur Gemini Live.
- Ne prend pas en charge les outils locaux ou le déclenchement par mot de réveil (voir `LocalToolRegistryDemo` et `WakeWordConversation`).
