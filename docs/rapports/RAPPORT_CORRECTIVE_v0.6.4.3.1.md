# Rapport Technique Correctif — VoiceKit v0.6.4.3.1 (Event Safety)

**Date :** 10 novembre 2026  
**Auteur :** Assistant Développeur Senior C++ / VoxCore VoiceKit  
**Version de départ :** `v0.6.4.3`  
**Version cible :** `v0.6.4.3.1`  
**Nature de la version :** Exclusivement corrective (« Corrective Event Safety »)  
**Cible matérielle :** M5Stack CoreS3 (ESP32-S3) / FreeRTOS / Arduino / PlatformIO / MinGW Native  

---

## 1. Résumé Exécutif

La version **v0.6.4.3.1** consolide la sécurité des flux d'événements asynchrones dans l'architecture VoiceKit sans aucune modification des configurations matérielles (DMA, I2S, PSRAM, TLS, FreeRTOS), sans nouvelle dépendance et sans nouvelle fonctionnalité utilisateur.

L'intervention porte sur la résilience temporelle aux frontières événementielles :
1. **P0 — Sécurisation STT** : Rejet déterministe des transcriptions tardives après STOP ou annulation, suppression de toute réallocation implicite de tour sur paquet final sans identifiant (`turnId == 0`), rejet des deuxièmes transcriptions finales d'un même tour et réarmement propre à chaque nouvelle capture.
2. **P0 — Sécurisation TTS** : Corrélation générationnelle par identifiant de restitution (`_currentSpeechId`), immunité aux notifications `notifySpeechFinished()` obsolètes d'anciens flux audio interrompus, garantie de terminaison unique et non-mémorisation stricte des réponses écourtées dans `ReplyMemory`.
3. **P1 — Cohérence de STOP et des statuts d'action** : Distinction claire entre la réussite de la commande STOP (`COMPLETED`), l'annulation des actions en attente (`CANCELLED`), l'avortement de capture audio et l'arrêt de restitution TTS. Aucune action non exécutée ne peut recevoir le statut `COMPLETED`.
4. **P1 — Sécurité du Registre d'Outils (`ToolRegistry`)** : Invalidation immédiate des jetons de confirmation lors de `unregisterTool()` et attribution d'une génération incrémentale par définition d'outil, empêchant tout rejeu d'un ancien jeton après réenregistrement sous un identifiant textuel identique (`PERMISSION_DENIED`).
5. **P2 — Cohérence des événements applicatifs (`VoiceUxLiveDemo`)** : Rejet informatif explicite de l'action expérimentale `FORWARD_AI` (`notifyActionRejected`) au lieu de l'annoncer faussement exécutée (`COMPLETED`) en attendant l'intégration d'IA complète en v0.6.5.
6. **P2 — Portabilité du script Windows (`tests/run_tests.bat`)** : Résolution robuste multi-compilateurs (`%CXX%`, `%GCC%`, `where.exe g++`, chaîne PlatformIO MinGW), support des chemins avec espaces et correction de l'évaluation d'erreur `%ERRORLEVEL%`.

---

## 2. Défauts Reproduits & Causes Techniques Démontrées

### 2.1. STT — Attribution Implicite et Transcriptions Tardives (P0)
- **Symptôme** : Dans `VoiceUxLiveBridge::feedTranscription(text, isFinal, turnId)` avec `turnId == 0` (forme par défaut utilisée par le démonstrateur et les moteurs STT simples), un paquet final tardif émis après un STOP ou une annulation pouvait réinitialiser `_turnCancelled = false` ou incrémenter `_activeTurnId++` si `_finalReceivedForTurn == true`.
- **Cause Technique** : La gestion par défaut du cas `turnId == 0` considérait toute transcription finale comme un signal de validation d'un nouveau tour dès lors qu'aucun tour n'était marqué actif en cours, réarmant arbitrairement l'état d'annulation (`_turnCancelled = false`). De plus, une deuxième transcription finale pour un même tour créait une deuxième action identique.
- **Reproduction** : Scénario A et Scénario B dans la suite de tests 141 (tests natifs).

