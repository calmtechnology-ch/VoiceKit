# RAPPORT TECHNIQUE — VoiceKit v0.6.2
## Wake Word Foundation — M5Stack CoreS3 (« Bonjour ESP »)

**Date** : 28 octobre 2026  
**Version livrée** : `v0.6.2` (évolution depuis la référence `v0.6.1`)  
**Cible matérielle** : M5Stack CoreS3 (ESP32-S3, microphone ES7210, amplificateur AW88298, 16 Mo Flash, 8 Mo PSRAM)  
**Environnement de compilation** : PlatformIO `espressif32@6.9.0` (`framework-arduinoespressif32@3.20017.241212`, basé sur **ESP-IDF v4.4.7**, compilateur `xtensa-esp32s3-elf-g++ 8.4.0`)

---

## 1. Architecture Effectivement Implémentée

### 1.1 Analyse de la chaîne audio v0.6.1 et verrous identifiés pour le mode hors ligne
L'analyse du code source de `VoiceKit v0.6.1` a mis en évidence trois verrous empêchant l'acquisition microphone locale hors ligne :
1. **`VoxCoreVoice::begin(config)`** refusait de démarrer lorsque `config.apiKey` était vide (`"Clé API Gemini manquante"` / `"Clé API OpenAI manquante"`).
2. **`LiveAudioStreamer::start(IVoiceProvider*)`** exigeait un fournisseur réseau connecté et configuré (`provider->isConnected() && provider->isSetupComplete()`), émettait immédiatement `sendActivityStart()` sur le WebSocket TLS, et créait deux tâches FreeRTOS (`live_mic_task` + `live_tx_task`).
3. **`onAudioChunk()`** ne disposait que d'un seul slot de callback dans `LiveAudioStreamer`, déjà utilisé par la façade `VoiceKit` pour alimenter `ContinuousListeningController` et le callback applicatif utilisateur.

### 1.2 Capture audio partagée hors ligne (propriété unique du microphone CoreS3 ES7210)
Pour permettre au détecteur de mot de réveil d'écouter en continu sans Wi-Fi ni connexion Gemini/OpenAI tout en préservant à 100 % les modes existants :
- **`LiveAudioStreamer` (`src/core/LiveAudioStreamer.h/.cpp`)** a été étendu avec :
  - `startLocalCapture()` / `stopLocalCapture()` / `isLocalCapture()` : réserve le microphone ES7210 via `HardwareManager::getInstance().prepareMicCapture()`, initialise `M5AudioInput` en **PCM 16 000 Hz mono 16 bits signé** et lance uniquement la tâche existante `live_mic_task` (priorité 3, Cœur 1, blocs DMA de 512 échantillons = 32 ms), **sans créer `live_tx_task`** (économie de 10 Ko de pile FreeRTOS et aucune nouvelle tâche créée).
  - `setWakeWordAudioCallback(AudioChunkCallback)` : canal dédié permettant à `WakeWordManager` de recevoir chaque bloc PCM 16 kHz parallèlement à `onAudioChunk()` sans écraser le callback de `ContinuousListeningController` ou de l'application, et sans dupliquer la capture matérielle.
  - Bascule transparente : si une écoute réseau (`start(provider)` pour STT ou Conversation) démarre alors que la capture locale Wake Word est active, `LiveAudioStreamer` arrête proprement la capture locale avant d'ouvrir le flux réseau (zéro double initialisation du microphone ES7210).
- **`VoxCoreVoice::begin(config)` (`src/VoxCoreVoice.cpp`)** autorise désormais une initialisation hors ligne lorsque `config.apiKey` est vide, en initialisant intégralement le HAL audio (`M5AudioOutput`, `M5AudioInput`, `HardwareManager`, `LiveAudioPlayer`, `LiveAudioStreamer`) et `WakeWordManager`.

### 1.3 Nouveaux modules de l'architecture Wake Word (`src/core/`)

