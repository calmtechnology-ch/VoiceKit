# Leçon 02 — Faire parler le CoreS3 (Text-to-Speech)

Bienvenue dans la Leçon 02 ! Dans la leçon précédente, vous avez connecté votre CoreS3 à Google Gemini Live. Il est maintenant temps de lui donner de la voix grâce à la **synthèse vocale (Text-to-Speech / TTS)**.

---

## 1. Objectif

À la fin de cette leçon, vous saurez :
- Activer l'amplificateur audio et le haut-parleur interne du M5Stack CoreS3.
- Utiliser la fonction `voice.say("...")` pour convertir n'importe quel texte en parole.
- Comprendre la nature asynchrone des requêtes vocales.
- Détecter le début effectif de la parole avec `voice.onSpeechStarted()`.
- Détecter la fin exacte de la restitution sonore avec `voice.onSpeechFinished()`.
- Vérifier si le haut-parleur est en train d'émettre avec `voice.isSpeaking()`.

---

## 2. Prérequis

- Avoir validé la [Leçon 01 — Mon premier programme VoiceKit](../01-HelloVoiceKit/README.md).
- Disposer de votre fichier `include/secrets.h` avec vos identifiants Wi-Fi et votre clé d'API Google Gemini Live.
- Votre M5Stack CoreS3 connecté en USB à l'ordinateur.

---

## 3. Nouvelles Notions

### L'activation du haut-parleur dans `M5Unified`
Le M5Stack CoreS3 intègre un amplificateur audio numérique I2S de haute qualité (**AW88298**) relié à un haut-parleur interne. Pour l'activer, il suffit de configurer :
```cpp
auto cfg = M5.config();
cfg.internal_spk = true; // Active l'ampli AW88298 et la sortie I2S
M5.begin(cfg);
```

### La méthode `voice.say(texte)`
```cpp
bool accepte = voice.say("Bonjour, je suis VoiceKit !");
```
Cette méthode transmet le texte demandé à Gemini Live. L'intelligence artificielle génère en temps réel les échantillons audio PCM à 24 kHz (haute fidélité) et les envoie en streaming vers l'ESP32.
- Elle renvoie `true` si la demande a été acceptée par le moteur.
- Elle renvoie `false` si le moteur est actuellement indisponible ou déjà occupé.

### Pourquoi `voice.say()` ne doit JAMAIS être appelé en continu dans `loop()`
C'est une erreur classique du débutant :
```cpp
// ATTENTION : ERREUR GRAVE À NE JAMAIS FAIRE !
void loop() {
    voice.update();
    voice.say("Bonjour !"); // Va inonder le serveur et saturer l'ESP32 !
}
```
La fonction `loop()` s'exécute des milliers de fois par seconde. Si vous y appelez `voice.say()` sans condition, vous allez envoyer des milliers de requêtes consécutives au serveur, provoquant le blocage de la mémoire et l'épuisement immédiat de vos quotas d'API !
`voice.say()` doit être déclenché **une seule fois par action** (un événement, un clic sur l'écran, un message série ou un changement d'état).

### Synchrone vs Asynchrone : comprendre la différence
- **Opération synchrone (bloquante)** : le programme s'arrête et attend la fin de l'opération avant d'exécuter la ligne suivante (par exemple `delay(1000);`). Durant ce temps, votre microcontrôleur est totalement figé : il ne peut ni lire l'écran tactile, ni rafraîchir l'affichage.
- **Opération asynchrone (non bloquante)** : `voice.say()` envoie la requête et rend **immédiatement** la main à votre code. La transmission réseau et la lecture audio se déroulent en arrière-plan pendant que votre boucle `loop()` continue de tourner à pleine vitesse.

### Suivre la parole : `onSpeechStarted` et `onSpeechFinished`
Puisque l'opération est asynchrone, comment savoir quand le CoreS3 parle ?
Grâce aux deux callbacks dédiés :
- `voice.onSpeechStarted([]() { ... })` : déclenché au moment précis où le premier son sort du haut-parleur.
- `voice.onSpeechFinished([]() { ... })` : déclenché lorsque le dernier échantillon sonore a été intégralement joué et que le haut-parleur redevient silencieux.

Vous pouvez également interroger à tout instant :
```cpp
if (voice.isSpeaking()) {
    // Le haut-parleur est en cours d'émission
}
```

---

## 4. Code Complet : `src/main.cpp`

