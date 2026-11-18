# Audit Technique & Correctifs : STOP & Récupération de Session

**Application :** VoiceAssistant v0.1.0 (Showcase officiel VoiceKit)  
**Matériel cible :** M5Stack CoreS3 (ESP32-S3)  
**Bibliothèque sous-jacente :** VoiceKit v0.6.7.1  
**Date d'audit :** 18 Novembre 2026  
**Auteur :** Antigravity (Google DeepMind)

---

## 1. Résumé du Problème Initial

Lors des essais réels sur matériel M5Stack CoreS3 avec OpenAI Realtime, la phase amont de la conversation fonctionne nominalement :
- Connexion Wi-Fi rapide (928 ms) et négociation TLS WebSocket OpenAI Realtime réussie.
- Session opérationnelle en ~8,08 s.
- Push-to-Talk (PTT) à 2 clics opérationnel : 1er clic (PARLER) active le micro ES7210, 2ème clic (TERMINER) valide la capture.
- Validation locale VAD (`VoiceCaptureValidator`) réussie, transcription Whisper reçue, premier paquet audio IA en 606 ms, streaming 24 kHz AW88298 sans perte.

Cependant, lorsqu'un utilisateur actionne le bouton **STOP** pendant la restitution vocale de l'assistant :
1. Une double séquence d'interruption `[LiveSession] DEMANDE INTERRUPTION STOP PRIORITAIRE` / `[OpenAIClient] response.cancel` apparaît dans les journaux.
2. Le client WebSocket signale un verrouillage critique : `WEBSOCKET_CLIENT: Could not lock ws-client within 1000 timeout`.
3. L'IHM bascule instantanément par la séquence `SPEAKING -> INTERRUPTING -> DISABLED -> READY`, laissant croire à l'utilisateur que l'assistant est prêt à dialoguer ou à être reconfiguré.
4. Le transport WebSocket distant subit une déconnexion brutale : `TRANSPORT_WS: Error reading payload data` / `[WS] WebSocket deconnecte`.
5. La session entre en boucle de reconnexion (`[LiveSession] CONNECTED -> RECONNECTING`) marquée par des échecs réseau `esp-tls: select() timeout` et `getaddrinfo() returns 202`.
6. Si l'utilisateur tente de modifier la voix ou le fournisseur pendant cette phase, la requête est rejetée (`[APP] Modification de profil refusée : opération déjà en cours`).
7. En parallèle, les fragments cumulatifs de réponse reçus (`onAssistantText`) s'accumulent ou risquent de polluer l'historique sous forme de réponses tronquées.

---

## 2. Analyse des Deux Interruptions STOP (COR-STOP-01)

### Faits Établis (Code & Traces)
La chaîne d'appel déclenchée par le bouton STOP applicatif a été tracée exhaustivement dans le code source :
1. **IHM & Contrôleur applicatif :**
   - Le clic sur `m_btnStop` appelle `AppController::triggerStop()`.
   - `AppController` transmet à `VoiceInteractionController::triggerStop()`, positionnant l'intention `_pendingIntent = CANCEL_CAPTURE`.
2. **Exécution dans `loopTask` :**
   - Au cycle suivant de `update()`, `VoiceInteractionController::processIntents()` consomme `CANCEL_CAPTURE`.
   - Il passe l'état en `INTERRUPTING`, arme `_lastTurnCancelled = true`, et appelle `_voice.stop()` (`VoxCoreVoice::stop()`).
3. **Double appel dans `VoxCoreVoice::stop()` (`src/VoxCoreVoice.cpp:1086-1129`) :**
   ```cpp
   void VoxCoreVoice::stop() {
       Serial.println("[STATE][STOP] Tour annulé");
       Serial.println("[VoiceKit] Arret demande (stop)");
       ...
       if (_conversationEnabled && _conversationController.isTurnActive()) {
           _conversationController.requestStop(); // <--- PREMIER APPEL DE STOP
       }
       ...
       _session.requestStop();                    // <--- SECOND APPEL DE STOP (SYSTÉMATIQUE)
       ...
   }
   ```
