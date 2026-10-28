# Leçon 01 — Mon premier programme VoiceKit

Bienvenue dans la Leçon 01 ! Après avoir préparé votre environnement dans la Leçon 00, vous allez écrire et exécuter votre tout premier programme exploitant la bibliothèque **VoiceKit**.

---

## 1. Objectif

L'objectif de cette leçon est d'apprendre à :
- Initialiser le matériel M5Stack CoreS3 avec `M5Unified`.
- Configurer VoiceKit avec vos identifiants Wi-Fi et votre clé d'API Gemini Live via la structure `VoiceKitConfig`.
- Lancer le moteur vocal et établir une session sécurisée avec Google Gemini Live.
- Suivre les étapes de connexion et les changements d'état du moteur via le moniteur série.
- Réagir aux événements majeurs (`onReady`, `onError`, `onStateChanged`).

> **Remarque :** Cette leçon initialise le haut-parleur au volume par défaut de VoiceKit, car VoiceKit exige une sortie audio disponible. Le microphone reste désactivé. Nous nous concentrons d'abord sur la solidité de la liaison réseau et de la session IA.

---

## 2. Prérequis

- Avoir terminé la [Leçon 00 — Préparer son environnement VoiceKit](../00-Installation/README.md).
- Disposer d'une clé d'API Google Gemini Live valide.
- Disposer d'un point d'accès Wi-Fi 2.4 GHz à portée.
- Le câble USB-C reliant le CoreS3 à votre ordinateur.

---

## 3. Nouvelles Notions

Voici les concepts clés introduits dans cette leçon :

### L'instance principale `VoiceKit`
```cpp
static VoiceKit voice;
```
La classe `VoiceKit` est le point d'entrée unique de toutes vos interactions vocales. Elle gère pour vous la machine à états, les connexions réseau et les pilotes audio.

### La structure de configuration `VoiceKitConfig`
```cpp
VoiceKitConfig config;
config.apiKey = GEMINI_KEY;
config.wifiMode = WiFiMode::MANAGED_WIFI;
config.wifiSSID = WIFI_SSID;
config.wifiPassword = WIFI_PASSWORD;
```
Cette structure regroupe les paramètres indispensables :
- `apiKey` : votre clé secrète Google AI Studio.
- `wifiMode = WiFiMode::MANAGED_WIFI` : indique à VoiceKit de prendre en charge lui-même la connexion au réseau Wi-Fi ainsi que la synchronisation d'horloge SNTP (nécessaire pour valider les certificats SSL/TLS).
- `wifiSSID` et `wifiPassword` : vos identifiants réseau locaux.

### La méthode `voice.begin(config)`
Elle initialise les sous-systèmes, démarre la tâche réseau en arrière-plan et tente la première connexion au service Gemini Live. Elle renvoie `true` si le démarrage s'est bien déroulé, `false` sinon.

### La méthode obligatoire `voice.update()`
```cpp
void loop() {
    M5.update();
    voice.update();
    ...
}
```
Dans l'univers Arduino, la fonction `loop()` tourne en boucle permanente. `voice.update()` doit impérativement être appelée à chaque passage pour maintenir la communication WebSocket, traiter les paquets réseau reçus et déclencher vos fonctions de rappel (*callbacks*).

### La vérification de disponibilité `voice.isReady()` et `voice.getState()`
- `voice.isReady()` : renvoie `true` dès que la session Gemini Live est établie, authentifiée et prête à recevoir des ordres.
- `voice.getState()` : renvoie l'état précis du moteur parmi : `UNINITIALIZED`, `CONNECTING_WIFI`, `CONNECTING_SERVICE`, `READY`, `SPEAKING`, `ERROR_STATE`, etc.
- La fonction d'aide `voiceStateToString(state)` convertit cet état en texte lisible pour la console.

### Les fonctions de rappel (*callbacks*)
VoiceKit fonctionne de manière non bloquante et asynchrone : au lieu d'attendre passivement une réponse du cloud (ce qui figerait votre ESP32), vous lui confiez des fonctions à exécuter lorsque certains événements se produisent :
- `voice.onReady(callback)` : appelée quand VoiceKit est connecté et prêt.
- `voice.onError(callback)` : appelée dès qu'une erreur réseau ou d'authentification survient.
- `voice.onStateChanged(callback)` : appelée à chaque transition de l'état interne.

---

## 4. Code Complet : `src/main.cpp`

