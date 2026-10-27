# Parcours d'Apprentissage VoiceKit — M5Stack CoreS3

Bienvenue dans le tutoriel officiel de **VoiceKit** (version de référence v0.6.0).

Ce parcours pédagogique progressif a été conçu pour permettre à tout développeur, même débutant en programmation vocale ou sur ESP32-S3, de découvrir, comprendre et maîtriser pas à pas la création d'applications vocales connectées avec **VoiceKit** sur la carte **M5Stack CoreS3**.

---

## 1. Philosophie Pédagogique : La Simplicité Avant Tout

La programmation vocale sur microcontrôleur moderne (gestion des flux audio I2S, protocoles WebSocket sécurisés TLS, compression PCM, reconnaissance d'intention, intelligence artificielle générative en temps réel) peut sembler intimidante.

VoiceKit a été conçu pour résoudre cette complexité grâce à une façade haut niveau (`VoiceKit.h`). Ce tutoriel respecte une règle stricte : **la simplicité avant la sophistication**.

- **Chaque leçon est autonome** : elle contient son propre projet PlatformIO compilable et prêt à l'emploi.
- **Progression pas à pas** : chaque leçon n'introduit qu'un nombre restreint de nouvelles fonctions.
- **Zéro abstraction superflue** : aucun patron de conception complexe, aucune classe intermédiaire, aucun code obscur.
- **Code intégralement commenté en français** : tout ce qui est écrit est expliqué.
- **Résultats concrets immédiats** : chaque étape se traduit par un comportement observable dans le moniteur série ou sur le CoreS3.

---

## 2. Matériel et Logiciels Requis

### Matériel
- **1× M5Stack CoreS3** (ESP32-S3 double cœur 240 MHz, 16 Mo Flash, 8 Mo PSRAM, écran tactile 320×240, double microphone ES7210, haut-parleur avec ampli AW88298).
- **1× Câble USB-C** (données et alimentation).
- **1× Connexion Wi-Fi 2.4 GHz** (avec accès Internet).

### Logiciels
- **Visual Studio Code** (Windows, macOS ou Linux).
- Extension **PlatformIO IDE** pour VS Code.
- Un navigateur Web pour générer votre clé d'API sur [Google AI Studio](https://aistudio.google.com/).

---

## 3. Sommaire Général du Parcours (Leçons 00 à 11)

Le parcours complet est découpé en 12 étapes progressives, de la prise en main de l'environnement jusqu'à la création d'une interface graphique tactile complète avec LVGL.

| Leçon | Titre | Statut | Compétences Acquises |
|---|---|---|---|
| [**00 — Installation**](00-Installation/README.md) | Préparer son environnement VoiceKit | **Disponible (v0.1.0)** | Prise en main du CoreS3, installation de VS Code / PlatformIO, création de la clé API Gemini Live, configuration Wi-Fi. |
| [**01 — Hello VoiceKit**](01-HelloVoiceKit/README.md) | Mon premier programme VoiceKit | **Disponible (v0.1.0)** | Initialisation de la façade `VoiceKit`, gestion de `VoiceKitConfig`, connexion Wi-Fi managée, suivi des états et événements de disponibilité. |
| [**02 — Text-to-Speech**](02-TextToSpeech/README.md) | Faire parler le CoreS3 | **Disponible (v0.1.0)** | Restitution vocale asynchrone avec `say()`, détection du début (`onSpeechStarted`) et de la fin de parole (`onSpeechFinished`), gestion non bloquante. |
| [**03 — Speech Control**](03-SpeechControl/README.md) | Contrôler la synthèse vocale | **Disponible (v0.1.0)** | Pilotage interactif depuis le moniteur série : réglage du volume (`setVolume`), arrêt d'urgence (`stop`), consultation d'état et d'erreurs. |
| **04 — Voice Settings** | Personnaliser la voix et le style | *À venir (v0.2.0)* | Choix de la voix Gemini (`Aoede`, `Puck`...), langue (`fr-FR`), styles d'élocution (`natural`, `professional`...) et tonalités. |
| **05 — Speech-to-Text** | Reconnaissance vocale Push-to-Talk | *À venir (v0.2.0)* | Enregistrement micro ES7210 à 16 kHz et dictée manuelle avec `listen()` et `stopListening()`. |
| **06 — Transcriptions** | Exploiter les transcriptions partielles et finales | *À venir (v0.3.0)* | Distinction en direct des transcriptions intermédiaires (interim) et de la transcription définitive stabilisée (`isFinal`). |
| **07 — Continuous STT** | Écoute continue et détection de silence (VAD) | *À venir (v0.3.0)* | Dictée mains libres automatique : déclenchement et segmentation automatique des phrases par détection d'activité vocale (VAD). |
| **08 — Conversation** | Conversation vocale bidirectionnelle | *À venir (v0.4.0)* | Dialogue complet en temps réel avec Gemini Live (Mode B) : interaction fluide où l'IA écoute, réfléchit et répond oralement. |
| **09 — Voice Actions** | Exécuter des actions à partir de la parole | *À venir (v0.4.0)* | Interprétation de commandes vocales simples pour piloter les fonctions du CoreS3 (couleur de l'écran, buzzer, actionneurs). |
| **10 — Multi-Provider** | Architecture multi-fournisseurs (Gemini & OpenAI) | *À venir (v0.5.0)* | Basculement transparent et sans refonte de code vers OpenAI Realtime API (`provider = VoiceProviderType::OPENAI`). |
| **11 — LVGL Interface** | Intégrer VoiceKit dans une interface tactile LVGL | *À venir (v0.6.0)* | Conception d'une interface graphique moderne avec écran tactile, visualiseur d'état et retours visuels animés. |

---

## 4. Tutoriel vs Exemples Techniques vs Applications Complètes

Pour vous repérer aisément dans le dépôt GitHub, il est essentiel de distinguer trois types de contenus :

1. **Le Tutoriel (`tutorials/`)** :
   - Parcours d'apprentissage progressif pour débutants.
   - Code le plus lisible et concis possible.
   - Axé sur la compréhension pédagogique des mécanismes de VoiceKit.

2. **Les Exemples Techniques (`examples/`)** :
   - Démonstrations techniques ciblées (ex. `MinimalTTS`, `MinimalSTT`, `ContinuousSTT`, `MinimalConversation`, `OpenAIConversation`, `VoiceDiagnostics`).
   - Destinés aux développeurs souhaitant intégrer rapidement un cas d'usage précis ou tester des paramètres avancés du moteur.

3. **Les Applications Indépendantes (`VoiceLab` et `VoiceCommands`)** :
   - Projets applicatifs complets autonomes hébergés dans leurs propres répertoires / dépôts.
   - **VoiceLab** : Laboratoire vocal graphique interactif complet sous LVGL (bancs de test TTS, STT, dictée continue, conversation, profils de voix).
   - **VoiceCommands** : Application d'automatisation domotique et de commande vocale par mots-clés.
   - Ces applications exploitent la puissance de VoiceKit mais ne font pas partie du tutoriel pour éviter toute surcharge cognitive.

---

## 5. Démarrer Immédiatement

Rendez-vous sur la première leçon : **[Leçon 00 — Préparer son environnement VoiceKit](00-Installation/README.md)**.
