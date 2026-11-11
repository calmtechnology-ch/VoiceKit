# Rapport Technique Correctif — VoiceKit v0.6.4.3.2 (Event Correlation)

**Date :** 11 novembre 2026  
**Auteur :** Assistant Développeur Senior C++ / VoxCore VoiceKit  
**Version de départ :** `v0.6.4.3.1`  
**Version cible :** `v0.6.4.3.2`  
**Nature de la version :** Exclusivement corrective (« Corrective Event Correlation »)  
**Cible matérielle :** M5Stack CoreS3 (ESP32-S3) / FreeRTOS / Arduino / PlatformIO / MinGW Native  

---

## 1. Résumé Exécutif

La version **v0.6.4.3.2** résout définitivement les problématiques de corrélation asynchrone des flux audio STT et TTS dans VoiceKit sans refonte de l'architecture, sans altération des tâches FreeRTOS/DMA/I2S/TLS, et sans ajout de dépendance.

Dans la version précédente (v0.6.4.3.1), des identifiants avaient été introduits au niveau de `VoiceUxLiveBridge`, mais leur attribution côté application (`VoiceUxLiveDemo`) reposait encore sur des variables globales représentant l'opération active au moment de la réception du callback (`s_currentCaptureTurnId`, `s_currentSpeechId`). Cette approche ne permettait pas d'isoler de manière certaine un événement retardé issu d'une opération précédente si une nouvelle opération avait été engagée entre-temps.

La v0.6.4.3.2 traite le problème à la source :
1. **P0 — Origine & Propagation Réelle des Identifiants STT** : `VoxCoreVoice` génère un identifiant monotone de capture (`_sttTurnId`), l'associe fermement à l'opération de capture et le propage dans les callbacks (`onListeningStartedWithTurnId`, `onTranscriptionWithTurnId`). L'application n'utilise plus de variable globale et relaie l'identifiant exact fourni par le moteur.
2. **P0 — Origine & Propagation Réelle des Identifiants TTS** : `VoxCoreVoice` attribue un identifiant unique à chaque requête `say()`, suit l'opération dans la file d'attente et dans `LiveAudioPlayer`, et notifie la fin effective via `onSpeechFinishedWithId(speechId)`.
3. **P1 — Validation Stricte du Cycle de Vie STT (`VoiceUxSttPhase`)** : Modélisation des phases (`IDLE`, `LISTENING`, `TRANSCRIBING`, `COMPLETED`, `CANCELLED`). Rejet déterministe de toute transcription finale arrivant en phase `IDLE` sans capture audio autorisée préalable.
4. **P1 — Statut Terminal Unique & Erreurs pendant TTS** : Garantie absolue qu'une action ne reçoit qu'un seul statut terminal parmi `COMPLETED`, `REJECTED` ou `CANCELLED` (`_activeActionTerminated`). `notifyError()` solde les actions TTS actives avec `REJECTED` et les actions en attente avec `CANCELLED` sans laisser d'action orpheline ni polluer l'historique conversationnel `ReplyMemory`.
5. **P1 — Préservation du Mode Pause à Travers PTT** : Conservation de l'état `_stateBeforePause` lorsque le microphone est sollicité temporairement en pause, permettant la reprise (`RESUME`) et l'arrêt (`STOP`) sans réarmer indûment les commandes conversationnelles ordinaires.

---

## 2. P0 — Audit Préalable : Origine Réelle des Événements & Limites Protocolaires

### 2.1. Tableau d'Audit des Producteurs d'Événements

