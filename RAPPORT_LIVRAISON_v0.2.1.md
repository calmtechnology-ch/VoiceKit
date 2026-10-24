# Rapport de Livraison — VoiceKit v0.2.1

> **Date** : 24 Octobre 2026  
> **Version** : v0.2.1  
> **Type de version** : Conditionnement, installation et compatibilité bibliothèque Arduino / PlatformIO  
> **Dépôt** : `https://github.com/Philippe-AD/VoiceKit`  
> **Commit de référence v0.2.0 (gel)** : `4670a27527d01064ffcacfb4d7f34638445438b7`  
> **Commit de livraison v0.2.1** : `713525fe2b78586c071d0e513813c015b6d91f24`  
> **Cible matérielle** : M5Stack CoreS3 (ESP32-S3FN8 / 8 MB PSRAM / Port COM4)  

---

## 1. Synthèse Exécutive

La version **VoiceKit v0.2.1** transforme VoiceKit v0.2.0 en une **bibliothèque Arduino et PlatformIO installable directement depuis GitHub**, sans exiger aucune copie locale de fichiers, aucun chemin d'en-tête forcé (`-I../../include`), ni aucune manipulation de répertoires (`lib_extra_dirs`).

### Réalisations Clés :
1. **Conformité stricte aux standards de bibliothèques Arduino 1.5+ et PlatformIO Registry** :
   - Organisation complète des fichiers sources et en-têtes dans le dossier `src/` (`src/VoxCoreVoice.h`, `src/VoiceKitConfig.h`, `src/core/`, `src/hal/`).
   - Mise en place d'en-têtes relais (`#pragma once`) garantissant la résolution de tous les en-têtes publics et internes, avec zéro impact sur l'empreinte mémoire et zéro conflit de redéfinition.
2. **Manifestes officiels complets** :
   - `library.json` : Nom unifié `"VoiceKit"`, version `0.2.1`, plateforme `espressif32`, framework `arduino`, dépendances `m5stack/M5CoreS3@1.0.0` et `m5stack/M5Unified@0.2.23`, options de compilation `-std=gnu++17` et `-DVOXCORE_AUDIO_MODE=2`.
   - `library.properties` : Version `0.2.1`, architectures `esp32`, dépendances déclarées.
   - `LICENSE` : Ajout formel de la licence MIT.
3. **Validation d'un projet externe indépendant (`tests/external-consumer`)** :
   - Installation réussie de VoiceKit directement depuis GitHub via `lib_deps = https://github.com/Philippe-AD/VoiceKit.git#713525f`.
   - Résolution automatique des dépendances (`M5Unified`, `M5CoreS3`, `M5GFX`).
   - Compilation et édition de liens réussies sans aucun drapeau d'inclusion personnalisé.
4. **Zéro Régression du Moteur Audio & Réseau** :
   - Les fichiers critiques du moteur VoxCore (`GeminiLiveClient.cpp`, `LiveAudioPlayer.cpp`, `LiveSessionController.cpp`, `WiFiManager.cpp`, `AudioTraceLog.cpp`, `M5AudioOutput.cpp` et `GoogleRoots.h`) présentent des **empreintes SHA-256 rigoureusement identiques** à la version stabilisée v0.2.0.
   - Les deux exemples historiques (`MinimalTTS` et `OrientationClockVoice`) compilent avec un succès total.

---

## 2. Preuve de Non-Régression (Empreintes Cryptographiques SHA-256)

Pour attester qu'aucun changement fonctionnel n'a été introduit dans le transport WebSocket TLS, les certificats Google PKI, le décodage audio, la gestion FreeRTOS ou le lecteur DMA, le tableau suivant confronte les empreintes SHA-256 entre le commit de référence v0.2.0 (`4670a27`) et la livraison v0.2.1 (`713525f`) :

