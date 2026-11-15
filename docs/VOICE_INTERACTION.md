# Guide d'Intégration de l'Interaction Vocale Stabilisée (v0.6.7.1)

## VoiceInteractionController & VoiceCaptureValidator pour M5Stack CoreS3 (ESP32-S3)

---

## 1. Introduction et Objectif

La version **v0.6.7.1 (Voice Interaction Stabilization)** de VoiceKit stabilise et pérennise les composants légers et modulaires d'interaction vocale introduits en v0.6.7.0 :
- **Push-to-Talk (PTT) à deux clics (PARLER / TERMINER)** avec filtrage anti-rebond matériel (120 ms).
- **Validation locale d'activité vocale (VAD holistique)** analysant l'énergie RMS par blocs PCM 16 kHz (32 ms) pour rejeter catégoriquement le silence et les bruits brefs sans appel au réseau.
- **Interruption prioritaire STOP** instantanée, coupant micro ou haut-parleur et rétablissant la session sans blocage.
- **Protection stricte contre les callbacks tardifs** (moteur ou matériel) suite à une annulation ou une interruption STOP.
- **Surveillance non-bloquante par watchdogs déterministes** (timeouts micro 15 s avec repli propre, serveur 15 s, reprise après erreur).

Ces composants sont **strictement additifs** et **indépendants de LVGL**. Leur présence ne modifie en rien le fonctionnement des applications existantes.

---

## 2. Architecture des Composants

```
                                  +------------------------------------+
                                  |            APPLICATION             |
                                  |   (Bouton LVGL, Bouton M5, Série)  |
                                  +-----------------+------------------+
                                                    |
                                                    | onTalkClick() / triggerStop()
                                                    v
                                  +------------------------------------+
                                  |     VoiceInteractionController     |
                                  |  - Machine à états 2 clics         |
                                  |  - Anti-rebond (120 ms)            |
                                  |  - Watchdogs non bloquants         |
                                  |  - Arbitrage STOP prioritaire      |
                                  +--------+------------------+--------+
                                           |                  |
                          feedChunk(rms)   |                  | start/stop/cancelListening()
                                           v                  v
                 +------------------------------------+   +------------------------------------+
                 |       VoiceCaptureValidator        |   |            VoxCoreVoice            |
                 |  - Plancher garanti 380 RMS ES7210 |   |  - Propriété du micro ES7210       |
                 |  - Durée min capture (350 ms)      |   |  - Propriété HP AW88298            |
                 |  - Parole cumulée (250 ms)         |   |  - Flux WebSocket TLS              |
                 |  - Segment continu min (120 ms)    |   |  - OpenAI Realtime / Gemini Live   |
                 +------------------------------------+   +------------------------------------+
```

### 2.1 Rôle de VoiceCaptureValidator
- Classe C++ pure sans dépendance matérielle ni allocation dynamique.
- Maintient un lissage exponentiel (EMA, $\alpha = 0.05$) du bruit ambiant.
- Calcule dynamiquement le seuil : $\text{seuil} = 1.8 \times \text{noiseFloor} + 150$, avec plancher matériel garanti à 380 RMS pour le microphone ES7210.
- Sur demande terminale `evaluate()`, qualifie la capture selon 4 critères stricts :
  1. `TOO_SHORT` : Durée totale $< 350\text{ ms}$ ou $< 10\text{ blocs}$ (rejet des doubles clics accidentels).
  2. `SILENCE` : Crête RMS $< \text{seuil}$ ou zéro bloc au-dessus du seuil.
  3. `BRIEF_NOISE` : Parole cumulée $< 250\text{ ms}$ ou segment continu le plus long $< 120\text{ ms}$ (chocs mécaniques, tapotements).
  4. `LOW_ENERGY` : Énergie moyenne $< \text{noiseFloor} + 30\text{ RMS}$.
  5. `OK` : Parole humaine confirmée.

### 2.2 Rôle de VoiceInteractionController
- Machine à états orchestrant le dialogue entre l'IHM et `VoxCoreVoice`.
- Découple les événements tactiles des tâches FreeRTOS audio et réseau via des intentions différées traitées dans `update()`.
- Garantit l'idempotence des opérations (aucune double invocation de `startListening()` ou `stop()`).
- Assure qu'une capture rejetée invoque `cancelListening()` au lieu de `stopListening()`, empêchant tout déclenchement de réponse serveur inutile.

---

## 3. Machine à États et Fonctionnement à Deux Clics

### 3.1 États disponibles (`VoiceInteractionState`)

