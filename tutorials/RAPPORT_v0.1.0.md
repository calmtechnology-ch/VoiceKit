# Rapport de Livraison — VoiceKit Tutorial v0.1.0

- **Date :** 27 Octobre 2026
- **Version livrée :** VoiceKit Tutorial v0.1.0
- **Version de référence VoiceKit :** v0.6.0
- **Cible matérielle :** M5Stack CoreS3 (ESP32-S3 @ 240 MHz, 16 Mo Flash, 8 Mo PSRAM)
- **Environnement :** PlatformIO / Arduino Framework / C++17

---

## 1. Synthèse Exécutive

La livraison **VoiceKit Tutorial v0.1.0** inaugure le parcours officiel d'apprentissage progressif pour VoiceKit sur M5Stack CoreS3. Conçu spécifiquement pour des développeurs maîtrisant les bases d'Arduino et du C++ mais novices en programmation vocale embarquée et en intégration d'intelligences artificielles génératives temps réel, ce tutoriel applique strictement la règle directrice : **« la simplicité avant la sophistication »**.

Toutes les contraintes impératives ont été scrupuleusement honorées :
- **Intégrité absolue du cœur VoiceKit :** aucun fichier dans `src/` n'a été modifié.
- **Intégrité des exemples existants :** aucun fichier dans `examples/` n'a été altéré.
- **Isolation :** aucune interférence avec les projets applicatifs indépendants `VoiceLab` et `VoiceCommands`.
- **Façade haut niveau unifiée :** utilisation prioritaire de `VoiceKit.h` et `VoiceKitConfig.h`.
- **Autonomie des projets :** chaque leçon de programmation dispose de son propre projet PlatformIO complet, immédiatement compilable et sans code dupliqué (liaison via `symlink://../..`).
- **Sécurité :** exclusion systématique des fichiers d'identifiants personnels (`secrets.h`) via `.gitignore` et fourniture de modèles documentés (`secrets.example.h`).

---

## 2. Inventaire des Fichiers Livrés

| Fichier / Dossier | Rôle | Statut |
|---|---|---|
| [`tutorials/README.md`](README.md) | Page d'accueil officielle du tutoriel, philosophie, prérequis, distinction tutoriel/exemples/applications, sommaire exhaustif des 12 leçons (00 à 11). | **Créé** |
| [`tutorials/00-Installation/README.md`](00-Installation/README.md) | **Leçon 00** : Présentation du CoreS3, installation de VS Code et PlatformIO, obtention de la clé API Gemini Live, configuration Wi-Fi, explication de `platformio.ini`, moniteur série. | **Créé** |
| [`tutorials/01-HelloVoiceKit/`](01-HelloVoiceKit/) | **Leçon 01** : Initialisation de `VoiceKit`, gestion du Wi-Fi managé, callbacks `onReady`, `onError`, `onStateChanged`. Projet PlatformIO complet et autonome. | **Créé** |
| [`tutorials/01-HelloVoiceKit/platformio.ini`](01-HelloVoiceKit/platformio.ini) | Configuration de compilation PlatformIO pour CoreS3 avec partition `no_ota.csv`. | **Créé** |
| [`tutorials/01-HelloVoiceKit/no_ota.csv`](01-HelloVoiceKit/no_ota.csv) | Table de partitionnement Flash (app0 à 2 Mo). | **Créé** |
| [`tutorials/01-HelloVoiceKit/.gitignore`](01-HelloVoiceKit/.gitignore) | Exclusion des caches `.pio`, `.vscode` et de `secrets.h`. | **Créé** |
| [`tutorials/01-HelloVoiceKit/include/secrets.example.h`](01-HelloVoiceKit/include/secrets.example.h) | Gabarit de configuration des clés Wi-Fi et Gemini Live. | **Créé** |
| [`tutorials/01-HelloVoiceKit/src/main.cpp`](01-HelloVoiceKit/src/main.cpp) | Programme source commenté de la Leçon 01. | **Créé & Compilé** |
| [`tutorials/01-HelloVoiceKit/README.md`](01-HelloVoiceKit/README.md) | Guide pédagogique en 10 sections de la Leçon 01. | **Créé** |
| [`tutorials/02-TextToSpeech/`](02-TextToSpeech/) | **Leçon 02** : Restitution vocale asynchrone avec `say()`, gestion du haut-parleur AW88298, détection de début (`onSpeechStarted`) et fin (`onSpeechFinished`). | **Créé** |
| [`tutorials/02-TextToSpeech/platformio.ini`](02-TextToSpeech/platformio.ini) | Configuration de compilation PlatformIO pour la Leçon 02. | **Créé** |
| [`tutorials/02-TextToSpeech/no_ota.csv`](02-TextToSpeech/no_ota.csv) | Table de partitionnement Flash. | **Créé** |
| [`tutorials/02-TextToSpeech/.gitignore`](02-TextToSpeech/.gitignore) | Exclusion des secrets et caches. | **Créé** |
| [`tutorials/02-TextToSpeech/include/secrets.example.h`](02-TextToSpeech/include/secrets.example.h) | Gabarit de configuration. | **Créé** |
| [`tutorials/02-TextToSpeech/src/main.cpp`](02-TextToSpeech/src/main.cpp) | Programme source commenté de la Leçon 02. | **Créé & Compilé** |
| [`tutorials/02-TextToSpeech/README.md`](02-TextToSpeech/README.md) | Guide pédagogique en 10 sections de la Leçon 02. | **Créé** |
| [`tutorials/03-SpeechControl/`](03-SpeechControl/) | **Leçon 03** : Contrôle interactif de la parole, volume 0-100% (`setVolume`/`getVolume`), arrêt d'urgence immédiat (`stop`), consultation d'état et erreurs (`getState`/`getLastError`), interface série (`T`, `S`, `+`, `-`, `?`). | **Créé** |
| [`tutorials/03-SpeechControl/platformio.ini`](03-SpeechControl/platformio.ini) | Configuration de compilation PlatformIO pour la Leçon 03. | **Créé** |
| [`tutorials/03-SpeechControl/no_ota.csv`](03-SpeechControl/no_ota.csv) | Table de partitionnement Flash. | **Créé** |
| [`tutorials/03-SpeechControl/.gitignore`](03-SpeechControl/.gitignore) | Exclusion des secrets et caches. | **Créé** |
| [`tutorials/03-SpeechControl/include/secrets.example.h`](03-SpeechControl/include/secrets.example.h) | Gabarit de configuration. | **Créé** |
| [`tutorials/03-SpeechControl/src/main.cpp`](03-SpeechControl/src/main.cpp) | Programme source commenté de la Leçon 03. | **Créé & Compilé** |
| [`tutorials/03-SpeechControl/README.md`](03-SpeechControl/README.md) | Guide pédagogique en 10 sections de la Leçon 03. | **Créé** |
| [`README.md`](../README.md) | Ajout de la section « Apprendre VoiceKit — Tutoriel progressif » et référencement dans le sommaire principal. | **Mis à jour** |
| [`tutorials/RAPPORT_v0.1.0.md`](RAPPORT_v0.1.0.md) | Le présent rapport de livraison. | **Créé** |

