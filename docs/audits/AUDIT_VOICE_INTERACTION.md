# AUDIT TECHNIQUE D'INTÉGRATION DES INTERACTIONS VOCALES
## VoiceKit v0.6.6.2 vs VoiceKit-SmartUI v0.1.0.6
### PTT à deux clics, validation locale de parole et composants réutilisables sur M5Stack CoreS3 (ESP32-S3)

**Document de référence :** `docs/audits/AUDIT_VOICE_INTERACTION.md`  
**Date :** 15 novembre 2026  
**Auteur :** Ingénierie Système & Logiciel Embarqué Antigravity (C++17 / ESP32-S3)  
**Nature de la mission :** Audit technique architectural exclusivement (aucun fichier de code modifié)

---

## 1. Résumé exécutif

L'application **VoiceKit-SmartUI v0.1.0.6** a établi et validé sur matériel réel (**M5Stack CoreS3**) un ensemble de comportements d'interaction vocale d'une grande robustesse :
- Un paradigme tactile **PARLER / TERMINER à deux clics** avec filtrage anti-rebond matériel (120 ms).
- Une **validation locale d'activité vocale (VAD)** par analyse d'énergie RMS sur blocs audio PCM 16 kHz (32 ms), capable de rejeter catégoriquement le silence ambiant, les chocs mécaniques et les clics brefs.
- L'annulation stricte d'une capture non vocale via `cancelListening()` au lieu de `stopListening()`, empêchant tout envoi de trame `input_audio_buffer.commit` ou `response.create` vers OpenAI Realtime (évitant ainsi les réponses parasites du serveur).
- Une **interruption prioritaire STOP** instantanée et non-bloquante capable de couper la capture micro ou la restitution haut-parleur et de réaligner la session.
- Une résilience face aux états transitoires (watchdogs non bloquants pour timeout micro 15 s, timeout réponse serveur 15 s, reprise automatique après STOPPED 1 s et récupération après erreur 15 s).

Cependant, ces mécanismes résident actuellement dans le répertoire applicatif `VoiceKit-SmartUI/src/voice/` (`ptt_controller.cpp`, `ptt_controller.h`, `voice_validator.h`). Une analyse transversale d'autres applications de l'écosystème (`VoiceFlow`, `OpenAIConversationLVGL`, `BuddyAvatar`, `VoiceCommands`) démontre que **chaque application réimplémente ou duplique son propre contrôleur PTT**, générant une dispersion de code, des comportements divergents (appui maintenu vs deux clics) et des lacunes fonctionnelles (absence de VAD dans la plupart des projets).

Le présent audit a examiné en profondeur l'infrastructure existante de la bibliothèque **VoiceKit v0.6.6.2** (`VoxCoreVoice`, `ConversationController`, `LiveSessionController`, `LiveAudioStreamer`, `IntegratedVoicePipeline`, `VoiceActivityDetector`, `VoiceUxController`).

### Conclusions clés
1. **Propriété audio :** `VoxCoreVoice` possède et arbitre légitimement les flux audio matériels (`IAudioInput`/`IAudioOutput`, tâches FreeRTOS DMA et WebSocket). Tout nouveau contrôleur ne doit pas gérer le microphone directement, mais déléguer à `VoxCoreVoice`.
2. **Inadéquation d'IntegratedVoicePipeline pour la conversation pure :** `IntegratedVoicePipeline` est conçu pour l'orchestration de commandes locales séquentielles (STT unitaire $\rightarrow$ routage $\rightarrow$ ToolRegistry $\rightarrow$ TTS). Il n'est pas adapté aux conversations bidirectionnelles fluides (Mode B WebSocket plein-duplex avec OpenAI Realtime ou Gemini Live), impose une dépendance inutile à `ToolRegistry` et ne possède aucun mécanisme de validation locale de parole.
3. **Inadéquation du VAD existant tel quel :** `VoiceActivityDetector` dans VoiceKit est un détecteur de flux en ligne conçu pour clore automatiquement un segment après 1 200 ms de silence consécutif. Il est dépourvu de l'évaluation holistique de fin de capture (durée minimale 350 ms, parole cumulée 250 ms, segment continu 120 ms, plancher de seuil à 380 RMS pour le microphone ES7210, catégorisation du motif de rejet) validée dans SmartUI.
4. **Recommandation formelle : Option B.** Extraire les mécanismes d'interaction communs sous la forme d'un composant autonome, léger et modulaire au sein de VoiceKit (ex: `VoiceInteractionController` couplé à un validateur de capture extrait `VoiceCaptureValidator`), sans altérer `IntegratedVoicePipeline` ni toucher à la version de production de `VoiceKit-SmartUI v0.1.0.6`.

---

## 2. Versions et commits examinés

La vérification statique et environnementale a été effectuée directement sur l'arborescence des dépôts :

| Élément | Dépôt VoiceKit | Dépôt VoiceKit-SmartUI |
|---|---|---|
| **Chemin local** | `c:\Users\phall\Documents\Antigravity-5\VoiceKit` | `C:\Users\phall\Documents\Antigravity-5\VoiceKit-SmartUI` |
| **Branche Git** | `main` | `master` |
| **Commit HEAD** | `c639ad0e10710f366b3cd7969b6c3d027e7d4c35` | `5840db09d62f22879a08651d012833b270ab9ed7` |
| **Message Commit** | `feat: add IntegratedVoiceDemo example and IntegratedVoicePipeline implementation for M5Stack CoreS3` | `docs: add release notes for v0.1.0.6 stable release` |
| **Version déclarée** | `0.6.6.2` (`library.json`) | `0.1.0.6` (`include/app_config.h`) |
| **Dépendance effective** | *N/A (Bibliothèque source)* | `https://github.com/calmtechnology-ch/VoiceKit#c639ad0e10710f366b3cd7969b6c3d027e7d4c35` |
| **Version VoiceKit cible** | `0.6.6.2` | `0.6.6.2` (`#define VOICEKIT_LIB_VERSION "0.6.6.2"`) |
| **Plateforme cible** | M5Stack CoreS3 (ESP32-S3, 16 MB Flash, 8 MB PSRAM) | M5Stack CoreS3 (ESP32-S3, 16 MB Flash, 8 MB PSRAM) |
| **Framework & Compilateur**| Arduino Core 3.20017 (ESP-IDF v4.4/5.1 compatible), C++17 (`-std=gnu++17`) | Idem |

**Constat :** Les deux dépôts sont rigoureusement alignés sur le commit VoiceKit `c639ad0e10710f366b3cd7969b6c3d027e7d4c35`. Aucun écart de version n'est constaté entre la bibliothèque de référence et la dépendance figée dans le `platformio.ini` de SmartUI.

---

## 3. Cartographie des composants existants dans VoiceKit

Pour comprendre la répartition des responsabilités, voici la cartographie des composants VoiceKit intervenant dans le cycle vocal :