```cpp
/**
 * @file main.cpp
 * @brief Leçon 01 — Mon premier programme VoiceKit
 * 
 * Objectif : Initialiser la façade VoiceKit et établir une session sécurisée
 *            avec le service Google Gemini Live sur M5Stack CoreS3.
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceKit.h>
#include <WiFi.h>

// Chargement sécurisé des identifiants (Wi-Fi et clé API)
#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

#ifndef GEMINI_KEY
    #define GEMINI_KEY ""
#endif
#ifndef WIFI_PASSWORD
    #define WIFI_PASSWORD ""
#endif

// Déclaration de l'instance unique de VoiceKit
static VoiceKit voice;

// Mémorisation de l'état précédent pour n'afficher que les changements
static VoiceState s_lastState = VoiceState::UNINITIALIZED;

void setup() {
    // 1. Initialisation de la console série à 115200 bauds
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  Tutoriel VoiceKit — Leçon 01 : Hello VoiceKit        ");
    Serial.println("  Initialisation et connexion à Google Gemini Live      ");
    Serial.println("========================================================");

    // 2. Initialisation matérielle du M5Stack CoreS3
    // VoiceKit exige une sortie audio initialisée dès voice.begin().
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = false;
    M5.begin(cfg);

    // Affichage d'accueil sur l'écran du CoreS3
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("VoiceKit - Lecon 01", 10, 20);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Connexion en cours...", 10, 60);

    // Vérification de sécurité des identifiants
    if (String(GEMINI_KEY) == "VOTRE_CLE_GEMINI_LIVE" || String(GEMINI_KEY).isEmpty()) {
        Serial.println("\n[ATTENTION] Vous devez renseigner votre cle d'API Gemini Live !");
        Serial.println("Copiez include/secrets.example.h vers include/secrets.h et editez-le.");
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString("Cle API manquante !", 10, 90);
    }

    // 3. Configuration des paramètres de VoiceKit
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;

    // En mode MANAGED_WIFI, VoiceKit prend en charge lui-même la connexion Wi-Fi
    // et la synchronisation de l'horloge système (SNTP) requise par TLS.
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;

    // Fournisseur par défaut : Google Gemini Live
    config.provider = VoiceProviderType::GEMINI;

    // 4. Enregistrement des fonctions de rappel (callbacks)

    // Déclenché lorsque la session Gemini Live est établie et prête à l'emploi
    voice.onReady([]() {
        Serial.println("[VoiceKit] >>> Callback onReady : VoiceKit est PRET ! <<<");
        M5.Display.fillRect(0, 60, 320, 180, TFT_BLACK);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.drawString("PRET !", 10, 70);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
        M5.Display.drawString("Session Gemini Live active", 10, 110);
        M5.Display.drawString("IP : " + WiFi.localIP().toString(), 10, 130);
    });

    // Déclenché en cas d'erreur de réseau, d'authentification ou de protocole
    voice.onError([](const String& erreur) {
        Serial.printf("[VoiceKit] >>> Callback onError : %s <<<\n", erreur.c_str());
        M5.Display.fillRect(0, 60, 320, 180, TFT_BLACK);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.setTextSize(1);
        M5.Display.drawString("Erreur : " + erreur, 10, 70);
    });

    // Déclenché lors de chaque transition d'état interne
    voice.onStateChanged([](VoiceState state) {
        Serial.printf("[VoiceKit] Changement d'etat : %s\n", voiceStateToString(state));
    });

    // 5. Lancement de VoiceKit
    Serial.println("[VoiceKit] Demarrage du moteur vocal avec voice.begin()...");
    if (!voice.begin(config)) {
        Serial.printf("[VoiceKit][FATAL] Echec voice.begin() : %s\n", voice.getLastError().c_str());
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString("Echec initialisation", 10, 90);
    }
}

void loop() {
    // 1. Mise à jour des composants M5Stack
    M5.update();

    // 2. Traitement périodique obligatoire de VoiceKit
    voice.update();

    // 3. Suivi visuel des transitions d'état
    VoiceState currentState = voice.getState();
    if (currentState != s_lastState) {
        s_lastState = currentState;
        if (!voice.isReady() && currentState != VoiceState::ERROR_STATE) {
            M5.Display.fillRect(0, 60, 320, 30, TFT_BLACK);
            M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
            M5.Display.setTextSize(1);
            M5.Display.drawString("Etat : " + String(voiceStateToString(currentState)), 10, 60);
        }
    }

    delay(10);
}
```

---

## 5. Explications : Décomposition par Étapes

### Étape 1 : Création du fichier `include/secrets.h`
Avant de compiler, créez une copie locale du fichier `include/secrets.example.h` sous le nom `include/secrets.h` :
```cpp
#pragma once
#define WIFI_SSID     "MonSuperWifi"
#define WIFI_PASSWORD "MonMotDePasseSecret"
#define GEMINI_KEY    "AIzaSyD..."
```
Ce fichier est ignoré par Git, ce qui garantit que vos clés ne seront jamais publiées par erreur.

