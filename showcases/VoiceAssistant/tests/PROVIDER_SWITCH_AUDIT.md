# Audit Technique & Correctifs : Basculement de Fournisseur (OpenAI Realtime <-> Gemini Live)

**Application :** VoiceAssistant v0.1.0 (Showcase officiel VoiceKit)  
**Matériel testé :** M5Stack CoreS3 (ESP32-S3, port `COM9`)  
**Bibliothèque sous-jacente :** VoiceKit v0.6.7.1  
**Date :** 19 Novembre 2026  

---

## 1. Synthèse des Causes Racines Identifiées

L'analyse du code source et les essais matériels sur M5Stack CoreS3 ont mis en évidence **six défauts cumulatifs** empêchant le passage d'OpenAI Realtime vers Gemini Live :

1. **Diagnostic unique masquant l'état réel (`AppController::canModifySettings()` / `SettingsScreen`) :**
   - `canModifySettings()` retournait un simple `bool` sans distinguer `WIFI_DISCONNECTED`, `SESSION_CONNECTING`, `SESSION_RECONNECTING`, `PROFILE_CHANGE_PENDING`, `STOP_IN_PROGRESS`, `VOICE_BUSY` et `SESSION_ERROR`.
   - `SettingsScreen` affichait systématiquement le même message (*« Connexion en cours — paramètres indisponibles »*) et désactivait globalement **tous** les boutons (y compris Gemini et OpenAI) dès que la session n'était pas `CONNECTED`.
2. **Couplage entre paramètres acoustiques et changement de fournisseur :**
   - Tant qu'OpenAI tentait une reconnexion ou se trouvait en erreur, les boutons de sélection de fournisseur restaient désactivés, empêchant l'utilisateur de quitter une session OpenAI indisponible.
3. **Limitation de l'API publique VoiceKit en reconnexion (`VoxCoreVoice`) :**
   - `VoxCoreVoice::applyVoiceProfile()` refuse strictement tout changement tant que `isConnecting()` est vrai (incluant `LiveSessionState::CONNECTING` et `RECONNECTING`).
   - Parallèlement, `VoxCoreVoice::update()` relançait immédiatement `_session.activate()` sur l'ancien fournisseur dès que la session passait `DISCONNECTED` ou `ERROR`, sans API publique permettant de sortir proprement d'une boucle de reconnexion avant un changement de fournisseur.
4. **Libération prématurée de `m_stopHandlingActive` et forçage `PENDING_STOP` à 1 500 ms :**
   - `m_stopHandlingActive` était remis à `false` immédiatement dans `onStateChanged` dès la sortie de `INTERRUPTING` (alors que `VoiceInteractionController` transite `INTERRUPTING -> READY/DISABLED` dans le même cycle), avant stabilisation effective du moteur et de la session.
   - `AppController::update()` contenait `if (stopped || (nowMs - m_profileOpStartMs > 1500))`, forçant `applyVoiceProfile()` sur un moteur encore occupé à l'expiration du délai.
5. **Modèle Gemini Live obsolète et erreurs serveur silencieuses :**
   - `models/gemini-2.0-flash-exp` n'est plus présent dans les modèles `bidiGenerateContent` actifs de l'API Gemini Live v1beta (interrogation directe de `/v1beta/models`).
   - De plus, `VoxCoreVoice::bindProviderForConfig()` et `onStateChanged(CONNECTED)` écrasaient explicitement tout modèle `gemini-2.5` ou `gemini-3.8` vers `VOICEKIT_DEFAULT_MODEL` (`models/gemini-2.0-flash-exp`).
   - `EspWebSocketTransport` ignorait silencieusement les trames de contrôle WebSocket `CLOSE` (opcode `0x08`) sans journaliser le code ni la raison serveur, et `GeminiLiveClient` ne journalisait pas les objets JSON `error` / `goAway`.