```cpp
/**
 * @file main.cpp
 * @brief Leçon 02 — Faire parler le CoreS3 (Text-to-Speech / TTS)
 * 
 * Objectif : Découvrir la synthèse vocale avec VoiceKit sur M5Stack CoreS3.
 *            Activer le haut-parleur interne, prononcer une phrase d'accueil
 *            dès que le moteur est prêt, et suivre les événements de parole.
 */

#include <Arduino.h>
#include <M5Unified.h>

// Chargement sécurisé des identifiants (Wi-Fi et clé API)
#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

#include <VoiceKit.h>
#include <WiFi.h>

#ifndef GEMINI_KEY
    #define GEMINI_KEY ""
#endif
#ifndef WIFI_PASSWORD
    #define WIFI_PASSWORD ""
#endif

// Instance unique de VoiceKit
static VoiceKit voice;

// Indicateur pour ne prononcer la phrase d'accueil qu'une seule fois
static bool s_welcomeSpoken = false;

// Phrase de test initiale
static const char* FIRST_PHRASE = "Bonjour, je suis VoiceKit !";

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  Tutoriel VoiceKit — Leçon 02 : Text-to-Speech (TTS)  ");
    Serial.println("  Faire parler le CoreS3 avec Google Gemini Live       ");
    Serial.println("========================================================");

    // 1. Initialisation matérielle avec activation du haut-parleur interne
    auto cfg = M5.config();
    cfg.internal_spk = true;   // Active l'amplificateur AW88298 et le haut-parleur
    cfg.internal_mic = false;  // Le microphone n'est pas encore utilisé dans cette leçon
    M5.begin(cfg);

    // Affichage d'accueil sur l'écran du CoreS3
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("VoiceKit - Lecon 02", 10, 15);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Synthese Vocale (TTS)", 10, 45);
    M5.Display.drawString("Connexion en cours...", 10, 75);

    // 2. Configuration de VoiceKit
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.provider = VoiceProviderType::GEMINI;

    // Réglage du volume sonore initial (entre 0 et 100 %)
    config.volume = 70;

    // Voix et langue française
    config.voice = "Aoede";
    config.language = "fr-FR";

    // 3. Enregistrement des callbacks

    // Callback onReady : déclenché dès que la session est prête
    voice.onReady([]() {
        Serial.println("[VoiceKit] Moteur connecte et pret !");
        M5.Display.fillRect(0, 75, 320, 165, TFT_BLACK);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.drawString("PRET !", 10, 80);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
        M5.Display.drawString("Generation de la parole en cours...", 10, 120);
    });

    // Callback onSpeechStarted : le son commence à sortir du haut-parleur
    voice.onSpeechStarted([]() {
        Serial.println("[VoiceKit] >>> Callback onSpeechStarted : Restitution vocale active !");
        M5.Display.fillRect(0, 115, 320, 60, TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setTextSize(1);
        M5.Display.drawString("Parole en cours (haut-parleur)...", 10, 120);
    });

    // Callback onSpeechFinished : la phrase est totalement terminée
    voice.onSpeechFinished([]() {
        Serial.println("[VoiceKit] >>> Callback onSpeechFinished : Restitution vocale terminee !");
        M5.Display.fillRect(0, 115, 320, 60, TFT_BLACK);
        M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5.Display.setTextSize(1);
        M5.Display.drawString("Parole terminee. Touchez l'ecran pour rejouer.", 10, 120);
    });

    // Callback onError : en cas d'incident
    voice.onError([](const String& err) {
        Serial.printf("[VoiceKit] Erreur : %s\n", err.c_str());
        M5.Display.fillRect(0, 75, 320, 165, TFT_BLACK);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString("Erreur : " + err, 10, 80);
    });

    // 4. Démarrage de VoiceKit
    Serial.println("[VoiceKit] Initialisation du moteur vocal...");
    if (!voice.begin(config)) {
        Serial.printf("[VoiceKit][FATAL] Echec voice.begin() : %s\n", voice.getLastError().c_str());
    }
}

void loop() {
    M5.update();
    voice.update();

    // 1. Premier test : prononcer la phrase d'accueil dès que VoiceKit est prêt
    if (!s_welcomeSpoken && voice.isReady() && !voice.isSpeaking()) {
        s_welcomeSpoken = true;
        Serial.printf("[VoiceKit] Envoi de la premiere phrase : \"%s\"\n", FIRST_PHRASE);
        
        bool ok = voice.say(FIRST_PHRASE);
        if (!ok) {
            Serial.println("[VoiceKit] La requete say() a ete refusee (moteur occupe).");
        }
    }

    // 2. Déclenchement manuel : touchez l'écran pour rejouer la phrase
    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        if (voice.isReady() && !voice.isSpeaking()) {
            Serial.println("[Touch] Ecran touche -> Rejeu de la phrase TTS");
            voice.say(FIRST_PHRASE);
        } else if (voice.isSpeaking()) {
            Serial.println("[Touch] Ecran touche pendant la parole -> Le moteur est occupe !");
        } else {
            Serial.println("[Touch] Ecran touche mais le moteur n'est pas encore pret.");
        }
    }

    delay(10);
}
```

