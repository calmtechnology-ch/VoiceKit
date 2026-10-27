# Leçon 03 — Contrôler la synthèse vocale (Speech Control)

Bienvenue dans la Leçon 03 ! Dans la leçon précédente, votre CoreS3 a prononcé ses premiers mots en synthèse vocale. Mais dans une véritable application, vous devez pouvoir **contrôler** ce comportement en cours d'exécution : monter ou baisser le volume, arrêter immédiatement la voix en cas d'urgence, et interroger l'état interne du système.

---

## 1. Objectif

À l'issue de cette leçon, vous saurez :
- Régler le volume sonore de 0 à 100 % avec `voice.setVolume()` et lire le volume avec `voice.getVolume()`.
- Interrompre instantanément la parole en cours avec `voice.stop()`.
- Vérifier si le système est occupé avec `voice.isBusy()`.
- Obtenir le dernier message d'erreur avec `voice.getLastError()`.
- Créer une interface interactive non bloquante par commandes sur le port série (`T`, `S`, `+`, `-`, `?`).

---

## 2. Prérequis

- Avoir validé la [Leçon 02 — Text-to-Speech](../02-TextToSpeech/README.md).
- Votre CoreS3 relié à l'ordinateur par USB-C.
- Le moniteur série ouvert à 115200 bauds.

---

## 3. Nouvelles Notions

### Le réglage dynamique du volume sonore
```cpp
voice.setVolume(80);      // Règle le volume à 80 %
uint8_t vol = voice.getVolume(); // Retourne le volume actuel (0..100)
```
VoiceKit applique ce réglage directement sur l'étage de traitement audio et sur l'amplificateur interne du CoreS3.
- L'échelle est comprise strictement entre **0 % (silence)** et **100 % (volume maximal)**.
- Le réglage est immédiat, y compris pendant que le CoreS3 est en train de parler.

### L'arrêt d'urgence : `voice.stop()`
```cpp
voice.stop();
```
Si le CoreS3 prononce une longue phrase et que l'utilisateur souhaite le faire taire immédiatement, `voice.stop()` vide instantanément les tampons audio en mémoire, stoppe l'émission I2S et réinitialise la session.

### L'état d'occupation : `voice.isBusy()`
`voice.isBusy()` renvoie `true` si le système est en train de traiter une requête, de recevoir des paquets audio ou d'émettre du son sur le haut-parleur. Cela permet de savoir si une nouvelle commande peut être acceptée.

### Le diagnostic d'erreur : `voice.getLastError()`
En cas d'échec ou d'incident réseau, `voice.getLastError()` retourne une chaîne de caractères (`String`) détaillant la cause précise du problème (ex: clé API invalide, déconnexion WebSocket, etc.).

### L'écoute non bloquante du port série
Pour piloter le microcontrôleur depuis le clavier de votre ordinateur, nous utilisons l'objet standard `Serial` :
```cpp
if (Serial.available() > 0) {
    char cmd = Serial.read();
    // Traitement de la commande sans bloquer loop()
}
```
Cette lecture est non bloquante : si aucune touche n'a été pressée, `loop()` continue son exécution normale sans jamais figer le système.

---

## 4. Code Complet : `src/main.cpp`