6. **Défauts bas niveau détectés lors des essais matériels CoreS3 (T5/T6) :**
   - **Fuite mémoire `cJSON` post-STOP dans `OpenAIRealtimeClient::handleIncomingMessage()` :** six `return;` anticipés (notamment sur les paquets `response.audio.delta` d'un tour annulé par STOP, ~26 Ko chacun) ne libéraient pas `cJSON* root`, amputant ~31 Ko de RAM interne et provoquant `mbedtls_ssl_setup returned -0x7F00` lors du basculement ultérieur vers Gemini Live.
   - **Déconnexion WebSocket par offsets internes (`EspWebSocketTransport`) et rollback depuis `websocket_task` (`VoxCoreVoice`) :** la manipulation directe d'offsets internes ESP-IDF (`raw[64]`, `raw + 68`, `raw + 20`) lors d'une déconnexion en phase `CONNECTING` provoquait un use-after-free (`LoadProhibited` / `InstrFetchProhibited`), et `_session.deactivate()` appelé depuis le callback `RECONNECTING` sur `websocket_task` déclenchait un assert spinlock FreeRTOS.

---

## 2. Correctifs Appliqués — VoiceAssistant v0.1.0 (`showcases/VoiceAssistant/`)

| Fichier | Modifications principales |
|---|---|
| `include/app_config.h` | Version maintenue à `v0.1.0`. Mise à jour de `GEMINI_MODEL` vers `models/gemini-2.5-flash-native-audio-latest`. |
| `src/AppController.h` / `.cpp` | - Ajout de l'énumération `SettingsBlockReason` (`ALLOWED`, `WIFI_DISCONNECTED`, `SESSION_CONNECTING`, `SESSION_RECONNECTING`, `PROFILE_CHANGE_PENDING`, `STOP_IN_PROGRESS`, `VOICE_BUSY`, `SESSION_ERROR`) avec `getSettingsBlockReason()`, `getProviderSwitchBlockReason()`, `canModifySettings()`, `canSwitchProvider()`, `settingsBlockReasonToString()`, `settingsBlockReasonToFrenchMessage()` et `logStateDiagnostics()`.<br>- Séparation stricte entre `requestAcousticProfileChange()` (exige `CONNECTED` stable) et `switchProvider()` (procédure dédiée non bloquante en 10 étapes : enregistrement, vérification clé API, suspension des captures, arrêt propre du tour, `DISCONNECTING_SESSION` via API publique VoiceKit + stabilisation RAM 180 ms, `APPLYING` uniquement lorsque `!isConnecting() && session == DISCONNECTED`, confirmation `onProfileApplied`, réactivation `PARLER`).<br>- Suppression de la réinitialisation prématurée de `m_stopHandlingActive` dans `onStateChanged` : attente d'une stabilisation continue (150 ms après maintien minimum 150 ms, watchdog 3 000 ms) du moteur, du player, du streamer et de la session.<br>- Suppression de `|| (nowMs - m_profileOpStartMs > 1500)` dans `PENDING_STOP` : un timeout (4 000 ms) signale un échec propre sans jamais forcer `applyVoiceProfile()`. |
| `src/SettingsScreen.h` / `.cpp` | - Séparation de la disponibilité des boutons fournisseur (`m_btnGemini`, `m_btnOpenAi` pilotés par `canSwitchProvider()`) et des paramètres acoustiques (`m_btnLang`, `m_btnVoice` pilotés par `canModifySettings()`).<br>- Affichage du message diagnostique français distinct pour chaque `SettingsBlockReason` et des erreurs explicites (ex. *« Clé Gemini manquante »*).<br>- Mise en surbrillance du fournisseur actif uniquement après confirmation serveur (`onProfileApplied`). |
| `src/VoiceAssistantUI.cpp` | - Affichage explicite de l'état de transition (*« Changement de profil IA... »*, *« Stabilisation arrêt... »*) tout en maintenant `PARLER` désactivé jusqu'à confirmation complète. |

---

## 3. Correctifs Minimaux de l'API Publique & Internes VoiceKit (`src/`)

Conformément à la section 3, les capacités manquantes dans VoiceKit pour sortir proprement d'une connexion/reconnexion et assurer la stabilité matérielle ont été isolées avec un impact minimal :

1. **Nouvelle API publique minimale de sortie de session (`src/VoxCoreVoice.h` & `src/VoxCoreVoice.cpp`) :**
   - `bool requestDisconnectSession()` : annule proprement tout tour/profil en cours, suspend temporairement la reconnexion automatique (`_autoReconnectSuspended = true`) et déclenche `_session.requestDeactivate()`.
   - `bool isSessionDisconnecting() const` : indique si la fermeture de session est en cours.
   - `void resumeAutoReconnect()` : réautorise la reconnexion automatique (également réarmée automatiquement par `applyVoiceProfile(..., true)`, `say()` et `askAi()`).
   - Rollback de perte réseau pendant changement de profil différé sur `loopTask` via `_pendingProfileDropRollback` (ne jamais appeler `_session.deactivate()` depuis `websocket_task`).
   - Suppression du repli forcé qui remplaçait `gemini-2.5` et `gemini-3.8` par `models/gemini-2.0-flash-exp`.
2. **Modèle Gemini Live par défaut (`src/VoiceKitConfig.h`, `src/core/VoiceHubConfig.h`) :**
   - Mise à jour de `VOICEKIT_DEFAULT_MODEL` et `GEMINI_LIVE_MODEL` vers `models/gemini-2.5-flash-native-audio-latest`.
3. **Observabilité erreurs Gemini & WebSocket (`src/core/EspWebSocketTransport.h`, `src/core/GeminiLiveClient.cpp`) :**
   - Journalisation du code et de la raison des trames WebSocket `CLOSE` (`opcode 0x08`) et des objets JSON `error` / `goAway`.
   - Remplacement des accès par offsets bruts ESP-IDF dans `disconnect()` / `pollDisconnect()` par `esp_websocket_client_stop()` + `esp_websocket_client_destroy()` et libération effective de la capacité de `_rxAccum` (`std::string().swap(_rxAccum)`).
4. **Protection de déconnexion intentionnelle (`src/core/LiveSessionController.cpp`) :**
   - Ignorer `onDisconnected` et les retries `onError` lorsque `_deactivating` est vrai.
5. **Correction fuite mémoire `cJSON` post-STOP (`src/core/OpenAIRealtimeClient.cpp`) :**
   - Garde RAII `CJsonGuard` sur `cJSON* root` dans `handleIncomingMessage()`, éliminant les 6 retours anticipés sans `cJSON_Delete(root)`.

---

## 4. Vérification Gemini Live (Modèles & Endpoint)

- **Endpoint :** `wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent`
- **Modèles `bidiGenerateContent` disponibles vérifiés via API `/v1beta/models` :**
  - `models/gemini-2.5-flash-native-audio-latest` *(retenu et validé sur matériel CoreS3)*
  - `models/gemini-2.5-flash-native-audio-preview-09-2025`
  - `models/gemini-2.5-flash-native-audio-preview-12-2025`
  - `models/gemini-3.1-flash-live-preview`
  - `models/gemini-3.8-live`
  - `models/gemini-3.8-live-extended-thinking`
- **Constat :** `models/gemini-2.0-flash-exp` n'est plus proposé pour `bidiGenerateContent`. Avec `models/gemini-2.5-flash-native-audio-latest`, le handshake TLS + `setupComplete` aboutit en **140 à 458 ms** sur CoreS3.

---

## 5. Résultats des Essais Matériels Réels sur M5Stack CoreS3 (`COM9`)

Tous les tests ont été exécutés sur le M5Stack CoreS3 réel connecté en USB (`COM9`), avec Wi-Fi réel et connexions TLS réelles vers OpenAI Realtime et Google Gemini Live.

| # | Scénario testé sur CoreS3 | Fournisseur actif final | État VoiceKit | État LiveSession | État WebSocket | Résultat | Observations / Métriques réelles |
|---|---|---|---|---|---|---|---|
| **T1** | Démarrage direct avec OpenAI | `OpenAI Realtime` | `PRET` | `CONNECTED` | `1` (connecté) | **PASS** | `READY` en 14,6 s (WS 2 553 ms, Setup 204 ms). Diagnostics : `WIFI_DISCONNECTED` -> `SESSION_CONNECTING (SwitchIA=ALLOWED)` -> `ALLOWED`. `freeInt=61 612 o`. |
| **T2** | Démarrage direct avec Gemini (`-DUSE_GEMINI`) | `Gemini Live` | `PRET` | `CONNECTED` | `1` (connecté) | **PASS** | `models/gemini-2.5-flash-native-audio-latest` (`Aoede`), `READY` en 3 535 ms (WiFi 1 116 ms, WS 2 102 ms, `setupComplete` 241 ms). `Interaction=READY`. |
| **T3** | OpenAI -> Gemini après une conversation terminée | `Gemini Live` | `PRET` | `CONNECTED` | `1` (connecté) | **PASS** | Tour OpenAI complet (`VOICE_BUSY` pendant le tour, retour `ALLOWED`), puis fermeture propre OpenAI (`DISCONNECTED`) et confirmation serveur `[Gemini] Aoede (fr-FR)`. `ready=1`. |
| **T4** | Gemini -> OpenAI | `OpenAI Realtime` | `PRET` | `CONNECTED` | `1` (connecté) | **PASS** | Fermeture propre Gemini (`DISCONNECTED`), connexion OpenAI (`setupComplete` ~200 ms), confirmation `[OpenAI] alloy (fr-FR)`. `freeInt=58 724 o`, `ready=1`. |
| **T5** | OpenAI -> Gemini après STOP | `Gemini Live` | `PRET` | `CONNECTED` | `1` (connecté) | **PASS** | STOP pendant `SPEAKING` : maintien `STOP_IN_PROGRESS` jusqu'à stabilisation effective (`STOP stabilisé`), `freeInt=58 176 o` (zéro fuite `cJSON`), puis bascule confirmée vers `[Gemini]`. |
| **T6** | Changement de fournisseur après reconnexion | `Gemini Live` | `PRET` | `CONNECTED` | `1` (connecté) | **PASS** | Perte WebSocket simulée (`CONNECTED -> RECONNECTING -> CONNECTING`) : `canModify=0`, `canSwitch=1 (ALLOWED)`. Bascule vers Gemini acceptée, sortie propre via `requestDisconnectSession()`, puis `CONNECTED` / `WS=1`. |
| **T7** | Rejet explicite si clé API Gemini manquante | `OpenAI Realtime` *(inchangé)* | `PRET` | `CONNECTED` | `1` (connecté) | **PASS** | Refus immédiat (`accepted=0`, message `Clé Gemini manquante`), session OpenAI active strictement préservée. |
| **T8** | Plusieurs basculements successifs sans redémarrage (4 cycles : O->G->O->G) | `Gemini Live` | `PRET` | `CONNECTED` | `1` (connecté) | **PASS** | T8.1 (`OpenAI`, `freeInt=57 288`), T8.2 (`Gemini`, `freeInt=20 628`), T8.3 (`OpenAI`, `freeInt=56 832`), T8.4 (`Gemini`, `freeInt=20 176`). Zéro crash, zéro redémarrage, `ready=1` après chaque cycle. |

---

## 6. Compilation Finale de Production

Commande exécutée :
```bash
pio run -d showcases/VoiceAssistant -e m5stack-cores3
```

- **Statut :** `SUCCESS`
- **Version applicative :** `VoiceAssistant v0.1.0` (conservée)
- **Version VoiceKit :** `0.6.7.1`
- **Empreinte mémoire :** RAM ~50.2 % (~164.5 Ko / 327.7 Ko), Flash ~25.0 % (~2.10 Mo / 8.39 Mo)

---

## 7. Conclusion

Les essais matériels complets sur M5Stack CoreS3 valident le passage bidirectionnel **OpenAI Realtime <-> Gemini Live** sans redémarrage, y compris après une conversation terminée, après une interruption **STOP**, et pendant une tentative de reconnexion, tout en préservant l'intégrité du PTT à deux clics, du VAD local et de l'arrêt prioritaire STOP.
