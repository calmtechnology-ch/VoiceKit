# MinimalTTS_Arduino — VoiceKit v0.6.5.2

Exemple autonome de synthèse vocale pour **l'IDE Arduino** officiel sur carte **M5Stack CoreS3**.

---

## 1. Objectif

Permettre aux utilisateurs de l'environnement officiel Arduino IDE (v2.x) de compiler et d'exécuter un projet VoiceKit sans utiliser PlatformIO.
Il démontre l'initialisation du moteur et la synthèse vocale unitaire via l'esquisse `MinimalTTS_Arduino.ino`.

---

## 2. Fonctionnalités

- Compatibilité directe avec Arduino IDE 2.x et le package de cartes officiel `esp32` (Espressif).
- Initialisation matérielle CoreS3 avec les bibliothèques officielles `M5Unified` et `M5CoreS3`.
- Synthèse vocale PCM 24 kHz via l'API simplifiée `voice.say()`.
- Déclenchement automatique de la phrase de bienvenue à la connexion.
- Déclenchement tactile d'une synthèse par toucher de l'écran.
- Absence d'identifiants sensibles codés en dur par défaut (placeholders explicites).

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3.
- **Câble** : USB-C vers USB-A/C compatible données.
- **Environnement logiciel** : Arduino IDE 2.3+ avec le support des cartes ESP32 par Espressif.

---

## 4. Configuration

### 4.1 Configuration de l'IDE Arduino

Dans le menu **Outils** de l'IDE Arduino :
- **Type de carte** : `ESP32S3 Dev Module` ou `M5Stack-CoreS3`
- **PSRAM** : `OPI PSRAM` *(Critique : indispensable pour les tampons audio)*
- **Partition Scheme** : `Huge APP (3MB No OTA)` ou `16M Flash (3MB APP/9.9MB FATFS)`
- **Flash Size** : `16MB (128Mb)`
- **Upload Speed** : `460800` ou `921600`
- **Core Debug Level** : `None` ou `Info`

### 4.2 Bibliothèques à installer

Via le *Gestionnaire de bibliothèques* de l'IDE Arduino :
1. `M5Unified` (version >= 0.2.23)
2. `M5CoreS3` (version >= 1.0.0)
3. La bibliothèque `VoiceKit` (déposer le dossier racine dans le répertoire `libraries/` de votre sketchbook Arduino).

### 4.3 Renseignement des identifiants

Dans le fichier `MinimalTTS_Arduino.ino`, remplacer les constantes :
```cpp
static const char* WIFI_SSID     = "VOTRE_SSID_WIFI";
static const char* WIFI_PASSWORD = "VOTRE_MOT_DE_PASSE_WIFI";
static const char* GEMINI_KEY    = "VOTRE_CLE_API_GEMINI";
```

---

## 5. Compilation

Dans Arduino IDE :
1. Ouvrir le fichier `MinimalTTS_Arduino.ino`.
2. Cliquer sur le bouton **Vérifier** (icône coche) ou raccourci `Ctrl + R`.
3. Vérifier que la compilation s'achève sans erreur de mémoire ni symbole manquant.

---

## 6. Téléversement

1. Connecter le M5Stack CoreS3 en USB.
2. Sélectionner le port COM correspondant dans le menu déroulant de l'IDE.
3. Cliquer sur le bouton **Téléverser** (icône flèche) ou raccourci `Ctrl + U`.
4. Ouvrir le **Moniteur série** à `115200` baud.

---

## 7. Mode d'utilisation

1. L'écran affiche l'état d'avancement de la connexion.
2. Dès que le moteur est prêt, une première synthèse vocale est prononcée.
3. Toucher l'écran du CoreS3 pour déclencher une nouvelle annonce vocale.

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| 1 | Compilation IDE | Vérifier l'esquisse dans l'IDE | Succès de compilation avec détection de la PSRAM. |
| 2 | Connexion & Sync | Téléverser et observer la console | Connexion au point d'accès Wi-Fi et synchronisation SNTP réussie. |
| 3 | Synthèse initiale | Attendre la fin du setup | Restitution vocale audible de la phrase d'accueil. |
| 4 | Synthèse au toucher | Toucher l'écran tactile | Restitution de la phrase de confirmation. |

---

## 9. Résultats attendus

- Aucune erreur de mémoire `out of memory` (la PSRAM doit être correctement allouée).
- Son propre et intelligible via le haut-parleur intégré.
- Journal série structuré avec logs d'état clairs.

---

## 10. Limitations connues

- L'environnement Arduino IDE ne dispose pas de la gestion automatique des dépendances par projet de PlatformIO.
- Les identifiants sont renseignés dans le fichier principal `.ino` : veillez à ne pas commiter ce fichier avec vos identifiants réels.
- La compilation est plus lente sous Arduino IDE sans le cache d'objets optimisé de PlatformIO.

