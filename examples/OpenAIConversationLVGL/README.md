# OpenAIConversationLVGL — VoiceKit v0.6.5.2

Exemple officiel de référence pour **OpenAI Realtime (`gpt-realtime-2.1-mini`)** avec interface graphique tactile **LVGL 9** sur **M5Stack CoreS3**.

---

## 1. Objectif

Fournir un exemple autonome de conversation vocale temps réel de haute qualité associant l'API OpenAI Realtime (WebSocket TLS) et une interface graphique fluide en mode sombre sous LVGL 9, avec prise en charge intégrale de la typographie française accentuée et machine d'état Push-to-Talk fiabilisée.

---

## 2. Fonctionnalités

- **Interface Dark Mode LVGL 9** : Rendu graphique structuré avec orbe vocal animé (`VoiceOrb`), cartes de transcription pour l'utilisateur et l'assistant, et bouton d'action tactile.
- **Typographie française native** : Polices Segoe UI personnalisées (`font_french_12`, `14`, `16`) intégrant tous les caractères accentués, ligatures (`œ`, `Œ`) et guillemets (`« »`).
- **Contrôleur Push-to-Talk (PTT) sécurisé** : Machine à états finis explicite avec watchdogs anti-blocage et absorption des rebonds tactiles.
- **Communication OpenAI Realtime** : Support direct du modèle `gpt-realtime-2.1-mini` via WebSocket TLS.
- **Synchronisation inter-threads sans allocation dynamique** : Tampons statiques bornés et verrous légers `portMUX_TYPE` pour transférer les événements sans perturber la tâche audio.
- **Interruption STOP prioritaire** : Arrêt immédiat de la parole de l'assistant par appui tactile ou commande série.
- **Commandes série de diagnostic** : Pilotage via console série (`P`, `R`, `S`, `D`, `E`).

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3 dual-core LX7 @ 240 MHz).
- **Mémoire** : 8 Mo PSRAM Octal SPI (indispensable pour les flux PCM et LVGL 9).
- **Affichage & tactile** : Écran LCD 320 × 240 ILI9342C avec tactile capacitif FT6336U.
- **Audio** : Microphone double ES7210 et amplificateur AW88298.
- **Alimentation** : Câble USB-C fournissant 5V / 1A.

---

## 4. Configuration

1. Copier le fichier d'exemple :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Éditer `include/secrets.h` avec vos identifiants :
   ```cpp
   #define WIFI_SSID       "Votre_SSID_WiFi"
   #define WIFI_PASSWORD   "Votre_Mot_De_Passe"
   #define OPENAI_API_KEY  "sk-proj-Votre_Cle_OpenAI"
   ```

---

## 5. Compilation

Compiler avec PlatformIO :
```bash
pio run -d examples/OpenAIConversationLVGL
```

---

## 6. Téléversement

1. Téléverser sur la carte connectée :
   ```bash
   pio run -d examples/OpenAIConversationLVGL -t upload
   ```
2. Ouvrir le moniteur série à 115 200 baud :
   ```bash
   pio device monitor -d examples/OpenAIConversationLVGL -b 115200
   ```

---

## 7. Mode d'utilisation

1. À la mise sous tension, l'orbe affiche une animation cyan de respiration pendant la connexion.
2. Dès que le statut indique **PRÊT**, maintenir le bouton inférieur pour parler.
3. L'orbe s'anime en corail au rythme du volume vocal détecté par le microphone.
4. Relâcher le bouton pour envoyer la capture vocale à OpenAI.
5. La transcription de l'utilisateur s'affiche en vert, l'orbe pulse en violet pendant la génération.
6. L'assistant répond vocalement en PCM et sa réponse s'affiche en bleu ciel.
7. Toucher le bouton **STOP** interrompt instantanément la restitution.

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| 1 | Échange conversationnel | Poser une question en maintenant l'appui | Réponse vocale OpenAI fluide et synchronisée avec le texte. |
| 2 | Interruption STOP | Toucher STOP pendant la réponse vocale | Arrêt audio immédiat, retour au statut PRÊT. |
| 3 | Typographie française | Poser une question sur les accents | Affichage parfait des caractères `é`, `è`, `œ`, `« »`. |
| 4 | Diagnostic mémoire | Envoyer la commande `D` sur le port série | Rapport détaillé affichant le heap disponible et les métriques de latence. |
| 5 | Relâchement prématuré | Relâcher en moins de 200 ms | Gestion propre sans blocage de l'interface graphique. |

---

## 9. Résultats attendus

- Maintien d'une cadence d'affichage fluide sans saccade (> 30 FPS).
- Absence totale de fuite mémoire ou d'erreur `out of memory` dans le pool LVGL.
- Audio pur sans claquement ni troncature de fin de réponse.

---

## 10. Limitations connues

- Nécessite un compte OpenAI avec accès à l'API Realtime (`gpt-realtime-2.1-mini`).
- Le transfert d'affichage vers l'écran ILI9342C est synchrone (typique 2 à 15 ms selon la surface modifiée).