```cpp
/**
 * @file main.cpp
 * @brief Leçon 03 — Contrôler la synthèse vocale (Speech Control)
 * 
 * Objectif : Piloter VoiceKit en direct via le moniteur série :
 *            - Déclencher la parole à la demande (T)
 *            - Interrompre la parole immédiatement (S)
 *            - Régler le volume sonore de 0 à 100 % (+ / -)
 *            - Consulter l'état détaillé du système (?)
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

// Phrase de test
static const char* SPEECH_TEXT = "VoiceKit repond parfaitement aux commandes serie. Vous pouvez regler le volume ou interrompre la parole a tout moment.";

// Fonction d'aide pour rafraîchir l'affichage sur l'écran du CoreS3
static void updateScreen(const String& actionMsg = "") {
    M5.Display.fillRect(0, 45, 320, 195, TFT_BLACK);
    
    // État du moteur
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("Etat : ", 10, 50);
    
    uint16_t stateColor = voice.isReady() ? TFT_GREEN : TFT_ORANGE;
    if (voice.isSpeaking()) stateColor = TFT_CYAN;
    M5.Display.setTextColor(stateColor, TFT_BLACK);
    M5.Display.drawString(voiceStateToString(voice.getState()), 60, 50);

    // Volume sonore
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("Volume : ", 10, 75);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString(String(voice.getVolume()) + " %", 80, 75);

    // Aide mémoire des commandes
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("Commandes serie (115200 bauds) :", 10, 110);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("[T] Parler   [S] Stop", 10, 130);
    M5.Display.drawString("[+] Vol +    [-] Vol -", 10, 150);
    M5.Display.drawString("[?] Statut systeme", 10, 170);

    // Dernier message d'action
    if (!actionMsg.isEmpty()) {
        M5.Display.setTextColor(TFT_MAGENTA, TFT_BLACK);
        M5.Display.drawString(actionMsg, 10, 205);
    }
}

// Affichage du menu d'aide dans la console série
static void printHelp() {
    Serial.println("\n--------------------------------------------------------");
    Serial.println("  Commandes disponibles dans le moniteur serie :        ");
    Serial.println("  T : Declencher la phrase de test                      ");
    Serial.println("  S : Arreter immediatement la parole (STOP)            ");
    Serial.println("  + : Augmenter le volume (+10 %)                       ");
    Serial.println("  - : Diminuer le volume (-10 %)                        ");
    Serial.println("  ? : Afficher l'etat complet du systeme                ");
    Serial.println("--------------------------------------------------------");
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  Tutoriel VoiceKit — Leçon 03 : Speech Control        ");
    Serial.println("  Controle interactif de la parole via le port serie    ");
    Serial.println("========================================================");

    // 1. Initialisation matérielle avec haut-parleur interne
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = false;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("VoiceKit - Lecon 03", 10, 15);
    updateScreen("Connexion en cours...");

    // 2. Configuration VoiceKit
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.provider = VoiceProviderType::GEMINI;
    config.volume = 70; // Volume initial 70%
    config.voice = "Aoede";
    config.language = "fr-FR";

    // 3. Callbacks
    voice.onReady([]() {
        Serial.println("\n[VoiceKit] >>> Moteur vocal PRET ! <<<");
        printHelp();
        updateScreen("Moteur pret !");
    });

    voice.onSpeechStarted([]() {
        Serial.println("[VoiceKit] >>> Parole en cours...");
        updateScreen("Parole en cours");
    });

    voice.onSpeechFinished([]() {
        Serial.println("[VoiceKit] >>> Fin de parole.");
        updateScreen("Fin de parole");
    });

    voice.onError([](const String& err) {
        Serial.printf("[VoiceKit] Erreur signalee : %s\n", err.c_str());
        updateScreen("Erreur : " + err);
    });

    // 4. Initialisation
    Serial.println("[VoiceKit] Demarrage...");
    if (!voice.begin(config)) {
        Serial.printf("[VoiceKit][FATAL] Echec begin : %s\n", voice.getLastError().c_str());
        updateScreen("Echec initialisation");
    }
}

void loop() {
    M5.update();
    voice.update();

    // Gestion non bloquante des commandes saisies sur la console série
    if (Serial.available() > 0) {
        char cmd = Serial.read();

        // Ignorer les retours chariot et sauts de ligne
        if (cmd == '\r' || cmd == '\n' || cmd == ' ') {
            return;
        }

        switch (cmd) {
            case 'T':
            case 't': {
                // Déclencher la parole
                if (!voice.isReady()) {
                    Serial.println("[Commande T] Impossible : VoiceKit n'est pas encore pret.");
                    updateScreen("Refus : Non pret");
                } else if (voice.isSpeaking() || voice.isBusy()) {
                    Serial.println("[Commande T] Impossible : VoiceKit est deja occupe.");
                    updateScreen("Refus : Moteur occupe");
                } else {
                    Serial.printf("[Commande T] Lancement : \"%s\"\n", SPEECH_TEXT);
                    bool ok = voice.say(SPEECH_TEXT);
                    if (ok) {
                        updateScreen("Requete TTS envoyee");
                    } else {
                        Serial.println("[Commande T] Requete refusee par le moteur.");
                    }
                }
                break;
            }

            case 'S':
            case 's': {
                // Arrêt immédiat de la parole
                Serial.println("[Commande S] Interruption immediate demandee (STOP) !");
                voice.stop();
                updateScreen("Interruption STOP");
                break;
            }

            case '+': {
                // Augmenter le volume (+10 %, max 100 %)
                uint8_t vol = voice.getVolume();
                if (vol <= 90) {
                    vol += 10;
                } else {
                    vol = 100;
                }
                voice.setVolume(vol);
                Serial.printf("[Commande +] Volume augmente a : %u %%\n", vol);
                updateScreen("Volume : " + String(vol) + " %");
                break;
            }

            case '-': {
                // Diminuer le volume (-10 %, min 0 %)
                uint8_t vol = voice.getVolume();
                if (vol >= 10) {
                    vol -= 10;
                } else {
                    vol = 0;
                }
                voice.setVolume(vol);
                Serial.printf("[Commande -] Volume diminue a : %u %%\n", vol);
                updateScreen("Volume : " + String(vol) + " %");
                break;
            }

            case '?': {
                // Diagnostic complet
                Serial.println("\n===== DIAGNOSTIC DU SYSTEME =====");
                Serial.printf("  Etat actuel       : %s\n", voiceStateToString(voice.getState()));
                Serial.printf("  Pret (isReady)    : %s\n", voice.isReady() ? "OUI" : "NON");
                Serial.printf("  Occupe (isBusy)   : %s\n", voice.isBusy() ? "OUI" : "NON");
                Serial.printf("  Parle (isSpeaking): %s\n", voice.isSpeaking() ? "OUI" : "NON");
                Serial.printf("  Volume actuel     : %u %%\n", voice.getVolume());
                Serial.printf("  Derniere erreur   : %s\n", voice.getLastError().isEmpty() ? "Aucune" : voice.getLastError().c_str());
                Serial.printf("  Adresse IP        : %s\n", WiFi.localIP().toString().c_str());
                Serial.println("=================================");
                updateScreen("Diagnostic affiche");
                break;
            }

            default:
                Serial.printf("[Commande] Caractere '%c' non reconnu. Tapez ? pour l'aide.\n", cmd);
                break;
        }
    }

    delay(10);
}
```