```
                                  +------------------------------------+
                                  |            APPLICATION             |
                                  |  (SmartUI / VoiceFlow / Exemples)   |
                                  +-----------------+------------------+
                                                    |
         +------------------------------------------+------------------------------------------+
         |                                                                                     |
         v (Approche Commande / Outils)                                                        v (Approche Conversation / Live)
+------------------------------------+                                        +------------------------------------+
|      IntegratedVoicePipeline       |                                        |         PTTController (*)          |
|  - Machine à états PipelineState   |                                        |  - Machine 2 clics PTTState        |
|  - Découplage STT -> Outil -> TTS  |                                        |  - Watchdogs non bloquants         |
|  - Dépendance ToolRegistry requise |                                        |  - (*) Actuellement dans SmartUI   |
+-----------------+------------------+                                        +-----------------+------------------+
                  |                                                                             |
                  |                                     +---------------------------------------+
                  |                                     | feeds audio chunks / RMS
                  |                                     v
                  |                   +------------------------------------+
                  |                   |     VoiceActivityValidator (*)     |
                  |                   |  - Validation locale d'énergie     |
                  |                   |  - Rejet silence & bruits brefs    |
                  |                   |  - (*) Actuellement dans SmartUI   |
                  |                   +------------------------------------+
                  |
                  +-------------------------------------+
                                                        |
                                                        v
                                      +------------------------------------+
                                      |            VoxCoreVoice            |
                                      |  (Façade publique & coordinateur)  |
                                      +---+--------------+--------------+--+
                                          |              |              |
                      +-------------------+              |              +-------------------+
                      |                                  |                                  |
                      v                                  v                                  v
+------------------------------------+  +------------------------------------+  +------------------------------------+
|         LiveAudioStreamer          |  |       LiveSessionController        |  |       ConversationController       |
|  - Tâche FreeRTOS micro (ES7210)   |  |  - Session WebSocket TLS           |  |  - Gestion des tours Mode B        |
|  - Tâche FreeRTOS TX streaming     |  |  - Reconnexion & ping              |  |  - Coordination micro / écoute     |
|  - Tampons DMA / Chunks 32 ms      |  |  - États LiveSessionState          |  |  - Mesure latences tour            |
+------------------------------------+  +------------------------------------+  +------------------------------------+
                      |                                  |                                  |
                      +-------------------+              |              +-------------------+
                                          |              |              |
                                          v              v              v
                                      +------------------------------------+
                                      |     Moteurs de transport et HAL    |
                                      |  - OpenAIRealtimeClient / Gemini   |
                                      |  - M5AudioInput / M5AudioOutput    |
                                      +------------------------------------+
```

### Réponses aux dix questions fondamentales de gouvernance

#### 1. Qui possède réellement le microphone ?
**Réponse :** `VoxCoreVoice` via son membre privé `LiveAudioStreamer _streamer` (`src/VoxCoreVoice.h:606`) et l'instance `IAudioInput* _audioInput` (`src/VoxCoreVoice.h:603`). C'est `LiveAudioStreamer` qui démarre la tâche FreeRTOS `liveMicTask` sur le Core 1 et lit les échantillons du codec ES7210 par bus I2S/DMA. Ni le PTTController, ni le VAD ne possèdent le périphérique micro.

#### 2. Qui contrôle le démarrage et l'arrêt d'une capture ?
**Réponse :**
- **Démarrage :** L'application ou le contrôleur PTT en appelant `VoxCoreVoice::startListening()` (`src/VoxCoreVoice.cpp:1152`). Celui-ci invoque `LiveAudioStreamer::start()` et `LiveSessionController::beginAudioTurn()`.
- **Arrêt normal (parole confirmée) :** L'appelant invoque `VoxCoreVoice::stopListening()` (`src/VoxCoreVoice.cpp:1189`), qui ordonne l'envoi d'`activityEnd` (Gemini) ou `input_audio_buffer.commit` + `response.create` (OpenAI Realtime).
- **Arrêt par annulation (silence ou rejet VAD) :** L'appelant invoque `VoxCoreVoice::cancelListening()` (`src/VoxCoreVoice.cpp:1218`), qui purge les files TX et arrête la tâche micro sans émettre de fin d'activité réseau.

#### 3. Qui valide une capture vocale ?
**Réponse :**
- Dans l'architecture actuelle de VoiceKit : **PERSONNE**. Ni `VoxCoreVoice`, ni `ConversationController`, ni `IntegratedVoicePipeline` ne valident la présence de voix avant d'envoyer la demande de réponse.
- Dans SmartUI : `VoiceActivityValidator` (`VoiceKit-SmartUI/src/voice/voice_validator.h:104-151`), interrogé par `PTTController::processIntents()` au moment précis du second clic (`FINISH_CAPTURE`).

#### 4. Qui déclenche la transmission vers le fournisseur IA ?
**Réponse :**
- En mode conversationnel (Mode B / OpenAI Realtime / Gemini Live) : La transmission audio est continue en cours d'élocution via `LiveAudioStreamer::tick()`. En revanche, **le déclenchement de la génération de réponse par l'IA** est déclenché par l'appel à `_voice.stopListening()`. Si `_voice.cancelListening()` est appelé à la place, le fournisseur ne génère aucune réponse.
- En mode `IntegratedVoicePipeline` (Mode STT unitaire) : `routeText()` déclenche explicitement `_voice->askAi()` à la réception de la transcription finale STT.

#### 5. Qui gère les états de conversation ?
**Réponse :** Une superposition de trois niveaux de machines à états :
1. `LiveSessionController` (`LiveSessionState` : `CONNECTED`, `WAITING_RESPONSE`, `PLAYING`, `INTERRUPTING`, etc.).
2. `VoxCoreVoice` / `ConversationController` (`VoiceState` : `READY`, `LISTENING`, `PROCESSING`, `SPEAKING`, etc.).
3. `PTTController` (`PTTState` : `DISABLED`, `READY`, `STARTING`, `LISTENING`, `STOPPING_CAPTURE`, `PROCESSING`, `SPEAKING`, `INTERRUPTING`, `RECOVERING`, `ERROR_STATE`).

#### 6. Qui possède l'autorité d'interruption STOP ?
**Réponse :** L'autorité d'interruption ultime est `VoxCoreVoice::stop()` (`src/VoxCoreVoice.cpp:817`), relayée par `ConversationController::requestStop()`, `LiveSessionController::requestStop()`, et `LiveAudioPlayer::stop(AudioStopReason::USER_STOP)`. Le `PTTController` détient l'autorité logique de déclencher cette cascade dès qu'un appui STOP intervient ou lors d'un clic en cours de lecture/traitement.

#### 7. Qui gère les timeouts ?
**Réponse :**
- Dans VoiceKit de base : Les timeouts concernent uniquement le protocole WebSocket et la connexion TLS. Aucun timeout micro ou applicatif n'est présent dans `VoxCoreVoice`.
- Dans `IntegratedVoicePipeline` : `update()` surveille `sttTimeoutMs` (6 s), `aiResponseTimeoutMs` (15 s), `confirmationTimeoutMs` (30 s).
- Dans SmartUI : `PTTController::checkWatchdogs()` gère le timeout micro (15 s), le timeout attente réponse serveur (15 s), la reprise STOPPED (1 s) et la reprise après erreur (15 s).

#### 8. Qui décide du retour à READY ?
**Réponse :**
- Dans VoiceKit : `LiveSessionController` bascule à `CONNECTED` quand le flux audio entrant est terminé. `ConversationController::tick()` détecte cette fin et émet `VoiceState::READY`.
- Dans SmartUI : `PTTController` vérifie que `_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()` avant de valider le retour à `PTTState::READY`.

#### 9. Qui gère les événements tardifs ?
**Réponse :**
- `VoxCoreVoice` et `IntegratedVoicePipeline` utilisent un horodatage monotone et des identifiants de tours (`_activeTurnId`, `_cancelledTurnId`, `_activeSpeechId`) pour rejeter les transcriptions et métriques orphelines.
- SmartUI applique un filtre explicite dans `voice_adapter.cpp:165` : `if (s_ptt.isLastTurnCancelled() || s_ptt.getState() == PTTState::INTERRUPTING) return;`.