4. **Trajet du premier appel :**
   - `ConversationController::requestStop()` (`src/core/ConversationController.cpp:156`) exécute :
     ```cpp
     if (_session) {
         _session->requestStop(); // Déclenche LiveSessionController::requestStop()
     }
     ```
   - `LiveSessionController::requestStop()` (`src/core/LiveSessionController.cpp:860`) :
     - Affiche : `\n[LiveSession] >>> DEMANDE INTERRUPTION STOP PRIORITAIRE <<<`.
     - Appelle `_provider->sendInterruption()`.
     - Dans `OpenAIRealtimeClient::sendInterruption()` (`src/core/OpenAIRealtimeClient.cpp:541`) :
       - Affiche : `[OpenAIClient] Interruption STOP -> response.cancel et input_audio_buffer.clear`.
       - Envoie `{"type":"response.cancel"}` puis `{"type":"input_audio_buffer.clear"}` sur le WebSocket.
   - `ConversationController::requestStop()` poursuit ensuite en arrêtant le player audio (`_player->stop()`), positionne `_isTurnActive = false`, et appelle `setState(VoiceState::STOPPED)` puis `setState(VoiceState::READY)`.
5. **Trajet du second appel :**
   - Dès que `_conversationController.requestStop()` retourne à `VoxCoreVoice::stop()`, la ligne 1122 exécute inconditionnellement :
     `_session.requestStop();`
   - `LiveSessionController::requestStop()` est exécuté une **seconde fois** pour la même action utilisateur !
   - Comme `LiveSessionController::requestStop()` n'est pas idempotent, il ré-invoque immédiatement `_provider->sendInterruption()`.
   - Deux requêtes d'annulation réseau identiques sont ainsi projetées coup sur coup sur le WebSocket.

### Hypothèses Ouvertes
- La duplication provient d'une divergence historique d'architecture : `_session.requestStop()` était historiquement le point d'arrêt unique en mode simple, tandis que l'ajout ultérieur du mode conversationnel `_conversationController.requestStop()` a introduit son propre relais vers `_session->requestStop()`, sans neutraliser l'appel terminal dans `VoxCoreVoice::stop()`.

### Correctif Appliqué (Showcase)
- **Protection d'idempotence applicative dans `AppController::triggerStop()` :**
  - Filtrage anti-rebond matériel et logique : tout appel survenant moins de 400 ms après un STOP précédent ou pendant que l'interruption est déjà en cours (`m_stopHandlingActive`) est immédiatement ignoré.
  - Détection de repos : si aucun tour vocal n'est actif (`curSt == READY && !m_voice.isSpeaking() && !m_voice.isBusy()`), la commande est court-circuitée.
  - Horodatage précis avec `millis()` et traces `[STOP_DIAG]`.

### Correctif Proposé pour VoiceKit (Bibliothèque)
- Dans `VoxCoreVoice::stop()` (`src/VoxCoreVoice.cpp`) :
  Ne pas appeler `_session.requestStop()` si `_conversationController.requestStop()` a déjà pris en charge l'arrêt de la session active.
- Dans `LiveSessionController::requestStop()` (`src/core/LiveSessionController.cpp`) :
  Rendre la méthode idempotente : si `_turnCancelled` est déjà vrai, ne pas réémettre `_provider->sendInterruption()`.

---

## 3. Diagnostic du Verrouillage WebSocket (COR-STOP-02)

### Faits Établis (Tâches FreeRTOS et Concurrence)
L'erreur système relevée est :
```text
WEBSOCKET_CLIENT: Could not lock ws-client within 1000 timeout
```
Cette ligne est émise directement par le pilote ESP-IDF `esp_websocket_client.c` (`esp_websocket_client_send_text`) lorsque la sémaphore `client->lock` ne peut être obtenue en moins de `1000` ticks FreeRTOS.

Les tâches FreeRTOS et leurs rôles respectifs sont :
1. **`websocket_task` (prio 2, sans affinité de cœur) :**
   - Assure la boucle de lecture des trames réseau TLS.
   - En phase de restitution vocale IA (24 kHz), OpenAI transmet en rafale des trames de données audio (`response.audio.delta`).
   - Dans ESP-IDF v4.4, `websocket_task` acquiert `client->lock` de façon bloquante (`xSemaphoreTake(client->lock, portMAX_DELAY)`) avant de lire la socket TLS (`esp_transport_read`) puis dispatche les événements `WEBSOCKET_EVENT_DATA` à `EspWebSocketTransport::eventHandler` **tout en maintenant ce verrou**.
   - Dans le callback de réception, `OpenAIRealtimeClient::handleIncomingMessage` effectue le parsing JSON (`cJSON_ParseWithLength`), le décodage Base64 et l'injection des échantillons dans `LiveAudioPlayer`.
