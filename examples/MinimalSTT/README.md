# MinimalSTT — VoiceKit v0.6.5.2

Exemple officiel de reconnaissance vocale unitaire **Push-to-Talk (STT — Speech-to-Text)** en streaming sur **M5Stack CoreS3**.

---

## 1. Objectif

Démontrer et valider le cycle Push-to-Talk unitaire sécurisé :
- Démarrage effectif du microphone confirmé par le moteur vocal.
- Streaming asynchrone PCM 16 kHz vers Google Gemini Live.
- Affichage temps réel des transcriptions partielles (*interim*) et finales.
- Fiabilisation complète contre les doubles commandes, les relâchements hors zone et la reprise automatique après erreur.

---

## 2. Fonctionnalités

- **Push-to-Talk fiable** : L'écoute démarre à l'appui tactile et se termine au relâchement du doigt.
- **Sécurisation de la capture** : Le microphone n'est considéré actif qu'après confirmation de `startListening()`.
- **Gestion des rejets** : Signalement clair si une tentative de démarrage d'écoute survient avant que le système ne soit prêt.
- **Gestion des sorties de zone tactile** : Si le doigt glisse hors du bouton tactile pendant l'appui, la capture s'arrête proprement.
- **Protection anti-double commande** : Verrouillage strict empêchant d'envoyer plusieurs requêtes de démarrage concurrentes.
- **États d'application distincts** : `READY`, `LISTENING`, `TRANSCRIBING` et `ERROR`.
- **Reprise après erreur** : Un toucher sur l'écran permet de réinitialiser l'état d'erreur dès que la connexion est rétablie.
- **Validité des transcriptions** : Aucune transcription n'est acceptée sans capture active correspondante.

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3.
- **Microphone** : Puce double microphone ES7210 intégrée au CoreS3.
- **Affichage** : Écran LCD tactile capacitif 320 × 240.
- **Alimentation** : USB-C 5V / 1A.

---

## 4. Configuration

1. Copier le fichier de modèle :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Renseigner vos accès dans `include/secrets.h` :
   ```cpp
   #define WIFI_SSID     "Votre_SSID_WiFi"
   #define WIFI_PASSWORD "Votre_Mot_De_Passe"
   #define GEMINI_KEY    "Votre_Cle_API_Google_Gemini"
   ```

---

## 5. Compilation

Compiler avec PlatformIO :
```bash
pio run -d examples/MinimalSTT
```

---

## 6. Téléversement

1. Téléverser le firmware :
   ```bash
   pio run -d examples/MinimalSTT -t upload
   ```
2. Ouvrir le moniteur série :
   ```bash
   pio device monitor -d examples/MinimalSTT -b 115200
   ```

---

## 7. Mode d'utilisation

1. Attendre l'affichage du badge vert **PRET**.
2. **Dicter** : Poser et maintenir le doigt sur le bouton bleu **DICTER (Maintenir)**. Le bouton devient rouge avec le libellé **ENREGISTREMENT...**.
3. Parler clairement face au microphone CoreS3.
4. Les transcriptions intermédiaires s'affichent en temps réel dans la boîte de dialogue centrale.
5. **Finaliser** : Relâcher le bouton. L'état passe brièvement à **TRANSCRIBING** (jaune), puis la transcription finale s'affiche en vert avec retour à l'état **PRET**.
6. En cas d'erreur réseau, l'état passe à **ERREUR** (magenta). Toucher l'écran pour réinitialiser une fois la connexion revenue.

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| 1 | Dictée nominale courte | Maintenir DICTER, dire *« Bonjour ESP »*, relâcher | Transcription finale exacte reçue et affichée. |
| 2 | Dictée longue | Maintenir DICTER pendant 10 secondes, parler en continu | Transcriptions intermédiaires visibles en direct, finalisation propre. |
| 3 | Sortie de zone tactile | Appuyer sur DICTER, glisser le doigt hors du bouton | Fin immédiate de la capture, finalisation sans blocage. |
| 4 | Double appui rapide | Toucher plusieurs fois très rapidement le bouton | Un seul démarrage effectif, rejet propre des appuis parasites. |
| 5 | Récupération erreur | Déconnecter le Wi-Fi (ou clé invalide) puis reconnecter | Affichage état ERREUR, toucher l'écran pour revenir à PRET. |

---

## 9. Résultats attendus

- Cycle d'état rigoureusement respecté :
  $$\text{INITIALIZING} \longrightarrow \text{READY} \rightleftarrows \text{LISTENING} \longrightarrow \text{TRANSCRIBING} \longrightarrow \text{READY}$$
- Pas de texte fantôme inventé sans parole de l'utilisateur.
- Pas de blocage du thread applicatif.

---

## 10. Limitations connues

- Portée optimale du microphone : 10 à 50 cm.
- Requiert un environnement sonore calme (les bruits de fond intenses peuvent perturber la reconnaissance).
- Mode unitaire uniquement : pour une conversation complète avec réponse sonore, utiliser `MinimalConversation` ou `IntegratedVoiceDemo`.
