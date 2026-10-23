# VoxCore VoiceKit v0.1.0.3

Bibliothèque C++/Arduino indépendante de **synthèse vocale (Text-To-Speech / TTS)** haute fidélité pour **M5Stack CoreS3** (ESP32-S3) exploitant le moteur **Gemini Live** via WebSocket bidirectionnel TLS.

---

## 1. Philosophie & Principes Fondamentaux

**VoxCore VoiceKit** a été conçu pour permettre d'ajouter la parole naturelle à n'importe quelle application CoreS3 existante (horloges, tableaux de bord, stations de capteurs, domotique) en quelques lignes de code, sans devoir comprendre ni importer toute l'architecture de VoxCore.

### Principes directeurs de la v0.1.0.3 :
1. **Composant invité strict** : VoiceKit ne prend jamais le contrôle de l'écran, ne crée aucune interface graphique intrusive, ne modifie pas la rotation de l'affichage, ne perturbe ni l'IMU, ni le RTC, ni les tâches applicatives de l'hôte.
2. **Propriété explicite du haut-parleur** : VoiceKit respecte scrupuleusement l'initialisation audio réalisée par l'hôte. Si l'application hôte a déjà démarré `M5.Speaker`, VoiceKit l'utilise en invité sans écraser ses réglages DMA ni appeler `M5.Speaker.end()`.
3. **Zéro couplage avec LVGL** : Aucune dépendance à LVGL. VoiceKit fonctionne aussi bien avec LVGL 8, LVGL 9, M5GFX brut ou en mode headless (sans affichage).
4. **API publique simplifiée et asynchrone** : L'appel `voice.say("Bonjour")` est non-bloquant. La boucle principale `loop()` reste instantanément réactive à chaque milliseconde.
5. **Sanctuaire audio éprouvé** : Préservation intégrale du pipeline audio stabilisé en **VoxCore v0.2.1.3.6 / v0.2.2.0** (Mode 2 `DEDICATED_AUDIO_TASK` FreeRTOS, tampon PSRAM 120 000 échantillons, quadruple buffer DMA, régulation douce sans perte, attente du vidage matériel DMA).
6. **Garantie de non-coupure** : Le callback `onSpeechFinished` n'est déclenché qu'après émission physique réelle des derniers échantillons par le haut-parleur. Zéro mot tronqué en fin de phrase.
7. **Validation TLS serveur stricte & Certificats racines complets** : Restauration intégrale de la chaîne officielle Google Trust Services (`https://pki.goog/roots.pem`, 21 racines incluant GTS Root R1 à R4 et GlobalSign) validée dans VoxCore, résolvant l'échec `MBEDTLS_ERR_X509_CERT_VERIFY_FAILED (-0x2700)` sur les certificats ECDSA sans affaiblir la sécurité.
8. **Récupération et reconnexion bornée** : Machine à états résiliente avec temporisation de 5 secondes après erreur réseau/TLS et plafond de 3 tentatives automatiques.

---

## 2. Nouveautés de la v0.1.0.3
* **Correction TLS ciblée** : Restauration du bundle complet officiel `GoogleRoots.h` de VoxCore (21 racines, dont GTS Root R4 et GlobalSign indispensables pour authentifier le serveur Google actuel `generativelanguage.googleapis.com:443`).
* **Résolution de l'erreur -0x2700** : Élimination de `MBEDTLS_ERR_X509_CERT_VERIFY_FAILED` sans compromettre la validation du certificat ni le nom d'hôte.
* **Étude `esp_crt_bundle_attach`** : Démonstration technique de la non-disponibilité du bundle dans `esp_websocket_client` sur ESP-IDF v4.4 et confirmation de l'architecture par bundle PEM complet dans `cfg.cert_pem`.
* **Diagnostics explicites série** : Affichage standardisé `[TLS] Connexion`, `[TLS] Certificats`, `[TLS] Vérification : OK`, `[Gemini] WebSocket : connecté` et `[Gemini] setupComplete : reçu`.
* **Récupération après erreur** : Retour temporisé vers `IDLE` après échec pour un nouvel essai propre avec compteur borné (3 tentatives).
* **Isolation du transport** : Restauration sécurisée du transport par défaut sur `setTransport(nullptr)`.
* **159 tests automatisés** : Couverture complète unitaire et d'intégration validée à 100%.