---

## 5. Explications : Décomposition par Étapes

### Étape 1 : Le menu d'accueil dans le terminal
Dès que la session Gemini Live est établie, le callback `onReady` affiche automatiquement la liste des touches utilisables dans la console série.

### Étape 2 : L'instruction `switch (cmd)`
Grâce à une structure `switch`, chaque caractère saisi au clavier déclenche une action précise :
- `T` vérifie si le moteur est libre (`!voice.isSpeaking() && !voice.isBusy()`) avant de lancer `voice.say()`.
- `S` invoque `voice.stop()`. La coupure du son est quasi-instantanée (moins de 20 millisecondes).
- `+` et `-` ajustent la variable de volume par pas de 10 %, en veillant à ne jamais descendre en dessous de 0 ni dépasser 100 %.
- `?` imprime un tableau de bord complet avec l'état en clair.

### Étape 3 : L'affichage miroir sur l'écran
La fonction `updateScreen()` permet d'observer en direct le niveau sonore et l'état opérationnel sur l'écran LCD du CoreS3 sans dépendre du terminal.

---

## 6. Compilation et Installation

1. Ouvrez le terminal PlatformIO.
2. Compilez la leçon 03 :
   ```bash
   pio run -d tutorials/03-SpeechControl
   ```
3. Téléversez sur le CoreS3 :
   ```bash
   pio run -d tutorials/03-SpeechControl -t upload
   ```
4. Ouvrez le moniteur série à 115200 bauds :
   ```bash
   pio device monitor -b 115200
   ```

---

## 7. Résultat Attendu

Dès que VoiceKit est prêt, la console affiche :
```text
[VoiceKit] >>> Moteur vocal PRET ! <<<

--------------------------------------------------------
  Commandes disponibles dans le moniteur serie :        
  T : Declencher la phrase de test                      
  S : Arreter immediatement la parole (STOP)            
  + : Augmenter le volume (+10 %)                       
  - : Diminuer le volume (-10 %)                        
  ? : Afficher l'etat complet du systeme                
--------------------------------------------------------
```

### Scénario de test :
1. Tapez `T` puis Entrée : le CoreS3 commence à lire la phrase.
2. Pendant qu'il parle, tapez immédiatement `S` puis Entrée : la parole est coupée net sans retard.
3. Tapez `+` plusieurs fois pour monter le volume jusqu'à 100 %.
4. Tapez `?` : vous observez `Volume actuel : 100 %` et `Etat : PRET`.
5. Tapez `T` à nouveau : la phrase est maintenant jouée à plein volume !

---

## 8. Exercice

Ajoutez une **deuxième phrase** accessible avec la touche `2` du clavier :
- Phrase 2 : *"Le bouton d'arret d'urgence a ete teste avec succes !"*

*Marche à suivre :*
1. Ajoutez une nouvelle constante `static const char* SECOND_PHRASE = "...";` en haut du fichier `main.cpp`.
2. Dans le bloc `switch (cmd)`, ajoutez un `case '2':` similaire au `case 'T':`, mais en appelant `voice.say(SECOND_PHRASE)`.
3. Mettez à jour la fonction `printHelp()` pour mentionner la commande `2`.
4. Compilez et testez en alternant les touches `T` et `2` !

---

## 9. Problèmes Fréquents

| Problème | Cause | Solution |
|---|---|---|
| Les caractères tapés n'ont aucun effet | Mauvais débit ou port série non connecté | Assurez-vous que le moniteur série est à 115200 bauds et que le champ d'envoi de votre terminal est actif. |
| Le volume ne change pas au-delà de 100 | Protection de borne atteinte | C'est le comportement normal : le volume de VoiceKit est borné entre 0 et 100 %. |
| Message `Impossible : VoiceKit est deja occupe` | Vous avez appuyé sur `T` pendant que la phrase précédente parlait encore | Attendez que le callback `Fin de parole` apparaisse ou appuyez d'abord sur `S` pour couper la parole en cours. |

---

## 10. Étape Suivante

Vous maîtrisez désormais les bases fondamentales de VoiceKit :
1. Connexion et configuration réseau (Leçon 01).
2. Restitution de synthèse vocale asynchrone (Leçon 02).
3. Pilotage interactif du volume, de l'état et de l'interruption STOP (Leçon 03).

La prochaine version du tutoriel introduira la **Leçon 04 — Voice Settings** pour apprendre à changer de voix (Aoede, Puck, Charon...), passer d'un style naturel à un style professionnel ou expressif, et explorer les tonalités de voix de Google Gemini Live.
