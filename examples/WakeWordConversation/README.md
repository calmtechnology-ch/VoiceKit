# WakeWordConversation — VoiceKit v0.6.5.2

Exemple officiel de dialogue vocal mains libres complet : réveil acoustique hors ligne par « **Bonjour ESP** » puis conversation multi-tours avec **Gemini Live** ou **OpenAI Realtime** sur **M5Stack CoreS3**.

---

## 1. Objectif

Démontrer et valider l'intégralité du cycle de vie mains libres sur microcontrôleur :
$$\text{WAKE\_ARMED} \overset{\text{« Bonjour ESP »}}{\longrightarrow} \text{DETECTED} \longrightarrow \text{CONNECTING} \longrightarrow \text{LISTENING} \rightleftarrows \text{SPEAKING} \longrightarrow \text{RETURNING\_TO\_WAKE} \longrightarrow \text{WAKE\_ARMED}$$
Cet exemple garantit l'arbitrage exclusif du microphone matériel ES7210 entre le moteur local WakeNet et la session cloud bidirectionnelle, sans conflit d'acquisition ni fuite mémoire.

---

## 2. Fonctionnalités

- **Activation acoustique réelle « Bonjour ESP »** : Détection locale hors ligne ultra-rapide par modèle WakeNet ESP-SR.
- **Passation sans coupure du microphone** : Libération contrôlée du microphone par WakeNet avant son assignation au flux conversationnel.
- **Support multi-fournisseurs** : Compatible Google Gemini Live (par défaut) et OpenAI Realtime.
- **Dialogue multi-tours naturel** : Une fois réveillé, l'assistant enchaîne les questions et réponses sans avoir à répéter le mot de réveil.
- **Clôture automatique intelligente** : Fin de session après 30 s d'inactivité, commande vocale finale (*« Au revoir »*) ou bouton tactile STOP.
- **Retour automatique à WakeNet** : Réarmement silencieux du détecteur local à la clôture de la session cloud.
- **Affichage M5GFX dynamique** : Rendu visuel 320 × 240 avec vumètre audio RMS temps réel et code couleur par état.

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3).
- **Mémoire** : 8 Mo OPI PSRAM requise.
- **Audio** : Microphone double ES7210 et haut-parleur AW88298.
- **Alimentation** : USB-C 5V / 1A.

---

## 4. Configuration

1. Copier le fichier des secrets :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Éditer `include/secrets.h` avec vos identifiants :
   ```cpp
   #define WIFI_SSID     "Votre_SSID_WiFi"
   #define WIFI_PASSWORD "Votre_Mot_De_Passe"
   #define GEMINI_KEY    "Votre_Cle_API_Google_Gemini"
   // ou #define OPENAI_API_KEY "Votre_Cle_API_OpenAI"
   ```

---

## 5. Compilation

Compiler avec PlatformIO :
```bash
pio run -d examples/WakeWordConversation
```

---

## 6. Téléversement

1. Téléverser le programme :
   ```bash
   pio run -d examples/WakeWordConversation -t upload
   ```
2. Ouvrir la console série :
   ```bash
   pio device monitor -d examples/WakeWordConversation -b 115200
   ```

---

## 7. Mode d'utilisation

1. L'écran affiche un bandeau vert : `EN ATTENTE — « Bonjour ESP »`. Le microphone écoute localement sans consommer de bande passante réseau.
2. Dire à voix intelligible : **« Bonjour ESP »**.
3. Le bandeau passe en orange vif (`BONJOUR ESP DÉTECTÉ`), puis en cyan (`CONNEXION...`), puis en vert clair (`PRÊT — PARLEZ`).
4. Poser votre question (ex: *« Quel temps fait-il aujourd'hui ? »*).
5. L'assistant répond vocalement (bandeau bleu ciel `RÉPONSE VOCALE`).
6. Enchaîner immédiatement avec une autre question sans répéter « Bonjour ESP ».
7. Pour clore la conversation : dire *« Au revoir »*, attendre 30 secondes de silence ou toucher le bouton STOP sur l'écran.
8. Le système affiche brièvement `RETOUR EN ÉCOUTE` puis revient en veille locale sur « Bonjour ESP ».

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| 1 | Réveil acoustique | Dire *« Bonjour ESP »* | Détection confirmée, connexion cloud initiée immédiatement. |
| 2 | Échange vocal multi-tours | Poser 2 questions à la suite | Échanges fluides, réponses orales complètes sans coupure. |
| 3 | Fin par mot-clé | Dire distinctement *« Au revoir »* | Clôture de la session cloud, retour à l'écoute « Bonjour ESP ». |
| 4 | Fin par timeout | Laisser 30 s de silence après une réponse | Fermeture automatique de la session, réarmement de WakeNet. |
| 5 | Arrêt manuel STOP | Toucher STOP pendant la réponse | Coupure immédiate, déconnexion et réarmement local. |

---

## 9. Résultats attendus

- Aucun conflit matériel sur le microphone (arbitrage sans double initialisation I2S).
- Absence totale de double session WebSocket.
- Réarmement automatique systématique et stable sur des dizaines de cycles successifs.

---

## 10. Limitations connues

- La transition de connexion réseau initiale (TLS handshake) prend généralement entre 800 ms et 1,8 s selon le Wi-Fi.
- Le mot de réveil « Bonjour ESP » doit être prononcé clairement avec une articulation naturelle.
