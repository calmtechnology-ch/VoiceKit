# Rapport Correctif & Stabilisation — VoiceKit v0.6.4.3

**Date :** 10 Novembre 2026  
**Auteur :** Développeur Principal Embarqué (ESP32-S3 / C++17)  
**Version cible :** `0.6.4.3`  
**Version de référence :** `0.6.4.2`  
**Cible matérielle :** M5Stack CoreS3 (ESP32-S3, 8 Mo PSRAM, 16 Mo Flash)  
**Environnement de build :** PlatformIO Core 6.2.0, Espressif32 6.9.0, GCC/MinGW C++17  

---

## 1. Contexte & Objectif

L'objectif de cette version **v0.6.4.3** est de fiabiliser et stabiliser le code existant de **VoiceKit**, sans ajouter de nouvelles fonctionnalités majeures, sans réécrire le moteur vocal (`VoxCoreVoice`, `LiveAudioStreamer`, `LiveAudioPlayer`) ni modifier l'architecture audio sous-jacente.

L'audit technique a identifié des anomalies de criticité **P0** (sécurité des jetons dans `ToolRegistry`) et **P1** (cycle de vie des métadonnées de restitution dans `VoiceUxLiveBridge`, concurrence d'événements PTT dans `VoiceUxLiveDemo`, et portabilité de la suite de tests).

---

## 2. Analyse Détaillée des Défauts et Corrections Appliquées

### 2.1. Correction P0 — Sécurisation de ToolRegistry

#### Défaut Initial Constaté
L'exécution d'une commande sensible avec un jeton de confirmation ne garantissait pas que les paramètres exécutés correspondaient exactement à ceux autorisés initialement.
Plus précisément :
1. Dans `execute(id, args, confirmationToken)`, la fonction acceptait n'importe quels `args` fournis lors du deuxième appel et les transmettait à l'exécuteur, ignorant les arguments validés lors de la génération du jeton.
2. Lors d'un appel à `confirm(token)`, les autorisations (`AuthorizationChecker`) n'étaient pas revalidées au moment de l'exécution, créant une faille si les droits avaient été révoqués entre-temps.
3. Si la commande avait été désenregistrée (`unregisterTool`), le comportement n'était pas formellement encadré.
4. Si l'exécuteur était occupé (`isBusy()`), le jeton était prématurément marqué comme consommé (`used = true`), interdisant toute réexécution légitime une fois l'exécuteur libre.
5. Si la fonction de commande échouait (`EXECUTION_FAILED`), le statut de consommation du jeton n'était pas garanti contre le rejeu multiple.
6. Le conteneur de confirmations en attente `_pendingConfirmations` n'était pas borné, exposant l'ESP32 à une saturation de la mémoire vive (RAM).
7. `cancelPending()` et `clear()` n'annulaient les tâches différées que sur l'exécuteur par défaut (`_defaultExecutor`), ignorant un éventuel exécuteur personnalisé (`_customExecutor`).

#### Cause Technique
- Absence d'opérateur d'égalité sur `ToolArgs` et absence de vérification de conformité entre les arguments stockés dans `PendingConfirmation` et les arguments fournis lors de `execute(..., token)`.
- Absence d'appel à `_authChecker` dans la méthode `confirm()`.
- Positionnement de `target->used = true` avant le test `exec.isBusy()`.
- Absence d'une capacité maximale sur la file `_pendingConfirmations`.
- `IToolExecutor` ne déclarait pas de méthode `cancelDeferred()`.

#### Correction Appliquée
1. **Opérateur d'égalité sur `ToolArgs`** : Ajout de `bool operator==(const ToolArgs&) const` et `operator!=` dans `ToolDefinition.h`.
2. **Immuabilité & Vérification Stricte** :
   - `execute(id, args, token)` compare strictement `args` avec `target->args`. En cas de discordance, rejet immédiat avec `PERMISSION_DENIED` (« Arguments non conformes à ceux autorisés lors de la demande de confirmation »).
   - `confirm(token)` et `execute(id, args, token)` utilisent obligatoirement la requête conservée `target->args`.
3. **Revalidation Systématique des Autorisations** :
   - Vérification de l'existence de l'outil via `findTool(target->toolId)` (retourne `NOT_FOUND` si supprimé).
   - Recontrôle de `_authChecker(*def, target->args)` (retourne `PERMISSION_DENIED` si révoqué).
4. **Gestion Déterministe de `isBusy()`** :
   - Le jeton n'est marqué `used = true` que si `!exec.isBusy()`, permettant de retenter la confirmation dès libération.