---

## 5. Explications : Décomposition par Étapes

### Étape 1 : Le haut-parleur interne
Dans `setup()`, nous demandons explicitement à `M5Unified` de démarrer l'amplificateur interne avec `cfg.internal_spk = true;`. Sans cette ligne, aucun son ne pourrait sortir du boîtier, même si le code s'exécute correctement.

### Étape 2 : Le paramètre `config.volume = 70;`
La structure `VoiceKitConfig` permet de définir le niveau sonore au démarrage sur une échelle de **0 à 100 %**. Un volume de 65 à 75 % est idéal sur le CoreS3 pour obtenir une voix claire et audible sans distorsion.

### Étape 3 : Le drapeau `s_welcomeSpoken`
Dans `loop()`, nous utilisons un booléen `s_welcomeSpoken` initialisé à `false`. Dès que le moteur passe à l'état prêt (`voice.isReady()`), nous passons ce drapeau à `true` et envoyons la phrase avec `voice.say()`. Cela garantit que la phrase d'accueil n'est envoyée qu'une seule et unique fois.

### Étape 4 : Le déclenchement au toucher
Grâce à `M5.Touch.getDetail()`, si l'utilisateur tapote l'écran du CoreS3, nous testons à nouveau si le moteur est disponible avec `voice.isReady() && !voice.isSpeaking()`. Si oui, la phrase est rejouée immédiatement !

---

## 6. Compilation et Installation

1. Ouvrez le terminal PlatformIO.
2. Compilez la leçon :
   ```bash
   pio run -d tutorials/02-TextToSpeech
   ```
3. Téléversez sur le CoreS3 :
   ```bash
   pio run -d tutorials/02-TextToSpeech -t upload
   ```
4. Lancez le moniteur série :
   ```bash
   pio device monitor -b 115200
   ```

---

## 7. Résultat Attendu

Quelques secondes après la mise sous tension :
1. La console affiche la connexion Wi-Fi puis `Moteur connecte et pret !`.
2. Le programme envoie la phrase : `[VoiceKit] Envoi de la premiere phrase : "Bonjour, je suis VoiceKit !"`.
3. Le callback `onSpeechStarted` s'active : le CoreS3 prononce distinctement la phrase à voix haute avec un timbre naturel en français.
4. Le callback `onSpeechFinished` signale la fin de la parole.
5. Si vous touchez l'écran du CoreS3, la phrase est prononcée à nouveau !

---

## 8. Exercice

Modifiez le programme pour prononcer **trois phrases successives** à la suite de manière ordonnée :
- Phrase 1 : *"Bonjour et bienvenue dans la leçon deux."*
- Phrase 2 : *"VoiceKit prend en charge la synthèse vocale en français."*
- Phrase 3 : *"Test terminé avec succès !"*

**Règle essentielle :** n'envoyez jamais la phrase suivante tant que la précédente n'est pas achevée.
*Indice :* Déclarez un tableau de textes et un indice `s_index = 0`. Dans le callback `onSpeechFinished()`, incrémentez l'indice et appelez `voice.say(...)` pour la phrase suivante s'il en reste à prononcer !

---

## 9. Problèmes Fréquents

| Problème | Cause | Solution |
|---|---|---|
| Le texte s'affiche sur la console mais aucun son ne sort | Haut-parleur désactivé ou volume à 0 | Vérifiez que `cfg.internal_spk = true;` est présent dans `setup()` et que `config.volume` est bien réglé (ex: 70). |
| `voice.say()` renvoie `false` | Moteur non prêt ou déjà en train de parler | Vérifiez toujours `voice.isReady() && !voice.isSpeaking()` avant d'appeler `say()`. |
| Son haché ou grésillant | Connexion Wi-Fi instable | Assurez-vous d'une bonne réception Wi-Fi 2.4 GHz. VoiceKit utilise un tampon circulaire en PSRAM de ~5 secondes pour absorber les variations réseau, mais une perte prolongée de paquets peut interrompre le flux. |

---

## 10. Étape Suivante

Vous savez maintenant faire parler votre CoreS3 à la demande !

Rendez-vous dans la **[Leçon 03 — Speech Control](../03-SpeechControl/README.md)** pour apprendre à piloter la parole en temps réel : régler le volume sonore, stopper immédiatement une phrase en cours et interroger l'état du système depuis le moniteur série.
