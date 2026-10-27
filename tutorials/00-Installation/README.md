# Leçon 00 — Préparer son environnement VoiceKit

Bienvenue dans la première étape de votre découverte de VoiceKit ! Cette leçon a pour but de vous guider pas à pas dans l'installation des outils, la découverte du matériel M5Stack CoreS3 et la préparation de vos identifiants réseau et d'intelligence artificielle.

---

## 1. Objectif

À l'issue de cette leçon, vous aurez :
- Compris le rôle des composants audio du M5Stack CoreS3.
- Installé votre environnement de développement complet (VS Code + PlatformIO).
- Récupéré le code source de VoiceKit.
- Obtenu et configuré votre clé d'accès à l'API Google Gemini Live.
- Compris la structure d'un fichier de configuration `platformio.ini`.
- Vérifié le bon fonctionnement de la liaison série avec votre CoreS3.

Votre poste sera alors parfaitement prêt pour programmer votre premier code vocal dans la Leçon 01.

---

## 2. Prérequis

### Matériel
- **1× Boîtier M5Stack CoreS3** : l'ESP32-S3 haut de gamme de M5Stack.
- **1× Câble USB Type-C** supportant le transfert de données (attention aux câbles de charge purs sans fils de données).
- **1× Ordinateur** sous Windows, macOS ou Linux.
- **1× Réseau Wi-Fi 2.4 GHz** connecté à Internet (l'ESP32-S3 ne gère pas les bandes Wi-Fi 5 GHz).

### Connaissances
- Notions de base en programmation C/C++ ou Arduino (savoir ce que sont les fonctions `setup()` et `loop()`).
- Aucune connaissance préalable en intelligence artificielle ou en traitement du signal audio n'est requise.

---

## 3. Nouvelles Notions

### Le M5Stack CoreS3 : une station vocale complète
Le **M5Stack CoreS3** est un boîtier tout-en-un idéal pour la voix embarquée :
- **Microcontrôleur ESP32-S3** : processeur double cœur Cadence Xtensa LX7 cadencé à 240 MHz, avec 16 Mo de mémoire Flash et 8 Mo de PSRAM.
- **Double microphone numérique ES7210** : capture le son ambiant avec un échantillonnage haute précision en 16 kHz.
- **Amplificateur audio AW88298 & haut-parleur interne 1W** : restitue un son clair en 24 kHz haute fidélité.
- **Écran couleur tactile IPS 320×240 (2 pouces)** : affiche des informations textuelles ou graphiques.
- **Gestionnaire d'alimentation AXP2101** : régule les tensions et gère la batterie interne.

### VoiceKit : le pont entre l'ESP32 et l'IA
VoiceKit est une bibliothèque logicielle qui transforme votre CoreS3 en assistant vocal interactif. Elle s'occupe de toutes les tâches complexes :
- Connexion Wi-Fi et sécurisation des échanges via chiffrement TLS et certificats d'autorité officiels Google.
- Gestion du microphone et du haut-parleur via le contrôleur audio DMA (Direct Memory Access).
- Échange audio bidirectionnel en temps réel avec les moteurs d'IA dans le cloud.

### Les trois grands modes de la programmation vocale
Pour bien programmer, il faut distinguer trois concepts fondamentaux :
1. **TTS (Text-to-Speech / Synthèse vocale)** :
   Votre microcontrôleur envoie une phrase textuelle (par exemple *"Bonjour !"*). Le moteur dans le cloud la convertit en un flux sonore que le haut-parleur du CoreS3 joue immédiatement.
2. **STT (Speech-to-Text / Reconnaissance vocale)** :
   Vous parlez dans le micro du CoreS3. Le son est envoyé en streaming au moteur cloud, qui vous renvoie la transcription écrite exacte de vos paroles.
3. **Conversation vocale interactive (Mode dialogue bidirectionnel)** :
   Un échange naturel et continu en temps réel. Le système écoute vos questions, réfléchit et répond vocalement avec sa propre voix sans rupture de flux.

### À propos de la facturation des fournisseurs d'IA
VoiceKit s'appuie par défaut sur l'API **Google Gemini Live** (et propose également l'API OpenAI Realtime à partir de la version 0.6.0).
- Google AI Studio propose un niveau d'utilisation gratuit avec des quotas confortables pour le développement et l'apprentissage.
- Au-delà des quotas gratuits ou si vous activez un compte de facturation, les requêtes audio peuvent engendrer des frais.
- **Conseil pour débuter** : gardez votre clé d'API strictement confidentielle et surveillez votre consommation sur la console Google AI Studio.

---

## 4. Code Complet : Configuration de Référence

Une leçon PlatformIO repose sur un fichier `platformio.ini` qui indique au compilateur comment construire le projet pour le CoreS3, et un fichier de configuration des secrets.

### Fichier `platformio.ini`
```ini
[env:m5stack-cores3]
platform = espressif32@6.9.0
platform_packages = platformio/framework-arduinoespressif32@3.20017.241212+sha.dcc1105b
board = m5stack-cores3
framework = arduino
lib_ldf_mode = deep
board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
board_build.flash_mode = dio
monitor_speed = 115200
upload_speed = 460800
board_build.partitions = no_ota.csv
build_unflags = -std=gnu++11
build_flags =
    -std=gnu++17
    -DCORE_DEBUG_LEVEL=1
    -DARDUINO_RUNNING_CORE=1
    -DARDUINO_EVENT_RUNNING_CORE=1
    -DBOARD_M5STACK_CORES3=1
    -DVOXCORE_AUDIO_MODE=2
    -Iinclude
lib_deps = 
    m5stack/M5CoreS3@1.0.0
    m5stack/M5Unified@0.2.23
    symlink://../..
```

### Fichier d'exemple d'identifiants (`include/secrets.example.h`)
```cpp
#pragma once

// Identifiants de connexion Wi-Fi (réseau 2.4 GHz obligatoire)
#define WIFI_SSID     "VOTRE_WIFI_SSID"
#define WIFI_PASSWORD "VOTRE_WIFI_PASSWORD"

// Clé d'API Google Gemini Live (obtenue sur Google AI Studio)
#define GEMINI_KEY    "VOTRE_CLE_GEMINI_LIVE"
```

---

## 5. Explications : Préparation Étape par Étape

### Étape 1 : Installer Visual Studio Code et PlatformIO
1. Téléchargez et installez **[Visual Studio Code](https://code.visualstudio.com/)**.
2. Lancez VS Code, ouvrez l'onglet **Extensions** (icône avec 4 carrés sur le bandeau gauche ou raccourci `Ctrl+Shift+X`).
3. Recherchez **PlatformIO IDE** et cliquez sur **Install**.
4. Patientez pendant l'installation complète du moteur PlatformIO, puis redémarrez VS Code si demandé.

### Étape 2 : Récupérer le dépôt VoiceKit
Ouvrez un terminal ou utilisez un outil Git pour cloner le projet :
```bash
git clone https://github.com/Philippe-AD/VoiceKit.git
```
Puis, dans VS Code : **Fichier > Ouvrir le dossier...** et sélectionnez le dossier `VoiceKit`.

### Étape 3 : Obtenir une clé d'API Gemini Live
1. Rendez-vous sur **[Google AI Studio](https://aistudio.google.com/)**.
2. Connectez-vous avec votre compte Google.
3. Cliquez sur le bouton bleu **Get API key**.
4. Créez une nouvelle clé (Create API key in new project).
5. Copiez la clé générée (une chaîne de caractères débutant généralement par `AIza...`).
6. Conservez cette clé en lieu sûr : elle vous servira dès la Leçon 01.

### Étape 4 : Comprendre le fichier `platformio.ini`
Le fichier `platformio.ini` regroupe les paramètres essentiels :
- `board = m5stack-cores3` : indique les caractéristiques matérielles du CoreS3.
- `board_build.partitions = no_ota.csv` : alloue 2 Mo complets pour l'application en mémoire Flash, indispensable pour les bibliothèques réseau et TLS.
- `-std=gnu++17` : active le standard moderne C++17 utilisé par VoiceKit.
- `-DVOXCORE_AUDIO_MODE=2` : active la gestion de l'audio haute performance sur le deuxième cœur de l'ESP32.
- `lib_deps` : déclare les dépendances requises (`M5Unified` pour le matériel et `VoiceKit`).

---

## 6. Compilation et Installation

Pour tester la communication entre votre ordinateur et le CoreS3 :
1. Connectez le M5Stack CoreS3 à votre ordinateur avec le câble USB-C.
2. Une petite LED rouge ou verte doit s'allumer près du connecteur USB.
3. Dans la barre inférieure bleue de VS Code, PlatformIO détecte automatiquement le port série (ex. `COM3` ou `COM4` sous Windows, `/dev/ttyUSB0` sous Linux, `/dev/cu.usbmodem...` sous macOS).
4. Cliquez sur l'icône de prise électrique **PlatformIO: Serial Monitor** (ou tapez `pio device monitor` dans le terminal).
5. Assurez-vous que la vitesse est configurée sur **115200 bauds**.

---

## 7. Résultat Attendu

Lorsque vous allumez ou redémarrez le CoreS3 (appui bref sur le bouton Reset situé sous l'appareil), le moniteur série affiche les messages de démarrage du processeur ESP32-S3 :

```text
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x1 (POWERON),boot:0x2b (SPI_FAST_FLASH_BOOT)
SPIWP:0xee
mode:DIO, clock div:1
load:0x3fce3808,len:0x44c
...
```

Si ces lignes apparaissent, votre câble USB est de bonne qualité et la communication série fonctionne parfaitement !

---

## 8. Exercice

1. Ouvrez le terminal PlatformIO dans VS Code.
2. Tapez la commande suivante pour lister les cartes connectées :
   ```bash
   pio device list
   ```
3. Identifiez le nom du port série de votre M5Stack CoreS3 (par exemple `COM4`).
4. Notez ce port série : si nécessaire, vous pourrez l'indiquer explicitement dans le fichier `platformio.ini` avec la ligne `upload_port = COM4`.

---

## 9. Problèmes Fréquents

| Symptôme | Cause probable | Solution |
|---|---|---|
| Le CoreS3 n'est pas détecté par l'ordinateur | Câble USB de charge seule ou pilote manquant | Utilisez un câble USB de données certifié. Installez le pilote USB CH9102 ou CP210x si votre système d'exploitation ne le reconnaît pas d'origine. |
| Erreur `Error: Could not open port` | Le port est déjà utilisé par un autre logiciel | Fermez tout autre terminal série ouvert (Arduino IDE, PuTTY, etc.) avant de relancer PlatformIO. |
| Clé d'API refusée | Espace involontaire copié avec la clé | Veillez à ne copier aucun espace avant ou après la chaîne `AIza...`. |
| Échec de connexion Wi-Fi ultérieur | Réseau 5 GHz sélectionné | L'ESP32-S3 fonctionne exclusivement sur les réseaux **2.4 GHz**. Vérifiez que votre box ou routeur diffuse bien un SSID en 2.4 GHz. |

---

## 10. Étape Suivante

Félicitations, vos outils sont en place et votre matériel est prêt !

Rendez-vous dès maintenant dans la **[Leçon 01 — Mon premier programme VoiceKit](../01-HelloVoiceKit/README.md)** pour initialiser VoiceKit et établir votre première connexion sécurisée avec Gemini Live.
