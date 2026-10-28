# Rapport Technique & de Livraison — VoiceKit v0.6.2.1
## Corrective Wake Word Foundation — Fiabilité et performances (M5Stack CoreS3)

**Date** : 28 octobre 2026  
**Dépôt** : `https://github.com/Philippe-AD/VoiceKit`  
**Application cible future** : `https://github.com/Philippe-AD/CoreS3-VoiceUI-Demo` (LVGL 9 sur M5Stack CoreS3)  
**Version livrée** : `0.6.2.1` (`VOICEKIT_VERSION = "v0.6.2.1"`)  
**Cible matérielle** : M5Stack CoreS3 (ESP32-S3, microphone ES7210 I2S PCM 16 kHz / 16 bits mono, amplificateur AW88298 PCM 24 kHz)

---

## 1. Synthèse Exécutive

La version **VoiceKit v0.6.2.1** est une version corrective ciblée visant à durcir et fiabiliser la fondation locale de détection de mot de réveil (**Wake Word Foundation**, introduite en `v0.6.2`) avant son intégration dans l'application graphique LVGL 9 `CoreS3-VoiceUI-Demo`.

Toutes les règles impératives ont été strictement respectées :
- **Aucune refonte générale** et aucune modification de l'apparence des applications existantes.
- **100 % de compatibilité ascendante** avec les API publiques `v0.6.2`.
- **Préservation intégrale** des moteurs **Gemini Live** et **OpenAI Realtime**, des 4 modes vocaux (`TTS`, `STT`, `ContinuousSTT`, `Conversation`), des correctifs réseau/TLS `v0.6.1` et du sanctuaire audio **VoxCore Mode 2** (tâche dédiée, tampon PSRAM de 120 000 échantillons, quadruple DMA, vidage complet sans coupure de la syllabe finale).
- **Propriétaire unique du microphone ES7210** (`LiveAudioStreamer` + `HardwareManager`) et **zéro nouvelle tâche FreeRTOS**.
- **Honnêteté stricte des moteurs** : le moteur `TEST` est systématiquement présenté comme simulé (`Engine: TEST — SIMULATED`) et le moteur `WAKENET` indisponible sous Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7 ne déclenche jamais une reconnaissance fictive.

---

## 2. Défauts Confirmés dans la v0.6.2

L'audit approfondi du code source `v0.6.2` a confirmé 5 défauts techniques :

### Défaut 1 — Risque de concurrence et d'invalidation de pointeur dans `WakeWordAudioBridge` (Section 2)
- Dans `src/core/WakeWordAudioBridge.cpp`, lorsque la file circulaire SPSC (8 slots) était pleine et que la politique `DROP_OLDEST` était sélectionnée, le producteur (`pushAudioChunk()`, exécuté dans `live_mic_task` sur Core 1) modifiait directement `_tail` (`_tail.store(tail + 1)`) et écrasait immédiatement `_slots[head % _capacity]` (qui, lorsque `head - tail == _capacity`, est exactement le slot `_slots[tail % _capacity]`).
- Or, le consommateur (`WakeWordManager::update()`, exécuté dans `loopTask`) pouvait être simultanément en train de lire ce même slot via le pointeur retourné par `peekFrame()` pendant l'exécution de `IWakeWordEngine::process()`, avant d'appeler `consumeFrame()`. Cela créait une course de données (*data race*) sur `_tail` et une corruption potentielle des échantillons PCM en cours d'analyse.
- De plus, `clear()`, `setEnabled(false)`, `begin()` et `end()` ne disposaient pas d'un garde d'époque ou de compteur d'écrivains actifs pour invalider une écriture `pushAudioChunk()` déjà engagée au moment d'une suspension ou d'une désactivation concurrente.

### Défaut 2 — Absence de budget CPU borné dans `WakeWordManager::update()` (Section 3)
- Dans `src/core/WakeWordManager.cpp`, `WakeWordManager::update()` pouvait dépiler et traiter jusqu'à `_bridge.getCapacity()` (8 trames de 512 échantillons = 256 ms d'audio) en un seul appel.
- Si un moteur acoustique réel (tel que WakeNet ou microWakeWord) remplace le moteur `TEST`, traiter jusqu'à 8 trames dans un seul tour de `voice.update()` bloquerait la boucle principale LVGL (`lv_timer_handler()`) et dégraderait la fluidité tactile.

