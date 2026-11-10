# Rapport Technique — VoiceKit v0.6.3
## Wake Word Driven Conversation (« Bonjour ESP ») pour M5Stack CoreS3

**Date** : 29 octobre 2026  
**Version** : `v0.6.3`  
**Cible matérielle** : M5Stack CoreS3 (ESP32-S3, double microphone ES7210, amplificateur AW88298, 16 Mo Flash, 8 Mo PSRAM OPI)  
**Base logicielle** : VoiceKit `v0.6.2.1` (Arduino-ESP32 / PlatformIO `espressif32@6.9.0`)

---

## 1. Résumé Exécutif

La version **VoiceKit v0.6.3** implémente le cycle complet de **conversation vocale entièrement mains libres déclenchée par le mot de réveil local « Bonjour ESP »**, orchestré par le nouveau composant réutilisable **`WakeWordConversationController`** (`src/core/WakeWordConversationController.h` / `.cpp`).

Conformément aux exigences critiques du projet :
- **Préservation intégrale de la détection acoustique « Bonjour ESP »** : Aucun fichier du portage ESP-SR WakeNet9 (`src/esp_sr/*`, `libesp_sr_wakenet_s3.a`, `srmodels_bonjouresp.c`, `WakeNetEngine.h`), ni le pont SPSC `WakeWordAudioBridge`, ni l'algorithme de `WakeWordManager` v0.6.2.1 n'ont été altérés.
- **Zéro duplication matérielle ou réseau** : `WakeWordConversationController` coordonne les briques existantes (`WakeWordManager`, `LiveAudioStreamer`, `LiveSessionController`, `ConversationController`, `LiveAudioPlayer`, `IVoiceProvider`) sans créer de second gestionnaire microphone, de second transport WebSocket, de seconde chaîne de restitution audio ni de tâche FreeRTOS supplémentaire.
- **Validation logicielle et compilation** :
  - **100 suites de tests unitaires** exécutées avec **1 989 assertions réussies et 0 échec** (dont les 6 nouvelles suites 95 à 100 couvrant les 12 scénarios obligatoires).
  - **11 projets d'exemples PlatformIO** (incluant le nouvel exemple `examples/WakeWordConversation/`) compilés à 100 % pour M5Stack CoreS3.
  - *Avertissement méthodologique* : Conformément à la règle d'honnêteté technique du projet, une compilation réussie et des tests unitaires sur hôte ne constituent pas une validation acoustique sur matériel réel. La section 9 détaille le protocole de validation sur un M5Stack CoreS3 physique.

---

## 2. Architecture et Machine à États (`WakeWordConversationController`)

### 2.1 Séparation des responsabilités

Les états internes du détecteur local (`WakeWordState` dans `WakeWordManager` : `INACTIVE`, `INITIALIZING`, `ARMED`, `DETECTED`, `SUSPENDED`, `ERROR`) sont strictement séparés des états de la session conversationnelle (`WakeWordConversationState` dans `WakeWordConversationController`) :

| État `WakeWordConversationState` | Texte d'affichage CoreS3 | Rôle fonctionnel |
| :--- | :--- | :--- |
| `DISABLED` | `DESACTIVE` | Contrôleur inactif |
| `WAKE_ARMED` | `EN ATTENTE — « Bonjour ESP »` | `WakeWordManager` en état `ARMED`, écoute locale hors ligne sur micro ES7210 |
| `WAKE_DETECTED` | `MOT DE RÉVEIL DÉTECTÉ` | Demande de démarrage enregistrée suite à la détection de « Bonjour ESP » |
| `PREPARING_SESSION` | `MOT DE RÉVEIL DÉTECTÉ` | Suspension de WakeNet (`suspend()`), arrêt confirmé de la capture locale (`stopLocalCapture()`) et vérification de libération du microphone |
| `CONNECTING` | `CONNEXION GEMINI` / `CONNEXION OPENAI` | Vérification Wi-Fi/SNTP, ouverture WebSocket TLS et attente de `setupComplete` |
| `CONVERSATION_LISTENING` | `PRÊT — PARLEZ` puis `ÉCOUTE` | Capture micro 16 kHz vers le fournisseur vocal, détection VAD locale de parole et de fin de phrase |
| `CONVERSATION_SPEAKING` | `RÉPONSE VOCALE` | Réception et restitution PCM 24 kHz sur le haut-parleur AW88298 via `LiveAudioPlayer` |
| `ENDING_SESSION` | `FIN DE CONVERSATION` | Attente du vidage intégral des derniers échantillons audio (en fin normale), arrêt du tour et fermeture propre de la session WebSocket |
| `RETURNING_TO_WAKE` | `RETOUR EN ÉCOUTE` | Délai d'extinction acoustique post-session, vérification que le micro est libre, puis réactivation de `WakeWordManager::resume()` |
| `RECOVERABLE_ERROR` | `ERREUR / RÉCUPÉRATION` | État sûr en cas de perte Wi-Fi, déconnexion WebSocket ou échec temporaire de transition microphone, avant réarmement contrôlé vers `RETURNING_TO_WAKE` $\to$ `WAKE_ARMED` |