| État | Description |
|---|---|
| `DISABLED` | Interaction désactivée (hors-ligne Wi-Fi ou session en cours d'établissement). |
| `READY` | Système opérationnel, prêt pour un premier clic. |
| `STARTING` | Demande d'ouverture micro engagée, attente confirmation moteur. |
| `LISTENING` | Microphone actif, capture et analyse VAD en cours. |
| `STOPPING_CAPTURE` | Second clic reçu, parole validée, demande de réponse envoyée au serveur. |
| `PROCESSING` | Attente de la transcription finale ou de la synthèse serveur. |
| `SPEAKING` | Restitution sonore active sur le haut-parleur AW88298. |
| `INTERRUPTING` | Commande STOP en cours d'application. |
| `RECOVERING` | Stabilisation et nettoyage après anomalie ou timeout. |
| `ERROR_STATE` | Erreur critique transport ou service. |

### 3.2 Transitions du cycle nominal

```
+---------------------------------------------------------------------------------------------------+
| PREMIER CLIC (PARLER)                                                                             |
|                                                                                                   |
|  READY ---[ onTalkClick() ]---> START_CAPTURE ---> startListening() ---> STARTING ---> LISTENING |
+---------------------------------------------------------------------------------------------------+

+---------------------------------------------------------------------------------------------------+
| DEUXIÈME CLIC (TERMINER)                                                                          |
|                                                                                                   |
|  LISTENING ---[ onTalkClick() ]---> FINISH_CAPTURE ---> evaluate VAD                              |
|                                                              |                                    |
|         +----------------------------------------------------+-----------------------------+      |
|         | SI PAROLE VALIDE (OK)                                                            |      |
|         v                                                                                  v      |
|   stopListening()                                                              cancelListening()  |
|         |                                                                                  |      |
|   STOPPING_CAPTURE                                                                  Notification  |
|         |                                                                                  |      |
|    PROCESSING ---> SPEAKING ---> READY                                                   READY    |
+---------------------------------------------------------------------------------------------------+
```

---

## 4. Gestion du Silence et Traitement de STOP

### 4.1 Rejet du silence (Zéro trame réseau)
Lorsqu'une capture est qualifiée de `SILENCE` ou `BRIEF_NOISE` :
1. Le contrôleur invoque formellement `_voice.cancelListening()`.
2. Aucune trame `input_audio_buffer.commit` ni `response.create` n'est transmise à OpenAI.
3. Le serveur ne génère aucune réponse sonore parasite et ne consomme aucun token.
4. Le callback `onSilenceDetected` est déclenché pour informer l'IHM (affichage `"(Aucune parole détectée)"`).
5. Le contrôleur retourne immédiatement à l'état `READY`.

### 4.2 Traitement prioritaire de STOP
La commande STOP est accessible à tout moment via `triggerStop()` ou par un clic sur le bouton d'action pendant `PROCESSING` ou `SPEAKING` :
1. Invalidation du tour en cours : `_lastTurnCancelled = true`.
2. Coupure du microphone si actif (`_voice.cancelListening()`).
3. Interruption immédiate du haut-parleur AW88298 (`_voice.stop()`).
4. Purge des événements tardifs orphelins (transcriptions ou audio arrivant après STOP).
5. Retour contrôlé à `READY` dès confirmation que le moteur et la session sont au repos.
6. L'opération est strictement **idempotente** : des appels répétés à STOP ne provoquent aucune anomalie.

---

## 5. Gestion des Callbacks et Responsabilités

Les callbacks de `VoxCoreVoice` étant à **abonné unique** (`std::function`), `VoiceInteractionController` ne s'y abonne pas silencieusement sans accord explicite :

### Option 1 — Relais explicite par l'application (Recommandé)
L'application conserve la maîtrise absolue de ses callbacks `VoxCoreVoice` et relaie les événements au contrôleur :

```cpp
voice.onAudioChunk([](const int16_t* samples, size_t count, uint32_t rms) {
    interaction.processAudioChunk(samples, count, rms);
});

voice.onReady([]() {
    interaction.notifyReady();
});

voice.onStateChanged([](VoiceState st) {
    interaction.notifyVoiceStateChanged(st);
});

voice.onListeningStarted([]() { interaction.notifyListeningStarted(); });
voice.onListeningFinished([]() { interaction.notifyListeningFinished(); });
voice.onSpeechStarted([]() { interaction.notifySpeechStarted(); });
voice.onSpeechFinished([]() { interaction.notifySpeechFinished(); });
voice.onError([](const String& err) { interaction.notifyError(err); });
```

### Option 2 — Liaison automatique
Si l'application ne souhaite pas gérer manuellement les notifications, elle peut invoquer :
```cpp
interaction.attachVoiceCallbacks();
```
Et observer les changements d'état via `interaction.onStateChanged(...)` et `interaction.onSilenceDetected(...)`.

---

## 6. Paramètres Configurables (`VoiceInteractionConfig`)

```cpp
VoiceInteractionConfig config;
config.debounceMs = 120;                  // Filtrage anti-rebond (ms)
config.micTimeoutMs = 15000;              // Watchdog micro (ms)
config.serverResponseTimeoutMs = 15000;   // Watchdog attente réponse serveur (ms)
config.minCaptureDurationMs = 350;        // Durée min de capture (ms)
config.minSpeechDurationMs = 250;         // Durée min de parole cumulée (ms)
config.minConsecutiveSpeechMs = 120;      // Segment continu min (ms)
config.speechThresholdFloorRms = 380;     // Plancher seuil ES7210 (RMS)
config.noiseMultiplier = 1.8f;            // Multiplicateur dynamique
config.noiseOffsetRms = 150;              // Décalage seuil (RMS)
```

---

## 7. Comment utiliser VoiceInteractionController sans recopier `src/voice` de SmartUI ?

Voici un exemple minimal, complet et compilable pour M5Stack CoreS3 (sans dépendance à LVGL, pilotable avec le bouton physique M5 ou l'écran tactile) :

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
#include <VoiceInteractionController.h>

VoxCoreVoice voice;
VoiceInteractionController interaction(voice);

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    // 1. Initialiser le contrôleur d'interaction
    interaction.begin();

    // 2. Callbacks d'observation
    interaction.onStateChanged([](VoiceInteractionState oldSt, VoiceInteractionState newSt) {
        Serial.printf("[PTT] Transition : %s -> %s\n",
                      voiceInteractionStateToString(oldSt),
                      voiceInteractionStateToString(newSt));
    });

    interaction.onSilenceDetected([](const char* reason) {
        Serial.printf("[PTT] Capture rejetée localement sans envoi IA [motif: %s]\n", reason);
    });

    // 3. Liaison aux flux VoxCoreVoice
    voice.onAudioChunk([](const int16_t* samples, size_t count, uint32_t rms) {
        interaction.processAudioChunk(samples, count, rms);
    });
    voice.onReady([]() { interaction.notifyReady(); });
    voice.onStateChanged([](VoiceState st) { interaction.notifyVoiceStateChanged(st); });
    voice.onListeningStarted([]() { interaction.notifyListeningStarted(); });
    voice.onListeningFinished([]() { interaction.notifyListeningFinished(); });
    voice.onSpeechStarted([]() { interaction.notifySpeechStarted(); });
    voice.onSpeechFinished([]() { interaction.notifySpeechFinished(); });
    voice.onError([](const String& err) { interaction.notifyError(err); });

    // 4. Configuration VoiceKit
    VoiceKitConfig vConfig;
    vConfig.provider = VoiceProviderType::OPENAI;
    vConfig.apiKey = "sk-proj-...";
    vConfig.wifiMode = WiFiMode::MANAGED_WIFI;
    vConfig.wifiSSID = "MonWiFi";
    vConfig.wifiPassword = "MonMotDePasse";
    vConfig.enableConversation = true;

    voice.begin(vConfig);
}

void loop() {
    M5.update();
    voice.update();
    interaction.update();

    // Interaction sur bouton physique M5 (Bouton A = PARLER / TERMINER, Bouton B = STOP)
    if (M5.BtnA.wasClicked()) {
        interaction.onTalkClick();
    }
    if (M5.BtnB.wasClicked()) {
        interaction.triggerStop();
    }

    delay(5);
}
```

---

## 8. Statut de Stabilisation et Limitations (v0.6.7.1)

1. **Validation Matérielle Confirmée :** Le protocole matériel de validation sur M5Stack CoreS3 physique a obtenu **8/8 PASS** (MAT-01 à MAT-08, confirmé utilisateur).
2. **Statut de Stabilisation :** Les classes `VoiceInteractionController` et `VoiceCaptureValidator` sont désormais stabilisées pour intégration applicative externe. La version préparée pour diffusion publique reste `v0.6.6.2`, tandis que la branche `v0.6.7.x` prépare la future version consolidée.
3. **Dépendance matérielle du microphone :** Le plancher par défaut (380 RMS) et les seuils VAD sont optimisés sur le microphone ES7210 du M5Stack CoreS3. Sur d'autres cartes ESP32-S3 (ex: microphone PDM SPM1423 ou analogique), ces seuils sont ajustables via `VoiceInteractionConfig`.
4. **Application VoiceKit-SmartUI :** Conformément au principe directeur de non-régression, **VoiceKit-SmartUI v0.1.0.6 n'a pas été modifiée**. L'application reste la référence matérielle historique. La migration de SmartUI vers ces nouveaux composants sera traitée séparément après stabilisation complète.
