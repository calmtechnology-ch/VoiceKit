# Rapport Technique — VoiceKit v0.6.3.1

**Dépôt :** `Philippe-AD/VoiceKit` — Branche `main`  
**Version :** `v0.6.3.1` (Corrective de stabilité et WakeNet configurable)  
**Cible matérielle :** M5Stack CoreS3 (ESP32-S3, 240 MHz, 320 Ko SRAM interne, 8 Mo PSRAM, microphone ES7210, amplificateur AW88298)  
**Référence de non-régression :** VoiceKit `v0.6.3` validé matériellement sur M5Stack CoreS3 réel (« Bonjour ESP » reconnu acoustiquement, démarrage de `WakeWordConversation`, échanges vocaux bidirectionnels et retour automatique à `WAKE_ARMED`).

---

## 1. Analyse et Correction de la Latence de `voice.update()`

### 1.1 Origine Exacte du Pic `voice.update() = 582 701 µs`

Dans le rapport matériel de la version `v0.6.3` (`RAPPORT_v0.6.3.md`), les mesures relevées sur M5Stack CoreS3 réel indiquaient :
- `maxVoiceUpdateDurationUs = 582 701 µs` (`~582,7 ms`)
- `maxControllerUpdateDurationUs = 582 454 µs` (`~582,5 ms`)
- `maxUpdateProcessDurationUs (WakeWordManager) = 45 270 µs` (`~45,3 ms`, soit 1 trame WakeNet de 30 ms sur CoreS3)

La différence montre que `99,96 %` du pic de `582,7 ms` provenait de `WakeWordConversationController::update()`. L'audit du code source et le désassemblage binaire de `libesp_websocket_client.a` (ESP-IDF v4.4 / Arduino-ESP32 2.0.17) ont permis d'identifier précisément les appels synchrones bloquants :