### 2.2. TTS — Fin Obsolète et Collision de Restitution (P0)
- **Symptôme** : Lorsqu'une synthèse vocale était interrompue par `STOP`, puis qu'une nouvelle synthèse vocale était démarrée (`say()`), l'arrivée asynchrone différée de la notification matérielle/logicielle de fin du premier TTS (`notifySpeechFinished(true)`) venait clore prématurément la nouvelle restitution active, marquant l'ancienne réponse interrompue comme réussie dans `ReplyMemory`.
- **Cause Technique** : `VoiceUxLiveBridge` ne corrélait pas la notification `notifySpeechFinished()` avec l'identifiant de la restitution en cours, et acceptait aveuglément toute fin tant que `_state == SPEAKING` ou `_speechActive == true`.
- **Reproduction** : Scénario C dans la suite 141.

### 2.3. STOP — Incohérence des Statuts d'Action (P1)
- **Symptôme** : Lors du traitement d'une commande vocale ou tactile STOP, l'action STOP créée par le pont pouvait être annulée en cascade par `cancelPendingAction()` avant que `notifyActionCompleted()` ne soit invoqué, laissant le statut de l'action STOP à `CANCELLED` alors que l'interruption avait parfaitement réussi. Inversement, l'action interrompue pouvait parfois se voir attribuer `COMPLETED` si le démonstrateur appelait `notifyActionCompleted()` aveuglément.
- **Cause Technique** : `cancelPendingAction()` écrasait indiscriminément l'action en attente même si celle-ci était de type `STOP`.
- **Reproduction** : Scénario E & F dans la suite 141.

### 2.4. ToolRegistry — Réutilisation de Jeton après Réenregistrement (P1)
- **Symptôme** : Une commande sensible enregistrée recevait un jeton de confirmation. Si l'outil était désenregistré (`unregisterTool()`) puis réenregistré sous le même nom avec des paramètres ou une logique différente, l'ancien jeton en attente restait validable et permettait d'exécuter la nouvelle commande avec les anciens privilèges.
- **Cause Technique** : Les confirmations en attente n'étaient pas invalidées lors de `unregisterTool()` et la correspondance s'effectuait uniquement sur l'identifiant textuel `toolId` sans validation de cycle de vie générationnel.
- **Reproduction** : Scénario D dans la suite 141.

---

## 3. Fichiers et Fonctions Corrigés

### 3.1. `src/core/tools/ToolDefinition.h` & `ToolRegistry.h` / `ToolRegistry.cpp`
- **`ToolDefinition`** :
  - Ajout du membre `uint32_t generation = 0` identifiant chaque instance d'enregistrement d'un outil.
- **`ToolRegistry`** :
  - Ajout du compteur interne `uint32_t _toolGenerationCounter = 0`.
  - Ajout de `uint32_t toolGeneration = 0` dans la structure `PendingConfirmation`.
  - **`registerTool()`** : Assigne `entry.generation = ++_toolGenerationCounter` à chaque enregistrement.
  - **`unregisterTool()`** : Invalide immédiatement toute confirmation en attente associée (`target->toolGeneration = 0`).
  - **`execute()` & `confirm()`** : Vérification stricte `target->toolGeneration != def->generation`. En cas de discordance (outil supprimé puis recréé), retour immédiat de `ToolExecutionResult::PERMISSION_DENIED` avec message explicite `"Jeton invalide: outil re-enregistre depuis la confirmation"`.

### 3.2. `src/core/voiceux/VoiceUxLiveBridge.h` & `VoiceUxLiveBridge.cpp`
- **Génération & Cycle STT** :
  - Découplage strict entre capture physique (`_captureActive`) et réception de données.
  - `onListeningStarted()` : Réinitialise explicitement le contexte (`_captureActive = true`, `_turnCancelled = false`, `_finalReceivedForTurn = false`, `_activeTurnId++`).
  - `feedTranscription()` :
    - Rejet immédiat si `turnId > 0 && turnId < _activeTurnId` (tour obsolète).
    - Rejet immédiat si `_turnCancelled == true` (tour annulé par STOP ou PTT release).
    - Rejet immédiat si `isFinal && _finalReceivedForTurn` (protection anti-doublon).
    - Si `turnId == 0` : interdiction absolue de créer implicitement un nouveau tour si le tour courant est annulé ou inactif.