---

## 3. Architecture de la Bibliothèque

```text
VoxCore-VoiceKit/
├── library.json                   # Manifeste PlatformIO (v0.1.0.3)
├── library.properties             # Manifeste Arduino IDE (v0.1.0.3)
├── README.md                      # Documentation complète
├── CHANGELOG.md                   # Historique des versions
├── RAPPORT_LIVRAISON_v0.1.0.3.md  # Rapport de conformité v0.1.0.3
├── include/
│   ├── VoxCoreVoice.h             # API publique simplifiée
│   └── VoiceKitConfig.h           # Configuration centralisée et modèles par défaut
├── src/
│   ├── VoxCoreVoice.cpp           # Orchestrateur d'état de la bibliothèque
│   ├── audio/
│   │   ├── LiveAudioPlayer.h      # Restitution PCM 24 kHz Mode 2 FreeRTOS
│   │   ├── LiveAudioPlayer.cpp    # Tampon PSRAM 120 000 ech, DMA 4x40ms
│   │   ├── AudioTraceLog.h        # Journalisation circulaire POD sans allocation
│   │   └── AudioTraceLog.cpp
│   ├── gemini/
│   │   ├── GeminiLiveClient.h     # Client WebSocket TLS Gemini Live
│   │   ├── GeminiLiveClient.cpp   # Décodage PCM 24 kHz, turns JSON, interruptions
│   │   ├── WebSocketReassembler.h # Réassemblage des trames fragmentées
│   │   ├── MockLiveTransport.h    # Transport simulé pour tests unitaires
│   │   ├── ILiveTransport.h       # Interface d'abstraction transport
│   │   └── GoogleRoots.h          # Certificats racines Google Trust Services
│   └── hal/
│       ├── IAudioOutput.h         # Abstraction matérielle avec isOwner()
│       ├── M5AudioOutput.h        # Adaptateur M5Unified avec gestion de propriété
│       └── M5AudioOutput.cpp
├── examples/
│   ├── MinimalTTS/                # Exemple PlatformIO minimal autonome
│   └── OrientationClockVoice/     # Intégration complète CoreS3 Orientation Clock
└── tests/
    ├── test_voicekit_all.cpp      # Suite de 101 tests unitaires automatisés C++17
    └── run_tests.bat              # Script d'exécution locale des tests natifs
```

---

## 4. Guide de Démarrage Rapide

### 4.1. Code Minimal

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>

VoxCoreVoice voice;

