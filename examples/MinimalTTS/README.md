# MinimalTTS — VoiceKit v0.6.5.2

Exemple minimaliste de synthèse vocale unitaire (TTS — Text-to-Speech) haute fidélité PCM 24 kHz sur **M5Stack CoreS3**.

---

## 1. Objectif

Fournir le point d'entrée le plus direct et lisible pour tester la synthèse vocale autonome avec VoiceKit et Google Gemini Live.
Le code démontre la connexion réseau, la négociation TLS, l'initialisation du moteur VoxCore en mode dédié FreeRTOS (Mode 2 DMA) et l'énonciation de texte fluide sans coupure sonore.

---

## 2. Fonctionnalités

- Initialisation matérielle simplifiée du M5Stack CoreS3 (I2C, PMIC AXP2101, amplificateur audio AW88298).
- Gestion autonome de la connexion Wi-Fi et de la synchronisation de l'horloge système SNTP.
- Restitution audio PCM 24 kHz / 16-bit mono haute fidélité.
- Énonciation d'une annonce automatique d'accueil dès la connexion effective.
- Déclenchement tactile d'une série de phrases de test (courtes, moyennes, longues).
- Support de l'interruption STOP par appui tactile pendant la restitution.
- Affichage graphique simple sans dépendance LVGL (M5Unified / M5GFX).

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3).
- **Mémoire** : 8 Mo OPI PSRAM requise pour le tampon de streaming PCM.
- **Audio** : Haut-parleur interne CoreS3.
- **Alimentation** : USB-C 5V / 1A.

---

## 4. Configuration

1. Copier le modèle d'identifiants :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Renseigner vos identifiants dans `include/secrets.h` :
   ```cpp
   #define WIFI_SSID     "Votre_SSID_WiFi"
   #define WIFI_PASSWORD "Votre_Mot_De_Passe"
   #define GEMINI_KEY    "Votre_Cle_API_Google_Gemini"
   ```

---

## 5. Compilation

Compiler avec PlatformIO :
```bash
pio run -d examples/MinimalTTS
```

---

## 6. Téléversement

1. Téléverser sur la carte connectée :
   ```bash
   pio run -d examples/MinimalTTS -t upload
   ```
2. Ouvrir la console série :
   ```bash
   pio device monitor -d examples/MinimalTTS -b 115200
   ```

---

## 7. Mode d'utilisation

1. À la mise sous tension, l'écran indique `Statut: DEMARRAGE...`.
2. Une fois connecté, le statut passe à `Statut: PRET (Touchez l'ecran)` et la phrase d'accueil est prononcée : *« Bonjour, je suis VoiceKit. »*.
3. Chaque appui sur l'écran tactile déclenche l'énonciation de la phrase de test suivante dans la liste.
4. Si l'écran est touché pendant qu'une phrase est prononcée, la restitution est immédiatement stoppée via `voice.stop()`.

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| 1 | Démarrage & Connexion | Mettre sous tension | Le Wi-Fi se connecte, l'heure NTP est acquise, le statut passe à PRÊT. |
| 2 | Phrase d'accueil | Attendre le passage à l'état prêt | Restitution audible et claire de *« Bonjour, je suis VoiceKit »*. |
| 3 | Phrase courte | Appuyer sur l'écran | Énonciation fluide sans coupure de la phrase #1. |
| 4 | Phrase longue | Appuyer 3 fois sur l'écran pour atteindre le test #4 | Restitution de ~20 s sans saturation ni décalage de buffer. |
| 5 | Interruption STOP | Toucher l'écran pendant une restitution | Coupure sonore immédiate, retour à l'état PRÊT. |

---

## 9. Résultats attendus

- Restitution audio claire et exempte de craquements ou de coupures sur la dernière syllabe.
- Transition propre des états :
  $$\text{UNINITIALIZED} \longrightarrow \text{CONNECTING\_WIFI} \longrightarrow \text{CONNECTING\_SERVICE} \longrightarrow \text{READY} \rightleftarrows \text{SPEAKING}$$
- Consommation mémoire stable sans fuite de heap.

---

## 10. Limitations connues

- Nécessite un réseau Wi-Fi 2.4 GHz connecté à Internet.
- Ne prend pas en charge la reconnaissance vocale (STT) ni le dialogue bidirectionnel (consulter `MinimalSTT` et `MinimalConversation`).