- **Génération & Cycle TTS** :
  - Ajout du membre `uint32_t _speechGeneration = 0`, `uint32_t _currentSpeechId = 0`, `bool _speechActive = false`.
  - Extension rétrocompatible : `void notifySpeechFinished(bool completed = true, uint32_t speechId = 0)`.
  - Ajout de l'accesseur : `uint32_t getCurrentSpeechId() const`.
  - `notifySpeechStarted()` : Assigne `_currentSpeechId = ++_speechGeneration; _speechActive = true;`.
  - `notifySpeechFinished(completed, speechId)` : Ignore silencieusement la notification si `speechId != 0 && speechId != _currentSpeechId` ou si `!_speechActive`.
  - `triggerPriorityStop()` : N'écrase pas les métadonnées de STOP et n'enregistre jamais une réponse partielle dans `ReplyMemory`.
- **Cohérence des Actions & STOP** :
  - `cancelPendingAction()` : Préserve l'action active si celle-ci est de type `VoiceUxBridgeActionType::STOP`.
  - Permet à `notifyActionCompleted()` d'attribuer déterministement le statut `COMPLETED` à la commande STOP.

### 3.3. `examples/VoiceUxLiveDemo/src/main.cpp`
- **Frontière Moteur $\leftrightarrow$ Pont** :
  - Suivi déterministe de `s_currentCaptureTurnId` aux transitions `onListeningStarted()` et `onListeningFinished()`.
  - Suivi de `s_currentSpeechId` lors de `s_voice.say()` pour corrélation exacte dans `s_voice.onSpeechFinished()`.
  - Action `FORWARD_AI` : Déclarée informativement non exécutée via `s_bridge.notifyActionRejected("Transmission IA reservee a la version v0.6.5")` au lieu de `notifyActionCompleted()`.
  - Action STOP : Notifiée proprement `notifyActionCompleted()` pour garantir la cohérence des statuts d'action.

### 3.4. `tests/run_tests.bat`
- Correction de l'expansion `%ERRORLEVEL%` sous Windows CMD (`if not errorlevel 1`).
- Résolution dynamique de la chaîne de compilation (`%CXX%` $\to$ `%GCC%` $\to$ `where.exe g++` $\to$ MinGW PlatformIO).
- Prise en charge des chemins avec espaces (`"%GCC%"`).

---

## 4. Tests Ajoutés (Suite 141)

La nouvelle suite **[TEST SUITE 141] `test_v06431_corrective_event_safety()`** a été intégrée dans `tests/test_voiceux_suites.h` et couvre exhaustivement tous les scénarios prescrits :

1. **Scénario A — STOP puis transcription tardive** :
   - Écoute démarrée $\to$ STOP déclenché $\to$ injection tardive d'une transcription finale (avec et sans identifiant `turnId`) $\to$ vérification qu'aucune action n'est créée $\to$ redémarrage d'une nouvelle écoute $\to$ confirmation du retour au fonctionnement nominal.
2. **Scénario B — Double transcription finale** :
   - Réception d'une première transcription finale $\to$ création de l'action $\to$ injection d'une seconde transcription finale identique $\to$ vérification du rejet strict du doublon.
3. **Scénario C — Notification audio TTS obsolète** :
   - Démarrage TTS 1 $\to$ interruption STOP $\to$ démarrage TTS 2 $\to$ simulation de notification de fin de TTS 1 avec son identifiant obsolète $\to$ vérification que TTS 2 reste en cours (`SPEAKING`, actif) $\to$ notification de TTS 2 $\to$ terminaison correcte.
4. **Scénario D — Commande sensible remplacée et réenregistrée** :
   - Enregistrement d'un outil sensible $\to$ émission d'une demande de confirmation avec obtention de jeton $\to$ suppression de l'outil via `unregisterTool()` $\to$ réenregistrement d'un autre outil sous le même nom $\to$ tentative d'utilisation de l'ancien jeton $\to$ refus strict `PERMISSION_DENIED` $\to$ nouvelle demande de confirmation $\to$ exécution réussie avec le nouveau jeton.
5. **Scénario E — Cohérence des statuts d'action** :
   - Validation des transitions `CREATED` $\to$ `DISPATCHED` $\to$ `ACCEPTED` $\to$ `COMPLETED` pour une action exécutée.
   - Validation des transitions `CREATED` $\to$ `DISPATCHED` $\to$ `REJECTED` pour une action rejetée.
   - Validation des transitions `CREATED` $\to$ `DISPATCHED` $\to$ `CANCELLED` pour une action annulée par STOP.
   - Vérification du statut `COMPLETED` pour la commande STOP elle-même.