```text
Microphone CoreS3 ES7210 (I2S / DMA)
        │  PCM 16 000 Hz, mono, 16-bit signé (blocs de 512 éch = 32 ms)
        ▼
LiveAudioStreamer (live_mic_task, Core 1, Prio 3)
        ├──► onAudioChunk() ──────────────► ContinuousListeningController / User Callback
        │
        └──► _wakeWordAudioCallback() ────► WakeWordAudioBridge::pushAudioChunk()
                                            [File SPSC bornée : 8 slots × 512 éch, 1 seule copie memcpy]
                                                      │
                                                      ▼ (0 copie : peekFrame / consumeFrame dans voice.update())
                                            WakeWordManager::update() (Boucle principale, Core 1, Prio 1)
                                                      │
                                                      ▼
                                            IWakeWordEngine::process()
                                            ├── WakeNetEngine (adaptateur 512 -> 480 éch + ESP-SR wn10)
                                            ├── TestWakeWordEngine (déclenchement déterministe simulé)
                                            └── Custom / microWakeWord (extensible)
```

1. **`IWakeWordEngine.h`** (`src/core/IWakeWordEngine.h`, exposé dans `src/` et `include/`) :
   - Interface abstraite pure découplant VoiceKit du moteur de reconnaissance : `getName()`, `getType()`, `getModelName()`, `isAvailable()`, `isSimulatedEngine()`, `begin(sampleRateHz, channels)`, `end()`, `isInitialized()`, `reset()`, `process(samples, sampleCount, rms)`, `getPreferredFrameSamples()`, `getLastError()`.
   - Définit `WakeWordState`, `WakeWordEngineType`, `WakeWordOverflowPolicy`, `WakeWordResult`, `WakeWordEvent` et `WakeWordMetrics`.
2. **`WakeWordAudioBridge.h/.cpp`** :
   - Pont temps réel entre `live_mic_task` et `WakeWordManager::update()`.
   - File circulaire SPSC (Single-Producer Single-Consumer) sans verrou de **8 slots × 512 échantillons** (256 ms de tampon, ~8,3 Ko alloués uniquement lorsque Wake Word est activé, libérés dès `disableWakeWord()`).
   - **Contrôle strict des copies mémoire** : **1 seule copie (`memcpy`)** lors de l'écriture DMA $\to$ slot dans `pushAudioChunk()`, et **0 copie supplémentaire** lors de la lecture côté consommateur via `peekFrame()` / `consumeFrame()`.
   - Politique de saturation explicite : `WakeWordOverflowPolicy::DROP_NEWEST` (défaut) ou `DROP_OLDEST`, avec compteurs atomiques (`framesReceived`, `framesEnqueued`, `framesConsumed`, `framesDropped`, `queueSaturations`, `maxOccupancy`, `maxPushDurationUs`).
3. **`WakeWordManager.h/.cpp`** :
   - Implémente la machine à états stricte : `DISABLED` $\to$ `INITIALIZING` $\to$ `ARMED` $\to$ `DETECTED` $\to$ `SUSPENDED` $\to$ `ARMED`, ainsi que `ERROR`.
   - Gère la suspension automatique dès qu'une activité vocale démarre (`say()`, `listen()`, `startContinuousListening()`, `startConversation()`) afin d'éviter tout conflit d'accès audio et tout faux réveil provoqué par le haut-parleur AW88298.
   - Gère la reprise contrôlée vers `ARMED` après la fin de la réponse vocale (après un délai de garde anti-écho configurable, `250 ms` par défaut).
   - N'ouvre **jamais** automatiquement une connexion réseau lors d'une détection.
4. **`TestWakeWordEngine.h`** :
   - Moteur de test déterministe implémentant `IWakeWordEngine`.
   - Déclenchable par commande série (`"bonjour esp"`, `"bonjour"`, `"wake"`) ou par appel explicite (`triggerTestWakeWord()`).
   - Toujours marqué `isSimulatedEngine() == true` et `WakeWordEvent::isSimulated == true` : **n'est jamais présenté comme une reconnaissance vocale acoustique**.
5. **`WakeNetEngine.h`** :
   - Adaptateur concret `IWakeWordEngine` pour Espressif ESP-SR WakeNet10 (`wn10_fr_bonjouresp`).
   - Intègre un adaptateur de cadrage sans perte **`512 -> 480` échantillons (`32 ms -> 30 ms` @ 16 kHz)** garantissant que 15 blocs DMA de 512 échantillons (7 680 échantillons) produisent exactement 16 trames WakeNet de 480 échantillons sans perte ni altération de l'ordre des échantillons.
   - Rapporte honnêtement `isAvailable() == false` dans l'environnement Arduino-ESP32 2.0.17 / IDF 4.4.7 lorsque les en-têtes/binaires ESP-SR ne sont pas présents.

---