### Défaut 3 — Gestion incomplète des échecs de `stopLocalCapture()` lors des transitions (Section 4 & Section 5)
- Dans `LiveAudioStreamer::start(IVoiceProvider*)` et `WakeWordManager::stopCaptureIfNeeded()`, la valeur de retour de `_streamer->stopLocalCapture()` n'était pas vérifiée.
- Dans `LiveAudioStreamer::stopLocalCapture()`, `stopMicHardware()` était appelé même lorsque `waitForTaskTermination()` expirait (`_taskQuarantined = true`), et `checkQuarantinedTasks()` ne libérait ni les tampons `RECORDING` ni la réservation matérielle du microphone lors de la levée ultérieure de quarantaine de `live_mic_task`.
- Dans `WakeWordManager::disable()`, `_bridge.end()` et `_activeEngine->end()` étaient appelés sans vérifier si le producteur audio avait effectivement réussi à s'arrêter.

### Défaut 4 — Fuites de ressources sur échec partiel, score de confiance fictif et perte d'échantillons sur détection dans `WakeNetEngine` (Section 6)
- Dans `src/core/WakeNetEngine.h` :
  1. En cas d'échec intermédiaire dans `begin()` (`esp_srmodel_filter`, `esp_wn_handle_from_name` ou `_wakenet->create`), la liste `_models` allouée par `esp_srmodel_init("model")` n'était pas libérée via `esp_srmodel_deinit(_models)`.
  2. `process()` assignait un score de confiance inventé `res.confidence = 0.95f` alors que l'interface `esp_wn_iface_t::detect()` ne retourne qu'un indice d'état/mot sans probabilité flottante.
  3. Dans `feedFrameAdapter()`, lorsque `onFrame480` retournait `true` (détection sur les 480 premiers échantillons d'un bloc de 512), l'instruction `break;` interrompait immédiatement la boucle `while (offset < sampleCount)`, perdant les échantillons restants du bloc DMA (ex. les 32 derniers échantillons d'un bloc de 512).
  4. L'API ne distinguait pas séparément le support compilé (`VOICEKIT_HAS_ESP_SR_WAKENET`) et la présence effective de la partition/modèle en mémoire flash (`srmodels.bin`).

### Défaut 5 — Pollution des métriques RMS par une trame silencieuse fictive en mode `TEST` (Section 7)
- Dans `WakeWordManager::triggerTestDetection()` et `WakeWordManager::handleSerialCommand()`, lorsque le pont `_bridge` était vide au moment de l'appel, une trame synthétique `silentFrame[512] = {0}` avec `rms = 0` était injectée dans `_bridge.pushAudioChunk()`, incrémentant artificiellement `framesReceived` et écrasant `lastRms` avec `0`.

---

## 3. Corrections Effectuées

### 3.1 Sécurisation SPSC de `WakeWordAudioBridge` (`src/core/WakeWordAudioBridge.h/.cpp`)
1. **Garantie d'intégrité de `peekFrame()` et désactivation de `DROP_OLDEST`** :
   - La politique `DROP_OLDEST` est désactivée (`isOverflowPolicySupported(DROP_OLDEST) == false`) et tout appel à `setOverflowPolicy(DROP_OLDEST)` se replie proprement sur `DROP_NEWEST` tout en incrémentant le compteur de diagnostic `getUnsupportedPolicyRequestCount()`.
   - Invariant mathématique SPSC garanti : seul le consommateur (`update()`) modifie `_tail` via `consumeFrame()`, uniquement **après** le retour de `IWakeWordEngine::process()`. Tant que la file n'est pas pleine (`used < _capacity`), `head % _capacity != tail % _capacity` : le producteur ne peut jamais modifier ni invalider le slot actuellement consulté via `peekFrame()`. Lorsque la file est pleine (`used >= _capacity`), `pushAudioChunk()` rejette la nouvelle trame entrante en temps constant sans toucher aux slots existants, et incrémente `_framesDropped` et `_saturationCount`.
2. **Protection sans mutex bloquant des opérations concurrentes (`_activeWriters` et `_epoch`)** :
   - Ajout d'un compteur atomique `_activeWriters` (géré par garde RAII dans `pushAudioChunk()`) et d'un compteur de génération `_epoch`.
   - `setEnabled(false)`, `clear()`, `begin()` et `end()` incrémentent `_epoch`. Si une écriture `pushAudioChunk()` était en cours au moment d'une suspension ou d'un `clear()`, elle détecte à la fin du `memcpy` que `_epoch` ou `_enabled` a changé et abandonne la publication de `_head`, garantissant qu'aucune trame partiellement écrite ou post-suspension n'est jamais visible par le consommateur.
   - `end()` vérifie `_activeWriters == 0` avant de libérer `_slots`.

### 3.2 Budget CPU borné et fluidité LVGL (`src/core/WakeWordManager.h/.cpp`, `VoiceKitConfig.h`, `VoxCoreVoice.h/.cpp`, `VoiceKit.h`)
1. **Bornage configurable par `update()`** :
   - Ajout de `setMaxFramesPerUpdate(size_t)` / `getMaxFramesPerUpdate()` (défaut : `DEFAULT_MAX_FRAMES_PER_UPDATE = 2` trames au lieu de 8) et `setProcessingBudgetUs(uint32_t)` / `getProcessingBudgetUs()` (défaut : `DEFAULT_PROCESSING_BUDGET_US = 5000` µs), également configurables via `VoiceKitConfig` (`wakeWordMaxFramesPerUpdate`, `wakeWordProcessingBudgetUs`) et exposés sur `VoxCoreVoice` et `VoiceKit`.
   - À chaque itération de `WakeWordManager::update()`, le temps réel de chaque `process()` est mesuré en microsecondes. Si le cumul `updateProcDurUs` atteint ou dépasse `_processingBudgetUs`, la boucle s'arrête immédiatement après la trame courante et incrémente `processingBudgetExceeded` lorsque le budget a été franchi, préservant la réactivité de la boucle LVGL.
   - Note architecturale documentée : un flux temps réel à 16 kHz par blocs de 512 échantillons produit 1 trame toutes les 32 ms. Si un futur moteur WakeNet sur ESP-IDF 5.x nécessite plus de ~5–8 ms par trame et perturbe le rafraîchissement LVGL, il devra être déporté dans une tâche dédiée épinglée, sans l'introduire prématurément dans cette version.
2. **Diagnostics complets dans `WakeWordMetrics`** :
   - Exposition et mise à jour exacte de : `maxWakeProcessDurationUs`, `lastWakeProcessDurationUs`, `maxUpdateProcessDurationUs`, `lastUpdateProcessDurationUs`, `processingBudgetUs`, `processingBudgetExceeded`, `maxFramesPerUpdate`, `lastFramesProcessedInUpdate`, `queueSaturations`, `framesDropped`, `lastRms`, `peakRms`, `captureStopFailures`.

### 3.3 Arrêt et reprise du microphone (`src/core/LiveAudioStreamer.cpp`, `src/core/WakeWordManager.cpp`, `src/VoxCoreVoice.cpp`, `src/VoiceKit.cpp`)
1. **Vérification stricte de `stopLocalCapture()`** :
   - `LiveAudioStreamer::start(IVoiceProvider*)` vérifie désormais le booléen retourné par `stopLocalCapture()`. En cas d'échec, le démarrage du streaming est immédiatement refusé (`false`, `AudioHardwareStatus::DEVICE_BUSY`) sans tenter une seconde initialisation du microphone ES7210.
   - `WakeWordManager::stopCaptureIfNeeded()` retourne `bool` et incrémente `_metrics.captureStopFailures` en cas d'échec.
   - `VoxCoreVoice::say()`, `VoxCoreVoice::startListening()`, `ContinuousListeningController::start()` / `resume()` et `VoiceKit::startContinuousListening()` vérifient que la suspension Wake Word et l'arrêt de la capture locale ont réussi avant d'engager le nouveau mode ; en cas d'échec, ils refusent proprement l'opération, conservent un état d'erreur explicite (`VoiceState::ERROR_STATE` / `WakeWordState::ERROR`) et notifient le diagnostic.
2. **Non-blocage définitif et récupération après quarantaine** :
   - Dans `LiveAudioStreamer::stopLocalCapture()`, `stopMicHardware()` n'est plus appelé tant que `live_mic_task` est en quarantaine.
   - Dès que la tâche en quarantaine finit par se terminer, `checkQuarantinedTasks()` (ou un nouvel appel à `stopLocalCapture()`) libère les tampons `RECORDING`, appelle `stopMicHardware()` et relâche la réservation dans `HardwareManager`, permettant au microphone de repartir normalement sans jamais rester bloqué définitivement.

### 3.4 Machine à états Wake Word et sûreté mémoire (`src/core/WakeWordManager.cpp`)
1. **Transitions et idempotence** :
   - Vérification complète des transitions `DISABLED (INACTIVE) -> INITIALIZING -> ARMED`, `ARMED -> DETECTED -> SUSPENDED`, `SUSPENDED -> ARMED`, `ARMED -> ERROR`, et `ERROR -> DISABLED (INACTIVE) -> INITIALIZING -> ARMED`.
   - Les appels répétés à `enableWakeWord(true)`, `disableWakeWord()`, `suspendWakeWord()` et `resumeWakeWord()` sont strictement idempotents et ne génèrent aucune transition parasite.
2. **Réentrance des callbacks et sûreté mémoire** :
   - Ajout d'un garde `_inUpdate` dans `WakeWordManager::update()`.
   - Si un callback `onWakeWordDetected` déclenche immédiatement `listen()`, `say()`, `resumeWakeWord()` ou `disableWakeWord()`, la transition demandée par le callback est respectée sans être écrasée par la fin de `handleDetectionResult()`.
   - Lors de `disableWakeWord()`, `_bridge.setEnabled(false)` coupe immédiatement les écritures, puis `stopCaptureIfNeeded()` est vérifié : si l'arrêt du producteur audio échoue, `disable()` bascule en `WakeWordState::ERROR` et **ne détruit pas** la mémoire de `_bridge` ni de `_activeEngine` tant que la tâche audio est encore susceptible d'y accéder.

### 3.5 Durcissement de `WakeNetEngine` et métriques RMS authentiques (`src/core/WakeNetEngine.h`, `src/core/WakeWordManager.cpp`)
1. **Distinction à 4 niveaux dans `WakeNetEngine`** :
   - `isCompiledSupportAvailable()` : indique uniquement si les en-têtes/symboles ESP-SR sont compilés (`VOICEKIT_HAS_ESP_SR_WAKENET`).
   - `isModelAvailable()` : vérifie que la partition `model` (`srmodels.bin`) est accessible et contient effectivement le modèle `wn10_fr_bonjouresp`.
   - `isInitialized()` : indique si `begin()` a initialisé avec succès l'instance WakeNet.
   - `isRealRecognitionActive()` (sur `WakeWordManager` / `VoiceKit`) : vrai uniquement lorsque le moteur est non simulé (`!isSimulatedEngine()`), initialisé et en écoute active (`ARMED`).
2. **Libération des ressources, score de confiance et adaptateur `512 -> 480`** :
   - `WakeNetEngine::begin()` appelle `end()` en préambule et libère systématiquement `esp_srmodel_deinit(_models)` sur chaque chemin d'erreur.
   - `WakeNetEngine::process()` retourne `res.confidence = 0.0f` (aucun score fictif n'est inventé).
   - `WakeNetEngine::feedFrameAdapter()` traite l'intégralité des échantillons du bloc entrant sans `break` prématuré lors d'une détection, conservant tous les échantillons de queue dans `_accumulator`, et `reset()` remet `_accumulator` à zéro.
3. **Métriques RMS authentiques** :
   - `WakeWordManager::triggerTestDetection()` et `handleSerialCommand()` n'injectent plus de trame silencieuse fictive (`rms = 0`) dans `_bridge`. Lorsque `triggerTestDetection()` est appelé sans flux micro actif, `update()` interroge directement `_testEngine` sans polluer `framesReceived`, `lastRms` ni `peakRms`.

---

## 4. Fichiers Modifiés et Créés

| Fichier | Nature des modifications |
| :--- | :--- |
| `src/core/IWakeWordEngine.h` | Documentation de la désactivation de `DROP_OLDEST`, ajout de `isCompiledSupportAvailable()` et `isModelAvailable()`, enrichissement de `WakeWordMetrics` (budget CPU, RMS, `captureStopFailures`). |
| `src/core/WakeWordAudioBridge.h` | Ajout de `_activeWriters`, `_epoch`, `_framePeeked`, `_unsupportedPolicyRequests`, `isOverflowPolicySupported()`, et hook natif de test concurrent `setSimMidWriteHook()`. |
| `src/core/WakeWordAudioBridge.cpp` | Désactivation sécurisée de `DROP_OLDEST` (repli `DROP_NEWEST`), garde RAII `_activeWriters` et validation d'époque `_epoch` dans `pushAudioChunk()`, protection de `clear()`, `setEnabled()`, `begin()` et `end()`. |
| `src/core/WakeNetEngine.h` | Distinction à 4 niveaux, sonde de partition/modèle, correction des fuites `_models` dans `begin()`, suppression du score `0.95f` (`0.0f`), continuité sans perte de `feedFrameAdapter()`, remise à zéro de `_accumulator` dans `reset()`. |
| `src/core/TestWakeWordEngine.h` | Ajout de `setSimulatedProcessDurationUs()` (test natif) pour tester de manière déterministe le budget CPU de `update()`. |
| `src/core/WakeWordManager.h` | Ajout des paramètres de budget CPU (`setMaxFramesPerUpdate`, `setProcessingBudgetUs`), des accesseurs à 4 niveaux, de `_inUpdate`, et changement de signature `bool stopCaptureIfNeeded()`. |
| `src/core/WakeWordManager.cpp` | Implémentation de la boucle `update()` bornée en nombre de trames et en microsecondes, gestion stricte des échecs de `stopCaptureIfNeeded()`, sûreté mémoire dans `disable()`, idempotence et suppression de la trame fictive `silentFrame`. |
| `src/core/LiveAudioStreamer.cpp` | Vérification du retour de `stopLocalCapture()` dans `start()`, protection de `stopMicHardware()` sous quarantaine et libération complète des tampons/réservation micro lors de la levée de quarantaine dans `checkQuarantinedTasks()`. |
| `src/core/ContinuousListeningController.cpp` | Propagation immédiate de l'échec si `_voice->startListening()` échoue dans `start()` ou `resume()`. |
| `src/VoiceKitConfig.h` | Ajout de `wakeWordMaxFramesPerUpdate = 2` et `wakeWordProcessingBudgetUs = 5000`. |
| `src/VoxCoreVoice.h` / `src/VoxCoreVoice.cpp` | Exposition des réglages de budget CPU et des états de modèle Wake Word ; vérification de l'état d'erreur Wake Word après suspension dans `say()` et `startListening()`. |
| `src/VoiceKit.h` / `src/VoiceKit.cpp` | Exposition des méthodes v0.6.2.1 sur la façade `VoiceKit` et vérification de la suspension Wake Word dans `startContinuousListening()`. |
| `src/core/VoiceHubConfig.h` | Mise à jour de `VOICEKIT_VERSION` vers `"v0.6.2.1"`. |
| `examples/WakeWordFoundation/src/main.cpp` | Affichage explicite `Engine: TEST — SIMULATED`, diagnostics complets (saturation pont, budget CPU, échecs d'arrêt capture, transitions, RMS) et commande `'c'` de cycles d'activation/désactivation. |
| `tests/test_voicekit_all.cpp` | Mise à jour de la Suite 84 et ajout de 6 nouvelles suites correctives v0.6.2.1 (Suites 89 à 94). |
| `library.json`, `library.properties`, `README.md`, `CHANGELOG.md` | Mise à jour de la version vers `0.6.2.1` et documentation des correctifs. |
| `RAPPORT_v0.6.2.1.md` | Présent rapport technique détaillé. |

---

## 5. Résultats des Tests et Compilations de Non-Régression

### 5.1 Exécution de la Suite de Tests Unitaires Natifs (`.\tests\run_tests.bat`)
- **Avant modifications (référence v0.6.2 vérifiée par exécution réelle)** : 88 suites, **1 457 tests réussis, 0 échec**.
- **Après modifications v0.6.2.1** : **94 suites (Suites 1 à 94), 1 805 assertions réussies, 0 échec** (+348 nouvelles assertions dans les Suites 89 à 94).
- Détail des nouvelles suites ajoutées :
  - **Suite 89** : `WakeWordAudioBridge` — Durée de vie `peekFrame()` et 7 scénarios de concurrence (producteur > consommateur, file pleine, 50 saturations répétées pendant `process()`, suspension mi-écriture via hook concurrent, désactivation pendant acquisition, 10 redémarrages successifs, intégrité bit-à-bit PCM et séquences sur 4 tours complets d'anneau).
  - **Suite 90** : Budget CPU borné (`setMaxFramesPerUpdate(1)`, `setProcessingBudgetUs(2000)`) et comptabilisation de `processingBudgetExceeded`.
  - **Suite 91** : Échec de `stopLocalCapture()` lors des transitions vers STT, TTS, ContinuousSTT et Conversation, refus sans double initialisation, levée de quarantaine sans blocage définitif, et cycle complet `WAKE ARMED -> WAKE DETECTED -> WAKE SUSPENDED -> VOICE CONVERSATION -> VOICE FINISHED -> WAKE ARMED`.
  - **Suite 92** : Machine à états complète, callbacks réentrants déclenchant des opérations VoiceKit, idempotence des appels répétés et non-destruction de la mémoire du pont si l'arrêt du producteur échoue lors de `disableWakeWord()`.
  - **Suite 93** : `WakeNetEngine` — Continuité `512 -> 480` sans perte aux frontières sur détection en milieu de bloc, `reset()` de l'accumulateur, libération des ressources sur chaque échec d'initialisation, score de confiance `0.0f` non inventé, et distinction des 4 niveaux d'état.
  - **Suite 94** : Capture locale hors ligne sans clé API ni Wi-Fi, exactitude des métriques RMS (`lastRms`, `peakRms`) sans pollution par trame silencieuse fictive, et non-interférence avec `ContinuousSTT`.

### 5.2 Compilation Croisée PlatformIO (`esp32s3` / `m5stack-cores3`)
Tous les exemples et tutoriels cibles ont été compilés pour `m5stack-cores3` (`espressif32@6.9.0`, Arduino-ESP32 2.0.17, `-std=gnu++17 -DVOXCORE_AUDIO_MODE=2`) avec **0 erreur** :

| Exemple PlatformIO | Résultat | Utilisation RAM (statique) | Utilisation Flash |
| :--- | :---: | :---: | :---: |
| `examples/MinimalTTS` | **SUCCESS** | 16.4 % (53 840 B / 327 680 B) | 31.1 % (1 018 593 B / 3 276 800 B) |
| `examples/MinimalSTT` | **SUCCESS** | 16.5 % (53 968 B / 327 680 B) | 31.3 % (1 024 445 B / 3 276 800 B) |
| `examples/MinimalConversation` | **SUCCESS** | 16.5 % (54 024 B / 327 680 B) | 31.3 % (1 026 917 B / 3 276 800 B) |
| `examples/ContinuousSTT` | **SUCCESS** | 16.5 % (54 112 B / 327 680 B) | 31.5 % (1 032 825 B / 3 276 800 B) |
| `examples/OpenAIConversation` | **SUCCESS** | 16.5 % (53 960 B / 327 680 B) | 31.1 % (1 019 953 B / 3 276 800 B) |
| `examples/WakeWordFoundation` | **SUCCESS** | 16.4 % (53 856 B / 327 680 B) | 31.1 % (1 018 937 B / 3 276 800 B) |

---

## 6. Mesures de Performance Disponibles

- **Empreinte mémoire du pont `WakeWordAudioBridge`** : 8 slots × `sizeof(FrameSlot)` (1 040 octets) = **8 320 octets** (alloués une seule fois dans `begin()` et libérés à `disable()` après arrêt confirmé du producteur).
- **Coût dans le callback audio critique (`live_mic_task`)** :
  - 1 seule copie `memcpy` de 1 024 octets (512 échantillons 16 bits) + opérations atomiques sans verrou (< 15 µs sur ESP32-S3 à 240 MHz).
  - En cas de saturation (`DROP_NEWEST`), rejet immédiat en $O(1)$ sans copie (< 1 µs).
- **Coût dans `voice.update()` (`loopTask`)** :
  - 0 copie de trame côté consommateur (`peekFrame()` pointe directement sur le slot protégé).
  - Borné par défaut à `maxFramesPerUpdate = 2` et `processingBudgetUs = 5000` µs (5 ms max par appel à `voice.update()`), garantissant une cadence LVGL fluide (> 60 Hz avec le moteur `TEST`, et bornée si un moteur acoustique plus coûteux est branché).

---

## 7. Limitations Connues et Régressions

- **Aucune régression identifiée** sur les 94 suites de tests ni lors de la compilation croisée des exemples existants (`TTS`, `STT`, `ContinuousSTT`, `Conversation`, `Gemini Live`, `OpenAI Realtime`).
- **Limitations connues** :
  1. **Politique `DROP_OLDEST` désactivée** : Dans une file SPSC sans copie côté consommateur (`peekFrame()`), autoriser le producteur à avancer `_tail` pendant que le consommateur exécute `IWakeWordEngine::process()` sur le slot de tête est intrinsèquement incompatible avec l'absence de verrou bloquant. `DROP_OLDEST` est donc désactivé au profit de `DROP_NEWEST`, ce qui préserve les trames consécutives déjà en file et protège le pointeur détenu par `process()`.
  2. **Indisponibilité binaire d'ESP-SR WakeNet10 sous Arduino-ESP32 2.0.17 (ESP-IDF 4.4.7)** : Le modèle français `wn10_fr_bonjouresp` requiert ESP-SR ≥ 2.0 (ESP-IDF ≥ 5.0). Sur la plateforme `espressif32@6.9.0`, `WakeNetEngine` rapporte honnêtement `isCompiledSupportAvailable() == false` et `isModelAvailable() == false`, et fait basculer `WakeWordManager` en état explicite `ERROR` sans jamais simuler une détection.
  3. **Budget CPU d'un futur moteur neuronal réel** : Lorsque WakeNet10 sera exécuté sur une cible ESP-IDF ≥ 5.0, une inférence neuronale sur 30 ms d'audio peut consommer entre 3 ms et 12 ms de CPU sur ESP32-S3. Si cette durée sur matériel réel crée des micro-saccades pendant les animations LVGL 9, l'invocation de `_activeEngine->process()` pourra être déportée dans une tâche consommatrice dédiée lisant `_bridge` (le pont `WakeWordAudioBridge` étant déjà conçu en SPSC inter-tâches).

---

## 8. Opérations Restant à Valider sur M5Stack CoreS3 Physique

Conformément aux règles d'honnêteté d'ingénierie, seuls les tests unitaires natifs (`1 805` assertions) et les compilations croisées PlatformIO ont été exécutés dans cet environnement sans carte connectée. La stabilité matérielle complète devra être confirmée sur **M5Stack CoreS3 physique** par les opérations suivantes :

1. **Téléversement et exécution de `examples/WakeWordFoundation`** :
   - Vérifier sur le moniteur série (`115200` bauds) et sur l'écran IPS l'affichage `Engine: TEST — SIMULATED` et l'état `ARMED` hors ligne (sans Wi-Fi).
   - Parler devant le microphone ES7210 et exécuter la commande série `m` pour vérifier que `lastRms` et `peakRms` varient proportionnellement au niveau sonore ambiant réel et que `Audio Push max` reste < 50 µs.
   - Exécuter la commande `c` (5 cycles rapides `disable -> enable -> suspend -> resume`) et vérifier que `CaptureStopFailures = 0`, `Errors = 0`, et que la mémoire `minFreeInternalHeap` reste stable.
   - Exécuter la commande `w` pour vérifier le passage explicite en état `ERROR` avec `WAKENET` sous IDF 4.4, puis `t` pour revenir à `TEST` (`ARMED`).
2. **Validation acoustique de la transition Wake Word $\leftrightarrow$ Conversation / TTS sur matériel** :
   - Sur CoreS3 connecté au Wi-Fi, déclencher un réveil suivi immédiatement d'un tour `Conversation` ou d'une annonce `say()`, et vérifier l'absence de clic/conflit I2S entre le microphone ES7210 et l'amplificateur AW88298 ainsi que le retour automatique à `ARMED` après le délai anti-écho (`250 ms`).
3. **Validation de la fluidité tactile LVGL 9 dans `CoreS3-VoiceUI-Demo`** :
   - Vérifier que `maxUpdateProcessDurationUs` et `maxLoopGapMs` restent bornés pendant le balayage tactile de l'écran LVGL lorsque la capture locale Wake Word tourne en tâche de fond.