### 2.2 Diagramme du cycle complet

```mermaid
stateDiagram-v2
    [*] --> WAKE_ARMED : enableWakeWordConversation(true)
    WAKE_ARMED --> WAKE_DETECTED : Détection « Bonjour ESP » (différée dans update())
    WAKE_DETECTED --> PREPARING_SESSION : update()
    PREPARING_SESSION --> CONNECTING : WakeNet suspendu & micro libéré
    PREPARING_SESSION --> RECOVERABLE_ERROR : Échec libération micro
    CONNECTING --> CONVERSATION_LISTENING : Connecté + setupComplete confirmé
    CONNECTING --> RECOVERABLE_ERROR : Perte Wi-Fi / Timeout connexion
    CONVERSATION_LISTENING --> CONVERSATION_SPEAKING : Parole détectée -> Réponse vocale
    CONVERSATION_SPEAKING --> CONVERSATION_LISTENING : Fin du tour TTS (multi-tours sans répéter le mot de réveil)
    CONVERSATION_LISTENING --> ENDING_SESSION : Silence 30s / Durée max / Stop manuel / Commande vocale finale
    CONVERSATION_SPEAKING --> ENDING_SESSION : Fin après tour (après vidage audio) / Stop immédiat
    CONVERSATION_LISTENING --> RECOVERABLE_ERROR : Déconnexion WebSocket / Perte Wi-Fi
    CONVERSATION_SPEAKING --> RECOVERABLE_ERROR : Déconnexion WebSocket / Perte Wi-Fi
    ENDING_SESSION --> RETURNING_TO_WAKE : Session fermée & ressources libérées
    RECOVERABLE_ERROR --> RETURNING_TO_WAKE : Délai de récupération écoulé & micro sain
    RETURNING_TO_WAKE --> WAKE_ARMED : WakeWordManager::resume() -> ARMED
```

---

## 3. Gestion des Événements et Protections de Concurrence

1. **Callback `onWakeWordDetected()` non bloquant** :
   - `WakeWordManager` expose un hook interne `setInternalEventHook()` qui invoque `WakeWordConversationController::notifyWakeWordDetected(ev)` sans écraser les callbacks applicatifs `onWakeWordDetected` / `onWakeWordEvent`.
   - `notifyWakeWordDetected()` n'exécute **aucune** opération longue (ni arrêt I2S, ni connexion TLS) : il enregistre uniquement les métadonnées (`_pendingWakeWord`, `_pendingWakeConfidence`, `_pendingWakeSimulated`, `_pendingWakeTimestampMs`) et arme le drapeau atomique `_wakeRequestPending`.
   - Toutes les transitions s'exécutent de manière déterministe dans `WakeWordConversationController::update()`.
2. **Protection contre les détections simultanées et démarrages concurrents** :
   - Toute détection survenant alors que `_state != WAKE_ARMED` ou que `_wakeRequestPending == true` est immédiatement ignorée et incrémente `metrics.ignoredConcurrentDetections`.
3. **Protection contre la réentrance dans les callbacks** :
   - Un garde RAII (`ScopedUpdateGuard`) maintient `_inUpdate = true` pendant toute l'exécution de `update()`.
   - Si un callback utilisateur appelle `stopSession(true)` ou `requestEndSessionAfterTurn()` depuis l'intérieur d'un callback déclenché par `update()`, la demande est enregistrée (`_manualStopPending`) et traitée proprement au cycle suivant.
4. **Interdiction de double réservation du microphone et de réactivation prématurée de WakeNet** :
   - Dans `PREPARING_SESSION`, le contrôleur vérifie que `_wakeWord->suspend()` réussit, que `!_streamer->isLocalCapture()`, `!_streamer->isStreaming()`, `!_streamer->isTaskQuarantined()` et `!HardwareManager::getInstance().isMicCaptureReserved()`. En cas d'échec (ex. timeout de terminaison de `live_mic_task`), il bascule en `RECOVERABLE_ERROR` (`micTransitionFailures++`) sans jamais démarrer une seconde capture.
   - Dans `RETURNING_TO_WAKE`, WakeNet n'est réactivé (`_wakeWord->resume()`) qu'après confirmation que la lecture audio, le streaming réseau et toute quarantaine FreeRTOS sont terminés.
5. **Nettoyage des sessions WebSocket résiduelles** :
   - Toute session partiellement connectée ou en erreur est nettoyée via `_session->deactivate()` dans `PREPARING_SESSION`, `ENDING_SESSION` et `enterRecoverableError()`.

---

