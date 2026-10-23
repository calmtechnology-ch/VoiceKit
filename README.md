# VoxCore VoiceKit v0.1.0

Bibliothèque C++/Arduino indépendante de **synthèse vocale (Text-To-Speech / TTS)** haute fidélité pour **M5Stack CoreS3** (ESP32-S3) exploitant le moteur **Gemini Live** via WebSocket bidirectionnel TLS.

---

## 1. Philosophie & Principes Fondamentaux

**VoxCore VoiceKit** a été conçu pour permettre d'ajouter la parole naturelle à n'importe quelle application CoreS3 existante (horloges, tableaux de bord, stations de capteurs, domotique) en quelques lignes de code, sans devoir comprendre ni importer toute l'architecture de VoxCore.

### Principes directeurs de la v0.1.0 :
1. **Composant invité strict** : VoiceKit ne prend jamais le contrôle de l'écran, ne crée aucune interface graphique, ne modifie pas la rotation de l'affichage, ne perturbe ni l'IMU, ni le RTC, ni les tâches applicatives de l'hôte.
2. **Zéro couplage avec LVGL** : Aucune dépendance à LVGL. VoiceKit fonctionne aussi bien avec LVGL 8, LVGL 9, M5GFX brut ou en mode headless (sans affichage).
3. **API publique simplifiée et asynchrone** : L'appel `voice.say("Bonjour")` est non-bloquant. La boucle principale `loop()` reste instantanément réactive à chaque milliseconde.
4. **Sanctuaire audio éprouvé** : Réutilisation intégrale du pipeline audio stabilisé en **VoxCore v0.2.1.3.6** (Mode 2 `DEDICATED_AUDIO_TASK` FreeRTOS, tampon PSRAM 120 000 échantillons, quadruple buffer DMA 160 ms, régulation douce sans perte, attente du vidage matériel DMA).
5. **Garantie de non-coupure** : Le callback `onSpeechFinished` n'est déclenché qu'après émission physique réelle des derniers échantillons par le haut-parleur. Zéro mot tronqué en fin de phrase.

---

## 2. Architecture de la Bibliothèque

```text
VoxCore-VoiceKit/
├── library.json                 # Manifeste PlatformIO
├── library.properties           # Manifeste Arduino IDE
├── README.md                    # Documentation complète
├── RAPPORT_LIVRAISON_v0.1.0.md  # Rapport de livraison et matrice de conformité
├── include/
│   ├── VoxCoreVoice.h           # API publique simplifiée
│   └── VoiceKitConfig.h         # Structures de configuration et énumérations
├── src/
│   ├── VoxCoreVoice.cpp         # Orchestrateur d'état de la bibliothèque
│   ├── audio/
│   │   ├── LiveAudioPlayer.h    # Restitution PCM 24 kHz Mode 2 FreeRTOS
│   │   ├── LiveAudioPlayer.cpp  # Tampon PSRAM 120 000 ech, DMA 4x40ms
│   │   ├── AudioTraceLog.h      # Journalisation circulaire POD sans allocation
│   │   └── AudioTraceLog.cpp
│   ├── gemini/
│   │   ├── GeminiLiveClient.h   # Client WebSocket TLS Gemini Live
│   │   ├── GeminiLiveClient.cpp # Décodage PCM 24 kHz, turns JSON, interruptions
│   │   ├── ILiveTransport.h     # Interface d'abstraction transport
│   │   └── GoogleRoots.h        # Certificats racines Google Trust Services
│   └── hal/
│       ├── IAudioOutput.h       # Abstraction matérielle haut-parleur
│       ├── M5AudioOutput.h      # Adaptateur M5Unified Speaker pour CoreS3
│       └── M5AudioOutput.cpp
├── examples/
│   ├── MinimalTTS/              # Exemple PlatformIO minimal autonome
│   └── OrientationClockVoice/   # Intégration complète avec CoreS3 Orientation Clock
└── tests/
    ├── test_voicekit_all.cpp    # Suite de tests logiques automatisés C++17
    └── run_tests.bat            # Script d'exécution locale des tests
```

---

## 3. Guide de Démarrage Rapide

### 3.1. Code Minimal

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>

VoxCoreVoice voice;

void setup() {
    // 1. Initialisation matérielle normale de votre application
    M5.begin();

    // 2. Configuration vocale
    VoiceKitConfig config;
    config.apiKey = "VOTRE_CLE_API_GEMINI";
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = "MonWiFi";
    config.wifiPassword = "MonMotDePasse";
    config.volume = 70;

    // Callbacks optionnels
    voice.onReady([]() {
        Serial.println("Moteur vocal pret !");
        voice.say("Bonjour, le service vocal est initialise.");
    });

    voice.onSpeechFinished([]() {
        Serial.println("Parole terminee jusqu'au dernier son !");
    });

    // 3. Demarrage
    voice.begin(config);
}