---

## 3. Structure Pédagogique Normalisée

Chaque guide de leçon (`README.md`) respecte rigoureusement la structure pédagogique en 10 étapes :
1. **Objectif** : Description claire et concise de la compétence ou du comportement visé.
2. **Prérequis** : Matériel requis, leçons précédentes validées et identifiants nécessaires.
3. **Nouvelles notions** : Explication didactique et sans jargon des méthodes C++ introduites.
4. **Code complet** : Programme `main.cpp` intégralement reproduit et commenté en français.
5. **Explications** : Décomposition étape par étape du fonctionnement interne.
6. **Compilation et installation** : Commandes PlatformIO précises pour compiler et téléverser.
7. **Résultat attendu** : Transcription exacte des messages console série et du comportement sur le CoreS3.
8. **Exercice** : Atelier pratique simple pour consolider les acquis sans blocage.
9. **Problèmes fréquents** : Tableau de diagnostic des erreurs courantes et remèdes.
10. **Étape suivante** : Transition logique vers la leçon suivante.

---

## 4. Vérifications Techniques & Validations Réelles de Compilation

Chaque projet applicatif a fait l'objet d'une compilation réelle avec la chaîne d'outils officielle PlatformIO (`espressif32@6.9.0`, GCC C++17, FreeRTOS ESP-IDF / Arduino) :

| Projet | Commande de test | Résultat | Empreinte Flash | Empreinte RAM |
|---|---|---|---|---|
| `examples/MinimalVoiceKit` | `pio run -d examples/MinimalVoiceKit` | **SUCCESS** (19.31 s) | 73.5% (1.54 Mo) | 21.3% (69.7 Ko) |
| `tutorials/01-HelloVoiceKit` | `pio run -d tutorials/01-HelloVoiceKit` | **SUCCESS** (11.02 s) | 58.3% (1.22 Mo) | 21.3% (69.6 Ko) |
| `tutorials/02-TextToSpeech` | `pio run -d tutorials/02-TextToSpeech` | **SUCCESS** (104.45 s) | 58.4% (1.22 Mo) | 21.3% (69.6 Ko) |
| `tutorials/03-SpeechControl` | `pio run -d tutorials/03-SpeechControl` | **SUCCESS** (79.96 s) | 58.5% (1.23 Mo) | 21.3% (69.7 Ko) |