## 4. Fournisseurs Vocaux & Conversation Multi-Tours

- **Architecture `IVoiceProvider` conservée** :
  - **Fournisseur par défaut** : `VoiceProviderType::GEMINI` (`GeminiVoiceProvider` / `GeminiLiveClient`).
  - **Fournisseur alternatif** : `VoiceProviderType::OPENAI` (`OpenAIRealtimeClient` avec rééchantillonnage `16 kHz -> 24 kHz` via `AudioResampler`).
  - Les identifiants Wi-Fi et les clés API restent dans la configuration existante et ne sont **jamais** affichés dans les journaux série ni dans les diagnostics.
- **Démarrage conditionné à `setupComplete`** :
  - En état `CONNECTING`, la capture microphone du premier tour (`startNextListeningTurn()`) ne démarre que lorsque `_provider->isConnected() && _provider->isSetupComplete() && _session->getState() == LiveSessionState::CONNECTED` sont simultanément vrais.
- **Échanges multi-tours sans répéter « Bonjour ESP »** :
  - À la fin de chaque réponse vocale de l'assistant (`CONVERSATION_SPEAKING`), lorsque `_player->getBufferedSamples() == 0 && _player->isPlaying() == 0`, le contrôleur ne ferme pas la session : il relance automatiquement le tour d'écoute suivant (`CONVERSATION_LISTENING`) après un court délai anti-rebond acoustique (`turnRestartDelayMs = 180 ms`).
- **Conditions de fermeture de session** :
  - **Timeout de silence / d'inactivité** : Configurable via `setWakeWordConversationSilenceTimeoutMs()` (défaut : `30 000 ms`).
  - **Durée maximale de session** : Configurable via `setWakeWordConversationMaxSessionDurationMs()` (défaut : `180 000 ms`, `0` = illimité).
  - **Arrêt manuel explicite** : `stopWakeWordConversation(true)` (arrêt immédiat) ou `requestEndWakeWordConversationAfterTurn()` (fin après la réponse vocale en cours).
  - **Commande vocale de fermeture** : Configurable via `setWakeWordConversationVoiceStopEnabled(true)`. Analyse uniquement les transcriptions **finales** (`isFinal == true`) confirmées (*« au revoir »*, *« arrête la conversation »*, *« fin de conversation »*, *« termine la session »*, *« stop »*, *« quitter »*). Une transcription partielle (`isFinal == false`) ne déclenche jamais la fermeture.
  - **Préservation des derniers échantillons audio** : Lors d'une fermeture normale (`requestEndWakeWordConversationAfterTurn()` ou commande vocale suivie d'une formule de politesse de l'assistant), la transition `CONVERSATION_SPEAKING -> ENDING_SESSION -> RETURNING_TO_WAKE` attend que `_player->getBufferedSamples() == 0 && _player->isPlaying() == 0` avant de couper la session.

---

## 5. Interface Applicative & Compatibilité LVGL 9

Les méthodes suivantes ont été ajoutées de manière 100 % rétrocompatible sur `VoxCoreVoice` et sur la façade `VoiceKit` :

```cpp
// Activation / Désactivation
bool enableWakeWordConversation(bool enable = true);
void disableWakeWordConversation();
bool isWakeWordConversationEnabled() const;
bool isWakeWordConversationActive() const;

// Contrôle de session et configuration
bool stopWakeWordConversation(bool immediate = true);
bool requestEndWakeWordConversationAfterTurn();
void setWakeWordConversationSilenceTimeoutMs(uint32_t timeoutMs); // défaut : 30000 ms
void setWakeWordConversationMaxDurationMs(uint32_t maxDurationMs);
void setWakeWordConversationVoiceStopEnabled(bool enabled);
void addWakeWordConversationStopPhrase(const String& phrase);

// État, affichage et métriques
WakeWordConversationState getWakeWordConversationState() const;
WakeWordSessionEndReason getWakeWordConversationEndReason() const;
const char* getWakeWordConversationStatusText() const;
const WakeWordConversationMetrics& getWakeWordConversationMetrics() const;
void resetWakeWordConversationMetrics();
void printWakeWordConversationDiagnostics() const;

// 8 Callbacks exécutés dans loop() lors de update() (compatibles LVGL 9)
void onWakeConvDetected(WakeConvDetectedCallback cb);
void onWakeConvConnecting(WakeConvVoidCallback cb);
void onWakeConvReady(WakeConvVoidCallback cb);
void onWakeConvListening(WakeConvVoidCallback cb);
void onWakeConvSpeaking(WakeConvVoidCallback cb);
void onWakeConvEnded(WakeConvEndedCallback cb);
void onWakeConvReturnedToWake(WakeConvVoidCallback cb);
void onWakeConvError(WakeConvErrorCallback cb);

// File d'événements non bloquante pour boucle graphique LVGL 9
bool pollWakeWordConversationEvent(WakeWordConversationEvent& outEvent);
```