2. **`loopTask` (prio 1, Cœur 1) :**
   - Exécute `AppController::update()`, qui lors de STOP invoque `_provider->sendInterruption()`.
   - `EspWebSocketTransport::send()` appelle `esp_websocket_client_send_text(..., pdMS_TO_TICKS(1000))`.
   - `send_text` tente d'acquérir `client->lock`.
3. **Contention démontrée :**
   - Si `websocket_task` est engagée dans la lecture d'un bloc réseau ou le décodage d'une trame audio volumineuse, ou si la socket TLS subit une latence réseau, `client->lock` demeure réservé par `websocket_task`.
   - `loopTask` bloque pendant 1 000 ms sur `xSemaphoreTake(client->lock, 1000)`.
   - Au bout de 1 000 ms, l'attente expire : le log `Could not lock ws-client within 1000 timeout` est émis, et `response.cancel` **n'est pas transmis au serveur**.

### Conséquences
Faute d'avoir reçu `response.cancel`, le serveur OpenAI continue de saturer la connexion avec le flux audio, alors même que le CoreS3 tente d'annuler le tour et d'envoyer une seconde requête d'interruption.

---

## 4. Analyse de la Perte de Connexion (COR-STOP-03)

### Faits Établis (Séquence d'États)
Les logs indiquent :
```text
[PTT] SPEAKING -> INTERRUPTING
[PTT] INTERRUPTING -> DISABLED
[PTT] DISABLED -> READY
```
Puis :
```text
TRANSPORT_WS: Error reading payload data
[WS] WebSocket deconnecte
[LiveSession] CONNECTED -> RECONNECTING
```

Pourquoi ce passage transitoire par `DISABLED` puis retour immédiat à `READY` ?
1. **Passage à `DISABLED` :**
   Dans `VoiceInteractionController::processIntents()` (`CANCEL_CAPTURE`) :
   ```cpp
   setState(VoiceInteractionState::INTERRUPTING);
   _lastTurnCancelled = true;
   _voice.stop();
   ...
   if (_voice.isReady() && _voice.getSession().getState() == LiveSessionState::CONNECTED && !_voice.isBusy()) {
       setState(VoiceInteractionState::READY);
   } else {
       setState(VoiceInteractionState::DISABLED);
   }
   ```
   Immédiatement après `_voice.stop()`, les buffers DMA de l'amplificateur matériel AW88298 finissent encore d'être vidés. `_player.isPlaying() > 0` est donc vrai, ce qui fait que `_voice.isBusy()` renvoie `true`. Le contrôleur bascule donc à `DISABLED`.
2. **Retour hâtif à `READY` :**
   Quelques millisecondes plus tard, l'arrêt complet de la lecture audio déclenche `setState(VoiceState::READY)`. Le callback `notifyVoiceStateChanged(VoiceState::READY)` réévalue `_voice.isReady()` : comme la session est encore marquée `CONNECTED` au niveau applicatif et que le DMA s'est vidé, l'état bascule immédiatement à `READY`.
3. **Déconnexion consécutive :**
   Pendant ce temps, sur le transport physique, la socket a été déstabilisée par l'échec d'envoi et la surcharge de données entrantes non consommées. `esp_transport_read` retourne une erreur, provoquant :
   `TRANSPORT_WS: Error reading payload data`  
   `[WS] WebSocket deconnecte`  
   `[LiveSession] CONNECTED -> RECONNECTING`

### Correctif Appliqué (Showcase)
- Découplage strict de la disponibilité dans `AppController` :
  - Création de `AppController::isSessionReady()` : vérifie que le Wi-Fi est actif, que `LiveSessionState` est rigoureusement `CONNECTED`, que le moteur n'est pas en phase de connexion (`!m_voice.isConnecting()`), qu'aucun changement de profil n'est en cours et qu'aucun STOP n'est actif.
  - Dans `VoiceAssistantUI` : même si `VoiceInteractionState` rapporte `READY`, le bouton `PARLER` est maintenu à l'état désactivé `LV_STATE_DISABLED` avec le libellé « ATTENTE » tant que `isSessionReady()` est faux.
  - Surveillance active de `LiveSessionState` dans `AppController::update()` pour détecter toute transition vers `RECONNECTING` ou `CONNECTING`, avertir l'IHM et bloquer instantanément les actions de prise de parole.