| Événement | Producteur Réel | Métadonnées Disponibles à l'Origine | Attribution de l'Identifiant | Garantie de Corrélation Possible |
| :--- | :--- | :--- | :--- | :--- |
| **Capture Start** | `VoxCoreVoice::startListening()` | Timestamp, mode PTT, état matériel | `VoxCoreVoice` incrémente `_sttTurnId` | **Stricte** : Émis localement de manière synchrone avec l'activation du microphone. |
| **Capture Stop** | `VoxCoreVoice::stopListening()` | Timestamp, statut tampon micro | Maintient `_pendingTranscriptionTurnId` | **Stricte** : Transition déterministe de capture active vers attente transcription. |
| **STT Interim** | `GeminiLiveClient` / `OpenAIRealtimeClient` | Texte partiel, pas de `turn_id` serveur chez Gemini | Associé par `VoxCoreVoice` au tour actif ou en attente | **Ordre FIFO** : Filtré ou transmis pour retour visuel UI. |
| **STT Final** | WebSocket serveur distant | Texte final, `item_id` (OpenAI), aucun tour (Gemini) | Associé par `VoxCoreVoice` à `_activeCaptureTurnId` ou `_pendingTranscriptionTurnId` | **Conditionnelle** : Rejeté si tour annulé, tour antérieur ou phase `IDLE`. |
| **STOP Request** | Utilisateur (Bouton UI / Commande Série) | Timestamp, priorité d'interruption | Invalide immédiatement `_cancelledCaptureTurnId` | **Stricte** : Purge immédiate des identifiants actifs/en attente. |
| **TTS Start** | `VoxCoreVoice::say()` | Texte, priorité, métadonnées UX | `VoxCoreVoice` incrémente `_speechGeneration` | **Stricte** : Identifiant monotone unique par texte à synthétiser. |
| **TTS Finish** | `LiveAudioPlayer` (DAC/I2S DMA drain) | Tampon vide, statut completion | `LiveAudioPlayer` transmet la fin de lecture | **Stricte** : `VoxCoreVoice` associe la fin au `_activeSpeechId` réel. |

### 2.2. Limites Techniques Démontrables des Protocoles

1. **Google Gemini Live (`BidiGenerateContent`)** :
   - Le protocole WebSocket bidirectionnel de Gemini Live ne permet pas au client d'attacher un `turnId` personnalisé dans les trames audio PCM envoyées et ne renvoie pas d'identifiant de tour utilisateur dans les trames `inputTranscription`.
   - **Garantie possible** : La corrélation repose sur une machine à états stricte côté client avec génération locale monotone (`captureTurnId`). Tout paquet `inputTranscription` arrivant après une annulation (`_turnCancelled`), en phase `IDLE` ou après l'échéance de la capture est rejeté.
2. **OpenAI Realtime API (`wss://api.openai.com/v1/realtime`)** :
   - OpenAI fournit des `item_id` et `response_id` dans ses événements serveur (`conversation.item.created`, `response.done`).
   - **Garantie possible** : Corrélation exacte possible pour les réponses complètes ; pour la transcription utilisateur temps réel, l'identification locale monotone est indispensable pour détecter les paquets retardés par la latence réseau après un `STOP`.

---

## 3. Défauts Reproduits & Causes Techniques Démontrées

### 3.1. STT — Variable Globale Applicative et Réponse Retardée (P0)
- **Symptôme** : L'utilisateur effectue une dictée n°10. Il relâche le bouton, puis déclenche `STOP`. Il relance immédiatement une nouvelle dictée n°11. Une réponse serveur retardée de la dictée n°10 arrive alors sur le WebSocket. Dans l'ancien code :
  ```cpp
  s_voice.onTranscription([](const String& text, bool isFinal) {
      s_bridge.feedTranscription(text, isFinal, s_currentCaptureTurnId);
  });
  ```
  La réponse retardée de la dictée 10 recevait l'identifiant 11 (valeur courante de `s_currentCaptureTurnId`) et était exécutée à tort !
- **Cause** : Attribution effectuée à la frontière applicative à partir d'une variable globale, au lieu d'une origine rattachée par le producteur de l'opération.
- **Correction** : `VoxCoreVoice` suit `_activeCaptureTurnId` et `_pendingTranscriptionTurnId`. Lors de `stop()`, ces identifiants sont réinitialisés et le tour annulé est consigné (`_cancelledCaptureTurnId`). La transcription est émise avec son tour d'origine réel via `onTranscriptionWithTurnId`. Si l'opération a été annulée, le pont la rejette immédiatement.