| Composant | Fichier source | Empreinte SHA-256 v0.2.0 (`4670a27`) | Empreinte SHA-256 v0.2.1 (`713525f`) | Statut |
| :--- | :--- | :--- | :--- | :---: |
| Client WebSocket TLS | `src/core/GeminiLiveClient.cpp` | `67840b069e82e43fe01744ce941d7b501acb78c582456679b1bff0de4dbf2f84` | `67840b069e82e43fe01744ce941d7b501acb78c582456679b1bff0de4dbf2f84` | **IDENTIQUE** |
| Lecteur Audio DMA | `src/core/LiveAudioPlayer.cpp` | `ad37ee304790dd288ce12f7826095bebc0b7e70e1d3d32756fb8b7f3b2362d85` | `ad37ee304790dd288ce12f7826095bebc0b7e70e1d3d32756fb8b7f3b2362d85` | **IDENTIQUE** |
| Contrôleur de Session | `src/core/LiveSessionController.cpp` | `c3ff8fa0bb447d5c4e544f4f8737aad15cba31e8b9a137e06645f94461402aa7` | `c3ff8fa0bb447d5c4e544f4f8737aad15cba31e8b9a137e06645f94461402aa7` | **IDENTIQUE** |
| Gestionnaire Wi-Fi / SNTP | `src/core/WiFiManager.cpp` | `1c77c893b03db7dc0f1161d580dc9cbde274f0628ba33ff88e73c4ac19efb574` | `1c77c893b03db7dc0f1161d580dc9cbde274f0628ba33ff88e73c4ac19efb574` | **IDENTIQUE** |
| Journaliseur Temps Réel | `src/core/AudioTraceLog.cpp` | `5a03a20af3769e5e6d7dc3192d77571be61f89a4f8a63e012d69d2bc84749515` | `5a03a20af3769e5e6d7dc3192d77571be61f89a4f8a63e012d69d2bc84749515` | **IDENTIQUE** |
| HAL M5.Speaker | `src/hal/M5AudioOutput.cpp` | `5a7e1eca64a6639fa104d98b8fce9112d5c7c52cd4097cfeeafab1682834db02` | `5a7e1eca64a6639fa104d98b8fce9112d5c7c52cd4097cfeeafab1682834db02` | **IDENTIQUE** |
| Certificats Google Trust Services | `src/core/GoogleRoots.h` | `03139270d421f26da9de461905bf8167bcf9ad77bf719a6df50e28349113ad2d` | `03139270d421f26da9de461905bf8167bcf9ad77bf719a6df50e28349113ad2d` | **IDENTIQUE** |

> **Conclusion** : Le moteur vocal stabilisé VoxCore est préservé à 100% au niveau binaire.

---

## 3. Résultats Réels de Compilation

Toutes les cibles ont été compilées avec succès sur la plateforme `espressif32@6.9.0` (ESP32-S3FN8 @ 240 MHz) :

| Projet | Commande de compilation | Statut | Durée | Utilisation RAM | Utilisation Flash | Mode d'installation de VoiceKit |
| :--- | :--- | :---: | :---: | :---: | :---: | :--- |
| **`MinimalTTS`** | `pio run -d examples/MinimalTTS` | **SUCCESS** | 4.61 s | 58 688 octets (17.9%) | 1 154 265 octets (55.0%) | `symlink://../..` (sans `-I../../include`) |
| **`OrientationClockVoice`** | `pio run -d examples/OrientationClockVoice` | **SUCCESS** | 9.60 s | 150 568 octets (45.9%) | 1 579 881 octets (75.3%) | `symlink://../..` (sans `-I../../include`) |
| **`external-consumer`** | `pio run -d tests/external-consumer` | **SUCCESS** | 4.83 s | 58 700 octets (17.9%) | 1 150 761 octets (54.9%) | **GitHub** (`git+https://github.com/...#713525f`) |

---

## 4. Test d'Installation Externe (`tests/external-consumer`)

Le projet `tests/external-consumer` valide de bout en bout l'expérience utilisateur cible :

```ini
[env:m5stack-cores3]
platform = espressif32@6.9.0
board = m5stack-cores3
framework = arduino
lib_ldf_mode = deep
board_build.partitions = no_ota.csv
build_flags =
    -std=gnu++17
    -DVOXCORE_AUDIO_MODE=2
lib_deps =
    m5stack/M5CoreS3@1.0.0
    m5stack/M5Unified@0.2.23
    https://github.com/Philippe-AD/VoiceKit.git#713525f
```

### Journal d'Exécution PlatformIO :
```text
Library Manager: Installing git+https://github.com/Philippe-AD/VoiceKit.git#713525f
Cloning into 'C:\Users\phall\.platformio\.cache\tmp\pkg-installing-134sf5ov'...
HEAD is now at 713525f feat: prepare VoiceKit v0.2.1 installable library structure
Library Manager: VoiceKit@0.2.1+sha.713525f has been installed!
Library Manager: Resolving dependencies...
Scanning dependencies...
Dependency Graph
|-- M5CoreS3 @ 1.0.0
|-- M5Unified @ 0.2.23
|-- VoiceKit @ 0.2.1+sha.713525f
Building in release mode
Compiling .pio\build\m5stack-cores3\src\main.cpp.o
Compiling .pio\build\m5stack-cores3\lib637\VoiceKit\VoxCoreVoice.cpp.o
Compiling .pio\build\m5stack-cores3\lib637\VoiceKit\core\AudioTraceLog.cpp.o
Compiling .pio\build\m5stack-cores3\lib637\VoiceKit\core\GeminiLiveClient.cpp.o
Compiling .pio\build\m5stack-cores3\lib637\VoiceKit\core\LiveAudioPlayer.cpp.o
Compiling .pio\build\m5stack-cores3\lib637\VoiceKit\core\LiveSessionController.cpp.o
Compiling .pio\build\m5stack-cores3\lib637\VoiceKit\core\WiFiManager.cpp.o
Compiling .pio\build\m5stack-cores3\lib637\VoiceKit\hal\M5AudioOutput.cpp.o
Archiving .pio\build\m5stack-cores3\lib637\libVoiceKit.a
Linking .pio\build\m5stack-cores3\firmware.elf
Checking size .pio\build\m5stack-cores3\firmware.elf
RAM:   [==        ]  17.9% (used 58700 bytes from 327680 bytes)
Flash: [=====     ]  54.9% (used 1150761 bytes from 2097152 bytes)
Building .pio\build\m5stack-cores3\firmware.bin
Merged 2 ELF sections
Successfully created esp32s3 image.
========================= [SUCCESS] =========================
```