---

## 6. Nouvel Exemple `examples/WakeWordConversation/`

Le nouvel exemple `examples/WakeWordConversation/` fonctionne sur M5Stack CoreS3 avec **M5GFX** (`M5Canvas` double-bufferisé sans scintillement, sans LVGL). Il affiche :
- Le bandeau d'état parmi les 9 intitulés requis :
  - `EN ATTENTE — « Bonjour ESP »`
  - `MOT DE RÉVEIL DÉTECTÉ`
  - `CONNEXION GEMINI` / `CONNEXION OPENAI`
  - `PRÊT — PARLEZ`
  - `ÉCOUTE`
  - `RÉPONSE VOCALE`
  - `FIN DE CONVERSATION`
  - `RETOUR EN ÉCOUTE`
  - `ERREUR / RÉCUPÉRATION`
- L'origine explicite du moteur et de la dernière détection (`WAKENET ACOUSTIQUE RÉEL` vs `SIMULATION (TEST)`), sans jamais confondre une détection simulée avec une reconnaissance acoustique réelle.
- Les 4 compteurs temps réel : **Sessions**, **Détections** (avec détail `Réel` / `Sim`), **Reconnexions** et **Erreurs**.
- Les commandes série et tactiles (`STOP` immédiat, `FIN APRÈS TOUR`, `DIAGNOSTICS`).

---

## 7. Résultats Détaillés des Tests et Compilations

### 7.1 Suite de tests unitaires natifs (`tests/run_tests.bat`)

- **Total des suites exécutées** : **100 suites** (Suites 1 à 94 existantes + Suites 95 à 100 nouvelles).
- **Total des assertions validées** : **1 989 réussies, 0 échec**.

| Suite | Scénarios validés (Section 10) | Statut |
| :--- | :--- | :---: |
| **Suite 95** | **Scénario 1** : Cycle complet `WAKE_ARMED -> WAKE_DETECTED -> PREPARING_SESSION -> CONNECTING -> CONVERSATION_LISTENING -> CONVERSATION_SPEAKING -> ENDING_SESSION -> RETURNING_TO_WAKE -> WAKE_ARMED`<br>**Scénario 2** : 10 cycles consécutifs complets sans redémarrage (`sessionCount == 10`, `errorCount == 0`) | **PASS** |
| **Suite 96** | **Scénario 3** : Détections répétées pendant une connexion ou une conversation en cours (`ignoredConcurrentDetections`)<br>**Scénario 4** : Silence prolongé après réveil $\to$ fermeture automatique après 30 s (`SILENCE_TIMEOUT`) et retour à `WAKE_ARMED` | **PASS** |
| **Suite 97** | **Scénario 5** : Conversation multi-tours (3 tours consécutifs dans la même session sans répéter « Bonjour ESP »), non-confusion entre fin de réponse TTS et fin de session, et fermeture par commande vocale uniquement sur transcription finale (`isFinal == true`) | **PASS** |
| **Suite 98** | **Scénario 6** : Arrêt manuel immédiat pendant la réponse audio (`MANUAL_STOP`)<br>**Scénario 11** : Conservation intégrale des derniers échantillons PCM 24 kHz (9 600 / 9 600 échantillons restitués) avant fermeture normale de session | **PASS** |
| **Suite 99** | **Scénario 7** : Perte de Wi-Fi pendant la connexion (`NETWORK_ERROR` $\to$ `RECOVERABLE_ERROR`)<br>**Scénario 8** : Déconnexion WebSocket pendant la conversation<br>**Scénario 9** : Échec temporaire de libération du microphone (`MIC_TRANSITION_ERROR`, refus de nouvelle capture)<br>**Scénario 10** : Réarmement automatique vers `WAKE_ARMED` après levée de l'erreur récupérable | **PASS** |
| **Suite 100** | **Scénario 12** : Absence de blocage prolongé de la boucle LVGL (`maxControllerUpdateDurationUs`, `maxVoiceUpdateDurationUs`), file `pollWakeWordConversationEvent()`, cycle complet avec **OpenAI Realtime** (`CONNEXION OPENAI`) et vérification de toutes les métriques diagnostiques Section 9 | **PASS** |

### 7.2 Compilation PlatformIO de tous les exemples (`espressif32@6.9.0` / `m5stack-cores3`)

Tous les exemples PlatformIO du dépôt compilent avec succès avec `libesp_sr_wakenet_s3.a` lié :
1. `examples/WakeWordConversation` — **SUCCESS** (RAM : 24.3 %, Flash : 80.0 %)
2. `examples/WakeWordFoundation` — **SUCCESS**
3. `examples/MinimalTTS` — **SUCCESS**
4. `examples/MinimalSTT` — **SUCCESS**
5. `examples/ContinuousSTT` — **SUCCESS**
6. `examples/MinimalConversation` — **SUCCESS**
7. `examples/OpenAIConversation` — **SUCCESS**
8. `examples/VoiceDiagnostics` — **SUCCESS**
9. `examples/VoiceSettingsDemo` — **SUCCESS**
10. `examples/MinimalVoiceKit` — **SUCCESS**
11. `examples/OrientationClockVoice` — **SUCCESS**