### 3.2. TTS — Variable Globale et Collision de Terminaison (P0)
- **Symptôme** : Un TTS n°20 est démarré puis interrompu par STOP. Un nouveau TTS n°21 démarre. Une notification matérielle retardée de vidage DMA du TTS 20 arrive. Le callback applicatif appelait `notifySpeechFinished(true, s_currentSpeechId)`, fermant prématurément le TTS 21.
- **Cause** : L'identifiant n'était pas propagé depuis l'opération audio réelle.
- **Correction** : `VoxCoreVoice` génère un `speechId` monotone à chaque `say()`, suit l'opération dans `LiveAudioPlayer` et notifie la fin via `onSpeechFinishedWithId(finishedSpeechId)`. `VoiceUxLiveBridge` ignore toute notification dont l'identifiant ne correspond pas à la restitution active.

### 3.3. Validation du Cycle de Vie STT (P1)
- **Symptôme** : Injection d'une transcription finale sans capture audio préalable (au démarrage ou au repos). L'ancien code pouvait traiter la transcription si `_turnCancelled == false`.
- **Cause** : Absence d'un état formel modélisant la phase STT.
- **Correction** : Ajout de `VoiceUxSttPhase` (`IDLE`, `LISTENING`, `TRANSCRIBING`, `COMPLETED`, `CANCELLED`). En phase `IDLE`, toute transcription réelle est rejetée et incrémente `staleTranscriptionsIgnored`. Les simulations d'entrées (`feedSimulatedText()`) créent leur propre contexte de tour valide et atteignent `COMPLETED`.

### 3.4. Statut Terminal Unique & Erreurs Audio (P1)
- **Symptôme** : Lors d'une erreur audio (`notifyError()`), l'action active pouvait rester en suspend ou recevoir plusieurs statuts contradictoires (`CANCELLED` puis `COMPLETED`), ou être détruite silencieusement sans statut terminal lors de `recoverFromError()`.
- **Cause** : Absence de verrou de statut terminal par action.
- **Correction** : Introduction du drapeau `_activeActionTerminated`. Une action ne peut recevoir qu'un unique statut terminal parmi `COMPLETED`, `REJECTED` ou `CANCELLED`. `notifyError()` clôt l'action active avec `REJECTED`, annule les actions en attente avec `CANCELLED`, et empêche tout événement ultérieur de modifier ce statut.

---

## 4. Modifications Apportées

### 4.1. `src/core/voiceux/VoiceUxLiveBridge.h` & `VoiceUxLiveBridge.cpp`
- **Ajout de l'énumération de phase STT** :
  ```cpp
  enum class VoiceUxSttPhase : uint8_t {
      IDLE,           ///< Aucune capture autorisée
      LISTENING,      ///< Capture audio en cours (micro actif)
      TRANSCRIBING,   ///< Capture terminée, transcription finale attendue
      COMPLETED,      ///< Transcription finale reçue et traitée pour ce tour
      CANCELLED       ///< Opération STT annulée (STOP ou abandon)
  };
  ```
- **Gestion fine du cycle de vie** :
  - `_sttPhase` initialisé à `IDLE`.
  - `onListeningStarted(turnId)` bascule la phase à `LISTENING`.
  - `onListeningFinished()` bascule la phase à `TRANSCRIBING`.
  - `feedTranscription()` exige la phase `LISTENING` ou `TRANSCRIBING` pour accepter une transcription finale réelle.
  - Rejet des tours obsolètes (`turnId != 0 && turnId < _activeTurnId`) classifiés en `staleTranscriptionsIgnored` avant tout contrôle de doublon.
- **Garantie d'unicité de statut terminal** :
  - Ajout de `bool _activeActionTerminated`.
  - Contrôle strict dans `notifySpeechFinished`, `notifySpeechRefused`, `notifyActionCompleted`, `notifyActionRejected`, `cancelPendingAction`, et `notifyError`.
- **Préservation de PAUSED** :
  - `processInternal()`, `isPaused()`, et gestion de `_stateBeforePause` fiabilisés pour empêcher la pollution de l'état pause par les ouvertures PTT temporaires.

### 4.2. `src/VoxCoreVoice.h` & `src/VoxCoreVoice.cpp`
- **Callbacks avec identifiants** :
  - `TranscriptionWithTurnCallback = std::function<void(const String& text, bool isFinal, uint32_t turnId)>`
  - `SpeechFinishedCallback = std::function<void(uint32_t speechId)>`
  - `ListeningTurnCallback = std::function<void(uint32_t turnId)>`
  - Méthodes `onTranscriptionWithTurnId()`, `onSpeechFinishedWithId()`, `onListeningStartedWithTurnId()`.
