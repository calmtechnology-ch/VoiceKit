# VoiceKit v0.6.6.2 — Rapport de Validation de Compilation & Intégration Externe
**Date :** 14 novembre 2026  
**Auteur :** Antigravity / Senior Software Engineer  
**Version évaluée :** v0.6.6.2  
**Outil de compilation :** PlatformIO Core 6.2.0 / ESP-IDF / Arduino Framework `espressif32@6.9.0` (ESP32-S3)  
**Compilateur :** `xtensa-esp32s3-elf-g++` 8.4.0 (C++17 `-std=gnu++17`)  
**Matériel cible :** M5Stack CoreS3 (ESP32-S3, 16 Mo Flash, 8 Mo PSRAM)

---

## 1. Synthèse Exécutive

La validation de compilation a porté sur l'intégralité des **11 exemples officiels** du dépôt ainsi que sur un projet de test isolé reproduisant l'installation de VoiceKit en tant que bibliothèque externe par un développeur tiers.

- **Exemples PlatformIO testés :** 10/10
- **Taux de succès PlatformIO :** **100 % (10/10 PASS)**
- **Exemple Arduino IDE (.ino) :** 1 (`MinimalTTS_Arduino`) annoté **NOT TESTED (CLI)** conformément aux contraintes (Arduino CLI non configuré dans l'environnement hôte).
- **Projet externe isolé :** **PASS (100 %)**. Compilation et génération du binaire `firmware.bin` validées.

---

## 2. Matrice Complète de Compilation des 11 Exemples Officiels

| # | Exemple | Type | Statut | Durée | Flash utilisée | RAM utilisée | Partition requise |
|---|---|---|---|---|---|---|---|
| 1 | `MinimalTTS` | PlatformIO | **PASS** | 15.56 s | 80.9 % (1 696 261 / 2 097 152 o) | 25.4 % (83 204 / 327 680 o) | `no_ota.csv` (2 Mo) |
| 2 | `MinimalSTT` | PlatformIO | **PASS** | 15.38 s | 81.0 % (1 698 685 / 2 097 152 o) | 25.4 % (83 256 / 327 680 o) | `no_ota.csv` (2 Mo) |
| 3 | `MinimalConversation` | PlatformIO | **PASS** | 15.16 s | 95.8 % (2 008 485 / 2 097 152 o) | 25.4 % (83 272 / 327 680 o) | `no_ota.csv` (2 Mo) |
| 4 | `MinimalTTS_Arduino` | Arduino (.ino) | **NOT TESTED** | — | — | — | Standard Arduino IDE |
| 5 | `WakeWordFoundation` | PlatformIO | **PASS** | 17.19 s | 82.1 % (1 722 113 / 2 097 152 o) | 25.7 % (84 320 / 327 680 o) | `no_ota.csv` (2 Mo) |
| 6 | `WakeWordConversation` | PlatformIO | **PASS** | 21.43 s | 81.7 % (1 713 369 / 2 097 152 o) | 25.7 % (84 320 / 327 680 o) | `no_ota.csv` (2 Mo) |
| 7 | `LocalToolRegistryDemo` | PlatformIO | **PASS** | 15.23 s | 26.5 % (555 793 / 2 097 152 o) | 7.1 % (23 232 / 327 680 o) | `no_ota.csv` (2 Mo) |
| 8 | `VoiceDiagnostics` | PlatformIO | **PASS** | 20.01 s | 82.1 % (1 722 245 / 2 097 152 o) | 25.9 % (84 932 / 327 680 o) | `no_ota.csv` (2 Mo) |
| 9 | `VoiceSettingsDemo` | PlatformIO | **PASS** | 43.81 s | 24.3 % (2 038 373 / 8 388 608 o) | 49.7 % (162 996 / 327 680 o) | `partitions_16MB.csv` (8 Mo) |
| 10 | `OpenAIConversationLVGL` | PlatformIO | **PASS** | 38.59 s | 97.6 % (2 047 229 / 2 097 152 o) | 40.0 % (131 228 / 327 680 o) | `no_ota.csv` (2 Mo) |
| 11 | `IntegratedVoiceDemo` | PlatformIO | **PASS** | 21.17 s | 25.5 % (2 140 913 / 8 388 608 o) | 40.2 % (131 712 / 327 680 o) | `partitions_16MB.csv` (8 Mo) |

### Observations clés sur les métriques :
1. **Occupation Flash critique sur `OpenAIConversationLVGL` (97.6 %) et `MinimalConversation` (95.8 %)** lorsque compilés avec la table `no_ota.csv` (partition app de 2 Mo). Pour des projets applicatifs combinant LVGL 9 et plusieurs fournisseurs, l'utilisation de `partitions_16MB.csv` (8 Mo d'application) est fortement recommandée pour éviter tout risque de dépassement.
2. **Utilisation RAM interne / Heap :** L'empreinte statique oscille entre 23 Ko (pour les outils locaux hors ligne) et 163 Ko (pour les interfaces graphiques tactiles LVGL 9 avec polices UTF-8 complètes), laissant entre 50 % et 75 % de RAM interne disponible avant l'allocation dynamique en PSRAM (8 Mo).