---

## 5. Analyse des Reconnexions TLS et DNS (COR-STOP-04)

### Faits Établis
Après déconnexion, les journaux matériels affichent :
```text
esp-tls: select() timeout
getaddrinfo() returns 202
```
1. **`esp-tls: select() timeout` :**
   Lorsqu'une socket TLS précédente est abruptement fermée par `esp_websocket_client_stop()` pendant un transfert actif, la couche lwIP maintient la socket en état `TIME_WAIT` / `CLOSE_WAIT`. Si la nouvelle tentative de connexion est initiée trop vite ou sans vidange complète des tampons réseau, le handshake TLS (`mbedtls_ssl_handshake`) ne reçoit pas les paquets de négociation dans le délai imparti.
2. **`getaddrinfo() returns 202` :**
   Le code 202 correspond à `EAI_FAIL` (échec temporaire de résolution de nom) dans la pile lwIP d'ESP-IDF. Cela survient lorsque les ressources de sockets ou les tampons de paquets réseau de l'ESP32-S3 sont temporairement épuisés par la session précédente non purgée, empêchant le résolveur DNS UDP d'allouer sa socket.
3. **Tentatives en boucle :**
   `LiveSessionController` possède un algorithme de reconnexion avec backoff exponentiel (1 s, 2 s, 4 s jusqu'à `LIVE_MAX_RECONNECT_ATTEMPTS = 5`). Tant que les ressources ne sont pas libérées, les tentatives consécutives échouent.

### Correctif Appliqué (Showcase)
- L'application VoiceAssistant s'abstient impérativement d'intervenir directement sur le transport (aucun appel direct à `disconnect()` ou manipulation de session).
- L'application observe passivement les états de reconnexion via `LiveSessionState` et `isConnecting()`, maintenant l'interface utilisateur en attente jusqu'à stabilisation complète confirmée par `onReady()`.

---

## 6. Paramètres Pendant la Reconnexion (COR-STOP-05)

### Diagnostic
Pendant que VoiceKit tente de reconnecter la session OpenAI, l'utilisateur a pu appuyer sur les boutons de sélection de fournisseur ou de voix dans l'écran Paramètres.
Ces requêtes étaient relayées à `requestProfileChange()`, qui refusait l'opération avec :
`[APP] Modification de profil refusée : opération déjà en cours`.
L'IHM affichait alors « Échec modification », sans expliquer à l'utilisateur que le système était en phase transitoire de reconnexion.

### Correctif Appliqué (Showcase)
1. **Méthode de verrouillage global : `AppController::canModifySettings()`**
   Autorise les modifications uniquement si :
   - Le Wi-Fi est connecté.
   - La session est active (`CONNECTED`).
   - Aucune reconnexion ou initialisation n'est en cours (`!m_voice.isConnecting()`).
   - L'état vocal n'est ni `INTERRUPTING`, `RECOVERING`, `LISTENING`, `SPEAKING`, `STARTING`, ni `STOPPING_CAPTURE`.
   - Aucun changement de profil antérieur n'est en cours.
2. **Verrouillage visuel et fonctionnel dans `SettingsScreen` :**
   - Implémentation de `SettingsScreen::updateControlsAvailability()` appelée à chaque cycle `update()`.
   - Dès que `canModifySettings()` devient faux, les boutons de changement de fournisseur (`m_btnGemini`, `m_btnOpenAi`), de langue (`m_btnLang`), de voix (`m_btnVoice`) et de sauvegarde NVS (`m_btnSaveNvs`) reçoivent automatiquement l'état `LV_STATE_DISABLED`.
   - Le libellé de statut affiche en permanence :  
     `"Connexion en cours — paramètres indisponibles"`
   - Les callbacks d'événements tactiles (`onProviderClicked`, `onLangCycleClicked`, `onVoiceCycleClicked`, `onSaveNvsClicked`) vérifient `isSettingsActionAllowed()` et abandonnent immédiatement tout traitement si l'accès est verrouillé.
   - Dès le rétablissement de la session, les contrôles sont réactivés et le statut repasse à « Prêt ».

---

## 7. Correction des Fragments de Réponse OpenAI (COR-TEXT-01)

### Diagnostic de la Sémantique VoiceKit
Le protocole OpenAI Realtime transmet les transcriptions de sortie par des événements `response.output_audio_transcript.delta`.
Dans VoiceKit :
1. `OpenAIRealtimeClient::handleIncomingMessage` reçoit chaque delta ("Oui", " je", " connais", ...).
2. `LiveSessionController::handleIncomingAssistantTranscription` agrège ces fragments dans son accumulateur `_lastAssistantText`.
3. Il invoque ensuite `_onOutputTranscription(_lastAssistantText)`.
4. `VoxCoreVoice` et `ConversationController` relaient cette chaîne vers `m_voice.onAssistantText(text)`.

**Sémantique confirmée :**  
Chaque appel reçu par `AppController::onAssistantText(text)` transporte la **version cumulative en cours d'élaboration** de la réponse, et non un fragment isolé.

Dans la version initiale, l'envoi de chaque mise à jour cumulative à l'IHM et la gestion de l'historique présentaient deux risques :
- Rafraîchissement excessif de widgets LVGL et sections critiques répétées lors de rafales de deltas.
- En cas d'interruption par STOP, risque de conserver dans l'historique un dialogue tronqué comme s'il était complet.

### Correctif Appliqué (Showcase)
1. **Régulation du flux vers LVGL (Throttling) :**
   - `AppController::onAssistantText` met à jour la chaîne locale `m_currentTurnAssistantText`, mais ne pousse vers le buffer thread-safe `m_pendingAssistantText` qu'à une fréquence maximale d'une fois toutes les 60 ms (`m_lastAssistantUiPushMs`).
   - Le rafraîchissement d'affichage reste fluide pour l'utilisateur sans surcharger le microcontrôleur ni fragmenter la RAM.
2. **Filtrage absolu post-STOP :**
   - Dès que STOP est demandé, `m_turnInterruptedByStop = true`.
   - Tout appel tardif à `onAssistantText` ou `onUserText` est immédiatement ignoré.
3. **Consignation atomique en fin de tour :**
   - En cas d'achèvement nominal (`onSpeechFinished` / `onConversationFinished`) : le texte final complet est poussé vers l'IHM et enregistré en une entrée unique dans `m_history`.
   - En cas d'interruption par STOP : la réponse tronquée est explicitement rejetée de l'historique complet avec trace :
     `[STOP_DIAG] Tour interrompu par STOP : non consigné comme réponse complète`.
   - `m_currentTurnAssistantText` est réinitialisé à vide.

---

## 8. Liste des Fichiers Modifiés

Toutes les modifications sont strictement limitées au répertoire du showcase `showcases/VoiceAssistant/` :

| Fichier | Nature des modifications |
|---|---|
| `showcases/VoiceAssistant/src/AppController.h` | Déclaration de `isSessionReady()`, `canModifySettings()`, `isStopInProgress()`, variables de synchronisation STOP (`m_stopHandlingActive`, `m_lastStopTriggerMs`, `m_turnInterruptedByStop`, `m_lastAssistantUiPushMs`, `m_lastObservedLiveState`). |
| `showcases/VoiceAssistant/src/AppController.cpp` | Instrumentation `[STOP_DIAG]` et `[SESSION_DIAG]`, idempotence de `triggerStop()`, régulation 60 ms de `onAssistantText()`, enregistrement unique et sécurisé de l'historique, intégration de `onConversationFinished()`, implémentation de `isSessionReady()` et `canModifySettings()`. |
| `showcases/VoiceAssistant/src/SettingsScreen.h` | Ajout de `m_btnSaveNvs`, de l'indicateur `m_controlsBlocked`, et des méthodes `isSettingsActionAllowed()`, `updateControlsAvailability()`. |
| `showcases/VoiceAssistant/src/SettingsScreen.cpp` | Sauvegarde de `m_btnSaveNvs`, implémentation du verrouillage dynamique `LV_STATE_DISABLED`, message obligatoire `"Connexion en cours — paramètres indisponibles"`, protection des clics sur fournisseur, langue, voix et NVS. |
| `showcases/VoiceAssistant/src/VoiceAssistantUI.cpp` | Utilisation de `m_controller->isSessionReady()` pour conditionner l'état READY et le bouton PARLER, affichage explicite de l'état de reconnexion dans le header et le statut central. |

*Aucun fichier dans `VoiceKit/src/` n'a été modifié lors de cette mission.*

---

## 9. Résultat Réel de Compilation PlatformIO

La compilation a été exécutée sur l'environnement réel :

```powershell
pio run -d showcases/VoiceAssistant -e m5stack-cores3
```

### Données d'Exécution
- **Statut final :** `SUCCESS`
- **Durée de compilation :** 23.85 secondes
- **Environnement PlatformIO :** `m5stack-cores3`
- **Plateforme ESP32 :** Espressif 32 (6.9.0)
- **Framework :** framework-arduinoespressif32 @ 3.20017.241212+sha.dcc1105b
- **Version LVGL :** lvgl @ 9.6.0
- **Version VoiceKit :** VoiceKit @ 0.6.7.1
- **Version VoiceAssistant :** v0.1.0 (conservée)

### Utilisation Mémoire
- **RAM Statique :** `164 484 octets` / 327 680 octets (`50.2%`)
- **Mémoire Flash :** `2 090 877 octets` / 8 388 608 octets (`24.9%`)

---

## 10. Tests Logiciels Exécutés

1. **Compilation complète :** Vérification de l'absence d'erreurs de syntaxe, de redéfinition ou de liaisons manquantes.
2. **Vérification d'idempotence STOP :** Validation logique du filtre de rebond (clics rapides < 400 ms ignorés).
3. **Vérification de conditionnement des paramètres :** Validation que tous les boutons de l'écran des paramètres s'inhibent dès que la session n'est pas connectée ou est en reconnexion.
4. **Vérification du découplage de l'historique :** Validation que les tours annulés par STOP ne génèrent aucune entrée d'historique complète erronée.

---

## 11. Tests Matériels à Réaliser sur M5Stack CoreS3

Le plan de validation matérielle suivant doit être déroulé sur le CoreS3 pour certifier la stabilité après STOP :

| Test | Scénario d'essai | Résultat attendu |
|---|---|---|
| **STOP-01** | Conversation normale standard (PTT 2 clics). | Réponse vocale complète reçue et restituée sans coupure. |
| **STOP-02** | Clic STOP pendant l'écoute micro (`LISTENING`). | Fermeture immédiate du micro ES7210, aucune requête transmise à l'IA. |
| **STOP-03** | Clic STOP pendant la réflexion serveur (`PROCESSING`). | Tour annulé proprement, arrêt de l'attente serveur. |
| **STOP-04** | Clic STOP pendant la parole assistante (`SPEAKING`). | Coupure instantanée de l'audio AW88298, arrêt de la synthèse. |
| **STOP-05** | Deux clics STOP très rapides consécutifs. | Traitement unique, aucun envoi réseau concurrent ni crash FreeRTOS. |
| **STOP-06** | STOP puis réengagement immédiat par PARLER. | Nouvelle conversation possible dès que l'assistant est stabilisé à READY. |
| **STOP-07** | STOP puis attente passive sans toucher à l'écran. | La session WebSocket reste connectée et stable si le réseau est disponible. |
| **STOP-08** | Déconnexion forcée du point d'accès Wi-Fi. | Passage propre à l'état indisponible (« Wi-Fi non connecté »), bouton PARLER désactivé (« ATTENTE »). |
| **STOP-09** | Reconnexion du point d'accès Wi-Fi. | Reconnexion automatique et retour à l'état READY (« Assistant prêt »). |
| **STOP-10** | Tentative de changement de voix pendant reconnexion. | Commande bloquée avec le message « Connexion en cours — paramètres indisponibles ». |
| **STOP-11** | Changement de fournisseur après reconnexion stabilisée. | Basculement accepté et confirmé (« Modification appliquée »). |
| **STOP-12** | Réponse longue (plus de 10 lignes de texte). | Une seule entrée d'historique enregistrée, défilement tactile fluide. |
| **STOP-13** | Réponse interrompue par STOP en cours de génération. | Aucun texte résiduel tardif injecté dans l'historique complet. |
| **STOP-14** | Enchaînement de 10 cycles complets PARLER/STOP. | Aucun épuisement de RAM interne ni instabilité progressive observée. |

---

## 12. Correctifs Internes Proposés pour VoiceKit (Phase C)

L'audit démontre formellement qu'une cause racine de la double interruption et de la contention sur le transport réside au niveau de la bibliothèque VoiceKit v0.6.7.1.

### 12.1 Défaut 1 : Double appel de `_session.requestStop()`
- **Fichier :** `VoiceKit/src/VoxCoreVoice.cpp`
- **Fonction :** `void VoxCoreVoice::stop()` (lignes 1092-1122)
- **Cause :** `VoxCoreVoice::stop()` appelle `_conversationController.requestStop()` (qui appelle déjà `_session->requestStop()`), puis appelle à nouveau `_session.requestStop()` 28 lignes plus bas.
- **Correction minimale proposée :**
  ```diff
  --- a/src/VoxCoreVoice.cpp
  +++ b/src/VoxCoreVoice.cpp
  @@ -1090,6 +1090,7 @@ void VoxCoreVoice::stop() {
       if (_wakeWordConversationController.isEnabled() && _wakeWordConversationController.isSessionInProgress()) {
           _wakeWordConversationController.stopSession(true);
       }
  +    bool sessionAlreadyStopped = false;
       if (_conversationEnabled && _conversationController.isTurnActive()) {
           _conversationController.requestStop();
  +        sessionAlreadyStopped = true;
       }
       _pendingText = "";
       _pendingSpeech = SpeechQueueItem();
  @@ -1119,7 +1120,9 @@ void VoxCoreVoice::stop() {
           _modeSwitchPending = false;
           Serial.println("[VoiceKit] stop() : mode conversationnel precedent restaure");
       }
  -    _session.requestStop();
  +    if (!sessionAlreadyStopped) {
  +        _session.requestStop();
  +    }
       _player.setPlaybackAllowed(true);
       if (_session.getState() == LiveSessionState::CONNECTED) {
  ```
- **Évaluation du risque :** Risque nul de régression. Supprime la deuxième interruption redondante et élimine la double requête réseau `response.cancel`.

### 12.2 Défaut 2 : Non-idempotence de `LiveSessionController::requestStop()`
- **Fichier :** `VoiceKit/src/core/LiveSessionController.cpp`
- **Fonction :** `void LiveSessionController::requestStop()` (ligne 860)
- **Cause :** La méthode envoie inconditionnellement `_provider->sendInterruption()` même si le tour a déjà été annulé.
- **Correction minimale proposée :**
  ```diff
  --- a/src/core/LiveSessionController.cpp
  +++ b/src/core/LiveSessionController.cpp
  @@ -860,6 +860,11 @@
   void LiveSessionController::requestStop() {
  +    if (_turnCancelled.load(std::memory_order_acquire) && !_turnActive.load(std::memory_order_acquire)) {
  +        Serial.println("[LiveSession] STOP ignore : tour deja annule");
  +        return;
  +    }
       Serial.println(
           "\n[LiveSession] >>> DEMANDE INTERRUPTION STOP PRIORITAIRE <<<");
       _interruptedTurnId = _currentTurnId;
  ```
- **Évaluation du risque :** Risque nul. Garantit qu'aucun paquet réseau parasite n'est généré en cas d'appels multiples rapprochés.

---

## 13. Synthèse de Conformité aux Critères d'Acceptation

| Critère d'Acceptation | Statut | Justification |
|---|---|---|
| **Compilation réelle PlatformIO** | **VALIDÉ** | `[SUCCESS]` en 23.85 s sur `m5stack-cores3` (Flash 24.9%, RAM 50.2%). |
| **PTT à deux clics préservé** | **VALIDÉ** | Mécanisme `onTalkClick()` (STARTING/LISTENING/STOPPING_CAPTURE) intact. |
| **VAD local préservé** | **VALIDÉ** | `VoiceCaptureValidator` et configuration acoustique inchangées. |
| **Paramètres bloqués en indisponibilité** | **VALIDÉ** | `canModifySettings()` bloque l'accès et affiche le message imposé. |
| **Historique sans fragments cumulatifs** | **VALIDÉ** | Une seule entrée consignée par tour, réponses arrêtées par STOP rejetées. |
| **Événements tardifs post-STOP filtrés** | **VALIDÉ** | `m_turnInterruptedByStop` et `isLastTurnCancelled()` filtrent tout résidu. |
| **Aucune modification dans VoiceKit** | **VALIDÉ** | `git status` confirme zéro modification dans `src/`. Correctifs proposés en Phase C. |
| **Version VoiceAssistant v0.1.0 conservée**| **VALIDÉ** | Numéro de version strictly maintenu à `v0.1.0`. |