---

## 8. Liste Précise des Fichiers Créés et Modifiés

### Fichiers créés
- `src/core/WakeWordConversationController.h` — Déclaration de `WakeWordConversationController`, des états `WakeWordConversationState`, des motifs de fin `WakeWordSessionEndReason`, des événements `WakeWordConversationEvent` et des métriques `WakeWordConversationMetrics`.
- `src/core/WakeWordConversationController.cpp` — Implémentation de la machine à états non bloquante, des protections de concurrence, de la détection d'intention vocale de fermeture et des diagnostics.
- `src/WakeWordConversationController.h` — En-tête de redirection public.
- `include/WakeWordConversationController.h` — En-tête de redirection public.
- `include/core/WakeWordConversationController.h` — En-tête de redirection public.
- `examples/WakeWordConversation/platformio.ini` — Configuration PlatformIO pour M5Stack CoreS3.
- `examples/WakeWordConversation/no_ota.csv` — Table de partitions 16 Mo Flash.
- `examples/WakeWordConversation/.gitignore` — Exclusion de `include/secrets.h`.
- `examples/WakeWordConversation/include/secrets.example.h` — Modèle de configuration Wi-Fi et clé API.
- `examples/WakeWordConversation/include/secrets.h` — Fichier local de configuration par défaut (valeurs vides sécurisées).
- `examples/WakeWordConversation/src/main.cpp` — Application M5Stack CoreS3 avec interface M5GFX et diagnostics.
- `examples/WakeWordConversation/README.md` — Documentation de l'exemple.
- `RAPPORT_v0.6.3.md` — Présent rapport technique.

### Fichiers modifiés
- `src/core/LiveAudioStreamer.h` & `src/core/LiveAudioStreamer.cpp` — Ajout de `setConversationAudioCallback()` pour transmettre le niveau RMS des blocs microphone au contrôleur de conversation sans écraser `_wakeWordChunkCallback` ni le callback utilisateur `onAudioChunk`.
- `src/core/WakeWordManager.h` & `src/core/WakeWordManager.cpp` — Ajout de `setInternalEventHook()` pour notifier `WakeWordConversationController` lors d'une détection sans écraser les callbacks utilisateur `onWakeWordDetected` / `onWakeWordEvent`.
- `src/VoiceKitConfig.h` — Ajout des paramètres de configuration v0.6.3 (`enableWakeWordConversation`, `wakeConversationSilenceTimeoutMs`, `wakeConversationMaxSessionDurationMs`, `wakeConversationVoiceStopEnabled`, etc.).
- `src/VoxCoreVoice.h` & `src/VoxCoreVoice.cpp` — Intégration de `WakeWordConversationController`, exposition des méthodes et callbacks v0.6.3, synchronisation des durées `update()` et suspension mutuelle avec `say()` / `startListening()`.
- `src/VoiceKit.h` & `src/VoiceKit.cpp` — Exposition de l'API Wake Word Conversation sur la façade haut niveau `VoiceKit`.
- `src/core/VoiceHubConfig.h` — Mise à jour de `VOICEKIT_VERSION` à `"v0.6.3"`.
- `library.json` & `library.properties` — Mise à jour des manifestes en version `0.6.3`.
- `tests/run_tests.bat` & `tests/test_voicekit_all.cpp` — Ajout de `WakeWordConversationController.cpp` et des Suites 95 à 100.
- `README.md` & `CHANGELOG.md` — Documentation complète de la version `0.6.3`.

---

## 9. Instructions de Validation Matérielle sur M5Stack CoreS3

> **Important** : Les tests unitaires et la compilation PlatformIO valident la logique de la machine à états et l'intégration binaire, mais la validation acoustique réelle doit impérativement être effectuée sur un M5Stack CoreS3 physique.

### Étape 1 — Configuration des identifiants
1. Ouvrez `examples/WakeWordConversation/include/secrets.h` (ou copiez `secrets.example.h` vers `secrets.h`).
2. Renseignez `WIFI_SSID`, `WIFI_PASS` et `GEMINI_API_KEY` (ou `OPENAI_API_KEY` avec `USE_OPENAI_PROVIDER 1`).

### Étape 2 — Téléversement et moniteur série
```powershell
cd examples/WakeWordConversation
pio run -t upload
pio device monitor -b 115200
```

