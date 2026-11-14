# VoiceKit v0.6.6.2 — Limitations Connues & Périmètre Opérationnel

Ce document détaille de manière transparente les contraintes physiques, réseau, cloud et logicielles identifiées lors du cycle de validation de **VoiceKit v0.6.6.2**.

---

## 1. Limitations Matérielles

- **Matériel supporté exclusivement :** M5Stack CoreS3 (ESP32-S3FN8, 16 Mo Flash, 8 Mo PSRAM Octal SPI). Les versions antérieures de M5Stack (Core Basic, Core2, Fire, Atom, StickC) ne disposent pas des capacités matérielles requises (absence de double micro ES7210, instructions vectorielles ESP32-S3 requises pour WakeNet).
- **Réseau sans fil :** L'ESP32-S3 ne prend en charge que la bande Wi-Fi **2.4 GHz (802.11 b/g/n)**. Les réseaux 5 GHz purs ne sont pas détectés.
- **Microphone et acoustique :** La captation vocale est optimisée pour une distance d'utilisation de 20 à 70 cm en environnement intérieur modérément bruité. En environnement industriel ou très bruyant, l'absence de réduction de bruit multi-micros active peut dégrader la qualité de transcription.
- **Haut-parleur interne :** Le haut-parleur AW88298 intégré (1 W) est adapté à une écoute de proximité. Pour éviter la saturation acoustique à fort volume, le réglage logiciel recommandé se situe entre 60 % et 85 %.

---

## 2. Limitations Réseau et Transport

- **Connexion Internet requise :** Les fonctionnalités de transcription (STT), de synthèse (TTS en ligne) et de conversation (Gemini Live / OpenAI Realtime) nécessitent une connexion Internet permanente à faible latence (ping < 150 ms recommandé vers les serveurs cloud).
- **Chiffrement TLS :** Le protocole WebSocket sécurisé (`wss://`) consomme environ 40 à 55 Ko de RAM interne pour les tampons de négociation et les certificats X.509.
- **Synchronisation temporelle SNTP :** L'établissement de la session TLS requiert une date et heure valides pour vérifier la validité des certificats racine. VoiceKit tente une synchronisation SNTP dès l'acquisition de l'adresse IP.

---

## 3. Limitations des Services Cloud d'IA

- **Google Gemini Live :**
  - Nécessite une clé API Google AI Studio valide.
  - Soumis aux quotas et limites d'utilisation de l'API Gemini Multimodal Live.
  - La session est limitée dans le temps par le serveur selon les politiques de Google.
- **OpenAI Realtime :**
  - Nécessite un compte OpenAI avec accès à l'API Realtime (modèle `gpt-4o-realtime-preview`).
  - Facturation à la seconde audio / token selon la grille tarifaire OpenAI.
- **Disponibilité des voix et langues :**
  - Le catalogue de voix est prédéfini par chaque fournisseur (Gemini : Puck, Charon, Aoede, Fenrir, Kore ; OpenAI : alloy, echo, shimmer, ash, ballad, coral, sage, verse, marin, cedar).
  - La prononciation et la fidélité de l'intonation dépendent entièrement du modèle cloud sélectionné.

---

## 4. Fonctionnalités Expérimentales

- **Détection de Mot de Réveil Hors Ligne (`WakeWordFoundation`) :**
  - Modèle acoustique pré-entraîné WakeNet 9 pour la phrase « Bonjour ESP ».
  - Sensibilité optimisée pour un débit de parole régulier à 30 cm.
  - Les faux positifs ou faux négatifs peuvent augmenter en cas de bruit de fond musical ou télévisuel.
- **Mode d'écoute continue adaptative (`ContinuousSTT`) :**
  - La segmentation par seuil d'énergie (VAD locale) peut être influencée par les bruits d'ambiance ou les souffles d'air.
  - Pour une fiabilité maximale en environnement public ou partagé, le mode **Push-to-Talk (PTT)** reste le mode nominal recommandé.

---

## 5. Compatibilité Logicielle et Environnements

- **Standard C++17 obligatoire :** Le code utilise des fonctionnalités C++17 (`std::string_view`, liaisons déstructurées, lambdas génériques). Les configurations avec `-std=gnu++11` échouent à la compilation.
- **PlatformIO vs Arduino IDE :**
  - **PlatformIO :** Environnement de référence validé à 100 % sur les 10 exemples officiels.
  - **Arduino IDE :** L'exemple autonome `MinimalTTS_Arduino.ino` est fourni pour compatibilité. Toutefois, en raison de la complexité des bibliothèques de partitionnement et des binaires statiques (`libesp_sr_wakenet_s3.a`), l'environnement PlatformIO est formellement recommandé pour toute application avancée.
- **Partitionnement Flash :**
  - Les exemples intégrant LVGL 9 et plusieurs polices de caractères (`IntegratedVoiceDemo`, `VoiceSettingsDemo`) dépassent la taille de partition par défaut de 2 Mo (`no_ota.csv`) et exigent impérativement l'utilisation de `partitions_16MB.csv` (8 Mo d'application).

---

## 6. Problèmes Connus Non Bloquants

- **Avertissements de dépréciation LVGL 9 dans les exemples :**
  - Lors de la compilation d'exemples basés sur LVGL 9, des avertissements du compilateur peuvent signaler la dépréciation future de certaines macros (`lv_obj_remove_flag`, `lv_obj_add_flag`). Ces avertissements n'affectent ni l'exécution ni la stabilité du firmware.
- **Délai initial FastConnect Wi-Fi :**
  - Si le canal Wi-Fi ou le BSSID du point d'accès a changé depuis le dernier démarrage, la tentative initiale FastConnect expire après 3,5 secondes et bascule automatiquement sur un scan complet sans intervention utilisateur.
- **Exclusion des clés API des sauvegardes NVS :**
  - Par conception stricte de sécurité (invariant INV-10), aucune clé d'API n'est jamais sauvegardée dans la mémoire NVS de la carte. En cas de coupure de courant, les clés d'API doivent être fournies par la configuration applicative (`VoiceKitConfig`).