5. **Protection Anti-Rejeu après Erreur** :
   - Une fois l'exécution engagée, `target->used` reste `true` même en cas d'erreur `EXECUTION_FAILED`, empêchant toute seconde tentative avec le même jeton.
6. **File Bornée Anti-Saturation RAM** :
   - Constante `DEFAULT_MAX_PENDING_CONFIRMATIONS = 8` et accesseurs `getMaxPendingConfirmations()` / `setMaxPendingConfirmations()`.
   - Purge automatique des jetons expirés et rejet avec statut `BUSY` si la file reste saturée.
7. **Délégation Complète de `cancelDeferred()`** :
   - Ajout de `virtual void cancelDeferred() {}` dans `IToolExecutor`, implémenté dans `ToolExecutor`.
   - `ToolRegistry::clear()` et `cancelPending()` appellent `getExecutor().cancelDeferred()`.

---

### 2.2. Correction P1 — Fiabilisation de VoiceUxLiveBridge

#### Défaut Initial Constaté
1. **Défaut A (Métadonnées TTS perdues)** :
   `consumePendingAction()` effaçait `_pendingAction` immédiatement. Lors de l'appel subséquent à `notifySpeechStarted()`, la fonction interrogeait `_pendingAction.isRecall` et `_pendingAction.isFeedback` qui valaient désormais systématiquement `false`.
   Conséquence directe : lors de `notifySpeechFinished(true)`, les réponses répétées (`REPEAT`) et les feedbacks système (« Aucune réponse... ») étaient ré-enregistrés dans `ReplyMemory` comme de nouvelles réponses de l'assistant, polluant l'historique conversationnel.