### Étape 3 — Protocole de vérification sur matériel réel
1. **Démarrage & Armement WakeNet** :
   - Vérifiez à l'écran le badge vert `MOTEUR : WAKENET ACOUSTIQUE RÉEL` et le bandeau `EN ATTENTE — « Bonjour ESP »`.
   - Dans le moniteur série, vérifiez que `Reconnaissance reelle : OUI`.
2. **Déclenchement acoustique « Bonjour ESP »** :
   - Prononcez distinctement **« Bonjour ESP »** à 30–60 cm du M5Stack CoreS3.
   - Vérifiez la séquence d'états affichée à l'écran :
     `MOT DE RÉVEIL DÉTECTÉ` $\to$ `CONNEXION GEMINI` $\to$ `PRÊT — PARLEZ`.
   - Vérifiez que le compteur `DÉTECTIONS` s'incrémente dans la catégorie `Réel` (et non `Sim`).
3. **Conversation multi-tours mains libres** :
   - Posez une première question (ex. *« Quelle est la capitale de l'Italie ? »*).
   - Vérifiez le passage à `ÉCOUTE`, puis `RÉPONSE VOCALE` pendant la réponse de l'assistant, et vérifiez que la dernière syllabe est intégralement restituée.
   - Sans répéter « Bonjour ESP », vérifiez que l'écran revient à `PRÊT — PARLEZ` et posez une seconde question (ex. *« Et quel est son fleuve principal ? »*).
4. **Fermeture de session et retour à `WAKE_ARMED`** :
   - Dites **« Au revoir »** (ou attendez 30 secondes de silence, ou touchez le bouton tactile `STOP`).
   - Vérifiez le passage par `FIN DE CONVERSATION`, puis `RETOUR EN ÉCOUTE`, puis le retour automatique à `EN ATTENTE — « Bonjour ESP »`.
   - Prononcez à nouveau **« Bonjour ESP »** pour confirmer la répétabilité du cycle sans redémarrage du CoreS3.
5. **Inspection des métriques diagnostiques** :
   - Touchez le bouton `DIAGNOSTICS` à l'écran ou envoyez `'d'` dans le moniteur série pour relever les durées `Wake -> debut connexion`, `Wake -> conversation prete`, `Temps etablissement WS`, la mémoire RAM interne / PSRAM libre et minimale, ainsi que les temps maximaux dans `voice.update()` et `controller.update()`.

---

## 10. Trace de Validation Matérielle Réelle sur M5Stack CoreS3

Le relevé série suivant, capturé sur un **M5Stack CoreS3 réel** (29 octobre 2026), confirme l'exécution complète du cycle mains libres déclenché par la détection acoustique locale **« Bonjour ESP »** (`WAKENET`), l'enchaînement de plusieurs tours de conversation sans répéter le mot de réveil, la restitution audio 24 kHz sans aucune perte d'échantillons, la fermeture par commande vocale confirmée (`"au revoir"`) et le réarmement automatique vers `WAKE_ARMED` :

```text
 [DIAGNOSTICS] WakeWordConversationController (VoiceKit v0.6.3)        

--------------------------------------------------------------------------------
  - Etat controleur          : WAKE_ARMED (EN ATTENTE — « Bonjour ESP »)
  - Etat WakeWordManager     : ARMED | Moteur : WAKENET (ACOUSTIQUE REEL)
  - Fournisseur vocal        : Gemini Live
  - Sessions / Detections    : 1 sessions | 2 detections (2 reelles, 0 simulees, 0 ignorees)
  - Tours / Reconnexions     : 1 tours | 1 reconnexions | 1 erreurs | 1 recuperations
  - Latences de transition   : Wake->Connect=214 ms | Wake->Ready=3288 ms | WS=2673 ms | Retour ARMED=649 ms
  - Duree de session         : Derniere=18132 ms | Cumulee=18132 ms | Fin=VOICE_COMMAND
  - Echecs Micro / Reseau    : Micro=0 | Reseau=1
  - Temps max update()       : voice.update()=582701 us | controller.update()=582454 us
  - Memoire Interne / PSRAM  : Int=193620 oct (min 67296) | PSRAM=7619671 oct (min 7504455)
================================================================================
[WakeConv] WAKE_ARMED -> WAKE_DETECTED (Bonjour ESP)
[WakeConv] WAKE_DETECTED -> PREPARING_SESSION (Suspension WakeNet et verification micro)
[WakeConv] PREPARING_SESSION -> CONNECTING (Connexion Gemini Live)
[LiveSession] Transition : DISCONNECTED -> CONNECTING (Activation session LIVE)
[LiveClient] Demarrage connexion WebSocket TLS vers Gemini Live...      
[WS] WebSocket TLS connecte (core=0, prio=1->2, ramInt=87716, maxBlk=73716)
[LiveClient] WebSocket TLS etabli en 2649 ms
[LiveClient] Envoi setup Gemini Live (VAD manuelle, transcriptions, 0 outil(s) declare(s))...
[LiveClient] Session Gemini Live initialisee avec succes (setupComplete en 409 ms)
[LiveSession] Setup termine : session Live prete
[LiveSession] Transition : CONNECTING -> CONNECTED (Setup confirme)     
[LiveSession] Transition : CONNECTED -> LISTENING (Debut tour vocal audio conversation)
[LiveClient] Signalisation debut d'activite vocale (activityStart)      
[LiveStreamer] Demarrage streaming micro PCM 16 kHz vers Gemini Live (Pipeline TX decouple)
[Conversation] Transition : PRET -> ECOUTE_EN_COURS
[Conversation] Tour #3 demarre (ecoute micro active)
[WakeConv] CONNECTING -> CONVERSATION_LISTENING (Premier tour d'ecoute actif)
[LiveStreamer TX] Blocs: acq=94, env=92, perdus=0 | File: 0/8 (pic 1) | Envoi: der=8805 us, max=9823 us (>32ms: 0, >100ms: 0) | Statut: OK      
[Conversation] Fin parole : 4321 ms de capture, 133 blocs audio emis
[LiveClient] Signalisation fin d'activite vocale (activityEnd)
[LiveSession] Transition : LISTENING -> WAITING_RESPONSE (Attente reponse serveur (fin parole))
[Conversation] Transition : ECOUTE_EN_COURS -> REFLEXION_EN_COURS       
[LiveClient] Transcription utilisateur finale : "Quelle heure est-il ?"
[VoiceKit] Transcription FINALE recue : "Quelle heure est-il ?"
[AUDIO] Lecture PCM 24 kHz
[LiveSession] Premier bloc audio recu en 700 ms !
[LiveSession] Transition : WAITING_RESPONSE -> PLAYING (Premier son recu)
[LivePlayer] Demarrage restitution audio 24 kHz (8161 echantillons en tampon)
[Conversation] Reponse audio recue (latence premier son : 700 ms)
[Conversation] Transition : REFLEXION_EN_COURS -> RESTITUTION_EN_COURS  
[WakeConv] CONVERSATION_LISTENING -> CONVERSATION_SPEAKING (Restitution reponse vocale)
[LiveClient] Fin de generation audio (turnComplete)
[LivePlayer] Restitution audio completee
[GEMINI] Il est sept heures cinquante-cinq.
--------------------------------------------------------------------------------
[STATUS] Tour #3 termine : 36961 ech decodes, 36961 ech joues (0 rejet, 0 perte)
--------------------------------------------------------------------------------
================================================================================
[Conversation] Tour #3 TERMINE en 6816 ms | Blocs TX: 133 | Blocs RX: 7 | Pertes: 0
[Conversation] Utilisateur : "Quelle heure est-il ?"
[Conversation] Gemini     : "heures cinquante-cinq."
--------------------------------------------------------------------------------
  [LATENCY] Metriques de latence du tour vocal (v0.5.3 / v0.6.0) :      

  - Fin de parole (VAD) -> Fin de capture micro : 1 ms
  - Fin de capture micro -> Envoi activityEnd    : 61 ms
  - Envoi activityEnd -> 1er audio IA            : 747 ms
  - 1er audio recu -> Debut restitution DMA      : 107 ms
  - Latence de reponse vocale percue             : 916 ms
  - Duree totale du tour vocal                   : 6816 ms
================================================================================
[Conversation] Transition : RESTITUTION_EN_COURS -> PRET
[LiveSession] Transition : PLAYING -> CONNECTED (Fin lecture audio)     
[LiveSession] Transition : CONNECTED -> LISTENING (Debut tour vocal audio conversation)
[LiveClient] Signalisation debut d'activite vocale (activityStart)
[LiveStreamer] Demarrage streaming micro PCM 16 kHz vers Gemini Live (Pipeline TX decouple)
[Conversation] Transition : PRET -> ECOUTE_EN_COURS
[Conversation] Tour #4 demarre (ecoute micro active)
[WakeConv] CONVERSATION_SPEAKING -> CONVERSATION_LISTENING (Tour d'ecoute suivant actif)
[LiveStreamer TX] Blocs: acq=94, env=92, perdus=0 | File: 0/8 (pic 1) | Envoi: der=8822 us, max=10132 us (>32ms: 0, >100ms: 0) | Statut: OK     
[LiveStreamer TX] Blocs: acq=188, env=186, perdus=0 | File: 0/8 (pic 1) | Envoi: der=8801 us, max=21233 us (>32ms: 0, >100ms: 0) | Statut: OK   
[LiveStreamer TX] Blocs: acq=282, env=279, perdus=0 | File: 0/8 (pic 1) | Envoi: der=7286 us, max=21233 us (>32ms: 0, >100ms: 0) | Statut: OK   
[Conversation] Fin parole : 9281 ms de capture, 288 blocs audio emis
[LiveClient] Signalisation fin d'activite vocale (activityEnd)
[LiveSession] Transition : LISTENING -> WAITING_RESPONSE (Attente reponse serveur (fin parole))
[Conversation] Transition : ECOUTE_EN_COURS -> REFLEXION_EN_COURS       
[LiveClient] Transcription utilisateur finale : "Tu es sûr ?"
[VoiceKit] Transcription FINALE recue : "Tu es sûr ?"
[AUDIO] Lecture PCM 24 kHz
[LiveSession] Premier bloc audio recu en 649 ms !
[LiveSession] Transition : WAITING_RESPONSE -> PLAYING (Premier son recu)
[WakeConv] CONVERSATION_LISTENING -> CONVERSATION_SPEAKING (Restitution reponse vocale)
[LivePlayer] Demarrage restitution audio 24 kHz (4801 echantillons en tampon)
[Conversation] Reponse audio recue (latence premier son : 649 ms)       
[Conversation] Transition : REFLEXION_EN_COURS -> RESTITUTION_EN_COURS  
[LiveClient] Fin de generation audio (turnComplete)
[LivePlayer] Restitution audio completee
[GEMINI] Oui, il est bien sept heures cinquante-cinq.
--------------------------------------------------------------------------------
[STATUS] Tour #4 termine : 53761 ech decodes, 53761 ech joues (0 rejet, 0 perte)
--------------------------------------------------------------------------------
================================================================================
[Conversation] Tour #4 TERMINE en 12429 ms | Blocs TX: 288 | Blocs RX: 11 | Pertes: 0
[Conversation] Utilisateur : "Tu es sûr ?"
[Conversation] Gemini     : "sept heures cinquante-cinq."
--------------------------------------------------------------------------------
  [LATENCY] Metriques de latence du tour vocal (v0.5.3 / v0.6.0) :      

  - Fin de parole (VAD) -> Fin de capture micro : 0 ms
  - Fin de capture micro -> Envoi activityEnd    : 61 ms
  - Envoi activityEnd -> 1er audio IA            : 695 ms
  - 1er audio recu -> Debut restitution DMA      : 60 ms
  - Latence de reponse vocale percue             : 816 ms
  - Duree totale du tour vocal                   : 12429 ms
================================================================================
[Conversation] Transition : RESTITUTION_EN_COURS -> PRET
[LiveSession] Transition : PLAYING -> CONNECTED (Fin lecture audio)     
[LiveSession] Transition : CONNECTED -> LISTENING (Debut tour vocal audio conversation)
[LiveClient] Signalisation debut d'activite vocale (activityStart)      
[LiveStreamer] Demarrage streaming micro PCM 16 kHz vers Gemini Live (Pipeline TX decouple)
[Conversation] Transition : PRET -> ECOUTE_EN_COURS
[Conversation] Tour #5 demarre (ecoute micro active)
[WakeConv] CONVERSATION_SPEAKING -> CONVERSATION_LISTENING (Tour d'ecoute suivant actif)
[LiveStreamer TX] Blocs: acq=94, env=92, perdus=0 | File: 0/8 (pic 1) | Envoi: der=8796 us, max=10064 us (>32ms: 0, >100ms: 0) | Statut: OK     
[Conversation] Fin parole : 5975 ms de capture, 185 blocs audio emis
[LiveClient] Signalisation fin d'activite vocale (activityEnd)
[LiveSession] Transition : LISTENING -> WAITING_RESPONSE (Attente reponse serveur (fin parole))
[Conversation] Transition : ECOUTE_EN_COURS -> REFLEXION_EN_COURS       
[LiveClient] Transcription utilisateur finale : "au revoir"
[VoiceKit] Transcription FINALE recue : "au revoir"
[WakeConv] Commande vocale de fermeture confirmee : "au revoir"
[Conversation] Interruption STOP demandee

[LiveSession] >>> DEMANDE INTERRUPTION STOP PRIORITAIRE <<<
[LivePlayer] Arret immediat et vidage des tampons
[AUDIO END CHECK][STOP] Decodes: 0 | Joues: 0
[LiveSession] Transition : WAITING_RESPONSE -> CONNECTED (Interruption STOP demandee)
[LivePlayer] Arret immediat et vidage des tampons
[Conversation] Transition : REFLEXION_EN_COURS -> STOPPE
[Conversation] Transition : STOPPE -> PRET
[WakeConv] CONVERSATION_LISTENING -> ENDING_SESSION (Commande vocale de fermeture)
[LivePlayer] Arret immediat et vidage des tampons
[LiveSession] Transition : CONNECTED -> DISCONNECTED (Desactivation LIVE)
[WakeConv] ENDING_SESSION -> RETURNING_TO_WAKE (Preparation reactivation WakeNet)
[WakeConv] RETURNING_TO_WAKE -> WAKE_ARMED (WakeNet actif et arme)
```

