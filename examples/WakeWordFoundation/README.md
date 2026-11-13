# WakeWordFoundation — VoiceKit v0.6.5.2

Exemple autonome de référence pour la détection acoustique locale hors ligne de mot de réveil (**Wake Word**) sur **M5Stack CoreS3**.

---

## 1. Objectif

Valider à 100 % hors ligne la détection acoustique locale du mot de réveil officiel « **Bonjour ESP** » (modèle WakeNet ESP-SR `wn9l_fr_bonjouresp_tts3`), sans recours à une connexion Wi-Fi ni à un service cloud.
Cet exemple sert de banc de diagnostic acoustique pour vérifier le bon fonctionnement du microphone ES7210, la cadence des trames audio et la stabilité de la machine à états de détection.

---

## 2. Fonctionnalités

- **Reconnaissance acoustique réelle WakeNet** : Exécution du modèle neural d'Espressif ESP-SR optimisé pour l'ESP32-S3.
- **Distinction honnête de l'origine** : Différencie clairement à l'écran et sur la liaison série une reconnaissance acoustique réelle d'un événement simulé.
- **Machine à états complète** :
  - `INITIALIZING` : Chargement du modèle neural.
  - `ARMED` : Écoute continue et analyse spectrale active.
  - `DETECTED` : Mot de réveil reconnu avec affichage de l'encadré vert de validation.
  - `SUSPENDED` : Détection mise en pause temporaire.
  - `ERROR` : Signalement d'anomalie.
- **Rendu graphique M5GFX ultra-léger** : Interface graphique fluide 320 × 240 sans surcharge de mémoire (sans LVGL).
- **Compteurs de métriques fiables** : Comptabilisation unitaire des détections, des réarmements et des erreurs.
- **Pilotage interactif par console série** : Commandes pour simuler, suspendre, réarmer ou changer de moteur.

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3 avec instructions vectorielles IA).
- **Mémoire** : 8 Mo PSRAM (utilisée pour les buffers d'inférence WakeNet).
- **Capteurs** : Microphone double ES7210 CoreS3.
- **Alimentation** : Câble USB-C connecté à une source stable (5V / 1A).

---

## 4. Configuration

Aucun fichier `secrets.h` ni identifiant Wi-Fi n'est requis : l'exemple fonctionne entièrement hors ligne.
Le fichier `platformio.ini` est préconfiguré pour inclure les drapeaux de compilation et les bibliothèques d'inférence nécessaires à ESP-SR.

---

## 5. Compilation

Compiler avec PlatformIO :
```bash
pio run -d examples/WakeWordFoundation
```

---

## 6. Téléversement

1. Téléverser le binaire sur le CoreS3 :
   ```bash
   pio run -d examples/WakeWordFoundation -t upload
   ```
2. Ouvrir le moniteur série à 115 200 baud :
   ```bash
   pio device monitor -d examples/WakeWordFoundation -b 115200
   ```

---

## 7. Mode d'utilisation

1. À la mise sous tension, l'écran affiche `Moteur actif : WAKENET` et le statut passe à `ARMED — En écoute...`.
2. **Prononcer** distinctement à voix normale à environ 20-40 cm du CoreS3 :
   > *« Bonjour ESP »*
3. Dès détection, l'écran s'illumine en vert avec l'encadré :
   `MOT DE RÉVEIL DÉTECTÉ ! — « Bonjour ESP » — Événement #1`.
4. Après 2,5 secondes de suspension, le système se réarme automatiquement (`ARMED — Prêt pour un nouveau réveil`).
5. Toucher l'écran permet de forcer une simulation ou de basculer l'état.

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| 1 | Détection acoustique nominale | Prononcer *« Bonjour ESP »* à 30 cm | Détection instantanée, compteur incrémenté de 1, badge vert. |
| 2 | Robustesse aux faux positifs | Parler d'autre chose (*« Bonjour tout le monde »*) | Pas de déclenchement intempestif. |
| 3 | Réarmement automatique | Attendre 2,5 s après détection | Transition automatique de `SUSPENDED` vers `ARMED`. |
| 4 | Suspension manuelle | Envoyer `s` sur la console série | L'état passe à `SUSPENDED`, l'écoute est arrêtée. |
| 5 | Réarmement manuel | Envoyer `r` sur la console série | L'état repasse à `ARMED`, prêt pour une nouvelle détection. |
| 6 | Compteurs cumulés | Déclencher 5 détections successives | Le compteur indique exactement 5 détections et 5 réarmements. |

---

## 9. Résultats attendus

- Cycle d'états :
  $$\text{INITIALIZING} \longrightarrow \text{ARMED} \overset{\text{« Bonjour ESP »}}{\longrightarrow} \text{DETECTED} \longrightarrow \text{SUSPENDED} \overset{2.5\text{ s}}{\longrightarrow} \text{ARMED}$$
- Zéro crash ni fuite mémoire lors de détections répétées sur de longues périodes.
- Distinction explicite entre détection acoustique réelle et test série.

---

## 10. Limitations connues

- Le modèle WakeNet est optimisé pour la langue française (« Bonjour ESP ») et nécessite une prononciation claire.
- Dans les environnements très bruyants (> 75 dB), la distance de détection efficace peut être réduite.
