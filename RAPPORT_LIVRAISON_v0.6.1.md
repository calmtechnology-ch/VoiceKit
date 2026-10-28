# RAPPORT DE LIVRAISON — VoiceKit v0.6.1

## Correctif de Stabilité Temps Réel — Gemini Live, WebSocket TLS et Wi-Fi FastConnect

- **Dépôt principal** : `https://github.com/Philippe-AD/VoiceKit`
- **Cible matérielle** : M5Stack CoreS3 (ESP32-S3, 240 MHz Dual-Core, 16 Mo Flash, 8 Mo PSRAM OPI, Audio I2S AW88298 + ES7210)
- **Version de départ** : `v0.6.0` (commit de référence `0d1c65f79dfa88effb95a2af4655b5724f9b199d`)
- **Version livrée** : `v0.6.1` (branche corrective `fix/v0.6.1`)
- **Applications clientes de référence** : `ENV-Pro-Dashboard`, `VoiceLab`, `VoiceCommands`

---

## 1. Synthèse Exécutive

La version **VoiceKit v0.6.1** est une version strictement corrective de `v0.6.0` résolvant les trois anomalies de stabilité identifiées sur matériel réel dans `ENV-Pro-Dashboard/RAPPORT_LIVRAISON_v0.2.0.2.md` :