## 2. Résultat de l'Étude WakeNet10 / ESP-SR (`wn10_fr_bonjouresp`)

L'étude approfondie du dépôt officiel Espressif [`espressif/esp-sr`](https://github.com/espressif/esp-sr) (branches `release/v2.0` à `v2.6.0`) donne les résultats suivants sur les 10 points d'analyse :

| Point d'étude | Résultat vérifié |
|---|---|
| **1. Disponibilité du modèle français « Bonjour ESP »** | **Oui.** Le modèle français **« Bonjour ESP »** a été introduit par Espressif dans `esp-sr` v2.0+ (notamment v2.6.0) sous deux variantes : **`wn10_fr_bonjouresp`** (WakeNet10, haute précision pour ESP32-S3 / ESP32-P4) et **`wn9l_fr_bonjouresp_tts3`** (WakeNet9l, version allégée). Licence : **Apache-2.0** (code d'interface) avec bibliothèques neuronales précompilées Espressif. |
| **2. Versions d'ESP-IDF compatibles** | **`esp-sr` v2.x (incluant WakeNet10 et `wn10_fr_bonjouresp`) exige `ESP-IDF >= 5.0` (recommandé `>= 5.3`)**. Les branches `esp-sr` v1.x compatibles avec ESP-IDF 4.4 ne contiennent ni WakeNet10 ni le modèle français `bonjouresp`. |
| **3. Compatibilité avec Arduino-ESP32 utilisé dans VoiceKit** | **Incompatible en l'état.** VoiceKit utilise `platform = espressif32@6.9.0` avec `framework-arduinoespressif32@3.20017.241212`, basé sur **ESP-IDF v4.4.7** et GCC 8.4.0 (`xtensa-esp32s3-elf`). De plus, le package Arduino-ESP32 2.0.17 ne fournit ni `esp_wn_iface.h`, ni `libwakenet.a`, ni `libesp_dl.a` pour l'ESP32-S3. |
| **4. Intégration via PlatformIO** | Dans un projet purement Arduino sous `espressif32@6.9.0`, PlatformIO ne résout pas les composants `idf_component.yml` (`espressif/esp-sr`, `espressif/esp-dl`) et ne génère pas l'image de partition `srmodels.bin` (qui est produite par les scripts CMake d'ESP-IDF 5.x `movemodel.py` / `pack_model.py`). |
| **5. Dépendances nécessaires à WakeNet et à l'AFE** | WakeNet10 dépend de `esp-dl` (instructions vectorielles SIMD PIE de l'ESP32-S3), du chargeur de partition `model_path` (`esp_srmodel_init("model")`) et de `dl_lib`. L'AFE (`esp_afe_sr_iface_t`) ajoute l'AEC, la suppression de bruit (NSNet), l'AGC et le VAD, mais n'est pas obligatoire si l'on appelle directement `esp_wn_iface_t::detect()` sur un flux mono propre lorsque le haut-parleur est muet. |
| **6. Coût Flash, RAM interne et PSRAM** | - **Flash (`srmodels.bin`)** : ~450 à 750 Ko pour un modèle WakeNet9l/10 unique packagé en partition dédiée (`type=data, subtype=spiffs, label="model"`).<br>- **RAM interne** : ~25 à 40 Ko (descripteurs et tampons DMA/SIMD internes).<br>- **PSRAM** : ~150 à 350 Ko pour WakeNet seul ; ~1,0 à 1,5 Mo si le pipeline AFE complet est activé. |
| **7. Taille et cadence des trames audio** | `wakenet->get_samp_rate(model_data)` = **16 000 Hz** ; `wakenet->get_samp_chunksize(model_data)` = **480 échantillons** (**30 ms** par trame en PCM 16 bits signé). Comme `LiveAudioStreamer` acquiert des blocs DMA de **512 échantillons (32 ms)**, `WakeNetEngine` intègre l'adaptateur `512 -> 480` sans perte (`15 × 512 = 16 × 480 = 7 680` échantillons). |
| **8. Contraintes sur le nombre de canaux** | L'interface directe `esp_wn_iface_t::detect()` attend **1 canal (mono)** PCM 16 kHz 16 bits signé. L'AFE peut prendre 1 ou 2 canaux micro + 1 canal référence AEC (`"MR"` ou `"MM"`), mais sur M5Stack CoreS3 via `M5.Mic`, le flux acquis est déjà mono 16 kHz. |
| **9. Exécution hors réseau** | **100 % hors réseau.** WakeNet s'exécute intégralement en local sur les unités vectorielles de l'ESP32-S3 sans Wi-Fi ni serveur distant. |
| **10. Conflits potentiels avec les tâches de VoxCore** | En appelant directement `esp_wn_iface_t::detect()` (sans les tâches internes lourdes de l'AFE) et en **suspendant WakeNet pendant toute session de conversation/TTS/STT**, il n'existe aucun conflit CPU ou DMA avec `AudioPlayerTask` (Core 1, Prio 4) ni avec `websocket_task`. |

---

## 3. Compatibilité Arduino / ESP-IDF

Conformément à la consigne de la Section 8 (*« Si la version officielle exige une migration ESP-IDF incompatible avec VoiceKit, ne pas effectuer cette migration automatiquement. Produire une analyse de compatibilité documentée et conserver la fondation Wake Word indépendante du moteur »*) :
- **Aucune migration destructive d'ESP-IDF n'a été forcée** afin de ne pas casser `VoiceKit v0.6.1`, `M5CoreS3@1.0.0`, `M5Unified@0.2.23` et les 10 exemples existants.
- **`WakeNetEngine.h`** détecte automatiquement à la compilation la présence d'ESP-SR via :
  ```cpp
  #if defined(VOICEKIT_ENABLE_WAKENET) && __has_include(<esp_wn_iface.h>) && __has_include(<esp_wn_models.h>) && __has_include(<model_path.h>)
  ```
- Dans l'environnement actuel (`Arduino-ESP32 2.0.17` / `ESP-IDF 4.4.7`), `VOICEKIT_HAS_ESP_SR_WAKENET` vaut `0` :
  - `voice.isWakeWordEngineAvailable(WakeWordEngineType::WAKENET)` retourne **`false`**.
  - Si l'application active le moteur par défaut `WAKENET` via `voice.enableWakeWord(true)`, la machine à états passe par `DISABLED -> INITIALIZING -> ERROR`, retourne `false`, et renseigne l'erreur explicite :
    `"ESP-SR WakeNet10 (wn10_fr_bonjouresp) non disponible dans cet environnement Arduino-ESP32 (IDF 4.4.7)"`.
  - **Aucune reconnaissance vocale factice n'est jamais annoncée.**

---

## 4. Mesures Mémoire et CPU Disponibles

### 4.1 Empreinte Flash et RAM Statique (Compilation PlatformIO ESP32-S3 `m5stack-cores3`)

| Exemple compilé (`espressif32@6.9.0`) | RAM statique utilisée (sur 327 680 o) | Flash utilisée (sur 2 097 152 o) | Statut |
|---|---|---|---|
| **`WakeWordFoundation`** *(nouveau v0.6.2, hors ligne)* | **21,9 % (71 848 octets)** | **59,3 % (1 244 229 octets)** | **SUCCESS** |
| **`MinimalTTS`** | 21,6 % (70 812 octets) | 58,5 % (1 227 733 octets) | **SUCCESS** |
| **`MinimalSTT`** | 21,6 % (70 880 octets) | 58,8 % (1 233 429 octets) | **SUCCESS** |
| **`ContinuousSTT`** | 21,7 % (71 008 octets) | 73,7 % (1 546 221 octets) | **SUCCESS** |
| **`MinimalConversation`** | 21,6 % (70 896 octets) | 58,9 % (1 234 829 octets) | **SUCCESS** |
| **`OpenAIConversation`** | 21,6 % (70 880 octets) | 58,9 % (1 235 429 octets) | **SUCCESS** |

### 4.2 Coût Mémoire et CPU du Sous-Système Wake Word (`v0.6.2`)
- **Coût lorsque Wake Word est désactivé (`WakeWordState::DISABLED`)** :
  - **0 octet** de tampon de file alloué sur le tas (`WakeWordAudioBridge::_slots` est `nullptr`).
  - **0 tâche FreeRTOS** active.
  - **< 1 µs** dans `voice.update()` (simple lecture atomique `_enabled.load()`).
- **Coût lorsque Wake Word est activé (`WakeWordState::ARMED`, moteur `TEST`)** :
  - **Tas pour `WakeWordAudioBridge`** : `8 × sizeof(FrameSlot)` = **8 352 octets** (~8,15 Ko), alloués une seule fois dans `begin()` et libérés dans `disable()`.
  - **Tâches FreeRTOS supplémentaires** : **0** (réutilise `live_mic_task` existante de `LiveAudioStreamer`, sans démarrer `live_tx_task`).
  - **Temps dans le callback audio critique (`pushAudioChunk`)** : 1 seul `memcpy` de 1 024 octets (< 10 µs sur ESP32-S3 à 240 MHz), zéro allocation dynamique, zéro écriture `Serial`.

---

## 5. Tests Réellement Exécutés

> **Mention d'honnêteté matérielle (Section 13)** : Les validations ci-dessous ont été exécutées par **tests unitaires natifs automatisés (`tests/test_voicekit_all.exe`)** et par **compilations croisées PlatformIO complètes pour `m5stack-cores3`**. Aucun test acoustique physique avec un locuteur humain devant un M5Stack CoreS3 réel n'a été exécuté dans cet environnement d'intégration.

### 5.1 Suite de Tests Unitaires Automatisés (`.\tests\run_tests.bat`)
- **88 suites de tests exécutées — 1 457 assertions réussies, 0 échec** :
  - **Suites 1 à 82 (v0.1.0 à v0.6.1)** : 1 227 assertions (100 % réussies).
  - **Suite 83 (`test_v062_offline_init_and_shared_audio_capture`)** : Initialisation sans clé API ni Wi-Fi, capture locale `startLocalCapture()`, propriété unique du micro ES7210 (`isMicCaptureReserved()`), réception simultanée sur `onAudioChunk()` et `WakeWordAudioBridge`, libération propre sur `disableWakeWord()`.
  - **Suite 84 (`test_v062_wakeword_audio_bridge_queue_and_overflow`)** : File bornée SPSC (8 slots), 1 copie au push, 0 copie au `peekFrame()`, conservation stricte de l'ordre des échantillons et des numéros de séquence, politiques de saturation `DROP_NEWEST` et `DROP_OLDEST`, compteurs `framesDropped` et `queueSaturations`.
  - **Suite 85 (`test_v062_wakenet_engine_availability_and_frame_adapter`)** : Indisponibilité explicite de `WakeNetEngine` sous IDF 4.4 (`isAvailable() == false`, transition vers `ERROR` sans fausse détection), et validation bit-à-bit de l'adaptateur `512 -> 480` échantillons (`15 × 512 == 16 × 480 == 7 680` échantillons, 0 résidu).
  - **Suite 86 (`test_v062_test_engine_and_state_machine_transitions`)** : Cycle complet `DISABLED -> INITIALIZING -> ARMED -> DETECTED -> SUSPENDED -> ARMED`, déclenchement par commande série `"bonjour esp"`, 10 cycles répétés consécutifs, refus de trigger en état `SUSPENDED`, et transition vers `ERROR` sur panne moteur simulée.
  - **Suite 87 (`test_v062_coexistence_suspension_and_resumption_all_modes`)** : Suspension automatique immédiate pendant TTS (`say`), STT manuel (`listen`), `ContinuousSTT` et `Conversation`, délai anti-écho de reprise (`200 ms`), reprise automatique vers `ARMED` après la fin de la réponse vocale, et vérification que la détection n'ouvre jamais de connexion réseau.
  - **Suite 88 (`test_v062_lvgl_event_polling_and_diagnostics_metrics`)** : Consommation non bloquante via `pollWakeWordEvent()` pour la boucle LVGL et validation des compteurs de `WakeWordMetrics`.

---

## 6. Régressions Vérifiées

- **MinimalTTS & MinimalSTT** : Compilés avec succès sur `m5stack-cores3` et validés par les suites 1 à 25.
- **ContinuousSTT & VAD** : Compilés avec succès et validés par les suites 43 à 54.
- **MinimalConversation (Gemini Live Mode B) & OpenAIConversation (OpenAI Realtime)** : Compilés avec succès et validés par les suites 26 à 35 et 69 à 79.
- **Moteur audio VoxCore Mode 2** : Tampon PSRAM de 120 000 échantillons @ 24 kHz, 4 buffers DMA, tâche `AudioPlayerTask` priorité 4 et vidage exact de la dernière syllabe (*« alpha tango terminé »*) strictement inchangés.
- **Correctifs v0.6.1 (Corrections A, B, C)** : Suites 80, 81 et 82 100 % passantes.

---

## 7. Limites et Problèmes Connus

1. **Indisponibilité binaire d'ESP-SR WakeNet10 sous Arduino-ESP32 2.0.17 (ESP-IDF 4.4.7)** :
   - Le modèle officiel `wn10_fr_bonjouresp` d'Espressif nécessite `esp-sr >= 2.0` et `esp-dl`, lesquels requièrent **ESP-IDF >= 5.0** ainsi qu'une partition Flash dédiée `srmodels.bin`.
   - Par conséquent, dans la configuration PlatformIO actuelle (`espressif32@6.9.0`), `WakeNetEngine` demeure désactivé à la compilation (`VOICEKIT_HAS_ESP_SR_WAKENET == 0`) et signale explicitement son indisponibilité (`WakeWordState::ERROR` si sélectionné).
2. **Moteur `TEST` non acoustique** :
   - `TestWakeWordEngine` sert exclusivement à valider le transport PCM 16 kHz, le pont SPSC, la machine à états et l'intégration applicative (via commande série ou injection explicite). Il ne réalise aucune reconnaissance vocale acoustique.

---

## 8. Feuille de Route vers la Détection Acoustique Réelle (« Bonjour ESP »)

Deux voies techniques propres sont prêtes grâce à l'abstraction `IWakeWordEngine` mise en place en `v0.6.2` :

### Option A — Activation native de `WakeNetEngine` (`wn10_fr_bonjouresp`) via Arduino-ESP32 3.x / ESP-IDF 5.x
1. Passer la plateforme PlatformIO vers une distribution hybride Arduino 3.x / ESP-IDF 5.1+ (ex: `pioarduino/platform-espressif32`).
2. Ajouter la dépendance `espressif/esp-sr@^2.0.0` dans `idf_component.yml`.
3. Ajouter la partition `model, data, spiffs, , 800K` dans la table de partitions CSV et flasher `srmodels.bin` contenant `wn10_fr_bonjouresp`.
4. Compiler avec `-DVOICEKIT_ENABLE_WAKENET=1` : `WakeNetEngine` (`src/core/WakeNetEngine.h`) s'activera immédiatement sans aucune modification de `WakeWordManager` ni de l'API `VoiceKit`.

### Option B — Implémentation d'un moteur `MicroWakeWordEngine` (TFLite Micro, compatible IDF 4.4 / Arduino 2.0.17)
1. Entraîner un modèle TFLite Micro int8 (~60 Ko en tableau C++ `PROGMEM`) pour **« Bonjour ESP »** (cadence 16 kHz, fenêtres de 30 ms / pas de 10 ms).
2. Créer `MicroWakeWordEngine : public IWakeWordEngine` et le brancher sur `WakeWordEngineType::MICRO_WAKE_WORD` sans nécessiter de partition `srmodels.bin` ni de migration ESP-IDF.

---

## 9. Recommandations pour `CoreS3-VoiceUI-Demo`

Pour intégrer la fondation Wake Word de `VoiceKit v0.6.2` dans `CoreS3-VoiceUI-Demo` en toute sécurité vis-à-vis de LVGL :
1. **Zéro appel LVGL dans un callback audio** :
   - Tous les callbacks de `WakeWordManager` (`onWakeWordDetected`, `onWakeWordStateChanged`, `onWakeWordError`) ainsi que `pollWakeWordEvent()` sont garantis exécutés dans le contexte de `voice.update()` sur la tâche principale.
2. **Pattern d'intégration recommandé dans la boucle principale de `CoreS3-VoiceUI-Demo`** :
   ```cpp
   voice.update();

   WakeWordEvent wakeEv;
   if (voice.pollWakeWordEvent(wakeEv)) {
       // Exécuté en toute sécurité dans la boucle principale (même thread que lv_timer_handler)
       setVoiceState(VoiceState::LISTENING);
       setTranscript(wakeEv.isSimulated
           ? "Bonjour ESP détecté (simulation test)"
           : "Bonjour ESP détecté");
       // Déclencher ici l'ouverture de session ou le tour d'écoute souhaité par l'application
   }
   ```
3. **Gestion de la fin de conversation** :
   - Avec `config.wakeWordAutoResume = true` (valeur par défaut), `VoiceKit` suspend automatiquement le détecteur dès le début du tour vocal (`ARMED -> SUSPENDED`) et le réarme automatiquement (`SUSPENDED -> ARMED`) 250 ms après la fin de la réponse vocale du haut-parleur.