void loop() {
    M5.update();

    // 4. Mise a jour periodique non-bloquante
    voice.update();

    // Declenchement sur appui tactile
    if (M5.Touch.getDetail().wasClicked()) {
        voice.say("Vous venez de toucher l'ecran.");
    }

    delay(5);
}
```

---

## 4. Référence de l'API Publique

### Méthodes de `VoxCoreVoice`

| Méthode | Rôle | Comportement Bloquant |
| :--- | :--- | :---: |
| `bool begin(const VoiceKitConfig& config)` | Initialise le sous-système audio, le client réseau et la file d'attente. | Non (< 10 ms) |
| `bool say(const String& text)` | Demande la prononciation mot à mot du texte. Accepte ou rejette selon la politique de file. | **Non bloquant** |
| `void update()` | Traitement périodique (maintien WebSocket, décodage, cadencement DMA). À placer dans `loop()`. | **Non bloquant** (< 1 ms) |
| `void stop()` | Arrête immédiatement la parole, coupe le son et vide les files. | Non |
| `bool isSpeaking() const` | Indique si des échantillons audio sont en cours d'émission par le haut-parleur. | Non |
| `bool isReady() const` | Indique si le service est prêt à accepter une nouvelle phrase immédiatement. | Non |
| `bool isBusy() const` | Indique si le moteur génère du son ou parle actuellement. | Non |
| `VoiceState getState() const` | Retourne l'état courant : `IDLE`, `CONNECTING_SERVICE`, `READY`, `GENERATING`, `SPEAKING`, `ERROR_STATE`. | Non |
| `String getLastError() const` | Retourne la dernière erreur textuelle survenue. | Non |
| `void setVolume(uint8_t percent)` | Règle le volume sonore de 0 à 100 %. | Non |
| `uint8_t getVolume() const` | Retourne le volume configuré. | Non |

### Callbacks Applicatifs

```cpp
voice.onReady([]() { /* Appelé dès que la session Gemini est établie */ });
voice.onSpeechStarted([]() { /* Appelé dès l'émission du premier échantillon sonore */ });
voice.onSpeechFinished([]() { /* Appelé après le vidage DMA complet des derniers échantillons */ });
voice.onError([](const String& err) { /* Signalement des anomalies réseau ou Gemini */ });
voice.onStateChanged([](VoiceState st) { /* Suivi des transitions d'état */ });
```

---

## 5. Gestion Réseau & Politiques de File

### 5.1. Modes Wi-Fi (`WiFiMode`)
- `WiFiMode::MANAGED_WIFI` : VoiceKit gère lui-même sa connexion au point d'accès Wi-Fi avec reconnexion automatique.
- `WiFiMode::EXTERNAL_WIFI` : L'application hôte conserve l'entière propriété de sa pile Wi-Fi. VoiceKit attend passivement qu'une connexion utilisable (`WiFi.status() == WL_CONNECTED`) et qu'une heure système valide pour TLS soient disponibles.

### 5.2. Économie d'Énergie (`connectOnDemand`)
Pour les objets sur batterie :
```cpp
config.connectOnDemand = true;       // N'établit la session WebSocket TLS que sur say()
config.autoDisconnectIdleMs = 15000; // Se déconnecte automatiquement après 15 s de silence
```

### 5.3. Politiques de Concurrence (`QueuePolicy`)
Si `voice.say()` est appelé alors que le moteur est déjà en train de parler :
- `QueuePolicy::REJECT_IF_BUSY` (défaut) : La demande est immédiatement refusée (retourne `false`, `getLastError() == "Moteur vocal occupe"`).
- `QueuePolicy::QUEUE_FIFO` : La phrase est mise en attente dans une file bornée (`config.maxQueueSize`) et sera prononcée automatiquement dès que la précédente sera achevée.
- `QueuePolicy::INTERRUPT_CURRENT` : La phrase en cours est instantanément coupée, et la nouvelle demande prend immédiatement le relais.

---

## 6. Intégration dans CoreS3 Orientation Clock

Le dossier `examples/OrientationClockVoice/` démontre l'intégration complète et non-intrusive dans l'application [CoreS3 Orientation Clock](https://github.com/Philippe-AD/CoreS3-Orientation-Clock) :

1. **Préservation stricte** :
   - Interface minimaliste LVGL 9 (grands chiffres blancs sur fond noir pur).
   - Détection d'orientation continue avec l'accéléromètre BMI270 (filtre passe-bas, rejet des perturbations, hystérésis).
   - Rotation matérielle instantanée de l'écran via M5GFX (registres MADCTL).
   - Horloge matérielle BM8563 (RTC) et synchronisation NTP en arrière-plan.
2. **Interaction vocale** :
   - Un simple tap sur l'écran tactile prononce l'heure courante en français : *« Il est vingt heures neuf. »*.
   - L'heure est convertie via le module dédié `FrenchTimeFormatter` à partir du `ClockManager` existant.
3. **Coordination Wi-Fi anti-coupure** :
   - La classe `WiFiTimeSync` a été dotée d'un verrou de maintien : `setKeepAliveLock(true)`.
   - Pendant la synthèse et la restitution sonore, l'extinction du Wi-Fi programmée par la synchronisation NTP est suspendue.
   - Dès la fin effective de la restitution (`onSpeechFinished`), le verrou est libéré et le Wi-Fi s'éteint pour préserver la batterie.
4. **Indicateur discret** :
   - Un point discret de 8 px situé dans l'angle supérieur droit reflète l'activité vocale (bleu = génération, vert = parole, rouge = erreur), sans jamais encombrer le dessin du cadran.

---

## 7. Validation & Tests Automatisés

Le dossier `tests/` contient un harnais de test unitaire C++17 exécutable sur machine de développement sans matériel :
```cmd
tests\run_tests.bat
```
Résultat : **55 tests exécutés, 55 réussis (100 % PASS)**.

Pour les tests matériels sur CoreS3 physique (restitution sonore audible, validation tactile, rotation physique), consulter le rapport complet dans [`RAPPORT_LIVRAISON_v0.1.0.md`](RAPPORT_LIVRAISON_v0.1.0.md).