1. **Correction A — Réinitialisation polymorphe des métriques PCM entre tours (`IVoiceProvider` / `LiveSessionController`)** :
   - Suppression de la dépendance résiduelle de `LiveSessionController` envers le pointeur historique `_legacyClient` (qui restait à `nullptr` lors d'une initialisation polymorphe via `IVoiceProvider*`).
   - Élimination totale des fausses alertes `[LiveClient] ALERTE ORDRE: Bloc PCM recu APRES turnComplete` et du diagnostic erroné `0 ech decodes` à partir du 2ᵉ tour, tout en conservant la détection stricte des véritables blocs PCM tardifs.
2. **Correction B — Ordonnancement FreeRTOS pendant le handshake TLS (`EspWebSocketTransport`)** :
   - Suppression de la préemption prolongée (~1,55 s) de `loopTask` (Cœur 1, priorité 1) causée par `websocket_task` (`cfg.task_prio = 5`, `tskNO_AFFINITY`) pendant le handshake cryptographique mbedTLS.
   - Démarrage de `websocket_task` à la priorité `1` pendant le handshake TLS (puis relèvement à `2` une fois `WEBSOCKET_EVENT_CONNECTED` établi, et retour à `1` lors d'une déconnexion).
   - Sur matériel réel M5Stack CoreS3, l'interruption maximale apparente de la boucle principale pendant le handshake TLS passe de **~1 550 ms** (en `v0.6.0`) à **12 ms** (en `v0.6.1`, bien en dessous de la cible `< 100 ms`).
3. **Correction C — Repli Wi-Fi FastConnect et reconnexion asynchrones non-bloquants (`WiFiManager`)** :
   - Désactivation de l'écriture flash NVS synchrone redondante (`WiFi.persistent(false)`) et déport des appels Wi-Fi bloquants (`fallbackToNormalScan`, `WiFi.reconnect`, sauvegarde NVS `"vk_wifi"`) vers une tâche ouvrière FreeRTOS unique persistante (`vk_wifi_wkr`, Cœur 0, priorité 1).
   - Sur matériel réel M5Stack CoreS3, la durée maximale de `WiFiManager::tick()` lors d'un échec `FastConnect` suivi d'un repli vers scan normal passe de **~343 ms** (en `v0.6.0`) à **2,24 ms** (en `v0.6.1`), et `voice.update()` reste à **6,06 ms max**.

**Priorité absolue respectée** : Le sanctuaire audio VoxCore/VoiceKit (flux PCM 24 kHz, tampon circulaire PSRAM de 120 000 échantillons, 4 buffers DMA de 960 échantillons, tâche `AudioPlayerTask` Mode 2 à priorité 4, vidange matérielle complète et gestion STOP) est **100 % préservé sans aucune modification**.

---

## 2. Analyse des Causes Racines et Correctifs Appliqués

### 2.1 Correction A — Fausses alertes PCM après `turnComplete` et compteur `0 ech decodes`

#### Cause racine
Dans `VoxCoreVoice::begin()`, le contrôleur de session est initialisé via la surcharge polymorphe :
```cpp
_session.begin(_activeProvider, &_player);
```
Cette surcharge renseigne `_provider`, mais laisse le pointeur historique `_legacyClient` (`GeminiLiveClient*`) à `nullptr`. Or, dans `v0.6.0`, `LiveSessionController::sendTextMessage()`, `LiveSessionController::beginAudioTurn()`, `LiveSessionController::finishTurn()` et `LiveSessionController::printAudioEndCheck()` utilisaient encore `if (_legacyClient)` pour :
- appeler `resetTurnRxMetrics()` au début du tour ;
- lire `getPcmSamplesDispatchedTotal()` en fin de tour.

En conséquence :
1. `GeminiLiveClient::_turnCompleteReceived` restait à `true` après la fin du Tour #1.
2. Dès le premier bloc PCM du Tour #2, `GeminiLiveClient::handleIncomingMessage()` voyait `_turnCompleteReceived == true` et déclenchait à tort `[LiveClient] ALERTE ORDRE: Bloc PCM recu APRES turnComplete` sur chaque bloc du tour.
3. En fin de tour, `LiveSessionController` affichait `0 ech decodes` alors que tous les échantillons avaient été décodés et joués sans perte.

#### Correctif appliqué
- Extension rétrocompatible de `IVoiceProvider` (`src/core/IVoiceProvider.h`) avec des méthodes virtuelles dotées d'implémentations par défaut (préservant les mocks existants) :
  - `virtual void resetTurnRxMetrics() {}`
  - `virtual uint32_t getTurnDecodedSamples() const { return 0; }`
  - `virtual uint32_t getPostTurnCompleteBlocks() const { return 0; }`
  - `virtual uint32_t getPostTurnCompleteSamples() const { return 0; }`
  - `virtual bool isTurnCompleteReceived() const { return false; }`
- Implémentation dans `GeminiVoiceProvider`, `GeminiLiveClient` et `OpenAIRealtimeClient`.
- Conversion des compteurs de tour de `GeminiLiveClient` et `OpenAIRealtimeClient` en `std::atomic` pour garantir l'absence de course critique (data race) entre `websocket_task` et `loopTask`.
- Réinitialisation déterministe au début de chaque tour (`LiveSessionController::sendTextMessage()`, `LiveSessionController::beginAudioTurn()`, `connect()`, `sendTextMessage()`, `sendActivityStart()`), et remise à zéro des compteurs de session par tour via `LiveSessionController::resetTurnSessionMetrics()`.

---

### 2.2 Correction B — Préemption FreeRTOS durant le handshake TLS

#### Cause racine
Dans `src/core/EspWebSocketTransport.h`, le client WebSocket ESP-IDF était configuré avec `cfg.task_prio = 5`.
Dans le SDK `framework-arduinoespressif32` (ESP-IDF v4.4), la structure `esp_websocket_client_config_t` ne possède pas de champ `task_core` : `esp_websocket_client_start()` crée donc `"websocket_task"` avec `tskNO_AFFINITY`.
Pendant le handshake mbedTLS (validation de chaîne de certificats X.509 et calcul ECDHE, opération fortement CPU-bound pendant ~900 à 1 550 ms), dès que le Cœur 0 exécutait une tâche réseau (`wifi` prio 23, `tiT` prio 18) ou capteur (`bsec_acq` prio 2), l'ordonnanceur SMP FreeRTOS migrait `"websocket_task"` (priorité 5) sur le Cœur 1, préemptant continuellement `loopTask` (Cœur 1, priorité 1) sans partage de temps (car `5 > 1`).

#### Correctif appliqué
1. **Ordonnancement adaptatif de `websocket_task` (`src/core/EspWebSocketTransport.h`)** :
   - Démarrage de `websocket_task` avec `cfg.task_prio = DEFAULT_HANDSHAKE_TASK_PRIO` (`1`), égale à la priorité de `loopTask` (`1`).
   - Pendant le handshake TLS : lorsque le Cœur 0 est libre, `websocket_task` s'exécute sur le Cœur 0 en parallèle de `loopTask` (Cœur 1) ; lorsqu'elle partage le Cœur 1 avec `loopTask`, l'ordonnanceur FreeRTOS applique le *Round-Robin Time-Slicing* (quantum de 1 ms) entre les deux tâches de priorité 1, évitant toute famine de LVGL ou de la lecture tactile.
   - Dès réception de `WEBSOCKET_EVENT_CONNECTED` (fin du handshake TLS), la priorité de `websocket_task` est relevée à `DEFAULT_STEADY_TASK_PRIO` (`2`) via `vTaskPrioritySet(NULL, 2)` afin d'assurer une réception réactive des trames audio PCM 24 kHz en régime établi (tout en restant sous `AudioPlayerTask` à priorité 4).
   - Lors de `WEBSOCKET_EVENT_DISCONNECTED`, la priorité est immédiatement redescendue à `1` afin que toute reconnexion TLS ultérieure reste non-bloquante pour `loopTask`.
2. **Instrumentation non-bloquante (`ConnectionMetrics`)** :
   - Cœur et priorité de `websocket_task` (`wsTaskCore`, `wsTaskPriority`, `wsHandshakeTaskPriority`, `wsSteadyTaskPriority`) et de `loopTask` (`loopTaskCore`, `loopTaskPriority`).
   - Mesure de l'intervalle maximal apparent de la boucle principale pendant TLS (`maxLoopGapDuringTlsMs`) et global (`maxLoopGapMs`, `lastLoopGapMs`).
   - Mesure de la RAM interne libre et du plus grand bloc contigu pendant TLS (`freeInternalHeapDuringTls`, `minFreeInternalHeapDuringTls`, `largestInternalBlockDuringTls`).

---

### 2.3 Correction C — Blocage Wi-Fi lors du repli FastConnect

#### Cause racine
Lorsqu'une tentative rapide `FastConnect` (`WiFi.begin(_ssid, _pass, chan, bssid)`) expirait au bout de `fastConnectTimeoutMs`, `WiFiManager::tick()` exécutait directement `fallbackToNormalScan()` -> `WiFi.begin(_ssid, _pass)` de manière synchrone sur `loopTask`.
Dans `framework-arduinoespressif32` (`WiFiSTA.cpp`), `WiFi.begin(_ssid, _pass)` appelle successivement `esp_wifi_disconnect()`, `esp_wifi_set_config()` (qui écrit en flash NVS car `WiFi.persistent()` vaut `true` par défaut), `set_esp_interface_ip()` et `esp_wifi_connect()`, bloquant `loopTask` pendant **~343 ms**.

#### Correctif appliqué
1. **Désactivation de la persistance NVS redondante du driver Wi-Fi** :
   - Appel de `WiFi.persistent(false)` dans `WiFiManager::begin()` (puisque `WiFiManager` gère déjà son propre cache NVS `"vk_wifi"`).
   - Suppression des écritures NVS inutiles dans `saveFastConnectParams()` lorsque le triplet `(ssid, channel, bssid)` connecté est déjà identique au cache mémorisé.
2. **Machine à états explicite (`WiFiManagerState`) et tâche ouvrière unique persistante (`vk_wifi_wkr`)** :
   - Création une seule fois dans `WiFiManager::ensureWorkerTask()` d'une tâche FreeRTOS légère (`vk_wifi_wkr`, Cœur 0, priorité 1, pile 4 096 octets) et d'une file `xQueueCreate(4, sizeof(AsyncCmd))`, détruites proprement dans `WiFiManager::end()` et `~WiFiManager()`.
   - Lorsque `WiFiManager::tick()` déclenche un repli `fallbackToNormalScan(true)`, une reconnexion automatique `RECONNECT`, ou une sauvegarde NVS `SAVE_NVS_CACHE`, la commande est postée de façon non-bloquante (`xQueueSend(..., 0)`) à `vk_wifi_wkr`.
   - Protection par drapeau atomique `_asyncOpInProgress` interdisant tout lancement d'appels Wi-Fi concurrents.
3. **Instrumentation séparée des durées** :
   - `WiFiManager::getLastTickDurationUs()` / `getMaxTickDurationUs()` (`lastWifiTickDurationUs`, `maxWifiTickDurationUs` dans `ConnectionMetrics`).
   - `VoxCoreVoice::getLastUpdateDurationUs()` / `getMaxUpdateDurationUs()` (`lastUpdateDurationUs`, `maxUpdateDurationUs` dans `ConnectionMetrics`).

---

## 3. Fichiers Modifiés et API Ajoutées

### 3.1 Fichiers modifiés
| Fichier | Nature des modifications |
| :--- | :--- |
| `src/core/IVoiceProvider.h` | Ajout des méthodes virtuelles (avec défauts) de métriques RX par tour (Correction A) et de diagnostics TLS/FreeRTOS/RAM interne (Correction B). |
| `src/core/ILiveTransport.h` | Ajout des accesseurs virtuels de diagnostic FreeRTOS et mémoire interne TLS (Correction B). |
| `src/core/EspWebSocketTransport.h` | Priorité `1` pendant le handshake TLS puis `2` à la connexion (`WEBSOCKET_EVENT_CONNECTED`), retour à `1` à la déconnexion, capture atomique du cœur, de la priorité et de la RAM interne (Correction B). |
| `src/core/GeminiLiveClient.h` / `.cpp` | Conversion des métriques RX par tour en `std::atomic`, accesseurs `getTurnDecodedSamples()`, `getPostTurnCompleteBlocks()`, `getPostTurnCompleteSamples()`, réinitialisation en début de tour et exposition des diagnostics TLS (Corrections A & B). |
| `src/core/GeminiVoiceProvider.h` / `.cpp` | Délégation polymorphe des nouvelles méthodes `IVoiceProvider` vers `GeminiLiveClient` (Corrections A & B). |
| `src/core/OpenAIRealtimeClient.h` / `.cpp` | Comptabilisation atomique des échantillons PCM 24 kHz par tour, détection des blocs reçus après `response.done`, implémentation de `resetTurnRxMetrics()` et des diagnostics TLS (Corrections A & B). |
| `src/core/LiveSessionController.h` / `.cpp` | Remplacement complet des appels dépendants de `_legacyClient` par `_provider`, ajout de `resetTurnSessionMetrics()` et des accesseurs de tour (Correction A). |
| `src/core/WiFiManager.h` / `.cpp` | Machine à états `WiFiManagerState`, `WiFi.persistent(false)`, tâche ouvrière persistante `vk_wifi_wkr` sur Cœur 0, file asynchrone non-bloquante, cache NVS sans réécriture redondante et mesure de durée de `tick()` (Correction C). |
| `src/VoiceKitConfig.h` | Enrichissement rétrocompatible de `ConnectionMetrics` (cœurs/priorités FreeRTOS, `maxLoopGapDuringTlsMs`, RAM interne TLS, durées `update()` et `WiFiManager::tick()`). |
| `src/VoxCoreVoice.h` / `.cpp` | Mesure des intervalles de boucle principale pendant TLS et de la durée de `update()`, consolidation de `updateConnectionMetrics()`. |
| `src/core/VoiceHubConfig.h`, `library.json`, `library.properties` | Passage de la version à `0.6.1` (`VOICEKIT_VERSION = "v0.6.1"`). |
| `tests/native_mock/MockLiveTransport.h`, `tests/native_mock/WiFi.h` | Support de simulation pour les tests unitaires des Corrections B et C. |
| `tests/test_voicekit_all.cpp` | Ajout des Suites 80, 81 et 82 (180 nouvelles assertions vérifiées). |
| `CHANGELOG.md`, `README.md` | Documentation complète de la version `v0.6.1`. |

### 3.2 Compatibilité Ascendante
- **Aucune rupture d'API publique** : Toutes les méthodes existantes de `VoiceKit.h` et `VoxCoreVoice.h` sont intégralement conservées avec la même signature.
- Toutes les nouvelles méthodes de `IVoiceProvider` et `ILiveTransport` possèdent une implémentation virtuelle par défaut.

---

## 4. Résultats des Tests Unitaires

Exécution via `cmd /c tests\run_tests.bat` :
- **Suites exécutées** : **82 suites** (Suites 1 à 82), incluant :
  - **Suite 80 (`test_v061_correction_a_turn_rx_metrics_and_order_alerts`)** : 1 annonce TTS, 2 annonces TTS consécutives, 10 annonces TTS consécutives, arrêt `stop()` pendant génération, arrêt `stop()` pendant lecture, déconnexion pendant génération et reconnexion après erreur, détection d'un véritable bloc PCM tardif après `turnComplete`, modes STT et Conversation, fournisseur OpenAI Realtime.
  - **Suite 81 (`test_v061_correction_b_tls_scheduling_and_loop_diagnostics`)** : Vérification des priorités FreeRTOS de handshake (`1`) et régime établi (`2`), retour à `1` sur déconnexion, mesure de `maxLoopGapDuringTlsMs` et des métriques mémoire interne TLS.
  - **Suite 82 (`test_v061_correction_c_wifi_10_scenarios_and_tick_durations`)** : Validation des 10 scénarios Wi-Fi obligatoires (FastConnect 1er essai, BSSID indisponible + repli, changement de canal routeur, réseau absent + backoff sans attente active, retour réseau, reconnexion auto, redémarrage CoreS3 avec cache persistant, horloge déjà synchronisée, SNTP réellement nécessaire, fonctionnement vocal de la phrase de contrôle après 3 reconnexions, et mesure séparée des durées `WiFiManager::tick()` et `VoiceKit::update()`).
- **Bilan final** : **1 227 tests réussis, 0 échec**.

---

## 5. Résultats de Compilation PlatformIO & Empreinte Mémoire

Tous les exemples de la bibliothèque compilent à 100 % sans erreur pour la cible `m5stack-cores3` (`espressif32@6.9.0`) :

| Exemple PlatformIO | Statut | RAM Interne Utilisée | Flash Utilisée |
| :--- | :---: | :---: | :---: |
| `examples/MinimalTTS` | **SUCCESS** | 21,1 % (69 012 / 327 680 o) | 57,8 % (1 213 141 / 2 097 152 o) |
| `examples/MinimalSTT` | **SUCCESS** | 21,1 % (69 080 / 327 680 o) | 58,3 % (1 221 877 / 2 097 152 o) |
| `examples/ContinuousSTT` | **SUCCESS** | 21,1 % (69 208 / 327 680 o) | 73,2 % (1 534 677 / 2 097 152 o) |
| `examples/MinimalConversation` | **SUCCESS** | 21,1 % (69 096 / 327 680 o) | 58,3 % (1 223 249 / 2 097 152 o) |
| `examples/OpenAIConversation` | **SUCCESS** | 21,1 % (69 080 / 327 680 o) | 58,4 % (1 223 857 / 2 097 152 o) |
| `examples/VoiceDiagnostics` | **SUCCESS** | 21,6 % (70 676 / 327 680 o) | 59,4 % (1 244 949 / 2 097 152 o) |
| `examples/VoiceSettingsDemo` | **SUCCESS** | 21,1 % (69 064 / 327 680 o) | 58,1 % (1 218 977 / 2 097 152 o) |
| `examples/OrientationClockVoice` | **SUCCESS** | 49,1 % (160 892 / 327 680 o) | 78,1 % (1 638 865 / 2 097 152 o) |
| `examples/MinimalVoiceKit` | **SUCCESS** | 21,4 % (70 080 / 327 680 o) | 73,7 % (1 545 853 / 2 097 152 o) |

- **Allocation PSRAM (inchangée)** : Tampon circulaire `LiveAudioPlayer` de `120 000` échantillons 16 bits (`240 000` octets en PSRAM OPI) sur les 8 Mo disponibles (~3 % de la PSRAM).

---

## 6. Mesures Matérielles Avant / Après sur M5Stack CoreS3 (`COM4`)

### 6.1 Comparatif Avant (`v0.6.0`) / Après (`v0.6.1`)

| Indicateur mesuré sur M5Stack CoreS3 | VoiceKit v0.6.0 (Avant) | VoiceKit v0.6.1 (Après) | Gain / Statut |
| :--- | :---: | :---: | :---: |
| **Fausses alertes PCM après `turnComplete` (Tour #2+)** | 29 blocs signalés à tort | **0 fausse alerte (`postTurnCompleteBlocks = 0`)** | **100 % corrigé** |
| **Compteur d'échantillons décodés en fin de tour** | `0 ech decodes` (erroné) | **Exact (`231 361` ech Tour #1, `190 321` ech Tour #2)** | **100 % corrigé** |
| **Interruption max de `loopTask` pendant handshake TLS** | **~1 550 ms** (`maxTouchGap = 1 550 ms`) | **12 ms** (`maxLoopGapDuringTlsMs = 12 ms`) | **-99,2 % (< 100 ms cible)** |
| **Durée max de `WiFiManager::tick()` lors d'un repli FastConnect** | **~343 ms** (bloquant sur `loopTask`) | **2,24 ms** (`2 237 µs`) | **-99,3 %** |
| **Durée max de `voice.update()` lors d'un repli FastConnect** | **~343 ms** | **6,06 ms** (`6 058 µs`) | **-98,2 %** |
| **RAM interne libre pendant handshake TLS** | Non instrumenté | **119 212 o libres** (min **119 148 o**, max bloc **110 580 o**) | **Marge > 3,9x le seuil de 30 Ko** |

### 6.2 Validation Acoustique & Intégrité Audio Matérielle (Sanctuaire Audio)

Essai exécuté sur le M5Stack CoreS3 connecté sur `COM4` avec deux tours TTS successifs vers Google Gemini Live :
- **Tour #1 — Phrase de contrôle obligatoire** :
  > *« Cette phrase de contrôle permet de vérifier que chaque syllabe finale est parfaitement intelligible jusqu'au mot repère : alpha tango terminé. »*
  - Échantillons PCM 24 kHz décodés : `231 361` (~9,64 secondes d'audio).
  - Échantillons PCM 24 kHz joués jusqu'à vidange DMA complète : `231 361` (`0` rejet, `0` perte, `0` overflow, `bufferIntegrity = 1`).
  - Restitution complète et intelligible jusqu'aux dernières syllabes de *« alpha tango terminé »*.
- **Tour #2 — Deuxième annonce consécutive dans la même session** :
  > *« Deuxième annonce consécutive pour vérifier la réinitialisation des métriques sans fausse alerte : alpha tango terminé. »*
  - Échantillons PCM 24 kHz décodés : `190 321` (~7,93 secondes d'audio).
  - Échantillons PCM 24 kHz joués : `190 321` (`0` rejet, `0` perte, `0` overflow, `postTurnCompleteBlocks = 0`, `bufferIntegrity = 1`).
  - Aucune fausse alerte d'ordre, aucune coupure de fin de phrase.

---

## 7. Limitations Restantes Éventuelles

- Dans l'ESP-IDF v4.4 utilisé par `framework-arduinoespressif32` (`espressif32@6.9.0`), la structure `esp_websocket_client_config_t` ne comporte pas de champ `task_core` (introduit uniquement dans ESP-IDF v5.x). L'ordonnancement mis en place en `v0.6.1` (`cfg.task_prio = 1` pendant le handshake TLS puis `2` après `WEBSOCKET_EVENT_CONNECTED`) contourne proprement cette limitation du SDK sans modifier ESP-IDF, réduisant l'interruption apparente de `loopTask` de ~1 550 ms à 12 ms sur matériel réel tout en maintenant la validation complète des certificats racine TLS.