---

## 5. Compatibilité et Limitations Arduino IDE

La bibliothèque VoiceKit respecte la spécification officielle **Arduino Library Specification rev 2.3** :
- Répertoire `src/` contenant les sources et en-têtes récursifs.
- Fichier `library.properties` conforme.
- Exemple officiel autonome `.ino` : `examples/MinimalTTS_Arduino/MinimalTTS_Arduino.ino`.

### Limitations Techniques Spécifiques à l'Arduino IDE :
1. **Allocation PSRAM Obligatoire** :
   Le tampon circulaire de VoiceKit alloue environ 240 Ko en PSRAM via `MALLOC_CAP_SPIRAM`.
   Dans l'Arduino IDE, l'utilisateur **doit impérativement activer la PSRAM** :
   - Menu : `Outils -> PSRAM -> OPI PSRAM`.
   - Si la PSRAM est désactivée (`Disabled`), l'appel à `voice.begin()` échouera avec le message série : `"Echec initialisation memoire LiveAudioPlayer"`.
2. **Schéma de Partition Flash** :
   L'image binaire totale comprenant M5Unified, mbedTLS et VoiceKit occupe ~1,15 Mo. La partition d'usine par défaut (`Default 4MB with spiffs` avec 1,2 Mo d'APP) laisse une marge étroite. Il est recommandé de sélectionner :
   - `Outils -> Partition Scheme -> Huge APP (3MB No OTA)` ou `16M Flash (3MB APP/9.9MB FATFS)`.
3. **Dépendances de bibliothèques tierces** :
   L'installateur ZIP d'Arduino IDE n'installe pas automatiquement les bibliothèques dépendantes. L'utilisateur doit installer manuellement via le Gestionnaire de bibliothèques :
   - `M5Unified` (version >= 0.2.23)
   - `M5CoreS3` (version >= 1.0.0)

---

## 6. Procédure pour la Création du Tag Git v0.2.1

Conformément à la directive :
> *« Ne pas créer automatiquement le tag v0.2.1 avant validation. Le tag ne sera créé qu'après confirmation du fonctionnement de la bibliothèque dans un projet externe. »*

La validation dans le projet externe `tests/external-consumer` étant pleinement confirmée avec succès, la création du tag peut être réalisée via les commandes suivantes :

```bash
# 1. Positionner le tag annoté v0.2.1 sur le commit validé
git tag -a v0.2.1 -m "VoiceKit v0.2.1 — Bibliothèque Arduino et PlatformIO installable"

# 2. Pousser le tag vers GitHub
git push origin v0.2.1
```

Dès la publication du tag, l'utilisateur pourra configurer son `platformio.ini` directement avec :
```ini
lib_deps = https://github.com/Philippe-AD/VoiceKit.git#v0.2.1
```

---

## 7. Inventaire des Livrables

| Livrable | Chemin | Description |
| :--- | :--- | :--- |
| **Manifeste PlatformIO** | `library.json` | Configuration v0.2.1, dépendances, flags |
| **Manifeste Arduino** | `library.properties` | Métadonnées de bibliothèque Arduino IDE 1.5+ |
| **Licence** | `LICENSE` | Licence MIT officielle |
| **En-tête public** | `src/VoxCoreVoice.h` | API unique autonome |
| **Config publique** | `src/VoiceKitConfig.h` | Structure de configuration et énumérations |
| **Façade publique** | `src/VoxCoreVoice.cpp` | Implémentation de la classe principale |
| **En-têtes relais** | `src/*.h` & `include/*.h` | Relais `#pragma once` évitant toute collision |
| **Moteur VoxCore** | `src/core/*` & `src/hal/*` | Composants internes certifiés conformes SHA-256 |
| **Exemple MinimalTTS** | `examples/MinimalTTS/` | Exemple PlatformIO nettoyé de tout chemin forcé |
| **Exemple Horloge** | `examples/OrientationClockVoice/` | Exemple LVGL 9 nettoyé de tout chemin forcé |
| **Exemple Arduino IDE** | `examples/MinimalTTS_Arduino/` | Croquis `.ino` autonome avec guide de menus |
| **Projet Externe** | `tests/external-consumer/` | Projet de test autonome téléchargeant depuis GitHub |
| **Documentation** | `README.md` | Guide complet avec 3 méthodes d'installation (A, B, C) |
| **Changelog** | `CHANGELOG.md` | Historique détaillé de la version v0.2.1 |
| **Rapport de Livraison** | `RAPPORT_LIVRAISON_v0.2.1.md` | Ce document |