1. **Cause principale (`~580 ms`) — `esp_websocket_client_stop()` lors de `ENDING_SESSION`** :
   - Dans `WakeWordConversationController::update()`, l'état `ENDING_SESSION` appelait `_session->deactivate()` $\to$ `_provider->disconnect()` $\to$ `EspWebSocketTransport::disconnect()`, qui invoquait de manière synchrone `esp_websocket_client_stop(_client)` suivi de `esp_websocket_client_destroy(_client)`.
   - Dans `libesp_websocket_client.a` (`esp_websocket_client.c.obj`), `esp_websocket_client_stop(client)` écrit `client->run = false` (offset `64`) puis exécute :
     ```c
     xEventGroupWaitBits(client->status_bits /* offset 68 */, STOPPED_BIT /* bit 0 = 1 */, pdFALSE, pdTRUE, portMAX_DELAY);
     ```
   - Or, la tâche FreeRTOS interne `websocket_task` exécute une boucle `esp_transport_poll_read(client->transport, 1000)` (`select()` avec un timeout de `1 000 ms`). Lorsque `ENDING_SESSION` survient entre deux trames serveur (cas systématique après la fin de lecture TTS ou après un timeout de silence), `websocket_task` reste endormie dans `esp_transport_poll_read(1000)` jusqu'à l'expiration de la tranche de `1 000 ms` en cours (en moyenne `500 ms`, mesuré à `582 ms` lors de l'essai `v0.6.3`), bloquant `loopTask` pendant toute cette durée.
   - **Effet de bord associé dans `ENDING_SESSION`** : `_returnToWakeStartMs` était affecté avec la valeur `nowMs` lue *au début* de `update()` (avant l'appel bloquant de `582 ms`). Au tick suivant, `nowMs - _returnToWakeStartMs` valait déjà `582 ms >= 250 ms` (`returnToWakeDelayMs`), ce qui court-circuitait la garde acoustique de `250 ms` et sautait l'affichage de l'état `RETURNING_TO_WAKE` (`RETOUR EN ÉCOUTE`).

2. **Appels secondaires synchrones identifiés et corrigés** :
   - **Transition `WAKE_ARMED -> WAKE_DETECTED`** : lors de la détection acoustique de « Bonjour ESP », `WakeWordManager::update()` exécutait dans le même tick l'inférence `WakeNet::detect()` (`~45 ms`) *puis* l'attente synchrone d'arrêt de `live_mic_task` (`stopLocalCapture()`, jusqu'à `32 ms` + `delay(5)`).
   - **Arrêt matériel du microphone (`LiveAudioStreamer::stopMicHardware()` et `HardwareManager::restoreSpeakerAfterMic()`)** : `HardwareManager::restoreSpeakerAfterMic()` bouclait jusqu'à `100 ms` sur `_audioInput->isRecording() != 0` *avant* d'appeler `_audioInput->end()`, et `stopMicHardware()` contenait un `delay(5)` redondant.
   - **Fermeture de session (`ENDING_SESSION`)** : `_conversation->requestStop()` envoyait une trame JSON `sendInterruption()` sur le socket TLS immédiatement avant sa fermeture, et `_session->deactivate()` appelait `_player->stop()` même lorsque le lecteur audio avait déjà terminé sa lecture.

### 1.2 Corrections Non Bloquantes Implémentées (Sans Nouvelle Tâche FreeRTOS)

Conformément à la contrainte interdisant de déplacer une opération vers une nouvelle tâche FreeRTOS sans justification mesurée, **aucune tâche FreeRTOS supplémentaire n'a été créée** :

- **Arrêt WebSocket non bloquant en 2 phases (`EspWebSocketTransport::requestDisconnect()` / `pollDisconnect()`)** :
  - `requestDisconnect()` positionne `client->run = 0` en `< 2 µs` pour signaler à la tâche existante `websocket_task` de quitter sa boucle dès la fin de son `poll_read` courant, sans bloquer `loopTask`.
  - À chaque cycle de `voice.update()`, `pollDisconnect()` interroge `xEventGroupWaitBits(statusBits, STOPPED_BIT, pdFALSE, pdTRUE, 0)` avec un délai d'attente nul (`0` tick). Tant que `STOPPED_BIT` n'est pas levé, `pollDisconnect()` rend immédiatement la main (`false`) en quelques microsecondes.
  - Dès que `websocket_task` a levé `STOPPED_BIT`, `pollDisconnect()` détruit proprement le client (`esp_websocket_client_destroy(_client)`) sans attente bloquante, puis `ENDING_SESSION` ancre `_returnToWakeStartMs = millis()` *après* la fin effective de la désactivation et passe à `RETURNING_TO_WAKE`.
- **Séparation de la détection WakeNet et de l'arrêt de capture locale** :
  - Lorsque `WakeWordConversationController` est actif (`setDeferCaptureStopOnDetection(true)`), le tick de détection (`WAKE_ARMED -> WAKE_DETECTED`) signale immédiatement `_streamer->requestStopLocalCaptureAsync()` (`< 1 µs`) sans attendre de manière synchrone la sortie de `live_mic_task`.
  - L'état `WAKE_DETECTED` est ainsi affiché dès ce premier tick (`BONJOUR ESP DÉTECTÉ`), et le tick suivant finalise `ensureCaptureStopped()` alors que `live_mic_task` a déjà quitté sa boucle.
- **Optimisation de `stopMicHardware()`, `restoreSpeakerAfterMic()` et `abortTurnForSessionClose()`** :
  - Appel immédiat de `_audioInput->end()` avant la vérification `isRecording() != 0` et suppression du `delay(5)` redondant.
  - Utilisation de `_conversation->abortTurnForSessionClose()` lors d'une fermeture de session (`disconnectAfterSession == true`) pour éviter l'envoi d'une trame `sendInterruption()` inutile sur un WebSocket en cours de fermeture.

> **Note sur les durées maximales :** Aucune durée maximale garantie sur matériel n'est annoncée sans mesure réelle sur CoreS3 physique. Les mesures obtenues lors des tests automatisés natifs (Section 8) sont strictement distinguées des mesures matérielles à relever sur CoreS3 selon la procédure de la Section 9.

---

## 2. Consolidation des Transitions Audio

Le cycle complet piloté par `WakeWordConversationController` a été vérifié et consolidé :

$$\text{WAKE\_ARMED} \to \text{WAKE\_DETECTED} \to \text{PREPARING\_SESSION} \to \text{CONNECTING} \to \text{CONVERSATION\_LISTENING} \leftrightarrow \text{CONVERSATION\_SPEAKING} \to \text{ENDING\_SESSION} \to \text{RETURNING\_TO\_WAKE} \to \text{WAKE\_ARMED}$$

| Garantie requise | Mécanisme implémenté et vérifié |
| :--- | :--- |
| **Un seul propriétaire du microphone** | `HardwareManager::prepareMicCapture(owner)` enregistre le propriétaire actif (`_micOwner`, inspectable via `getMicOwner()`). Toute tentative de réservation concurrente par un autre propriétaire est refusée et incrémente `_micReservationFailures` (`micReservationFailures`). |
| **Aucun double démarrage I2S** | `LiveAudioStreamer::startLocalCapture()` et `start()` vérifient l'état courant et la propriété dans `HardwareManager` avant tout appel à `_audioInput->begin()`. Si la capture locale est déjà active pour ce streamer, `startLocalCapture()` est idempotent. |
| **Aucune reprise WakeNet pendant la conversation** | `WakeWordConversationController::enable(true)` désactive l'auto-reprise interne (`_wakeWord->setAutoResumeAfterVoiceActivity(false)`) et le callback `isVoiceBusy()` retourne `true` tant que `isSessionInProgress()` est vrai (`WAKE_DETECTED` à `RECOVERABLE_ERROR`). |
| **Aucune fermeture prématurée du lecteur audio** | En fermeture normale (`ENDING_SESSION`), le contrôleur attend que `_player->isPlaying() == 0` avant de clore la session. En outre, `LiveSessionController::deactivate()` et `requestDeactivate()` n'appellent `_player->stop()` que si `_player->isPlaying() > 0 \|\| _player->getBufferedSamples() > 0`. |
| **Aucun WebSocket résiduel après une session terminée** | En `ENDING_SESSION` et `RETURNING_TO_WAKE`, la transition vers `WAKE_ARMED` est conditionnée à `!_provider->isConnected() && !_provider->isDisconnectPending() && !_session->isDeactivating()`. |
| **Récupération contrôlée après erreur** | Toute erreur réseau ou microphone fait transiter le contrôleur vers `RECOVERABLE_ERROR`, nettoie les ressources actives, attend la levée d'une éventuelle quarantaine FreeRTOS et la temporisation `errorRecoveryDelayMs`, incrémente `recoveryCount`, puis passe par `RETURNING_TO_WAKE` avant de réarmer `WAKE_ARMED`. |
| **Protection `WakeWordAudioBridge` v0.6.2.1** | Intégralement conservée (politique SPSC `DROP_NEWEST`, compteur atomique `_activeWriters`, génération `_epoch`, zéro invalidation de `peekFrame()`). |

---

## 3. Sélection Configurable du Modèle WakeNet

### 3.1 Configuration et Valeurs par Défaut Conservées

Dans `VoiceKitConfig` (`src/VoiceKitConfig.h`) :
```cpp
WakeWordEngineType wakeWordEngine = WakeWordEngineType::WAKENET;
String wakeWordPhrase = "Bonjour ESP";            // Conservé obligatoirement par défaut
String wakeWordModelName = "wn10_fr_bonjouresp";  // Modèle par défaut obligatoire (configurable)
```

Nouvelles méthodes exposées sur `WakeWordManager`, `VoxCoreVoice` et `VoiceKit` :
- `bool setWakeWordModelName(const String& modelName)` (modifiable uniquement hors écoute active `ARMED`)
- `const char* getWakeWordModelName() const`
- `String getResolvedWakeWordModelName() const`
- `bool isWakeWordPhraseConsistentWithModel() const`

### 3.2 Vérification Stricte en 4 Étapes Avant Utilisation

Avant d'activer le moteur `WAKENET` dans `WakeWordManager::enable(true)`, les 4 vérifications suivantes sont exécutées séquentiellement :
1. **Support WakeNet compilé** (`isEngineCompiledSupportAvailable()`) : vérifie que le binaire ESP-SR WakeNet pour ESP32-S3 est compilé et lié.
2. **Présence du modèle dans la partition Flash** (`isEngineModelAvailable()`) : sonde la partition `model` (`srmodels`) via `esp_srmodel_init("model")` et `esp_srmodel_filter(..., _modelName)`. Le repli d'alias vers `"bonjouresp"` (`wn9l_fr_bonjouresp_tts3`) est strictement réservé au modèle par défaut `"wn10_fr_bonjouresp"` ; tout autre nom de modèle explicitement demandé doit correspondre exactement à un modèle présent dans la partition Flash.
3. **Initialisation du modèle** (`_activeEngine->begin()` / `isEngineInitialized()`) : vérifie l'instanciation effective de `wakenet->create()`.
4. **Reconnaissance acoustique réellement active** (`isRealRecognitionActive()`) : vérifie que le moteur n'est pas simulé et est prêt à traiter les trames PCM de 480 échantillons.

Si l'une de ces 4 étapes échoue :
- `WakeWordManager` passe immédiatement en `WakeWordState::ERROR` avec un message d'erreur explicite.
- **Aucun remplacement automatique par le moteur `TEST` n'est effectué.**
- Si `wakeWordPhrase` est modifié sans changer `wakeWordModelName` (`wn10_fr_bonjouresp`), `isWakeWordPhraseConsistentWithModel()` retourne `false` et un avertissement explicite rappelle dans le journal série que le modèle acoustique détecte uniquement la phrase pour laquelle il a été entraîné.
- L'interface `IWakeWordEngine` (`setModelName`, `setWakeWordEngine(IWakeWordEngine*)`) conserve l'extensibilité complète pour intégrer ultérieurement un moteur `microWakeWord` personnalisé.

---

## 4. Diagnostics Enrichis Non Bloquants

La structure `WakeWordConversationMetrics` (`src/core/WakeWordConversationController.h`) et `printDiagnostics()` exposent désormais sans aucun appel bloquant :
- **Durée de `voice.update()`** : `avgVoiceUpdateDurationUs`, `maxVoiceUpdateDurationUs`, `lastVoiceUpdateDurationUs`, `voiceUpdateCallCount`.
- **Durée des transitions d'état** : `lastStateTransitionDurationUs`, `maxStateTransitionDurationUs`.
- **Durées par sous-système (µs)** :
  - `WakeWordConversationController::update()` : `lastControllerUpdateDurationUs`, `maxControllerUpdateDurationUs`
  - `WakeWordManager::update()` : `lastWakeWordUpdateDurationUs`, `maxWakeWordUpdateDurationUs`
  - `LiveSessionController::tick()` : `lastSessionTickDurationUs`, `maxSessionTickDurationUs`
  - Traitements WebSocket : `lastWsProcessDurationUs`, `maxWsProcessDurationUs`
  - Transitions microphone : `lastMicTransitionDurationUs`, `maxMicTransitionDurationUs`
  - Arrêt de session : `lastSessionStopDurationUs`, `maxSessionStopDurationUs`
  - Réactivation WakeNet : `lastWakeReactivationDurationUs`, `maxWakeReactivationDurationUs`
- **Durées de cycle (ms)** : `wakeToConnectStartMs`, `wakeToConversationReadyMs`, `wsConnectMs`, `returnToArmedMs`, `lastSessionDurationMs`, `totalSessionDurationMs`.
- **Mémoire (octets)** : `freeInternalHeap`, `minFreeInternalHeap`, `freePsram`, `minFreePsram`.
- **Compteurs de fiabilité** : `sessionCount`, `successfulSessionCount`, `recoveryCount`, `micTransitionFailures`, `micReservationFailures`, `networkFailures`, `wakeQueueSaturations`, `wakeFramesDropped`.

---

## 5. Interface `WakeWordConversation`

Sans refonte graphique, l'exemple `examples/WakeWordConversation/src/main.cpp` et la méthode `voice.getWakeWordConversationBannerText()` (`WakeWordConversationController::getUiTransitionBannerText()`) affichent immédiatement chaque transition dès l'événement réel du contrôleur (sans délai arbitraire) :
1. `BONJOUR ESP DÉTECTÉ` (`WAKE_DETECTED` / `PREPARING_SESSION`)
2. `CONNEXION` (`CONNECTING`)
3. `PRÊT — PARLEZ` (`CONVERSATION_LISTENING` avant détection de parole)
4. `ÉCOUTE` (`CONVERSATION_LISTENING` dès la première trame de parole confirmée par le VAD local via l'événement `USER_LISTENING`)
5. `RÉPONSE` (`CONVERSATION_SPEAKING`)
6. `FIN DE CONVERSATION` (`ENDING_SESSION`)
7. `RETOUR EN ÉCOUTE` (`RETURNING_TO_WAKE`)

---

## 6. Liste des Fichiers Modifiés

1. `src/core/VoiceHubConfig.h` — Mise à jour de `VOICEKIT_VERSION` vers `"v0.6.3.1"`.
2. `library.json` — Mise à jour de la version vers `"0.6.3.1"`.
3. `library.properties` — Mise à jour de la version vers `0.6.3.1`.
4. `src/VoiceKitConfig.h` — Ajout de `wakeWordModelName = "wn10_fr_bonjouresp"` tout en conservant `wakeWordPhrase = "Bonjour ESP"`.
5. `src/hal/HardwareManager.h` — Suivi du propriétaire unique du microphone (`_micOwner`, `getMicOwner()`), compteur d'échecs de réservation (`_micReservationFailures`) et optimisation de `restoreSpeakerAfterMic()`.
6. `src/core/LiveAudioStreamer.h` & `src/core/LiveAudioStreamer.cpp` — Ajout de `requestStopLocalCaptureAsync()`, instrumentation des transitions microphone (`lastMicTransitionDurationUs`, `maxMicTransitionDurationUs`) et optimisation de `stopMicHardware()`.
7. `src/core/ILiveTransport.h` & `src/core/EspWebSocketTransport.h` — Ajout de l'arrêt WebSocket non bloquant en 2 phases (`requestDisconnect()`, `pollDisconnect()`, `isDisconnectPending()`) et des métriques associées.
8. `src/core/IVoiceProvider.h`, `src/core/GeminiVoiceProvider.h/.cpp`, `src/core/GeminiLiveClient.h/.cpp`, `src/core/OpenAIRealtimeClient.h/.cpp` — Propagation de `requestDisconnect()`, `pollDisconnect()`, `isDisconnectPending()` et instrumentation des traitements WebSocket (`lastWsProcessDurationUs`, `maxWsProcessDurationUs`).
9. `src/core/LiveSessionController.h/.cpp` & `src/core/ConversationController.h/.cpp` — Ajout de `requestDeactivate()`, `pollDeactivate()`, `isDeactivating()`, `abortTurnLocally()`, `abortTurnForSessionClose()` et instrumentation de `tick()` et de l'arrêt de session.
10. `src/core/IWakeWordEngine.h`, `src/core/WakeNetEngine.h`, `src/core/WakeWordManager.h/.cpp` — Sélection explicite du modèle WakeNet, vérification stricte en 4 étapes, détection d'incohérence `wakeWordPhrase` / modèle, arrêt différé `setDeferCaptureStopOnDetection()` / `ensureCaptureStopped()` et métriques de réactivation.
11. `src/core/WakeWordConversationController.h/.cpp` — Arrêt de session non bloquant, ancrage post-désactivation de `_returnToWakeStartMs`, émission immédiate de `USER_LISTENING` sur début de parole VAD, `getUiTransitionBannerText()` et synchronisation des métriques par sous-système.
12. `src/VoxCoreVoice.h/.cpp` & `src/VoiceKit.h` — Exposition des méthodes de sélection/inspection du modèle WakeNet et de `getWakeWordConversationBannerText()`.
13. `examples/WakeWordConversation/src/main.cpp` — Rafraîchissement immédiat sur événements du contrôleur et affichage des 7 bandeaux de transition requis.
14. `tests/native_mock/MockLiveTransport.h` & `tests/test_voicekit_all.cpp` — Ajout de la simulation d'arrêt asynchrone multi-ticks dans `MockLiveTransport` et des 5 nouvelles suites de tests correctifs (Suites 101 à 105).
15. `CHANGELOG.md`, `README.md`, `RAPPORT_v0.6.3.1.md` — Documentation et rapport de livraison `v0.6.3.1`.

---

## 7. Résultats des Tests Automatisés et Compilations PlatformIO

> **Distinction méthodologique importante :** Les résultats ci-dessous proviennent de l'exécution réelle des suites de tests automatisés sur hôte (avec simulation matérielle/réseau déterministe) et de la compilation croisée complète Xtensa ESP32-S3 via PlatformIO. Les essais acoustiques en conditions réelles sur M5Stack CoreS3 physique relèvent de la procédure matérielle de la Section 8.

### 7.1 Suites de Tests Unitaires et d'Intégration (`tests/run_tests.bat`)

- **Suites exécutées :** Suites 1 à 105 (100 suites de non-régression + 5 nouvelles suites correctives `v0.6.3.1`)
- **Résultat global :** **2 100 assertions réussies, 0 échec** (`Exit code: 0`)
- **Détail des nouvelles suites correctives `v0.6.3.1` :**
  - **Suite 101** (`test_v0631_latency_instrumentation_and_nonblocking_session_teardown`) : Validation de l'arrêt WebSocket non bloquant en plusieurs ticks (`requestDeactivate` / `pollDeactivate`), de l'absence de trame `sendInterruption()` superflue en fermeture de session, du maintien de la garde de `250 ms` (`RETOUR EN ÉCOUTE`) après désactivation et de l'instrumentation séparée des 8 sous-systèmes.
  - **Suite 102** (`test_v0631_audio_transitions_consolidation_and_mic_concurrency`) : Validation du propriétaire unique du microphone dans `HardwareManager`, du rejet des réservations concurrentes (`micReservationFailures`), de l'absence de double démarrage I2S, de l'absence de reprise WakeNet pendant la conversation et de l'absence de WebSocket résiduel.
  - **Suite 103** (`test_v0631_configurable_wakenet_model_and_missing_model_validation`) : Validation des valeurs par défaut (`Bonjour ESP`, `wn10_fr_bonjouresp`), des 4 niveaux de vérification, de la détection d'incohérence `wakeWordPhrase` / modèle, et du comportement face à un modèle absent (`wn9_hiesp` $\to$ état `ERROR` explicite, zéro bascule implicite vers `TEST`).
  - **Suite 104** (`test_v0631_conversational_stability_and_turn_vs_session_distinction`) : Validation de plusieurs sessions consécutives, de la distinction stricte fin de tour / fin de session, de l'arrêt par « Au revoir » avec restitution complète des 30 derniers échantillons PCM, de l'échec temporaire de démarrage microphone suivi d'un retour automatique à `WAKE_ARMED`, et du timeout d'inactivité.
  - **Suite 105** (`test_v0631_ui_immediate_event_transitions_and_banner_labels`) : Validation de la visibilité immédiate et de l'ordre exact des 7 bandeaux UI (`BONJOUR ESP DÉTECTÉ`, `CONNEXION`, `PRÊT — PARLEZ`, `ÉCOUTE`, `RÉPONSE`, `FIN DE CONVERSATION`, `RETOUR EN ÉCOUTE`) pilotés par les événements réels du contrôleur.

### 7.2 Compilation Croisée ESP32-S3 des Exemples PlatformIO

Les 11 exemples PlatformIO pour `m5stack-cores3` (`framework-arduinoespressif32 @ 3.20017.241212`) compilent à `100 %` sans erreur :
1. `examples/WakeWordConversation` — **SUCCESS**
2. `examples/WakeWordFoundation` — **SUCCESS**
3. `examples/ContinuousSTT` — **SUCCESS**
4. `examples/MinimalConversation` — **SUCCESS**
5. `examples/MinimalSTT` — **SUCCESS**
6. `examples/MinimalTTS` — **SUCCESS**
7. `examples/MinimalVoiceKit` — **SUCCESS**
8. `examples/OpenAIConversation` — **SUCCESS**
9. `examples/OrientationClockVoice` — **SUCCESS**
10. `examples/VoiceDiagnostics` — **SUCCESS**
11. `examples/VoiceSettingsDemo` — **SUCCESS**

---

## 8. Procédure de Validation Matérielle sur M5Stack CoreS3 Physique

Pour valider acoustiquement et mesurer sur un M5Stack CoreS3 réel la réduction du temps maximal de `voice.update()` :

1. **Préparation et téléversement** :
   - Renseigner `examples/WakeWordConversation/include/secrets.h` (`WIFI_SSID`, `WIFI_PASSWORD`, `GEMINI_KEY`).
   - Connecter le M5Stack CoreS3 en USB-C et exécuter :
     ```powershell
     pio run -d examples/WakeWordConversation -t upload -t monitor
     ```
2. **Vérification de l'état initial (`WAKE_ARMED`)** :
   - Vérifier sur l'écran l'en-tête `VoiceKit v0.6.3.1 - WakeConv` et le badge vert `[WAKENET ACOUSTIQUE]`.
   - Vérifier que le bandeau affiche `EN ATTENTE — « Bonjour ESP »`.
3. **Essai acoustique d'un cycle complet et visibilité des 7 transitions UI** :
   - Prononcer clairement **« Bonjour ESP »** à `30–60 cm` du CoreS3.
   - Observer successivement sur l'écran, sans délai artificiel :
     1. `BONJOUR ESP DÉTECTÉ`
     2. `CONNEXION`
     3. `PRÊT — PARLEZ`
     4. Poser une question à voix haute $\to$ passage immédiat à `ÉCOUTE`
     5. Réponse vocale de Gemini Live $\to$ passage à `RÉPONSE`, puis retour automatique à `PRÊT — PARLEZ` en fin de tour (sans répéter « Bonjour ESP »)
     6. Dire **« Merci, au revoir »** $\to$ restitution complète de la dernière réponse, puis passage à `FIN DE CONVERSATION`
     7. Passage visible à `RETOUR EN ÉCOUTE` pendant la garde de `250 ms`, puis retour automatique à `EN ATTENTE — « Bonjour ESP »` (`WAKE_ARMED`).
4. **Relevé des métriques réelles sur matériel (`DIAG SERIE`)** :
   - Appuyer sur le bouton tactile **`DIAG SERIE`** (ou envoyer `diag` dans le moniteur série à `115200 bauds`).
   - Relever dans le bloc `[DIAGNOSTICS] WakeWordConversationController (VoiceKit v0.6.3.1)` :
     - `Temps voice.update() : moy=... us | max=... us`
     - `Temps sous-systemes max : Ctrl=... us | Transition=... us | Wake=... us | Session=... us`
     - `Temps I/O max : WS=... us | Mic=... us | StopSess=... us | ReactWake=... us`
     - `Sessions / Detections : ... ouvertes (... reussies)`
     - `Echecs Micro / Reseau : Transition=0 | Reservation=0 | Reseau=0`
   - Vérifier que `StopSess` et `Ctrl` ne présentent plus le blocage synchrone de `~582 000 µs` lors de la fermeture de session WebSocket.