#### 10. Quelles fonctions sont déjà exposées publiquement aux applications ?
**Réponse :**
- `VoxCoreVoice.h` : `startListening()`, `stopListening()`, `cancelListening()`, `stop()`, `say()`, `askAi()`, `onAudioChunk()`, `onUserText()`, `onAssistantText()`, `onStateChanged()`, `onReady()`, `onError()`.
- `IntegratedVoicePipeline.h` : `begin()`, `startListening()`, `stopListening()`, `cancel()`, `update()`, callbacks de tour.
- `VoiceActivityDetector.h` : Classe publique d'analyse de flux audio.

---

## 4. Inventaire détaillé du répertoire `src/voice` de SmartUI

Le répertoire `VoiceKit-SmartUI/src/voice/` comprend 5 fichiers. Chaque méthode a été analysée et classée selon les quatre catégories requises :
- **A — Déjà disponible dans VoiceKit** (aucune réimplémentation nécessaire).
- **B — Disponible partiellement dans VoiceKit** (une adaptation ou configuration peut suffire).
- **C — Spécifique à SmartUI mais réutilisable** (candidat potentiel à une extraction).
- **D — Strictement applicatif** (doit rester dans VoiceKit-SmartUI).

### 4.1. Fichier `voice_validator.h`

| Symbole / Méthode | Responsabilité | Dépendances | Catégorie | Justification |
|---|---|---|:---:|---|
| `struct VadMetrics` | Structure des métriques et causes de rejet (`isValid`, `rejectReason`, `captureDurationMs`, etc.) | `<stdint.h>`, `<stddef.h>` | **C** | Structure propre et réutilisable pour toute application vocale. |
| `VoiceActivityValidator::VoiceActivityValidator(...)` | Initialisation des seuils configurables | `cmath` | **C** | Paramètres par défaut optimisés pour ES7210 CoreS3. |
| `reset()` / `start(uint32_t nowMs)` | Réinitialisation des compteurs et prise de repère temps | Horloge `millis()` | **C** | Gestion de session de capture. |
| `feedChunk(uint32_t rms)` | Intégration du niveau RMS par bloc de 32 ms et suivi EMA du bruit | Aucune | **C** | Cœur du suivi adaptatif de dynamique sonore. |
| `updateIdleRms(uint32_t rms)` | Adaptation douce du bruit de fond hors capture | Aucune | **C** | Maintient une estimation fidèle du bruit ambiant. |
| `getSpeechThreshold()` | Calcul dynamique du seuil avec plancher matériel (380 RMS) | Aucune | **C** | Empêche les faux positifs en milieu silencieux. |
| `evaluate(uint32_t nowMs)` | Évaluation multicritère terminale (durée, silence, bruits brefs, énergie) | Aucune | **C** | **Composant clé absent de VoiceKit**, garantissant le rejet strict. |
| `stop()` / `isActive()` | Arrêt de capture et consultation d'état | Aucune | **C** | État interne non bloquant. |

### 4.2. Fichier `ptt_controller.h` et `ptt_controller.cpp`

| Symbole / Méthode | Responsabilité | Dépendances | Catégorie | Justification |
|---|---|---|:---:|---|
| `enum class PTTState` | États PTT (`DISABLED` à `ERROR_STATE`) | `<stdint.h>` | **B** | VoiceKit a `VoiceState` et `PipelineState`, mais pas d'état dédié au PTT à 2 clics. |
| `enum class PTTIntent` | Intentions différées pour exécution thread-safe dans la boucle principale | `<stdint.h>` | **C** | Modèle asynchrone évitant les blocages FreeRTOS/LVGL. |
| `PTTController::begin()` | Réinitialisation de tous les états et horodatages | Aucune | **C** | Réutilisable. |
| `onParlerClick()` | Gestionnaire du clic principal : anti-rebond, bascule START/FINISH, STOP si actif | `millis()`, `PTTIntent` | **C** | **Logique centrale 2 clics validée sur le terrain.** |
| `triggerStop()` | Déclenchement prioritaire de l'interruption STOP | `PTTIntent::CANCEL_CAPTURE` | **B** | Relaye l'intention vers `_voice.cancelListening()` et `_voice.stop()`. |
| `processAudioChunk(...)` | Acheminement des blocs audio vers `VoiceActivityValidator` | `voice_validator.h` | **C** | Passerelle entre le callback de flux et le validateur. |
| `onSilenceDetected(...)` | Callback notifiant l'UI d'un rejet de parole | `std::function` | **C** | Permet à l'IHM d'afficher un retour visuel ("Silence"). |
| `onButtonPress/Release/PressLost` | Surcharges historiques pour appui maintenu | Aucune | **D** | Obsolète en mode 2 clics, conservé pour rétro-compatibilité. |
| `triggerSerialPress/Release` | Commandes série 'p' et 'r' de test | Aucune | **D** | Strictement orienté débogage console/test. |
| `notifyVoiceStateChanged(...)` | Synchronisation avec les transitions de `VoxCoreVoice` | `VoiceState` | **B** | VoiceKit fournit les événements, le contrôleur s'y aligne. |
| `notifyListeningStarted/Finished` | Notification explicite du cycle micro | `VoxCoreVoice` | **B** | Synchronisation d'état. |
| `notifySpeechStarted/Finished` | Notification explicite de la restitution haut-parleur | `VoxCoreVoice` | **B** | Synchronisation d'état. |
| `notifyError(...)` / `notifyReady()` | Gestion des alertes et du rétablissement opérationnel | `VoxCoreVoice` | **B** | Réalignement automatique sur le moteur. |
| `processIntents(uint32_t nowMs)` | Traitement des intentions : STOP, RECOVER, START_CAPTURE, FINISH_CAPTURE | `VoxCoreVoice`, `Validator` | **C** | Cœur décisionnel reliant VAD et `_voice.stopListening()` vs `cancelListening()`. |
| `checkWatchdogs(uint32_t nowMs)` | Surveillance non-bloquante des timeouts et reprise automatique | `app_config.h`, `WiFi` | **C** | Assure qu'aucun blocage permanent ne peut survenir sur CoreS3. |
| `isActionAllowed()` / `getRejectCause()` | Contrôle des conditions préalables (Wi-Fi, session, moteur disponible) | `VoxCoreVoice`, `WiFi` | **B** | Interroge les API publiques existantes de `VoxCoreVoice`. |
| `update(uint32_t nowMs, ...)` | Exécution cyclique dans `loop()` | `processIntents`, `watchdogs` | **C** | Boucle principale non bloquante. |

### 4.3. Fichier `voice_adapter.h` et `voice_adapter.cpp`

| Symbole / Méthode | Responsabilité | Dépendances | Catégorie | Justification |
|---|---|---|:---:|---|
| `voice_adapter_init(...)` | Configuration initiale de VoiceKit (SSID, clé API, modèle OpenAI) | `VoiceKitConfig`, `app_config.h` | **D** | Paramétrage applicatif propre aux variables de configuration SmartUI. |
| `voice_adapter_start()` | Instanciation et initialisation effective de `VoxCoreVoice::begin()` | `VoxCoreVoice` | **D** | Séquençage propre au démarrage de SmartUI. |
| `voice_adapter_update()` | Appel de `_voice.update()`, mesure des microsecondes, throttling UI | `micros()`, `diagnostics.h` | **D** | Métrologie et instrumentation propres à l'application. |
| `utf8_safe_length(...)` | Découpe sécurisée d'une chaîne UTF-8 sans corrompre de point de code | `<cstring>`, `<stdint.h>` | **A** | **Déjà présent dans VoiceKit** (`src/Utf8Utils.h:37`). |
| `copyTextToPendingBuf(...)` | Copie thread-safe protégée par spinlock FreeRTOS | `portMUX_TYPE` | **D** | Découplage de la tâche réseau et du thread LVGL applicatif. |
| `voice_adapter_poll_text(...)` | Lecture non-bloquante des textes pour affichage graphique | Buffers applicatifs | **D** | Interface directe vers les widgets d'affichage de SmartUI. |
| `print_conn_status(...)` | Journalisation périodique console des conditions de connexion | Série, diagnostics | **D** | Débogage applicatif spécialisé. |