---

## 3. Validation d'Installation dans un Projet Externe Isolé

Pour vérifier que VoiceKit n'a aucune dépendance cachée envers son propre monorepo (chemins absolus ou `symlink://../..`), un projet PlatformIO vierge et indépendant a été généré dans un répertoire temporaire isolé :

### 3.1 Procédure de test
1. Création d'une arborescence PlatformIO standard :
   ```text
   test_project/
   ├── platformio.ini
   ├── no_ota.csv
   ├── src/main.cpp
   └── lib/VoiceKit/  (archive de distribution complète)
   ```
2. Configuration `platformio.ini` consommateur :
   ```ini
   [env:m5stack-cores3]
   platform = espressif32@6.9.0
   board = m5stack-cores3
   framework = arduino
   lib_ldf_mode = deep
   board_build.partitions = no_ota.csv
   build_unflags = -std=gnu++11
   build_flags =
       -std=gnu++17
       -DCORE_DEBUG_LEVEL=1
       -DBOARD_M5STACK_CORES3=1
       -DVOXCORE_AUDIO_MODE=2
       -Iinclude
   lib_deps =
       m5stack/M5CoreS3@1.0.0
       m5stack/M5Unified@0.2.23
   ```
3. Programme de test `main.cpp` :
   - Inclusion directe de `<VoiceKit.h>`.
   - Instanciation statique de `VoiceKit`.
   - Initialisation avec `M5Unified` et appel `voice.begin()`.

### 3.2 Résultat du test d'intégration externe
- **Résolution des dépendances :** Succès (M5CoreS3, M5Unified, M5GFX résolus et installés automatiquement).
- **Compilation de VoiceKit :** Succès. Le script `extra_script.py` inclus dans le paquet a correctement ajouté le chemin d'inclusion ESP-SR et la bibliothèque binaire `libesp_sr_wakenet_s3.a`.
- **Édition de liens (`firmware.elf`) :** Succès.
- **Génération du binaire (`firmware.bin`) :** **SUCCESS en 52.69 s** (Flash: 82.4 %, RAM: 25.8 %).

---

## 4. Prérequis et Directives pour les Développeurs Tiers

Pour intégrer VoiceKit v0.6.6.2 avec succès dans un projet tiers, les exigences suivantes doivent être respectées dans le `platformio.ini` utilisateur :

1. **Standard C++17 obligatoire :**
   ```ini
   build_unflags = -std=gnu++11
   build_flags = -std=gnu++17
   ```
2. **Mode audio FreeRTOS recommandé :**
   ```ini
   build_flags = ... -DVOXCORE_AUDIO_MODE=2
   ```
3. **Partition Flash adaptée :**
   - Pour les exemples simples sans LVGL : une table de partition allouant au moins 2 Mo d'application (`no_ota.csv` ou `huge_app.csv`).
   - Pour les applications avec interface graphique tactile LVGL 9 : une table allouant au moins 3 à 8 Mo d'application (comme `partitions_16MB.csv`).
