# VoiceDiagnostics — VoiceKit v0.6.5.2

Exemple officiel de **télémétrie, diagnostic et visualisation temps réel** pour le moteur vocal **VoiceKit v0.6.5.2** sur **M5Stack CoreS3**.

---

## 1. Objectif

Fournir un outil d'inspection matériel et réseau complet permettant de valider les performances intrinsèques du CoreS3 :
- Mesure précise de la RAM interne et de la PSRAM disponible.
- Contrôle d'intégrité du microphone ES7210 (niveau RMS) et du haut-parleur AW88298.
- Suivi des latences audio et réseau (jalons temporels détaillés, seuils VAD).
- Télémétrie de connexion Wi-Fi et session WebSocket TLS.
- Historique d'événements horodaté sans allocation dynamique excessive ni clignotement d'écran.

---

## 2. Fonctionnalités

- **Interface tactile à 6 onglets (320 × 240 M5GFX)** :
  1. `SYS` (Système) : RAM libre/min, PSRAM libre, fréquence CPU, uptime, modèle ESP32-S3.
  2. `AUDIO` (Audio) : Vumètre RMS micro, statut STT/TTS, compteurs d'acquisition et restitution.
  3. `LAT` (Latence) : Délais d'aller-retour réseau, latence du premier échantillon DMA, ajustement dynamique du seuil VAD.
  4. `NET` (Réseau) : État Wi-Fi, IP, RSSI, état de la session WebSocket, compteurs de reconnexions.
  5. `CONN` (Connexion) : Temps d'amorçage Wi-Fi, synchronisation SNTP, négociation TLS et passage à READY.
  6. `EVENTS` (Événements) : Journal défilant horodaté des 16 derniers événements vocaux.
- **Rafraîchissement non bloquant** : Cadencé à 5 Hz (~200 ms) pour ne pas perturber les tâches audio FreeRTOS.
- **Sortie série structurée** : Télémétrie périodique au format `[DIAG]` ou CSV optionnel.

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3, 8 Mo PSRAM, 16 Mo Flash).
- **Affichage & tactile** : Écran LCD tactile capacitif 320 × 240.
- **Audio** : Microphone double ES7210 et haut-parleur AW88298.
- **Alimentation** : USB-C 5V / 1A.

---

## 4. Configuration

1. Copier le fichier des secrets :
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
pio run -d examples/VoiceDiagnostics
```

---

## 6. Téléversement

1. Téléverser sur la carte :
   ```bash
   pio run -d examples/VoiceDiagnostics -t upload
   ```
2. Lancer la console série :
   ```bash
   pio device monitor -d examples/VoiceDiagnostics -b 115200
   ```

---

## 7. Mode d'utilisation

1. À la mise sous tension, l'écran démarre sur l'onglet `SYS`.
2. **Navigation** : Toucher les onglets dans la barre supérieure (`SYS`, `AUDIO`, `LAT`, `NET`, `CONN`, `EVENTS`) pour basculer d'écran.
3. **Test audio direct** : Dans l'onglet `AUDIO`, un bouton tactile permet de déclencher une phrase test de synthèse vocale.
4. **Réglage VAD** : Dans l'onglet `LAT`, toucher la dernière ligne pour cycler entre les seuils VAD recommandés (800 ms, 1 000 ms, 1 200 ms).
5. **Commandes série** :
   - `t` : Déclencher un test de synthèse vocale.
   - `p` : Afficher l'instantané complet des métriques dans la console.

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| 1 | Mesure mémoire | Consulter l'onglet `SYS` | RAM libre > 120 Ko, PSRAM libre > 7,5 Mo. |
| 2 | Niveau microphone | Parler face au CoreS3 sur l'onglet `AUDIO` | La barre de niveau RMS fluctue avec le volume de la voix. |
| 3 | Synthèse test | Appuyer sur *TEST TTS* | Énonciation claire, état haut-parleur passant à *OCCUPÉ*. |
| 4 | Métriques de latence | Réaliser une synthèse ou dictée, aller sur `LAT` | Temps de premier bloc audio renseigné en millisecondes. |
| 5 | Reconnexion Wi-Fi | Couper brièvement le routeur Wi-Fi | Incrémentation du compteur de reconnexions, retour à l'état connecté. |

---

## 9. Résultats attendus

- Zéro fuite de mémoire après des heures de monitoring continu.
- Métriques authentiques (aucun chiffre inventé ou statique).
- Pas de clignotement de l'écran lors des rafraîchissements périodiques.

---

## 10. Limitations connues

- Les mesures de latence nécessitent au moins un échange complet pour afficher des moyennes significatives.
- L'affichage sans LVGL utilise une police matricielle compacte pour optimiser la réactivité.