---

## 5. Comparaison précise des mécanismes PTT

La comparaison confronte le mécanisme validé de SmartUI aux implémentations trouvées dans le dépôt VoiceKit :

| Critère | SmartUI `PTTController` | VoiceKit `IntegratedVoicePipeline` | VoiceKit `ConversationController` | Exemple `OpenAIConversationLVGL` |
|---|---|---|---|---|
| **Paradigme d'interaction** | **2 clics : PARLER puis TERMINER** | Appui / Relâchement (ou 2 appels programmatiques) | Appui / Relâchement (`startTurn` / `finishTurnAudio`) | **Appui maintenu strict** (`onButtonPress` / `onButtonRelease`) |
| **Filtrage anti-rebond** | **120 ms matériel** (`ptt_controller.cpp:100`) | Aucun (rejet par état si non READY) | Aucun | Aucun |
| **Validation de parole** | **Oui (VAD holistique local)** | **Non (aucun VAD)** | **Non (aucun VAD)** | **Non (aucun VAD)** |
| **Action si silence** | **`_voice.cancelListening()`** (zéro requête IA) | Envoi systématique au STT $\rightarrow$ timeout 6 s | Envoi systématique $\rightarrow$ attente serveur | Envoi systématique $\rightarrow$ attente serveur |
| **Interruption STOP** | **Universelle, immédiate et prioritaire** | Universelle via `cancel()` | Immédiate via `requestStop()` | Déclenchée sur appui pendant élocution |
| **Gestion des timeouts** | **4 watchdogs intégrés (15s, 15s, 1s, 15s)** | 5 timeouts configurables | Aucun timeout applicatif interne | Aucun timeout applicatif interne |
| **Dépendance ToolRegistry** | **Aucune (zéro couplage)** | **Obligatoire (ToolRegistry injecté)** | Aucune | Aucune |
| **Couplage IHM** | **Indépendant de LVGL** (gère des intents) | Indépendant de LVGL | Indépendant de LVGL | Couplé à l'analyse tactile LVGL |

### Analyse détaillée des scénarios obligatoires

```
+---------------------------------------------------------------------------------------------------+
| SCÉNARIO A : CAPTURE NORMALE                                                                      |
|                                                                                                   |
|  READY ---[ Clic PARLER ]---> STARTING ---> LISTENING ---[ Clic TERMINER ]---> STOPPING_CAPTURE   |
|                                                                                      |            |
|                                                                          [ Parole validée ]       |
|                                                                                      |            |
|  READY <---[ Fin élocution ]--- SPEAKING <---[ Réponse audio ]--- PROCESSING <-------+            |
+---------------------------------------------------------------------------------------------------+
```
- **SmartUI :**
  1. 1er clic sur bouton : `onParlerClick()` vérifie `isActionAllowed()`, arme `PTTIntent::START_CAPTURE`. Dans `loop()`, `_validator.start(nowMs)` est appelé et `_voice.startListening()` est engagé.
  2. Microphone actif : blocs audio de 32 ms envoyés au VAD par `_voice.onAudioChunk()`.
  3. 2ème clic : `PTTIntent::FINISH_CAPTURE` est armé. `_validator.evaluate(nowMs)` valide la capture (durée > 350 ms, parole cumulée > 250 ms, segment continu > 120 ms).
  4. Confirmation : appel de `_voice.stopListening()`. OpenAI génère la réponse.
  5. Progression : passage en `PROCESSING`, puis `SPEAKING` dès réception du son, puis retour automatique à `READY`.
- **Composants VoiceKit existants :**
  - `ConversationController` et `IntegratedVoicePipeline` supportent la séquence normale, **mais sans aucune validation de parole intermédiaire**.

```
+---------------------------------------------------------------------------------------------------+
| SCÉNARIO B : CAPTURE SILENCIEUSE (REJET SANS ENVOI IA)                                             |
|                                                                                                   |
|  READY ---[ Clic PARLER ]---> LISTENING ---[ Clic TERMINER ]---> VAD Évaluation                   |
|                                                                         |                         |
|                                                                 [ SILENCE ou BRUIT BREF ]         |
|                                                                         |                         |
|  READY <---[ Notification silence ]--- cancelListening() <-------------+                         |
|             (Aucune trame générée vers OpenAI / Gemini)                                           |
+---------------------------------------------------------------------------------------------------+
```
- **SmartUI :**
  1. L'utilisateur clique PARLER, ne dit rien (ou tape légèrement sur le boîtier), puis clique TERMINER.
  2. `_validator.evaluate(nowMs)` détecte `SILENCE` (`peakRms < speechThreshold`) ou `BRIEF_NOISE` (`maxConsecutiveSpeechMs < 120`).
  3. Le contrôleur PTT invoque **`_voice.cancelListening()`** (`ptt_controller.cpp:476`) et non `stopListening()`.
  4. **Résultat :** Le flux micro est fermé localement, la trame `commit` n'est jamais envoyée au serveur, OpenAI ne consomme aucun token et ne répond rien. Le callback `_onSilence` informe l'IHM qui affiche "(Aucune parole détectée)". L'état redevient immédiatement `READY`.
- **VoiceKit existant :**
  - **DÉFAILLANCE MAJEURE SANS CE MÉCANISME.** `IntegratedVoicePipeline` appelle `stopListening()`, passe en `TRANSCRIBING` et attend 6 secondes pour une transcription vide. `ConversationController` transmet `activityEnd` au serveur qui réagit par une hallucination vocale ou un tour vide.

```
+---------------------------------------------------------------------------------------------------+
| SCÉNARIO C : COMMANDE STOP PRIORITAIRE                                                           |
|                                                                                                   |
|  [ Écoute / Traitement / Élocution ] ---[ Clic STOP ou appui ]---> INTERRUPTING                  |
|                                                                         |                         |
|                                                                 _voice.stop()                     |
|                                                             _voice.cancelListening()              |
|                                                                         |                         |
|  READY <-----------------[ Reprise contrôlée < 1.5 s ]------------------+                         |
+---------------------------------------------------------------------------------------------------+
```
- **SmartUI :**
  1. Si un clic intervient alors que le système est en `PROCESSING` ou `SPEAKING`, `onParlerClick()` le transforme automatiquement en STOP prioritaire (`ptt_controller.cpp:109`).
  2. `triggerStop()` arme `PTTIntent::CANCEL_CAPTURE`.
  3. L'exécution appelle `_voice.cancelListening()`, `_voice.stop()`, coupe le haut-parleur et passe en `INTERRUPTING`.
  4. Dès que la session est stable au repos (`_voice.isReady() && !_voice.isBusy()`), le watchdog rétablit `READY`.
- **VoiceKit existant :**
  - `VoxCoreVoice::stop()` et `IntegratedVoicePipeline::cancel()` fournissent une base d'annulation saine, mais n'ont pas la conversion intelligente "Clic en cours de parole = STOP".

```
+---------------------------------------------------------------------------------------------------+
| SCÉNARIO D : CONNEXION INDISPONIBLE                                                               |
|                                                                                                   |
|  Wi-Fi coupé ou Session non CONNECTED ---> PTTState::DISABLED (clic rejeté avec motif consigné)   |
|                                                   |                                               |
|  Wi-Fi rétabli + Session confirmée -------------> PTTState::READY                                 |
+---------------------------------------------------------------------------------------------------+
```
- **SmartUI :** `isActionAllowed()` filtre l'interaction : si le Wi-Fi est déconnecté ou si `_voice.getSession().getState() != LiveSessionState::CONNECTED`, l'état reste `DISABLED`. Les clics sont ignorés et tracés avec la cause exacte (`WIFI_NOT_CONNECTED`, `CONNECTING_SERVICE`, etc.).
- **VoiceKit existant :** Couvert partiellement dans `IntegratedVoicePipeline::startListening()` qui vérifie `_voice->isReady()`.