2. **Défaut B (Transitions d'états incohérentes)** :
   - Si `s_voice.say()` échouait, le pont pouvait rester bloqué dans l'état `SPEAKING` sans aucun moyen de récupération déterministe.
   - Les transitions lors d'un STOP pendant `LISTENING`, `TRANSCRIBING` ou `ACTION_PENDING` ne garantissaient pas le nettoyage complet de l'action en attente.
   - Absence de suivi explicite des étapes du cycle de vie de l'action.
3. **Défaut C (Actions consommées prématurément)** :
   `consumePendingAction()` déclenchait le callback de fin d'action `_actionExecutedCb` dès l'extraction de la file, alors que la restitution matérielle n'avait même pas encore commencé ni réussi.

#### Cause Technique
- Absence d'un état intermédiaire ou d'un membre `_activeAction` persistant entre le dépilage et la fin de l'audio.
- Absence d'une méthode de signalement de refus TTS (`notifySpeechRefused`).
- Déclenchement de `_actionExecutedCb` dans `consumePendingAction()`.

#### Correction Appliquée
1. **Cycle de Vie des Métadonnées TTS** :
   - Ajout de `_activeAction` et `_currentSpeechTurnId` dans `VoiceUxLiveBridge`.
   - Lors de `consumePendingAction()`, `_activeAction` reçoit une copie complète de l'action (`text`, `turnId`, `isRecall`, `isFeedback`, `type`, `intent`).
   - `notifySpeechStarted()` initialise fidèlement `_currentSpeechIsRecall` et `_currentSpeechIsFeedback` depuis `_activeAction`.
   - `notifySpeechFinished(completed)` :
     - Si `completed == true` et `!isRecall && !isFeedback`, mémorisation dans `ReplyMemory`.
     - Si `isRecall == true`, la réponse répétée n'est **pas** ré-insérée dans `ReplyMemory`.
     - Si `isFeedback == true`, le message système ne pollue **pas** l'historique.
     - Si `completed == false` (interruption STOP), la réponse partielle n'est **pas** mémorisée.
2. **Absence de Blocage & Récupération Déterministe** :
   - Ajout de `notifySpeechRefused(const String& reason)` : en cas d'échec de `say()`, le pont réinitialise immédiatement son contexte de parole et retourne à l'état `READY` (ou `PAUSED`).
   - `triggerPriorityStop()` annule les actions en attente, nettoie le tour et réinitialise l'état à `READY` (ou `PAUSED`).
   - `recoverFromError()` réinitialise l'état après erreur technique (`ERROR` $\to$ `READY`).
   - Rejet déterministe des transcriptions partielles, des transcriptions finales dupliquées pour un même tour, et des événements tardifs ou annulés.
3. **Statuts Fins d'Action & Découplage des Callbacks** :
   - Définition de l'énumération `VoiceUxActionStatus` (`CREATED`, `DISPATCHED`, `ACCEPTED`, `COMPLETED`, `REJECTED`, `CANCELLED`).
   - Callback d'observation `onActionStatus(ActionStatusCallback cb)`.
   - `_actionExecutedCb` n'est plus appelé dans `consumePendingAction()`, mais uniquement lors de la terminaison effective de l'action (`notifySpeechFinished(true)` pour TTS, ou `notifyActionCompleted()` pour les actions locales STOP, PAUSE, RESUME, FORWARD_AI).

---

### 2.3. Correction P1 — VoiceUxLiveDemo et Push-to-Talk (PTT)

#### Défaut Initial Constaté
Dans `examples/VoiceUxLiveDemo/src/main.cpp`, les notifications de démarrage et d'arrêt de capture microphone étaient émises en double :
- Une première fois directement depuis le gestionnaire tactile (`touch.wasPressed()`) ou série (`'P'`).
- Une deuxième fois depuis les callbacks du moteur vocal (`s_voice.onListeningStarted()`).
De plus, `s_touchListening` passait à `true` avant de savoir si `s_voice.startListening()` avait accepté la demande, et un échec de `s_voice.say()` laissait le pont dans l'état `SPEAKING`.

#### Cause Technique
- Manque de séparation entre la demande applicative et la confirmation de transition par le moteur audio.

#### Correction Appliquée
1. **Source Unique de Vérité des Transitions Microphone** :
   - L'interface tactile ou série se contente de demander `s_voice.startListening()` ou `s_voice.stopListening()`.
   - Seuls les callbacks confirmés du moteur (`s_voice.onListeningStarted()` et `s_voice.onListeningFinished()`) notifient `s_bridge.onListeningStarted()` et `s_bridge.onListeningFinished()`.
2. **Fiabilisation de `s_touchListening`** :
   - `s_touchListening` ne passe à `true` que si `s_voice.startListening()` a renvoyé `true`.
3. **Contrôle des Retours & Non-Blocage TTS** :
   - Appel de `s_voice.say(act.text)`. En cas d'échec (`false`), appel immédiat de `s_bridge.notifySpeechRefused()`.
   - L'agencement graphique, les dimensions et les fonctionnalités de l'écran tactile demeurent inchangés.

---

### 2.4. Exécution Différée & Portabilité de la Distribution

1. **Option A (ToolExecutor)** :
   Documentation formelle dans `ToolExecutor.h` précisant que `enqueueDeferred()` s'exécute de façon synchrone dans `update()`, et imposant des handlers courts (< 10-20 ms) pour respecter les contraintes audio temps réel sur ESP32-S3.
2. **Reproductibilité des Tests (`tests/run_tests.bat`)** :
   Remplacement du chemin absolu en dur (`C:\Users\phall\...`) par une résolution dynamique et ordonnée :
   1. Variable d'environnement `%CXX%`
   2. Variable d'environnement `%GCC%`
   3. Compilateur `g++` présent dans le `PATH`
   4. Compilateur MinGW du dossier utilisateur PlatformIO (`%USERPROFILE%\.platformio\...`)
3. **Tests Mock (`tests/native_mock/Arduino.h`)** :
   Ajout des wrappers portables Windows/MinGW pour `localtime_r` et `gmtime_r`, assurant la compilation native sur tout environnement Windows GCC/MinGW sans erreur POSIX.
4. **Validation Consommateur Externe (`tests/external-consumer`)** :
   Remplacement de la dépendance Git obsolète `v0.3.0` par une référence locale `symlink://../../` validant l'intégration de la version courante de VoiceKit.
5. **Alignement des Versions** :
   - `library.json` : `0.6.4.3`
   - `library.properties` : `0.6.4.3`
   - `src/core/VoiceHubConfig.h` : `VOICEKIT_VERSION = "v0.6.4.3"`
   - `README.md` & `CHANGELOG.md` mis à jour.

---

## 3. Matrice Complète des Tests de Validation

### 3.1. Suites de Tests Automatisées Natives (G++ / MinGW C++17)

Exécution via `tests/run_tests.bat` :

| Suite | Description | Nombre d'Assertions | Résultat |
|---|---|---|---|
| **Suites 1 à 129** | Tests historiques VoiceKit (Audio, Streaming, VAD, Gemini Live, OpenAI Realtime, WakeNet, ToolRegistry) | 2404 | **SUCCÈS (0 échec)** |
| **Suites 130 à 134** | Voice UX Foundation v0.6.4.1 (TextGate, IntentRouter, ReplyMemory, Controller, Métriques) | 100 | **SUCCÈS (0 échec)** |
| **Suites 135 à 138** | Voice UX Live Bridge v0.6.4.2 (STT/TTS, Pause/Resume, Concurrence, Sûreté des tours) | 75 | **SUCCÈS (0 échec)** |
| **Suite 139 (Nouvelle)** | **v0.6.4.3 : ToolRegistry (Sécurité, Immuabilité, Anti-Rejeu, Auth, Concurrence, File)** | 48 | **SUCCÈS (0 échec)** |
| **Suite 140 (Nouvelle)** | **v0.6.4.3 : VoiceUxLiveBridge (Métadonnées TTS, Dédoublonnage, Refus, STOP, Statuts)** | 35 | **SUCCÈS (0 échec)** |
| **TOTAL** | **140 suites de tests automatiques** | **2662** | **2662 RÉUSSIS, 0 ÉCHEC** |

### 3.2. Détail des Validations de la Suite 139 (ToolRegistry)

1. **Confirmation valide** : Commande sensible `system.reboot` émettant un jeton, exécutée avec succès après `confirm()`.
2. **Confirmation expirée** : Avancement artificiel du temps (+35 s) ; `confirm()` renvoie `PERMISSION_DENIED` citant l'expiration.
3. **Protection anti-rejeu** : Deuxième appel à `confirm()` avec le même jeton rejeté avec `PERMISSION_DENIED`.
4. **Jeton d'une autre commande** : Jeton émis pour `tool.a` refusé pour `tool.b`.
5. **Modification des arguments** : Jeton émis pour `storage.format` avec `drive="SD"` ; appel avec `drive="FLASH_SYSTEM"` rejeté avec `PERMISSION_DENIED` (« Arguments non conformes »). Appel ultérieur légitime avec arguments d'origine réussi.
6. **Suppression de commande** : Outil désenregistré avant confirmation ; `confirm()` renvoie `NOT_FOUND`.
7. **Modification d'autorisation** : Révocation de l'accès dans `AuthorizationChecker` avant confirmation ; `confirm()` renvoie `PERMISSION_DENIED`.
8. **Exécuteur occupé (`BUSY`)** : `confirm()` renvoie `BUSY` sans invalider le jeton. Dès libération de l'exécuteur, `confirm()` réussit.
9. **Confirmation répétée après erreur** : Outil renvoyant `EXECUTION_FAILED` ; le jeton reste consommé et une seconde tentative est rejetée par la protection anti-rejeu.
10. **File de confirmations bornée** : Limitation à 3 requêtes en attente ; la 4ᵉ tentative retourne `BUSY` (« File de confirmations en attente saturée »).
11. **Annulation custom executor** : Vérification que `cancelDeferred()` est correctement propagé sur l'exécuteur actif.

### 3.3. Détail des Validations de la Suite 140 (VoiceUxLiveBridge)

1. **Séquence STT $\to$ REPEAT $\to$ TTS $\to$ READY** : Métadonnées `isRecall` préservées dans `_activeAction` et pendant toute la restitution active.
2. **Préservation de `ReplyMemory` sur répétition** : La réponse répétée n'est pas ré-enregistrée dans la mémoire (compte maintenu à 2).
3. **Feedback système sans pollution** : « Aucune réponse à répéter. » marqué `isFeedback` et non mémorisé (mémoire reste vide).
4. **Séquence STOP à chaque étape** :
   - STOP pendant `LISTENING` $\to$ retour immédiat à `READY`, capture ré-autorisée.
   - STOP pendant `TRANSCRIBING` $\to$ retour à `READY`.
   - STOP pendant `ACTION_PENDING` $\to$ retour à `READY`, action annulée.
5. **Séquence Erreur réseau $\to$ Récupération $\to$ Nouvelle écoute** : `notifyError()` $\to$ `recoverFromError()` $\to$ nouvel appel `onListeningStarted()` accepté sans blocage.
6. **Séquence Refus TTS** : `notifySpeechRefused()` réinitialise le pont et autorise immédiatement une nouvelle écoute sans bloquer en `SPEAKING`.
7. **Séquence STOP pendant restitution** : Restitution interrompue non mémorisée dans `ReplyMemory` et microphone libéré.
8. **Dédoublonnage des transcriptions finales** : Deuxième transcription pour le même tour rejetée.
9. **Transcription tardive après annulation** : Transcription avec ancien `turnId` annulé rejetée.
10. **Statuts d'action** : Transition ordonnée `CREATED` $\to$ `DISPATCHED` $\to$ `ACCEPTED` $\to$ `COMPLETED`.

---

## 4. Résultats de Compilation PlatformIO (M5Stack CoreS3)

Tous les projets cibles ont été compilés avec succès pour ESP32-S3 sous PlatformIO :

1. **`examples/VoiceUxLiveDemo`** :
   - Résultat : **SUCCESS** (durée : 12.92 s)
   - RAM utilisée : 79 348 octets (24.2 %)
   - Flash utilisée : 1 722 149 octets (82.1 %)
2. **`examples/LocalToolRegistryDemo`** :
   - Résultat : **SUCCESS** (durée : 18.94 s)
   - RAM utilisée : 23 232 octets (7.1 %)
   - Flash utilisée : 555 121 octets (26.5 %)
3. **`tests/external-consumer`** :
   - Résultat : **SUCCESS** (durée : 59.41 s)
   - RAM utilisée : 78 872 octets (24.1 %)
   - Flash utilisée : 1 689 817 octets (80.6 %)

---

## 5. Liste des Fichiers Modifiés

| Fichier | Nature des Modifications |
|---|---|
| `src/core/tools/ToolDefinition.h` | Opérateurs `==` et `!=` sur `ToolArgs` |
| `src/core/tools/ToolExecutor.h` | Méthode virtuelle `cancelDeferred()`, documentation Option A |
| `src/core/tools/ToolRegistry.h` | File de confirmations bornée, accesseurs, délégation `cancelDeferred()` |
| `src/core/tools/ToolRegistry.cpp` | Immuabilité des arguments de confirmation, revalidation auth, protection anti-rejeu, gestion `BUSY` |
| `src/core/voiceux/VoiceUxLiveBridge.h` | Énumération `VoiceUxActionStatus`, `_activeAction`, `notifySpeechRefused`, accesseurs métadonnées TTS |
| `src/core/voiceux/VoiceUxLiveBridge.cpp` | Cycle de vie des métadonnées TTS, protection de `ReplyMemory` sur REPEAT/feedback, gestion d'erreurs |
| `examples/VoiceUxLiveDemo/src/main.cpp` | Source unique de vérité PTT, sécurisation de `s_touchListening`, gestion refus `say()` |
| `tests/native_mock/Arduino.h` | Définitions portables Windows MinGW pour `localtime_r` et `gmtime_r` |
| `tests/run_tests.bat` | Résolution dynamique et portable du compilateur C++ |
| `tests/test_voiceux_suites.h` | Nouvelles suites 139 (ToolRegistry) et 140 (VoiceUxLiveBridge) |
| `tests/test_voicekit_all.cpp` | Mise à jour de la vérification de constante de version vers `v0.6.4.3` |
| `tests/external-consumer/platformio.ini` | Dépendance `symlink://../../` remplaçant l'ancien tag v0.3.0 |
| `tests/external-consumer/src/main.cpp` | Bannière mise à jour vers v0.6.4.3 |
| `src/core/VoiceHubConfig.h` | Constante `VOICEKIT_VERSION` mise à jour vers `v0.6.4.3` |
| `library.json` | Version `0.6.4.3` et description mise à jour |
| `library.properties` | Version `0.6.4.3` et manifestes mis à jour |
| `README.md` | Titre, section v0.6.4.3 et documentation des correctifs |
| `CHANGELOG.md` | Section détaillée pour la version `0.6.4.3` |
| `docs/README.md` | Référencement du présent rapport |

---

## 6. Risques Résiduels & Tests Matériels Recommandés

### 6.1. Risques Résiduels
- **Durée des Handlers Différés (Option A)** : Conformément aux spécifications retenues, l'exécution des tâches différées via `ToolExecutor` est synchrone dans `update()`. Un handler utilisateur effectuant un blocage long (ex: écriture flash synchrone > 50 ms) induirait une saccade dans la tâche audio. Les handlers doivent rester courts (< 10-20 ms).
- **Statut Expérimental de SLOWER et REPHRASE** : Ces deux intentions restent reconnues par `VoiceIntentRouter` et diagnostiquées dans `VoiceUxLiveBridge`, mais ne modifient pas encore la fréquence audio du lecteur (prévu pour une version ultérieure).

### 6.2. Tests Matériels Recommandés sur CoreS3
Bien que la compilation firmware et les tests logiques automatisés soient validés à 100%, les tests physiques suivants devront être exécutés sur banc M5Stack CoreS3 physique :
1. **Épreuve d'endurance tactile PTT** : 50 appuis/relâchements rapides sur l'écran tactile pour valider l'absence de rebond acoustique I2S.
2. **Test acoustique STOP en environnement bruité** : Déclenchement de « STOP » vocal pendant une restitution audio à volume maximal (70 %) pour vérifier l'efficacité de l'annulation d'écho / AEC du codec ES7210.
3. **Confirmation d'outil sensible sous coupure réseau** : Génération d'un jeton sensible, coupure du point d'accès Wi-Fi, puis validation par bouton tactile pour vérifier que la commande locale s'exécute sans dépendre du réseau.