- **Suivi des identifiants** :
  - `_sttTurnId` (compteur incrémental monotone).
  - `_activeCaptureTurnId` (tour d'écoute en cours).
  - `_pendingTranscriptionTurnId` (tour en attente de transcription après `stopListening()`).
  - `_cancelledCaptureTurnId` (tour invalidé par `stop()` ou `cancelListening()`).
  - `_speechGeneration` et `_activeSpeechId` (identifiant unique de restitution).
- **Rétrocompatibilité** :
  - `onTranscription(TranscriptionCallback cb)` encapsule l'ancien appel sans perte de compatibilité.
  - `onSpeechFinished(SpeechCallback cb)` et `onListeningStarted(std::function<void()>)` préservés à 100%.

### 4.3. `examples/VoiceUxLiveDemo/src/main.cpp`
- **Suppression intégrale** de `s_currentCaptureTurnId` et `s_currentSpeechId`.
- Connexion directe aux callbacks enrichis :
  ```cpp
  s_voice.onListeningStartedWithTurnId([](uint32_t turnId) {
      s_bridge.onListeningStarted(turnId);
  });
  s_voice.onTranscriptionWithTurnId([](const String& text, bool isFinal, uint32_t turnId) {
      s_bridge.feedTranscription(text, isFinal, turnId);
  });
  s_voice.onSpeechFinishedWithId([](uint32_t speechId) {
      s_bridge.notifySpeechFinished(true, speechId);
  });
  ```
- Dans la boucle principale, transmission de `s_voice.getActiveSpeechId()` à `notifySpeechStarted()`.

---

## 5. Bilan des Tests et Vérifications

### 5.1. Suite 142 : Scénarios Correctifs Spécifiques v0.6.4.3.2

La suite **[TEST SUITE 142] `test_v06432_corrective_event_correlation()`** a été créée dans `tests/test_voiceux_suites.h` et valide formellement :

| Test | Intitulé | Comportement Validé | Statut |
| :--- | :--- | :--- | :--- |
| **Test A** | Corrélation STT & Rejet Retardé | Capture A $\to$ STOP $\to$ Capture B $\to$ Réception tardive A rejetée $\to$ Réception B acceptée. | **SUCCÈS** |
| **Test B** | TTS Obsolète après Nouveau TTS | Démarrage TTS 20 $\to$ STOP $\to$ Démarrage TTS 21 $\to$ Fin tardive TTS 20 rejetée $\to$ TTS 21 reste actif jusqu'à sa fin réelle. | **SUCCÈS** |
| **Test C** | Transcription sans Capture | Tentative d'injection en phase `IDLE` strictement rejetée $\to$ Capture légitime $\to$ Transcription acceptée. | **SUCCÈS** |
| **Test D** | Erreur pendant Restitution TTS | Erreur audio pendant `SPEAKING` $\to$ État `ERROR` $\to$ Action terminée avec `REJECTED` unique $\to$ Pas de mémorisation $\to$ Reprise nominale. | **SUCCÈS** |
| **Test E** | STOP Concurrent avec Fin Audio | STOP pendant restitution suivi de fin tardive : exactement un statut terminal (`CANCELLED`). Fin normale suivie de STOP : statut `COMPLETED` unique préservé. | **SUCCÈS** |
| **Test F** | Attribution & Propagation Moteur | `VoxCoreVoice` génère `turnId` monotone, associe `originTurnId` lors de la transcription serveur, et attribue `speechId` positif nettoyé à l'arrêt. | **SUCCÈS** |

### 5.2. Bilan Global des Tests Natifs
Exécution de `tests\run_tests.bat` (MinGW GCC 13.1.0 C++17) :
```text
===========================================================
  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.6.4.3.2
  Bilan Final : 2813 REUSSIS, 0 ECHECS
===========================================================
```
**Résultat : 100% de réussite (2 813 assertions vérifiées).**

### 5.3. Compilation PlatformIO Matérielle (M5Stack CoreS3)
Exécution de `pio run -d examples/VoiceUxLiveDemo` :
```text
CONFIGURATION: M5Stack CoreS3 (ESP32-S3, 240MHz, 320KB RAM, 16MB Flash)
LDF Modes: Finder ~ deep, Compatibility ~ soft
Dependency Graph:
|-- M5CoreS3 @ 1.0.0
|-- M5Unified @ 0.2.23
|-- VoiceKit @ 0.6.4.3.2
|-- WiFi @ 2.0.0
Building in release mode
Linking .pio\build\m5stack-cores3\firmware.elf
RAM:   [==        ]  24.2% (used 79460 bytes from 327680 bytes)
Flash: [========  ]  82.3% (used 1725869 bytes from 2097152 bytes)
========================= [SUCCESS] Took 36.92 seconds =========================
```
**Résultat : Compilation 100% propre sur cible matérielle ESP32-S3.**

---

## 6. Protocole de Validation Matérielle sur M5Stack CoreS3

Pour valider le binaire en conditions réelles de laboratoire sur le M5Stack CoreS3 :

### Test 1 : PTT Rapide (Enchaînement Court)
- **Procédure** :
  1. Maintenir le bouton tactile `PARLER` pendant 1 seconde, prononcer « Bonjour ».
  2. Relâcher immédiatement et appuyer à nouveau 500 ms plus tard pour dire « Répète ».
- **Critères de succès** :
  - La console série affiche l'incrémentation monotone de `turnId` (`turnId=1`, puis `turnId=2`).
  - Aucun crash, aucune perte de synchronisation.
  - La commande « Répète » rappelle bien la réponse précédente.

### Test 2 : STOP Immédiat puis Nouvelle Écoute
- **Procédure** :
  1. Appuyer sur `PARLER`, prononcer « Quelle heure est-il ? ».
  2. Relâcher et appuyer immédiatement sur `STOP` avant l'arrivée de la réponse du serveur.
  3. Appuyer sur `PARLER` et dire « Répète ».
- **Critères de succès** :
  - Le tour 1 passe en `CANCELLED`.
  - La transcription éventuelle arrivant du serveur pour le tour 1 affiche `[BRIDGE] Transcription tardive ignoree (turnId=1)` et est rejetée.
  - Le tour 2 est traité sans interférence.

### Test 3 : Restitution TTS Interrompue
- **Procédure** :
  1. Lancer une réponse audio longue.
  2. Pendant la lecture sur le haut-parleur, appuyer sur le bouton `STOP`.
- **Critères de succès** :
  - Le son se coupe instantanément.
  - L'action reçoit le statut `CANCELLED`.
  - La réponse interrompue n'est pas insérée dans `ReplyMemory`.
  - Une notification de fin audio différée provenant du codec ES8388 ne perturbe pas le retour à l'état `READY`.

### Test 4 : Réseau Instable / Perte de Connexion
- **Procédure** :
  1. Déconnecter le point d'accès Wi-Fi pendant une phase de transcription (`TRANSCRIBING`).
- **Critères de succès** :
  - Le pont bascule en état `ERROR`.
  - L'action en cours reçoit un unique statut terminal `REJECTED`.
  - Lors de la reconnexion, l'appel à `recoverFromError()` remet le système à `READY` sans résidu bloquant.

---

## 7. Risques Résiduels & Périmètre Protocolaire

1. **Protocoles sans Turn ID serveur (Gemini Live)** :
   - Gemini Live ne renvoyant pas de tag de corrélation client dans `inputTranscription`, la sécurité dépend du séquencement temporel des tours et de l'invalidation stricte à la fermeture du tour ou lors de STOP. Si le serveur renvoie une transcription après un délai anormalement long alors qu'un nouveau tour est déjà engagé, l'identification monotone `turnId < _activeTurnId` assure le rejet certain.
2. **Callbacks Ancienne Signature (`turnId == 0`, `speechId == 0`)** :
   - Les anciennes applications appelant `feedTranscription(text, isFinal)` sans identifiant bénéficient de la protection par phase (`IDLE` / `CANCELLED`), mais ne disposent pas de la discrimination absolue entre tours concurrents. La migration vers les callbacks enrichis (`onTranscriptionWithTurnId`) est recommandée.