```
+---------------------------------------------------------------------------------------------------+
| SCÉNARIO E : CLICS RAPIDES ET TRANSITIONS ANORMALES                                               |
+---------------------------------------------------------------------------------------------------+
```
- **SmartUI :**
  - Filtre matériel de 120 ms rejetant les rebonds de l'écran tactile capacitif.
  - Découplage par intentions volatiles (`_pendingIntent`) exécutées en début d'itération de `loop()`, éliminant les conditions de course entre les interruptions tactiles et les tâches réseau.
  - Ignorance stricte des transcriptions tardives ou des fins de lecture lorsque `isLastTurnCancelled()` est vrai.
  - Verrou d'interruption : empêche de relancer `stop()` en boucle si `INTERRUPTING` est déjà actif.
- **VoiceKit existant :** `IntegratedVoicePipeline` utilise les identifiants de tours monotones pour rejeter les événements orphelins, mais ne protège pas contre les rebonds tactiles physiques.

---

## 6. Comparaison approfondie des deux VAD

VoiceKit possède déjà `VoiceActivityDetector` (`src/core/VoiceActivityDetector.h`). SmartUI utilise `VoiceActivityValidator` (`VoiceKit-SmartUI/src/voice/voice_validator.h`).

### Tableau comparatif exhaustif

| Critère | `VoiceActivityDetector` (VoiceKit) | `VoiceActivityValidator` (SmartUI) |
|---|---|---|
| **Objectif de conception** | Détection en flux continu (streaming) pour borner la parole en direct et clore la capture automatiquement après un silence | Évaluation holistique post-capture pour valider ou rejeter un enregistrement PTT à 2 clics |
| **Entrée audio** | Échantillons bruts PCM 16 bits `int16_t*` (calcul interne RMS) ou RMS précalculé via `processRms()` | RMS précalculé par bloc de 32 ms via `feedChunk(rms)` et `updateIdleRms(rms)` |
| **Lissage du bruit ambiant** | Filtre EMA : `_noiseFloor = _noiseFloor * (1 - \alpha) + rms * \alpha` avec $\alpha = 0.05$ | Filtre EMA identique : $\alpha = 0.05$, mis à jour sous le seuil de parole ou hors capture |
| **Bornes du plancher de bruit**| Borné entre 50 et 3 500 RMS (initial : 200) | Borné entre 50 et 3 000 RMS (initial : 180) |
| **Formule du seuil de parole**| `thresh = (noiseFloor * 2.0) + 250` (sans borne minimale) | `thresh = (noiseFloor * 1.8) + 150` avec **plancher matériel garanti à 380 RMS** |
| **Validation durée cumulée** | Non mesurée sur l'ensemble de la capture | Exigée : `cumulativeSpeechMs >= 250 ms` |
| **Validation continuité** | `minSpeechDurationMs = 200 ms` pour confirmer l'attaque de parole | `maxConsecutiveSpeechMs >= 120 ms` (au moins 4 blocs consécutifs) |
| **Validation durée totale** | Aucune durée minimale (durée maximale : 30 s) | Exigée : `captureDurationMs >= 350 ms` (rejet des doubles clics involontaires) |
| **Validation rapport d'énergie**| Aucun | Exigée : `averageRms >= noiseFloor + 30` (rejet de l'énergie trop faible) |
| **Détection de fin de parole** | Déclenchée automatiquement après **1 200 ms de silence consécutif** | Décidée manuellement par le **2ème clic utilisateur** |
| **Format du résultat** | État courant (`VadState`) et callback `onSpeechEnded(VadEndReason)` | Structure `VadMetrics` avec booléen `isValid` et motif explicite (`rejectReason`) |
| **Empreinte mémoire** | ~56 octets, 0 allocation dynamique | ~48 octets, 0 allocation dynamique |
| **Coût CPU** | Négligeable (< 0.1% CPU ESP32-S3 à 240 MHz) | Négligeable (< 0.1% CPU ESP32-S3 à 240 MHz) |

### Réponse à la question essentielle du cahier des charges

> **Question :** Peut-on utiliser le VAD existant de VoiceKit pour rejeter une capture silencieuse avant `stopListening()`, sans modifier le comportement validé de SmartUI ?

**Réponse formelle : NON, PAS EN L'ÉTAT.**

**Démonstration technique :**
1. **Divergence de paradigme :** `VoiceActivityDetector` est une machine d'état temporelle. Si l'utilisateur clique sur PARLER, reste silencieux pendant 2 secondes, puis clique sur TERMINER :
   - `VoiceActivityDetector` est toujours dans l'état `VadState::SILENCE`. `isSpeechConfirmed()` vaut `false`.
   - On pourrait naïvement tester `!vad.isSpeechConfirmed()`. Cependant, si l'utilisateur produit un **bruit sec ou un frottement de 60 ms** juste avant de cliquer, `VoiceActivityDetector` bascule brièvement en `VadState::SPEECH_STARTING`. Si le clic survient à cet instant, l'état n'est pas stabilisé.
2. **Absence de critères de qualification :** `VoiceActivityDetector` ne conserve pas les métriques de capture cumulées :
   - Il n'a aucun compteur de durée globale de capture $\rightarrow$ impossible de rejeter les appuis trop courts (`TOO_SHORT` < 350 ms).
   - Il ne calcule pas le segment continu le plus long sur l'ensemble de la capture $\rightarrow$ impossible d'exiger `maxConsecutiveSpeechMs >= 120 ms`.
   - Il ne mémorise pas la parole totale cumulée $\rightarrow$ impossible d'exiger `cumulativeSpeechMs >= 250 ms`.
   - Il n'intègre pas le plancher matériel de 380 RMS indispensable pour immuniser le microphone ES7210 contre les bruits électroniques de fond.
   - Il ne fournit pas la structure de métriques `VadMetrics` requise pour la traçabilité et le diagnostic.
3. **Conclusion :** Pour reproduire strictement les garanties validées sur le matériel réel dans SmartUI, VoiceKit doit disposer de l'algorithme d'évaluation holistique de `VoiceActivityValidator`.

---

## 7. Audit d'IntegratedVoicePipeline

`IntegratedVoicePipeline` a été introduit dans VoiceKit v0.6.5 (`src/core/pipeline/IntegratedVoicePipeline.h` et `.cpp`) pour offrir un pipeline vocal complet.

### Caractéristiques observées

```cpp
bool startListening();
bool stopListening();
void cancel();
void update();
```