### Points de conformité vérifiés :
- Zéro avertissement de compilation (`-Wall`) lors de l'inclusion des en-têtes et des macros Wi-Fi.
- Respect strict des signatures des méthodes de `VoiceKit.h` :
  - `voice.begin(config)`
  - `voice.update()`
  - `voice.isReady()`, `voice.isBusy()`, `voice.isSpeaking()`
  - `voice.getState()`, `voice.getLastError()`
  - `voice.setVolume(percent)`, `voice.getVolume()`
  - `voice.say(text)`
  - `voice.stop()`
  - Callbacks `onReady`, `onError`, `onSpeechStarted`, `onSpeechFinished`, `onStateChanged`.
- Le volume est strictement encadré entre 0 % et 100 %.
- Les programmes restent totalement non bloquants (`delay(10)` court dans `loop()`, écoute série via `Serial.available()`).

---

## 5. Sommaire du Parcours Complet (12 Leçons)

| Leçon | Titre | Statut dans v0.1.0 | Objectif Pédagogique |
|---|---|---|---|
| **00** | Préparer son environnement VoiceKit | **Disponible** | Découverte du CoreS3, installation de VS Code / PlatformIO, Wi-Fi 2.4 GHz, clé API Gemini Live. |
| **01** | Mon premier programme VoiceKit | **Disponible** | Initialisation de `VoiceKit`, Wi-Fi managé, gestion des états et de la disponibilité. |
| **02** | Faire parler le CoreS3 | **Disponible** | Restitution vocale asynchrone (`say`), gestion des callbacks de parole. |
| **03** | Contrôler la synthèse vocale | **Disponible** | Pilotage interactif série, volume sonore 0-100 %, interruption immédiate (`stop`). |
| **04** | Personnaliser la voix et le style | *Prévu v0.2.0* | Sélection des voix Gemini (`Aoede`, `Puck`...), langue (`fr-FR`), styles et tonalités. |
| **05** | Reconnaissance vocale (STT) Push-to-Talk | *Prévu v0.2.0* | Dictée manuelle avec micro ES7210 16 kHz (`listen` / `stopListening`). |
| **06** | Exploiter les transcriptions | *Prévu v0.3.0* | Traitement des flux de texte, distinction direct (`interim`) vs final (`isFinal`). |
| **07** | Écoute continue et détection de silence (VAD) | *Prévu v0.3.0* | Dictée continue mains libres automatique (`startContinuousListening`). |
| **08** | Conversation vocale bidirectionnelle | *Prévu v0.4.0* | Dialogue vocal interactif temps réel avec Gemini Live (Mode B). |
| **09** | Exécuter des actions à partir de la voix | *Prévu v0.4.0* | Commandes vocales et pilotage des périphériques matériels du CoreS3. |
| **10** | Architecture multi-fournisseurs | *Prévu v0.5.0* | Basculement sans modification de code entre Gemini Live et OpenAI Realtime. |
| **11** | Créer une interface graphique tactile (LVGL) | *Prévu v0.6.0* | Intégration de VoiceKit au sein d'une interface graphique moderne avec écran tactile. |

---

## 6. Limitations de la Version v0.1.0

- **Périmètre ciblé :** La version v0.1.0 couvre intégralement le socle de base (Leçons 00 à 03). Les leçons 04 à 11 ne sont pas encore implémentées dans cette version afin d'éviter la prolifération de répertoires ou de fichiers vides.
- **Microphone et capture audio :** Le microphone n'est pas encore activé dans les leçons 00 à 03 pour concentrer l'apprentissage sur la connexion et la restitution sonore. Il sera abordé dans la Leçon 05.
- **Interface graphique tactile :** Les leçons 01 à 03 utilisent l'affichage graphique de base `M5Unified` (texte et formes simples). L'intégration du moteur graphique LVGL sera introduite dans la Leçon 11.

---

## 7. Prochaines Étapes (Feuille de Route v0.2.0)

Pour la prochaine version **v0.2.0** du tutoriel :
1. **Implémentation de la Leçon 04 — Voice Settings** :
   - Présentation de la méthode `voice.setVoiceProfile(...)`, `voice.setVoice(...)`, `voice.setStyle(...)`, `voice.setTone(...)`.
   - Comparaison auditive des voix officielles Google Gemini Live (`Aoede`, `Puck`, `Charon`, `Fenrir`, `Kore`).
2. **Implémentation de la Leçon 05 — Speech-to-Text (Push-to-Talk)** :
   - Activation du double micro ES7210 avec `cfg.internal_mic = true`.
   - Déclenchement de la capture avec `voice.listen()` et arrêt avec `voice.stopListening()`.
   - Affichage de la première transcription textuelle dans la console série.