### Étape 2 : L'initialisation matérielle dans `setup()`
`M5.begin(cfg)` initialise l'horloge interne, l'écran et la gestion d'énergie. Nous passons `internal_spk = true` pour configurer la sortie audio requise par `voice.begin()`. Le volume par défaut de VoiceKit est conservé (65). Le microphone reste désactivé avec `internal_mic = false`. Désactiver aussi le haut-parleur empêche M5Unified de configurer ses broches et provoque `Echec initialisation materielle AudioOutput`.

### Étape 3 : La préparation des callbacks
Il est recommandé d'enregistrer vos fonctions de rappel **avant** d'appeler `voice.begin()`. Ainsi, dès que la connexion Wi-Fi s'établit ou que le serveur répond, votre code est prêt à intercepter l'événement.

### Étape 4 : L'appel de `voice.update()` dans `loop()`
La fonction `loop()` doit exécuter `voice.update()` en continu sans blocage. C'est elle qui donne du temps CPU à la machine réseau pour finaliser la négociation WebSocket avec Google Gemini Live.

---

## 6. Compilation et Installation

1. Ouvrez le dossier de la leçon dans VS Code (`tutorials/01-HelloVoiceKit`).
2. Ouvrez le terminal PlatformIO et lancez la compilation :
   ```bash
   pio run -d tutorials/01-HelloVoiceKit
   ```
3. Téléversez le binaire sur votre CoreS3 :
   ```bash
   pio run -d tutorials/01-HelloVoiceKit -t upload
   ```
4. Ouvrez le moniteur série à 115200 bauds :
   ```bash
   pio device monitor -b 115200
   ```

---

## 7. Résultat Attendu

Dans le moniteur série, vous observez la progression suivante :

```text
========================================================
  Tutoriel VoiceKit — Leçon 01 : Hello VoiceKit        
  Initialisation et connexion à Google Gemini Live      
========================================================
[VoiceKit] Demarrage du moteur vocal avec voice.begin()...
[VoiceKit] Changement d'etat : CONNEXION_WIFI
[VoiceKit] Changement d'etat : CONNEXION_SERVICE
[VoiceKit] >>> Callback onReady : VoiceKit est PRET ! <<<
[VoiceKit] Changement d'etat : PRET
```

Sur l'écran du M5Stack CoreS3 :
- Le bandeau affiche **"VoiceKit - Lecon 01"**.
- Dès la connexion réussie, un grand message vert **"PRET !"** apparaît avec votre adresse IP locale.

---

## 8. Exercice

1. Modifiez la fonction enregistrée dans `voice.onReady()` pour afficher dans la console série la vitesse du processeur avec `ESP.getCpuFreqMHz()`.
2. Testez le comportement en introduisant volontairement une erreur dans le mot de passe Wi-Fi dans `include/secrets.h`.
3. Observez quel état est affiché et comment VoiceKit signale la situation. N'oubliez pas de remettre le bon mot de passe ensuite !

---

## 9. Problèmes Fréquents

| Message / Symptôme | Cause | Solution |
|---|---|---|
| `[ATTENTION] Vous devez renseigner votre cle d'API` | Le fichier `secrets.h` n'existe pas ou contient les valeurs d'exemple | Créez `include/secrets.h` et renseignez votre vraie clé `AIza...`. |
| Bloqué sur `CONNEXION_WIFI` | Nom de réseau ou mot de passe incorrect, ou réseau 5 GHz | Vérifiez que votre réseau est bien en 2.4 GHz et que le mot de passe ne comporte pas d'erreur de saisie. |
| `onError: 400 Bad Request` ou `403 Forbidden` | Clé d'API invalide ou API Gemini non activée | Vérifiez votre clé sur Google AI Studio et assurez-vous qu'elle a accès au modèle `gemini-3.8-live` ou `gemini-2.0-flash-exp`. |
| Écran blanc ou sombre | Luminosité ou alimentation | Vérifiez que la batterie du CoreS3 est chargée ou qu'il est alimenté via son port USB-C. |

---

## 10. Étape Suivante

Bravo ! Votre CoreS3 est désormais connecté au cloud et la session sécurisée avec l'intelligence artificielle est opérationnelle.

Passez maintenant à la **[Leçon 02 — Text-to-Speech](../02-TextToSpeech/README.md)** pour activer le haut-parleur et faire prononcer à votre CoreS3 ses toutes premières paroles !