1. **Utilisation de VoxCoreVoice :** Il utilise `VoxCoreVoice` comme moteur d'exécution exclusif (`_voice->startListening()`, `_voice->stopListening()`, `_voice->cancelListening()`, `_voice->askAi()`, `_voice->say()`). Il n'alloue aucune ressource micro/haut-parleur redondante.
2. **Coordination des états :** Machine à 12 états `PipelineState` (`UNINITIALIZED` $\rightarrow$ `READY` $\rightarrow$ `LISTENING` $\rightarrow$ `TRANSCRIBING` $\rightarrow$ `ROUTING` $\rightarrow$ `TOOL_EXECUTING` / `AI_PROCESSING` $\rightarrow$ `SPEAKING` $\rightarrow$ `READY`).
3. **Prévention des captures concurrentes :** Dans `startListening()`, la capture est formellement rejetée si le pipeline n'est pas dans l'état `READY` ou `WAITING_CONFIRMATION`, ou si `_voice->isBusy()` est vrai.
4. **Traitement de STOP :** `cancel()` invalide immédiatement le tour actif via `_cancelledTurnId = _activeTurnId`, annule les outils locaux en attente, et invoque `_voice->stop()`.
5. **Couplage avec ToolRegistry :** `IntegratedVoicePipeline` est **strictement couplé à ToolRegistry**. Sa méthode `begin()` exige une référence à `ToolRegistry& tools` (`IntegratedVoicePipeline.h:145`). Il instancie un `VoiceCommandDispatcher` et un `VoiceUxController`.
6. **Mode de fonctionnement :** Il fonctionne en mode **séquentiel STT $\rightarrow$ Traitement $\rightarrow$ TTS** :
   - Écoute micro $\rightarrow$ transcription finale STT.
   - Routage textuel $\rightarrow$ si intention locale UX (REPEAT, PAUSE, RESUME) ou commande locale (ToolRegistry), exécution sur microcontrôleur.
   - Si requête IA $\rightarrow$ envoi via `_voice->askAi(text)` $\rightarrow$ réception du texte $\rightarrow$ synthèse vocale via `_voice->say(responseText)`.