void setup() {
    // 1. Initialisation matérielle normale de votre application
    auto cfg = M5.config();
    cfg.internal_spk = true;
    M5.begin(cfg);

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

## 5. Référence de l'API Publique

### Méthodes de `VoxCoreVoice`

| Méthode | Rôle | Comportement Bloquant |
| :--- | :--- | :---: |
| `bool begin(const VoiceKitConfig& config)` | Initialise le sous-système audio, le client réseau et la file d'attente. | Non (< 10 ms) |
| `bool say(const String& text)` | Demande la prononciation mot à mot du texte. Accepte ou rejette selon la politique de file. | **Non bloquant** |
| `void update()` | Traitement périodique (maintien WebSocket, décodage, cadencement DMA, timeouts). À placer dans `loop()`. | **Non bloquant** (< 1 ms) |
| `void stop()` | Arrête immédiatement la parole, coupe le son et vide les files. | Non |
| `bool isSpeaking() const` | Indique si des échantillons audio sont en cours d'émission par le haut-parleur. | Non |
| `bool isReady() const` | Indique si le service est prêt à accepter une nouvelle phrase immédiatement. | Non |
| `bool isBusy() const` | Indique si le moteur génère du son, parle actuellement ou a une requête en cours. | Non |
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

## 6. Gestion Réseau & Politiques de File

### 6.1. Modes Wi-Fi (`WiFiMode`)
- `WiFiMode::MANAGED_WIFI` : VoiceKit gère lui-même sa connexion au point d'accès Wi-Fi avec reconnexion automatique.
- `WiFiMode::EXTERNAL_WIFI` : L'application hôte conserve l'entière propriété de sa pile Wi-Fi. VoiceKit attend passivement qu'une connexion utilisable (`WiFi.status() == WL_CONNECTED`) et qu'une heure système valide pour TLS soient disponibles.

### 6.2. Économie d'Énergie (`connectOnDemand`)
Pour les objets sur batterie :
```cpp
config.connectOnDemand = true;       // N'établit la session WebSocket TLS que sur say()
config.autoDisconnectIdleMs = 15000; // Se déconnecte automatiquement après 15 s de silence
```

### 6.3. Politiques de Concurrence (`QueuePolicy`)
Si `voice.say()` est appelé alors que le moteur est déjà en train de parler :
- `QueuePolicy::REJECT_IF_BUSY` (défaut) : La demande est immédiatement refusée (retourne `false`, `getLastError() == "Moteur vocal occupe"`).
- `QueuePolicy::QUEUE_FIFO` : La phrase est mise en attente dans une file bornée (`config.maxQueueSize`) et sera prononcée automatiquement dès que la précédente sera achevée.
- `QueuePolicy::INTERRUPT_CURRENT` : La phrase en cours est instantanément coupée, et la nouvelle demande prend immédiatement le relais.

---

## 7. Intégration dans CoreS3 Orientation Clock

Le dossier `examples/OrientationClockVoice/` démontre l'intégration complète et non-intrusive dans l'application [CoreS3 Orientation Clock](https://github.com/Philippe-AD/CoreS3-Orientation-Clock) :

1. **Préservation stricte** :
   - Interface minimaliste LVGL 9 (grands chiffres blancs sur fond noir pur).
   - Détection d'orientation continue avec l'accéléromètre BMI270 (filtre passe-bas, rejet des perturbations, hystérésis).
   - Rotation matérielle instantanée de l'écran via M5GFX (registres MADCTL).
   - Horloge matérielle BM8563 (RTC) et synchronisation NTP en arrière-plan.
2. **Interaction vocale** :
   - Un simple tap sur l'écran tactile prononce l'heure courante en français : *« Il est vingt heures neuf. »*.
   - L'heure est convertie via le module dédié `FrenchTimeFormatter` à partir de la méthode `clock_mgr.getLocalTime(hour, minute)` de l'horloge.
3. **Coordination Wi-Fi anti-coupure** :
   - La classe `WiFiTimeSync` a été dotée d'un verrou de maintien : `setKeepAliveLock(true)`.
   - Pendant la synthèse et la restitution sonore, l'extinction du Wi-Fi programmée par la synchronisation NTP est suspendue.
   - À l'issue de la parole, un maintien radio chaud de 15 secondes permet une seconde annonce immédiate sans délai de reconnexion.
   - Dès la fin effective de la restitution et du délai d'inactivité, le verrou est libéré et le Wi-Fi s'éteint pour préserver la batterie.
4. **Indicateur discret** :
   - Un point discret de 8 px situé dans l'angle supérieur droit reflète l'activité vocale (bleu = génération, vert = parole, rouge = erreur), sans jamais encombrer le dessin du cadran.

---

## 8. Validation & Tests Automatisés

Le dossier `tests/` contient un harnais de test unitaire C++17 exécutable sur machine de développement sans matériel :
```cmd
tests\run_tests.bat
```
Résultat : **101 tests exécutés, 101 réussis (100 % PASS)**.

Les deux exemples compilent rigoureusement sans erreur sur PlatformIO :
- `pio run -d examples/MinimalTTS` : **SUCCESS**
- `pio run -d examples/OrientationClockVoice` : **SUCCESS**

Pour la procédure de test sur M5Stack CoreS3 physique (restitution sonore audible, validation tactile, rotation physique), consulter le rapport complet dans [`RAPPORT_LIVRAISON_v0.1.0.1.md`](RAPPORT_LIVRAISON_v0.1.0.1.md).