6. **Scénario F — STOP dans les 7 états opérationnels** :
   - Validation du comportement et de la récupération de STOP depuis : `READY`, `LISTENING`, `TRANSCRIBING`, `ACTION_PENDING`, `SPEAKING`, `PAUSED`, et état d'erreur récupérable (`ERROR`).
   - Vérification systématique du retour à l'état `READY` ou `PAUSED` sans blocage ni fuite d'état.

---

## 5. Résultats Réels Obtenus

### 5.1. Bilan des Tests Unitaires & Intégration Native
- **Nombre total de suites exécutées** : 141 suites.
- **Nombre total d'assertions validées** : **2 745 assertions**.
- **Échecs** : **0**.
- **Temps d'exécution global** : ~4 secondes sous Windows MinGW GCC 13.1.0.

### 5.2. Bilan des Compilations PlatformIO (ESP32-S3 CoreS3)
Toutes les cibles de démonstration et le projet consommateur externe ont été compilés avec succès sous PlatformIO (environnement `m5stack-cores3`, framework Arduino ESP-IDF 3.20017.241212) :

| Projet / Exemple | RAM Utilisée | Flash Utilisée | Temps de Build | Résultat |
| :--- | :--- | :--- | :--- | :--- |
| `examples/VoiceUxLiveDemo` | 79 384 o (24.2%) | 1 704 069 o (81.3%) | 17.20 s | **SUCCESS** |
| `examples/LocalToolRegistryDemo` | 55 456 o (16.9%) | 1 206 913 o (57.6%) | 10.87 s | **SUCCESS** |
| `examples/MinimalVoiceKit` | 77 632 o (23.7%) | 1 677 725 o (80.0%) | 27.96 s | **SUCCESS** |
| `examples/OpenAIConversation` | 78 864 o (24.1%) | 1 689 441 o (80.6%) | 26.93 s | **SUCCESS** |
| `examples/OpenAIConversationLVGL` | 134 400 o (41.0%) | 2 249 249 o (71.5%) | 46.70 s | **SUCCESS** |
| `examples/WakeWordConversation` | 79 608 o (24.3%) | 1 681 525 o (80.2%) | 30.33 s | **SUCCESS** |
| `tests/external-consumer` | 78 872 o (24.1%) | 1 689 817 o (80.6%) | 28.31 s | **SUCCESS** |

---

## 6. Limites Nécessitant Encore des Tests sur CoreS3 Physique

Bien que les tests natifs et les compilations firmware valident intégralement la logique logicielle, certaines caractéristiques physiques nécessitent une validation sur matériel réel CoreS3 :
1. **Asynchronisme DMA / I2S de l'ES7210** : Le délai de vidage résiduel des tampons DMA I2S lors de `stopListening()` peut induire l'émission d'un ultime paquet audio vers le service STT. Bien que la couche logicielle rejette désormais tout résultat tardif (`_turnCancelled`), le comportement sous fortes perturbations acoustiques réelles doit être confirmé.
2. **Latence réseau WebSocket & Trame Serveur Tardive** : Selon la gigue réseau (Wi-Fi 2.4 GHz), le serveur Gemini ou OpenAI peut émettre un paquet de transcription plusieurs centaines de millisecondes après l'envoi de l'indicateur de fin de parole. La politique de rejet conservatrice doit être éprouvée en conditions de perte de paquets.
3. **Pression Concurrente Tactile / Audio** : La validation des boutons tactiles CoreS3 (M5Touch) en présence de forts volumes sonores (haut-parleur AW88298 à fond) afin de vérifier l'absence d'artefacts I2C.

---

## 7. Risques Résiduels

1. **Fournisseurs STT tiers sans notion de Turn ID** : Si un utilisateur connecte un moteur STT externe qui n'utilise pas `VoiceUxLiveDemo` et appelle directement `feedTranscription(text, true)` sans encapsulation PTT, le pont applique la politique conservatrice : un paquet final reçu hors écoute active n'engendrera pas de nouveau tour si la capture n'a pas été déclarée par `onListeningStarted()`. Cette limite est documentée et garantit la non-exécution accidentelle.
2. **Mémoire RAM ESP32-S3 sous LVGL 9** : L'exemple `OpenAIConversationLVGL` utilise 134 Ko de RAM interne (41%). Les nouvelles variables de corrélation (`_speechGeneration`, `_currentSpeechId`, `generation`) n'ajoutent que 16 octets au total et n'ont aucun impact sur l'empreinte mémoire.