7. **Absence de mode Conversation bidirectionnelle (Mode B) :** Il n'exploite pas le mode `enableConversation(true)` dans lequel le flux audio est envoyé et reçu en continu en duplex avec OpenAI Realtime.
8. **Indépendance graphique :** Il est totalement exempt de dépendance à LVGL et peut fonctionner sur n'importe quel microcontrôleur ESP32-S3 sous Arduino/FreeRTOS.
9. **Coût mémoire :** Faible en RAM interne (~2 Ko d'instances et buffers), mais introduit un graphe de dépendances important (`ToolRegistry`, `VoiceCommandDispatcher`, `VoiceUxController`, `ReplyMemory`, `TextGate`).

### Réponse à la question fondamentale

> **Question :** Est-il pertinent d'utiliser `IntegratedVoicePipeline` pour une simple application conversationnelle comme SmartUI ?

**Réponse formelle : NON.**

**Justification :**
1. **Discordance d'architecture :** SmartUI utilise le mode conversationnel direct d'OpenAI Realtime (audio entrant streaming $\rightarrow$ audio sortant streaming direct avec le serveur, géré par `ConversationController` et `LiveSessionController`). `IntegratedVoicePipeline` force un modèle asynchrone par requêtes textuelles (`askAi` + `say`), détruisant la faible latence et l'inférence audio native d'OpenAI Realtime.
2. **Absence du paradigme 2 clics :** `IntegratedVoicePipeline` n'intègre pas la logique de bascule tactile 2 clics.
3. **Absence de VAD :** `IntegratedVoicePipeline` n'a aucune validation de parole locale ; il envoie toute capture au STT.
4. **Complexité superflue :** Imposer `ToolRegistry` et `VoiceCommandDispatcher` à une application qui souhaite uniquement discuter avec une IA est une surcharge d'architecture injustifiée.

---

## 8. Identification des duplications réelles

L'examen comparatif des projets de l'écosystème met en évidence les duplications suivantes :

```
                                  DUPLICATIONS IDENTIFIÉES
                                  
   VoiceKit-SmartUI                     VoiceFlow                     OpenAIConversationLVGL
+----------------------+         +----------------------+         +----------------------+
|  ptt_controller.cpp  |         |  PTTController.cpp   |         |  PTTController.cpp   |
|  - Machine 2 clics   |         |  - Appui maintenu    |         |  - Appui maintenu    |
|  - Anti-rebond       |         |  - Duplication       |         |  - Code d'exemple    |
|  - Watchdogs         |         |  - Dérivé exemple    |         |                      |
+----------------------+         +----------------------+         +----------------------+
          |                                                                  |
          v                                                                  |
+----------------------+                                                     |
|  voice_validator.h   |                                                     |
|  - Seul projet avec  |                                                     |
|    VAD 2 clics local |                                                     |
+----------------------+                                                     v
                                                                  +----------------------+
                                                                  |  Diagnostics.h/.cpp  |
                                                                  |  - Dupliqué dans     |
                                                                  |    VoiceFlow         |
                                                                  +----------------------+
```

1. **Le contrôleur PTT :**
   - `VoiceKit-SmartUI/src/voice/ptt_controller.cpp` (600 lignes).
   - `examples/OpenAIConversationLVGL/src/PTTController.cpp` (445 lignes).
   - `VoiceFlow/src/PTTController.cpp` (copie quasi conforme de l'exemple LVGL).
   - `BuddyAvatar/src/BuddyController.cpp` (implémente une logique PTT ad-hoc avec appels directs à `startListening()` et `stopListening()`).
2. **La gestion des Watchdogs :**
   - Chaque application recrée ses propres horodatages `millis()` pour surveiller les timeouts de capture micro (15 s) ou d'absence de réponse serveur, avec des implémentations hétérogènes et des risques de blocage.
3. **Le filtrage d'événements tardifs :**
   - SmartUI filtre manuellement les transcriptions tardives dans `voice_adapter.cpp:165`. Cette vérification devrait être encapsulée dans le contrôleur d'interaction.
4. **La fonction `utf8_safe_length` :**
   - Codée dans `voice_adapter.cpp:30-56`, alors que VoiceKit dispose déjà de `src/Utf8Utils.h`.

---

## 9. Fonctionnalités manquantes dans VoiceKit

Pour permettre à toute application d'implémenter l'interaction validée dans SmartUI sans réécrire de code applicatif, voici les briques manquantes dans VoiceKit v0.6.6.2 :

1. **Validation holistique de capture vocale (VAD de clôture de tour) :**
   - Absence d'un validateur évaluant globalement l'énergie d'une capture terminée (`cumulativeSpeechMs`, `maxConsecutiveSpeechMs`, plancher dynamique adaptatif 380 RMS pour ES7210, cause de rejet explicite).
2. **Contrôleur d'interaction vocale à 2 clics (PTT à bascule) :**
   - Absence d'une classe d'interaction réutilisable, indépendante de LVGL, gérant :
     - Le premier clic $\rightarrow$ engagement de l'écoute.
     - Le second clic $\rightarrow$ évaluation VAD $\rightarrow$ `stopListening()` si valide, `cancelListening()` si silence/bruit.
     - Le clic pendant la réponse $\rightarrow$ transformation en STOP immédiat.
     - L'anti-rebond matériel configurable (120 ms par défaut).
     - Le découplage des intentions pour une exécution non-bloquante dans `loop()`.
3. **Surveillance temporelle intégrée (Watchdogs non-bloquants) :**
   - Absence de surveillance autonome ramenant le système à l'état `READY` en cas d'oubli d'arrêt du micro (15 s) ou de non-réponse du serveur (15 s).

---

## 10. Analyse comparative des options d'architecture

Trois options d'architecture sont évaluées.

### Option A — Conserver les modules applicatifs
Laisser `ptt_controller` et `voice_validator` dans `VoiceKit-SmartUI`. Les autres applications continuent d'écrire ou de copier leur propre gestion PTT.
- **Avantages :** Zéro modification dans VoiceKit ; zéro risque de régression sur la bibliothèque.
- **Inconvénients :** Duplication permanente de code ; absence de standardisation ; chaque nouveau projet doit réimplémenter 800 lignes de logique complexe ; maintenance lourde.

### Option B — Extraire les mécanismes communs dans un composant léger de VoiceKit
Créer un module d'interaction dédié au sein de VoiceKit (ex: `VoiceInteractionController` et `VoiceCaptureValidator` dans `src/core/interaction/` ou directement dans `src/`). Ce module encapsule la machine 2 clics, le VAD de validation de parole, l'arbitrage STOP et les watchdogs, en se branchant uniquement sur les API publiques existantes de `VoxCoreVoice`.
- **Avantages :** 
  - N'altère en rien `VoxCoreVoice`, `ConversationController`, ni `IntegratedVoicePipeline`.
  - Réutilisable immédiatement par SmartUI, VoiceFlow, BuddyAvatar ou un projet sans écran.
  - Zéro dépendance à LVGL ou ToolRegistry.
  - Empreinte mémoire minimale (< 1.5 Ko RAM).
  - Fournit l'API ergonomique cible en 4 lignes de code.
- **Inconvénients :** Nécessite l'ajout de nouveaux fichiers propres dans VoiceKit lors de la phase de développement ultérieure.

### Option C — Étendre `IntegratedVoicePipeline`
Modifier `IntegratedVoicePipeline` pour lui ajouter un mode `TWO_CLICK_CONVERSATION`, intégrer le VAD et adapter sa machine d'état.
- **Avantages :** Centralise tout dans un composant existant.
- **Inconvénients :** 
  - Conflit d'architecture majeur entre le mode STT/Outils et le mode Conversation Live WebSocket.
  - Alourdit considérablement une machine à états déjà complexe (12 états).
  - Oblige les applications conversationnelles à inclure `ToolRegistry` et `VoiceCommandDispatcher`.
  - Risque élevé de régression sur les exemples existants de VoiceKit v0.6.5/v0.6.6.

---

## 11. Matrice de décision

Notation de chaque option sur une échelle de 1 (très défavorable) à 5 (excellent) :

| Critère d'évaluation | Poids | Option A (Statu quo) | Option B (Extraction modulaire) | Option C (Extension Pipeline) |
|---|:---:|:---:|:---:|:---:|
| **1. Réutilisation du code existant** | x1 | 1 / 5 | **5 / 5** | 3 / 5 |
| **2. Simplicité pour une nouvelle application** | x1 | 2 / 5 | **5 / 5** | 2.5 / 5 |
| **3. Risque de régression sur l'existant** | x1.5 | 5 / 5 (aucun risque) | **4.5 / 5** (très faible) | 2 / 5 (élevé) |
| **4. Coût mémoire RAM et CPU sur CoreS3** | x1 | 4 / 5 | **5 / 5** (optimal) | 3 / 5 |
| **5. Maintenabilité à long terme** | x1.5 | 2 / 5 | **5 / 5** | 2.5 / 5 |
| **TOTAL PONDÉRÉ (sur 30)** | | **17.5 / 30** | **29.25 / 30** | **15.75 / 30** |

### Justification des notes
- **Option A (17.5/30) :** Bien qu'elle présente un risque immédiat nul pour la bibliothèque, elle condamne la base de code à la duplication permanente et oblige chaque développeur à réinventer la synchronisation PTT/VAD.
- **Option B (29.25/30) :** Vainqueur indiscutable. Elle standardise la logique validée de SmartUI dans un composant réutilisable, sans toucher aux fondations éprouvées de `VoxCoreVoice` et sans imposer de dépendances externes.
- **Option C (15.75/30) :** Option la plus risquée et la moins adaptée, tentant de fusionner deux paradigmes d'interaction fondamentalement divergents.

---

## 12. Architecture recommandée

L'architecture recommandée repose sur **l'Option B**.

Elle s'articule autour de deux composants découplés :
1. **`VoiceCaptureValidator` :** Extraction directe et standardisée de `VoiceActivityValidator`. Classe pure C++, sans dépendance matérielle, recevant des valeurs RMS et renvoyant une structure `VadMetrics` avec décision `isValid` et motif de rejet (`TOO_SHORT`, `SILENCE`, `BRIEF_NOISE`, `LOW_ENERGY`, `OK`).
2. **`VoiceInteractionController` :** Contrôleur de haut niveau orchestrant l'interaction à deux clics, les intentions différées, la commande STOP, les callbacks de notification, et les watchdogs de sécurité.

```
+---------------------------------------------------------------------------------------+
|                                    APPLICATION                                        |
|              (Bouton LVGL, Bouton Physique CoreS3 M5.BtnA, ou Commande Série)         |
+-------------------------------------------+-------------------------------------------+
                                            |
                                            |  onTalkClick() / cancel()
                                            v
+---------------------------------------------------------------------------------------+
|                    NOUVEAU COMPOSANT : VoiceInteractionController                     |
|                                                                                       |
|   Machine d'interaction 2 clics :                                                     |
|     - Anti-rebond (120 ms)                                                            |
|     - Intentions différées (PTTIntent)                                                |
|     - Watchdogs intégrés (Timeout micro 15 s, Timeout réponse 15 s, Reprises auto)    |
|                                                                                       |
|   Contrôle de validation :                                                            |
|     - 1er clic : voice.startListening()                                               |
|     - 2ème clic : validation via VoiceCaptureValidator                                |
|         -> SI VALIDE  : voice.stopListening()   (génération IA déclenchée)            |
|         -> SI SILENCE : voice.cancelListening() (rejet propre, zéro trame réseau)     |
|                                                                                       |
|   Interruption STOP :                                                                 |
|     - Clic pendant SPEAKING / PROCESSING -> STOP prioritaire                          |
+---------------------+-------------------------------------------+---------------------+
                      |                                           |
                      | feeds RMS                                 | délègue les actions
                      v                                           v
+---------------------------------------+       +---------------------------------------+
|         VoiceCaptureValidator         |       |             VoxCoreVoice              |
|  - Plancher matériel 380 RMS          |       |  - Propriété du micro ES7210          |
|  - Filtre continuité (120 ms)         |       |  - Propriété du haut-parleur AW88298  |
|  - Filtre cumulé (250 ms)             |       |  - Sessions WebSocket TLS             |
|  - Filtre durée min (350 ms)          |       |  - Mode Conversation B / TTS / STT    |
+---------------------------------------+       +---------------------------------------+
```

---

## 13. API publique cible proposée

Pour répondre aux exigences d'ergonomie du prompt (Section 10), voici l'API cible modulaire, totalement indépendante de LVGL :

```cpp
#pragma once

#include <Arduino.h>
#include <VoxCoreVoice.h>
#include <functional>

// Configuration optionnelle de l'interaction
struct VoiceInteractionConfig {
    uint32_t debounceMs = 120;
    uint32_t micTimeoutMs = 15000;
    uint32_t serverResponseTimeoutMs = 15000;
    uint32_t minCaptureDurationMs = 350;
    uint32_t minSpeechDurationMs = 250;
    uint32_t minConsecutiveSpeechMs = 120;
    uint32_t speechThresholdFloorRms = 380;
};

class VoiceInteractionController {
public:
    using SilenceCallback = std::function<void(const char* reason)>;
    using StateCallback = std::function<void(PTTState oldState, PTTState newState)>;

    explicit VoiceInteractionController(VoxCoreVoice& voiceEngine, 
                                        const VoiceInteractionConfig& config = VoiceInteractionConfig());

    // Initialisation
    bool begin();

    // Interaction utilisateur universelle (tactile LVGL, bouton physique M5, GPIO, série)
    void onTalkClick();     // 1er clic : écoute / 2ème clic : arrêt & validation / Clic si occupé : STOP
    void triggerStop();     // Commande STOP prioritaire directe
    void recover();         // Rétablissement après erreur

    // Cycle obligatoire dans loop()
    void update();

    // Callbacks d'observation IHM
    void onSilenceDetected(SilenceCallback cb);
    void onStateChanged(StateCallback cb);

    // Accesseurs
    PTTState getState() const;
    bool isListening() const;
    bool isReady() const;
    const char* getRejectCause() const;
    const VadMetrics& getLastMetrics() const;
};
```

### Exemple d'utilisation dans une application (CoreS3 sans ou avec LVGL)

```cpp
#include <M5CoreS3.h>
#include <VoiceKit.h>
#include <VoiceInteractionController.h>

VoxCoreVoice voice;
VoiceInteractionController interaction(voice);

void setup() {
    M5.begin();
    // Configuration et démarrage de VoiceKit
    voice.begin(config);
    interaction.begin();

    // Notification IHM en cas de silence rejeté
    interaction.onSilenceDetected([](const char* reason) {
        Serial.printf("Capture rejetée localement : %s\n", reason);
    });
}

// Gestionnaire d'événement bouton LVGL ou bouton physique
void onTalkButtonClicked() {
    interaction.onTalkClick();
}

void onStopButtonClicked() {
    interaction.triggerStop();
}

void loop() {
    M5.update();
    voice.update();
    interaction.update();
    lv_timer_handler(); // Si LVGL est utilisé
}
```

---

## 14. Risques techniques et compatibilité

| Domaine | Risque identifié | Niveau | Stratégie d'atténuation |
|---|---|:---:|---|
| **VoiceKit-SmartUI v0.1.0.6** | Régression sur la version stabilisée en production | **NUL** | **Règle absolue :** Ne pas toucher au code de SmartUI. La nouvelle classe est développée dans VoiceKit et testée séparément. SmartUI ne sera migré que lors d'une version ultérieure validée. |
| **OpenAIConversationLVGL** | Incompatibilité de paradigme (appui maintenu vs 2 clics) | **FAIBLE** | L'exemple existant reste intact. L'introduction d'un nouvel exemple `TwoClickConversationDemo` démontrera la nouvelle approche. |
| **VoiceFlow & VoiceCommands** | Divergence de contrôleurs | **FAIBLE** | Ces applications pourront migrer vers `VoiceInteractionController` à leur propre rythme pour supprimer leurs duplications locales de `PTTController`. |
| **Consommation mémoire RAM** | Dépassement de la mémoire interne ESP32-S3 | **NUL** | `VoiceInteractionController` et `VoiceCaptureValidator` n'allouent aucun buffer dynamique ; coût total estimé < 1.5 Ko en BSS/Data. |
| **Latence CPU** | Surcharge de la boucle `loop()` | **NUL** | L'évaluation VAD s'effectue par opérations arithmétiques entières/flottantes légères ($< 50\,\mu\text{s}$) uniquement sur le 2ème clic. |
| **Multi-fournisseurs (Gemini vs OpenAI)** | Comportement différent de `cancelListening()` | **FAIBLE** | `VoxCoreVoice::cancelListening()` est déjà universel : il stoppe `LiveAudioStreamer` sans envoyer de fin d'activité, fonctionnant de manière identique pour Gemini Live et OpenAI Realtime. |

---

## 15. Plan de tests de non-régression

Avant toute intégration future dans une application cliente, les scénarios de test suivants devront être exécutés sur matériel réel CoreS3 :

1. **Test N-01 — Validation 2 clics nominale (Parole fluide) :**
   - Clic PARLER $\rightarrow$ élocution d'une phrase de 5 secondes $\rightarrow$ Clic TERMINER.
   - *Critère de succès :* Capture validée (`isValid == true`), transition vers `STOPPING_CAPTURE`, puis `PROCESSING`, puis restitution audio complète de la réponse.
2. **Test N-02 — Rejet catégorique du silence :**
   - Clic PARLER $\rightarrow$ silence absolu pendant 3 secondes $\rightarrow$ Clic TERMINER.
   - *Critère de succès :* Capture rejetée avec `rejectReason == "SILENCE"`, appel de `cancelListening()`, zéro paquet réseau `commit`, notification callback silence, retour immédiat à `READY`.
3. **Test N-03 — Rejet des bruits transitoires (clic mécanique / tapotement) :**
   - Clic PARLER $\rightarrow$ tapotement bref du doigt sur le boîtier CoreS3 $\rightarrow$ Clic TERMINER.
   - *Critère de succès :* Rejet avec `rejectReason == "BRIEF_NOISE"`, aucun appel serveur.
4. **Test N-04 — Rejet du double clic accidentel (< 350 ms) :**
   - Double clic très rapide sur le bouton PARLER.
   - *Critère de succès :* Rejet immédiat avec `rejectReason == "TOO_SHORT"`.
5. **Test N-05 — Interruption STOP en cours d'écoute :**
   - Clic PARLER $\rightarrow$ en cours d'écoute, clic STOP.
   - *Critère de succès :* Arrêt instantané du microphone, transition `INTERRUPTING` $\rightarrow$ `READY`.
6. **Test N-06 — Interruption STOP en cours de réponse haut-parleur :**
   - Assistant en train de parler $\rightarrow$ clic PARLER ou STOP.
   - *Critère de succès :* Coupure immédiate du son sur le haut-parleur AW88298, purge du buffer DMA, retour contrôlé à `READY`.
7. **Test N-07 — Résistance au mitraillage de clics :**
   - 10 clics consécutifs ultra-rapides sur l'écran tactile.
   - *Critère de succès :* L'anti-rebond de 120 ms filtre les sollicitations ; aucun crash, aucun double démarrage, mémoire intacte.
8. **Test N-08 — Timeout de sécurité microphone (15 s) :**
   - Clic PARLER $\rightarrow$ laisser le micro ouvert 16 secondes sans cliquer.
   - *Critère de succès :* Le watchdog se déclenche à 15 s, évalue la parole et clôture proprement la capture.

---

## 16. Proposition des prochaines étapes de développement

Conformément à la règle impérative de l'audit (**aucune modification de code à cette étape**), le travail s'arrête ici.

Pour les étapes futures (lorsqu'elles seront autorisées par l'utilisateur) :

1. **Étape 1 (Sous VoiceKit) :**
   - Créer `src/core/interaction/VoiceCaptureValidator.h` (portage propre et documenté de `voice_validator.h`).
   - Créer `src/core/interaction/VoiceInteractionController.h` et `.cpp` (implémentant l'API cible découplée).
   - Créer l'en-tête public `src/VoiceInteractionController.h`.
2. **Étape 2 (Exemple de référence sous VoiceKit) :**
   - Créer un exemple minimal `examples/TwoClickConversationLVGL` validant le composant sur CoreS3 sans toucher aux anciens exemples.
3. **Étape 3 (Validation et benchmark matériel) :**
   - Vérifier sur CoreS3 physique que les 8 tests de non-régression sont validés avec 100% de succès.
4. **Étape 4 (Adoption ultérieure par SmartUI) :**
   - Uniquement après validation complète de l'Étape 3, publier une nouvelle version mineure de VoiceKit et proposer la migration de SmartUI vers le composant officiel.

---

## 17. Conclusion technique

Le présent audit démontre qu'**il ne faut pas modifier `VoxCoreVoice` ni `IntegratedVoicePipeline`**, car ces composants remplissent déjà parfaitement leurs rôles respectifs.

La solution optimale, élégante et pérenne consiste à **extraire la logique 2 clics et le VAD validés dans SmartUI sous la forme d'un composant d'interaction dédié (`VoiceInteractionController` et `VoiceCaptureValidator`)** au sein de VoiceKit. Cette approche :
- Élimine la duplication de code dans tout l'écosystème.
- Garantit une utilisation simplifiée en 4 lignes de code pour toute future application.
- Préserve rigoureusement la stabilité absolue de **VoiceKit-SmartUI v0.1.0.6**.
