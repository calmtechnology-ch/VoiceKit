# Changelog — VoxCore VoiceKit

Toutes les modifications notables apportées à ce projet sont documentées dans ce fichier.

Le format est basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.0.0/)
et ce projet adhère au [Semantic Versioning](https://semver.org/lang/fr/).

---

## [0.6.1] - 2026-10-28 (Correctif de Stabilité — Gemini Live, WebSocket TLS et Wi-Fi FastConnect)

### Correction A — Réinitialisation Polymorphe des Métriques PCM entre Tours (`IVoiceProvider` & `LiveSessionController`)
- **Indépendance de `_legacyClient` et Support Multi-Fournisseurs** :
  - Correction de l'anomalie où `VoxCoreVoice::begin()` initialisait `LiveSessionController` via `IVoiceProvider*` en laissant `_legacyClient` à `nullptr`, ce qui empêchait la réinitialisation de `_turnCompleteReceived` entre deux tours et provoquait de fausses alertes `[LiveClient] ALERTE ORDRE: Bloc PCM recu APRES turnComplete` ainsi qu'un bilan `0 ech decodes` à partir du 2ᵉ tour.
  - Extension rétrocompatible de `IVoiceProvider` avec implémentations virtuelles par défaut : `resetTurnRxMetrics()`, `getTurnDecodedSamples()`, `getPostTurnCompleteBlocks()`, `getPostTurnCompleteSamples()`, `isTurnCompleteReceived()`.
  - Implémentation complète dans `GeminiVoiceProvider`, `GeminiLiveClient` et `OpenAIRealtimeClient`.
  - Conversion des compteurs de réception par tour en variables `std::atomic` pour éliminer tout risque de concurrence entre la tâche WebSocket (`websocket_task`) et la boucle principale (`loopTask`).
  - Réinitialisation déterministe au début de chaque tour (`sendTextMessage`, `beginAudioTurn`, `sendActivityStart`, `connect`) tout en conservant la détection stricte des véritables blocs PCM tardifs arrivant après `turnComplete`.

### Correction B — Ordonnancement FreeRTOS pendant le Handshake TLS (`EspWebSocketTransport`)
- **Suppression de la Famine CPU de `loopTask` durant mbedTLS** :
  - Analyse de l'API `esp_websocket_client_config_t` de l'ESP-IDF v4.4 (`framework-arduinoespressif32`) : absence de champ `task_core` (`websocket_task` créée avec `tskNO_AFFINITY`). Avec `cfg.task_prio = 5`, la tâche WebSocket préemptait `loopTask` (Cœur 1, priorité 1) pendant toute la durée du calcul cryptographique mbedTLS (~1,55 s d'interruption de la boucle applicative LVGL/tactile).
  - Démarrage de `websocket_task` à la priorité `DEFAULT_HANDSHAKE_TASK_PRIO = 1` (égale à `loopTask` priorité 1) pendant la connexion TCP et le handshake TLS : l'ordonnanceur FreeRTOS SMP exécute le handshake sur le Cœur 0 lorsqu'il est disponible et partage le Cœur 1 en Round-Robin (1 ms) avec `loopTask` sans jamais bloquer l'interface.
  - Relèvement automatique de la priorité à `DEFAULT_STEADY_TASK_PRIO = 2` dès réception de `WEBSOCKET_EVENT_CONNECTED` (après le handshake TLS) pour garantir une réception réactive des trames PCM 24 kHz (< `AudioPlayerTask` priorité 4), et retour automatique à `1` sur `WEBSOCKET_EVENT_DISCONNECTED` avant toute reconnexion TLS.
- **Instrumentation Non-Bloquante TLS / FreeRTOS / Mémoire (`ConnectionMetrics`)** :
  - Ajout des diagnostics de cœur et priorité (`wsTaskCore`, `wsTaskPriority`, `wsHandshakeTaskPriority`, `wsSteadyTaskPriority`, `loopTaskCore`, `loopTaskPriority`), des intervalles d'interruption apparente de la boucle principale (`lastLoopGapMs`, `maxLoopGapMs`, `maxLoopGapDuringTlsMs`), et de la mémoire interne libre pendant TLS (`freeInternalHeapDuringTls`, `minFreeInternalHeapDuringTls`, `largestInternalBlockDuringTls`).

### Correction C — Repli Wi-Fi FastConnect et Reconnexion Asynchrones (`WiFiManager`)
- **Élimination du Blocage Synchrone (~343 ms) sur la Tâche Principale** :
  - Désactivation de l'écriture flash NVS synchrone redondante du pilote Wi-Fi via `WiFi.persistent(false)` dans `WiFiManager::begin()`, et suppression des réécritures NVS `"vk_wifi"` lorsque le BSSID et le canal n'ont pas changé.
  - Création d'une machine à états explicite (`WiFiManagerState`) et d'une tâche ouvrière FreeRTOS unique persistante (`vk_wifi_wkr`, Cœur 0, priorité 1, file de commandes non-bloquante) chargée d'exécuter en arrière-plan les opérations Wi-Fi bloquantes (`fallbackToNormalScan`, `WiFi.reconnect`, sauvegarde NVS `"vk_wifi"`), réduisant la durée de `WiFiManager::tick()` sur `loopTask` de ~343 ms à < 1 ms.
  - Libération propre des ressources (`end()` et `~WiFiManager()`) et protection par drapeau atomique `_asyncOpInProgress` contre tout appel Wi-Fi concurrent.
  - Mesure séparée des durées d'exécution de `WiFiManager::tick()` (`lastWifiTickDurationUs`, `maxWifiTickDurationUs`) et de `VoiceKit::update()` / `VoxCoreVoice::update()` (`lastUpdateDurationUs`, `maxUpdateDurationUs`).

### Validation & Non-Régression
- **Sanctuaire Audio VoxCore Intact** : Zéro modification des tampons PSRAM (120 000 échantillons @ 24 kHz), des 4 buffers DMA (160 ms), de la tâche `AudioPlayerTask` Mode 2 ni de la politique de vidange matérielle complète.
- **3 Nouvelles Suites de Tests Unitaires (Suites 80 à 82)** : **1 227 validations réussies avec 0 échec**.

---

## [0.6.0] - 2026-10-26 (Multi-Fournisseurs — Intégration OpenAI Realtime & Préservation VoxCore)

### Architecture Multi-Fournisseurs (`IVoiceProvider`)
- **Contrat d'Abstraction Unifié** :
  - Création de l'interface pure `IVoiceProvider` définissant le cycle de vie complet d'un fournisseur vocal : initialisation/connexion TLS (`connect`, `disconnect`, `isConnected`), configuration dynamique (`updateSessionConfig`), émission audio (`sendAudioChunk`, `commitAudio`), requêtes textuelles (`sendTextMessage`), contrôle de conversation (`createResponse`, `sendInterruption`), et enregistrement de callbacks standardisés (transcriptions STT/TTS, audio streaming RX, fin de réponse, fermeture, statut de session et erreurs).
  - Fournisseur par défaut : `VoiceProviderType::GEMINI` (Google Gemini Live) pour une compatibilité ascendante stricte à 100%.
  - Fournisseur alternatif : `VoiceProviderType::OPENAI` (OpenAI Realtime API).
  - Sélection transparente via `VoiceKitConfig` : `config.provider = VoiceProviderType::OPENAI;` sans aucune modification nécessaire dans le code applicatif.

### Client Natif OpenAI Realtime (`OpenAIRealtimeClient`)
- **Connexion WebSocket TLS Directe ESP32-S3** :
  - Connexion sécurisée sur l'endpoint officiel `wss://api.openai.com/v1/realtime?model=<model>` (par défaut `gpt-realtime-2.1-mini`).
  - Authentification sécurisée par en-têtes HTTP `Authorization: Bearer <OPENAI_API_KEY>` et `OpenAI-Beta: realtime=v1`.
  - Intégration du bundle de certificats officiels DigiCert (`OpenAIRoots.h`).
  - Masquage systématique des clés d'API dans les logs de débogage et protection des secrets.
- **Protocole Realtime Bidi** :
  - Négociation de session via `session.update` dès réception de `session.created` (désactivation du turn_detection serveur pour laisser la priorité au VAD local matériel de VoiceKit, voix `alloy`, modèle de transcription `whisper-1`).
  - Streaming audio montant : encodage Base64 direct et émission d'événements `input_audio_buffer.append`.
  - Contrôle de tour : envoi automatique de `input_audio_buffer.commit` suivi de `response.create` lors de la détection de fin de parole.
  - Streaming audio descendant : réception des événements `response.audio.delta`, décodage Base64 optimisé et injection dans le tampon PSRAM du lecteur VoxCore.
  - Gestion des transcriptions : extraction asynchrone des transcriptions utilisateur Whisper (`conversation.item.input_audio_transcription.completed`) et assistant (`response.audio_transcript.delta` / `done`).
  - Interruption prioritaire STOP : émission immédiate de `response.cancel` et `input_audio_buffer.clear` avec vidage instantané du buffer de restitution.

### Rééchantillonnage Audio Dédié (`AudioResampler`)
- **Conversion 16 kHz vers 24 kHz Non-Bloquante** :
  - Conception d'un rééchantillonneur linéaire haute performance en arithmétique entière à virgule fixe (`(x0 + 2*x1)/3`, `(2*x1 + x2)/3`).
  - Conversion par blocs de capture micro (512 échantillons 16 kHz $\to$ 768 échantillons 24 kHz pour OpenAI).
  - Traitement en moins de 15 µs sur ESP32-S3 Core 1, n'impactant aucunement la tâche audio critique FreeRTOS.

### Préservation Intégrale du Moteur Audio VoxCore
- **Stabilité Audio & Zéro Coupure Finale** :
  - Conservation intégrale de la tâche FreeRTOS audio dédiée (Mode 2), des buffers DMA I2S et du tampon circulaire PSRAM de 120 000 échantillons (~5 secondes d'audio).
  - L'événement `response.done` (OpenAI comme Gemini) signale la fin de la génération serveur mais ne coupe jamais la restitution : vidage exact jusqu'au tout dernier échantillon PCM.
  - Préservation certifiée du test de prononciation « Cette phrase se termine par alpha tango terminé » sans coupure de la syllabe finale.

### Nouvel Exemple `OpenAIConversation`
- **Démonstration Minimaliste sans LVGL** :
  - `examples/OpenAIConversation/` : conversation bidirectionnelle en français avec OpenAI Realtime sur M5Stack CoreS3.
  - Indicateur d'état, capture microphone continue avec VAD local, restitution haut-parleur AW88298, transcription console et interruption par bouton tactile.

### Validation & Non-Régression
- **11 Nouvelles Suites de Tests (Suites 69 à 79)** :
  - Validation de `IVoiceProvider`, `AudioResampler`, `OpenAIRealtimeClient`, négociation `session.update`, encodage/décodage Base64, streaming bidirectionnel, transcriptions Whisper, arrêt STOP, gestion des erreurs et reconnexion.
  - **1047 tests unitaires réussis avec 0 échec**.
  - Compilation PlatformIO validée à 100% sur `OpenAIConversation`, `MinimalConversation`, `MinimalTTS` et `MinimalSTT`.

---

## [0.5.3.1] - 2026-10-26 (Consolidation & Validation — Fiabilisation, Documentation & Métriques)

### Consolidation & Correction des Incohérences Documentaires
- **Alignement Global des Versions** :
  - Mise à jour de toutes les références de version vers `v0.5.3.1` (`library.json`, `library.properties`, `VoiceHubConfig.h`, `VoiceKit.h`, bannières console et exemples).
  - Correction de `VOICEKIT_VERSION` resté à `v0.5.2` dans `src/core/VoiceHubConfig.h`.
  - Correction des anciennes références Git obsolètes dans le `README.md` (`#v0.5.0` -> `#v0.5.3.1`, archive `v0.2.1.zip` -> `v0.5.3.1.zip`).
  - Alignement des versions documentées dans l'ensemble des exemples (`MinimalTTS`, `MinimalSTT`, `ContinuousSTT`, `MinimalConversation`, `VoiceSettingsDemo`, `MinimalVoiceKit`, `VoiceDiagnostics`, `MinimalTTS_Arduino`).
  - Documentation exhaustive des 6 onglets de `VoiceDiagnostics` (dont `LAT`), du cyclage interactif VAD et du format de télémétrie `[LATENCY_CSV]`.

### Fiabilisation & Audit des Métriques de Latence (`VoiceLatencyMetrics`)
- **Audit des 7 Jalons & 6 Durées Dérivées** :
  - Vérification de l'origine exacte et de la précision temporelle de chaque jalon : `speechEndDetectedMs`, `micStoppedMs`, `activityEndSentMs`, `firstResponseReceivedMs`, `firstAudioReceivedMs`, `firstAudioPlayedMs`, `playbackFinishedMs`.
  - Distinction formelle et rigoureuse entre `firstAudioReceivedMs` (réception réseau du 1er paquet PCM 24 kHz) et `firstAudioPlayedMs` (remise effective du 1er échantillon au DMA/AW88298).
  - Protection contre les anomalies d'ordre chronologique : toute inversion temporelle renvoie strictement `-1` sans provoquer d'underflow entier 32 bits non signé.
  - Préservation absolue de la politique anti-faux zéro : tout événement non observé renvoie `-1` (`N/D`) et n'est jamais remplacé artificiellement par `0`.
- **Protection Anti-Débordement `millis()`** :
  - Introduction d'un garde non-zéro (`if (ms == 0) ms = 1;`) lors de la capture des horodatages à la milliseconde (`turnStartedMs`, `activityEndSentMs`, `micStoppedMs`, `firstResponseReceivedMs`, `firstAudioReceivedMs`, `firstAudioPlayedMs`, `playbackFinishedMs`).
  - Empêche un horodatage valide capturé à l'instant précis `millis() == 0` (démarrage du microcontrôleur ou rebouclage après ~49.7 jours) d'être interprété à tort comme un événement non observé (`0 == unobserved`).

### Contrôle & Sécurisation du Seuil de Silence VAD
- **Protection contre les Valeurs Invalides** :
  - Sécurisation de `setVadSilenceDurationMs(uint32_t ms)` dans `VoiceKit`, `VoiceActivityDetector` et `ContinuousListeningController` par le filtre `if (ms > 0)`.
  - Rejet des valeurs nulles (`0 ms`) qui provoqueraient une coupure immédiate de parole dès le premier chunk de silence ; conservation transparente du seuil antérieur valide (1 200 ms par défaut).
- **Validation des 3 Paliers d'Usage** :
  - 800 ms : Commandes et interactions courtes ultra-réactives.
  - 1 000 ms : Équilibre optimal pour interactions vocales fluides.
  - 1 200 ms : Parole naturelle avec pauses (seuil par défaut).
- **Changement à Chaud Sécurisé** :
  - Vérification du comportement lors de modifications dynamiques à chaud et pendant une écoute active.

### Renforcement des Tests & Non-Régression
- **Ajout de 3 Nouvelles Suites de Tests Unitaires (64 nouvelles assertions)** :
  - **Suite 66** : Audit approfondi de `VoiceLatencyMetrics` (7 jalons, 6 durées dérivées, ordre chronologique, rejet des inversions, cas limites, distinction paquet vs DMA, garde non-zéro).
  - **Suite 67** : Validation des 3 seuils VAD, rejet strict de la valeur 0, persistance de la configuration et bascule à chaud pendant l'écoute.
  - **Suite 68** : Non-régression complète de la façade `VoiceKit` sur les 4 modes (TTS, STT, ContinuousSTT, Conversation), transitions inter-modes, rejet des requêtes concurrentes (`isBusy()`), unicité des callbacks et résilience aux déconnexions/reconnexions.
- **Bilan Global des Tests** : 976 validations réussies avec 0 échec (contre 912 en v0.5.3).
- **Compilations PlatformIO** : 100% de réussite sur les 8 exemples cibles CoreS3 (`MinimalTTS`, `MinimalSTT`, `ContinuousSTT`, `MinimalConversation`, `MinimalVoiceKit`, `VoiceSettingsDemo`, `VoiceDiagnostics`, `OrientationClockVoice`).

---

## [0.5.3] - 2026-10-26 (Voice Latency Optimization — Réactivité des interactions vocales & Instrumentation)

### Instrumentation Précise de la Latence Vocale (7 Jalons / 6 Durées Dérivées)
- **Structure Complète `VoiceLatencyMetrics`** :
  - `speechEndDetectedMs` : Horodatage à la milliseconde de détection/validation de fin de parole par le VAD local.
  - `micStoppedMs` : Arrêt matériel effectif de la capture microphone ES7210.
  - `activityEndSentMs` : Envoi de la notification `clientContent.turnComplete: true` sur la WebSocket Gemini Live.
  - `firstResponseReceivedMs` : Réception du premier événement `serverContent` de Gemini Live.
  - `firstAudioReceivedMs` : Réception du premier paquet PCM 24 kHz extrait du flux Bidi.
  - `firstAudioPlayedMs` : Remise du premier échantillon au pilote DMA I2S / AW88298 (début effectif de restitution).
  - `playbackFinishedMs` : Fin matérielle et acoustique de la restitution audio.
- **Calcul Strict des Durées Dérivées (Politique Anti-Faux Zéro)** :
  - Fin de parole $\rightarrow$ fin de capture : `getSpeechEndToMicStoppedMs()`.
  - Fin de capture $\rightarrow$ notification Gemini : `getMicStoppedToActivityEndMs()`.
  - Notification Gemini $\rightarrow$ premier audio reçu : `getActivityEndToFirstAudioMs()`.
  - Premier audio reçu $\rightarrow$ début restitution : `getFirstAudioToPlaybackMs()`.
  - Fin de parole $\rightarrow$ début restitution (latence perçue) : `getResponseLatencyMs()`.
  - Durée totale du tour vocal : `getTotalTurnDurationMs()`.
  - **Règle absolue** : Tout événement non observé ou non applicable renvoie explicitement `-1` (`N/D`), éliminant tout faux `0 ms`.
  - Intégration transparente dans `ConversationMetrics` et accesseur public direct `VoiceKit::getLatencyMetrics()`.

### Optimisations Locales & Suppression des Allocations Dynamiques
- **Encodage Base64 Direct sur Pile (`GeminiLiveClient::encodeBase64Direct`)** :
  - Encodage Base64 direct sans aucune allocation dynamique d'objet `String` sur le tas FreeRTOS toutes les 32 ms.
  - Suppression de la fragmentation mémoire du tas et élimination des saccades audio pendant le streaming microphone continu.
- **Décodage Base64 Optimisé (`GeminiLiveClient::decodeBase64`)** :
  - Décodage direct dans le vecteur cible sans allocations intermédiaires de chaînes temporaires.
- **Sérialisation Légère des Trames JSON** :
  - Réduction de l'empreinte mémoire des trames `realtimeInput` et `clientContent`.
- **Formats Audio Strictement Préservés** :
  - Capture microphone : PCM 16 kHz 16 bits mono.
  - Restitution haut-parleur : PCM 24 kHz 16 bits mono.

### Contrôleur VAD Configurable & Réduction du Délai de Fin de Parole
- **Seuil de Silence VAD Paramétrable** :
  - Configuration de `vadSilenceDurationMs` dans `VoiceKitConfig` (1 200 ms par défaut).
  - Nouvelles méthodes de contrôle dynamique : `voice.setVadSilenceDurationMs(uint32_t ms)` et `voice.getVadSilenceDurationMs()` (avec alias `setSilenceDurationMs`/`getSilenceDurationMs`).
  - Validation des paliers **800 ms**, **1 000 ms** et **1 200 ms** :
    - 1 200 ms : Référence par défaut pour pauses longues et dictée continue.
    - 1 000 ms : Équilibre optimal réduisant la latence perçue de 200 ms sans coupure.
    - 800 ms : Mode ultra-réactif pour questions directes courtes.

### Enrichissement de l'Outil de Diagnostic `VoiceDiagnostics`
- **6e Onglet Tactile Dédié `LATENCY` (`LAT`)** :
  - Interface compacte 320 × 240 avec 6 onglets de navigation (`SYS`, `AUDIO`, `LAT`, `NET`, `CONN`, `EVT`).
  - Affichage temps réel : Dernière latence perçue, délai Gemini, délai local DMA, durée de capture microphone.
  - Statistiques de session : Tours mesurés, tours incomplets, min, moyenne, max et médiane glissante.
  - Réglage interactif du seuil VAD (800 / 1 000 / 1 200 ms) par touche tactile et par commande série `VAD <ms>`.
- **Journalisation Série Structurée et Format CSV** :
  - Émission de synthèses horodatées `[LATENCY]` à chaque fin de tour vocal.
  - Export CSV direct `[LATENCY_CSV]` avec en-tête `[LATENCY_CSV_HEADER]` pour analyse comparative.

### Sanctuaire Audio & Garanties de Non-Régression
- Maintien intégral du buffer circulaire en PSRAM de 120 000 échantillons (~5 s) et du quadruple buffer DMA (160 ms).
- Préservation absolue des garanties de vidage de `LiveAudioPlayer` : aucun arrêt prématuré du haut-parleur, restitution vérifiée jusqu'au dernier mot (« alpha tango terminé »).
- 912 assertions unitaires automatisées validées (65 suites de tests, +79 assertions en v0.5.3).
- 100% des exemples compilés sous PlatformIO sans erreur.

---

## [0.5.2] - 2026-10-26 (Fast Connection — Optimisation des temps de démarrage Wi-Fi & Gemini Live)

### Optimisation WiFiManager & FastConnect
- **Connexion Wi-Fi Rapide Facultative (FastConnect)** :
  - Mémorisation persistante en NVS (`Preferences`) du dernier canal RF et du BSSID du point d'accès Wi-Fi.
  - Connexion directe via `WiFi.begin(ssid, pass, channel, bssid)` évitant le balayage complet des 13 canaux RF.
  - Réduction mesurée du temps d'obtention IP sur CoreS3 de 2384 ms à 946 ms (-60.3%).
  - Repli automatique et transparent vers le scan multi-canaux classique en cas d'échec ou d'expiration du délai FastConnect (3.5 s).
  - Sauvegarde automatique des nouvelles coordonnées réseau à chaque association réussie sans altération des identifiants enregistrés.
- **Réduction de la Première Reconnexion & Backoff Progressif** :
  - Première tentative de reconnexion accélérée à 1.5 s (au lieu de 2.0 s) dès détection de perte réseau.
  - Backoff progressif avec multiplicateur 2x plafonné à 30 s (1.5s, 3s, 6s, 12s, 24s, 30s max).
  - Verrou d'exclusion mutuelle évitant tout lancement de reconnexions concurrentes.

### Optimisation de la Synchronisation SNTP
- **Détection d'Horloge Système Déjà Valide** :
  - Évaluation de l'horloge système (`time(nullptr) >= 1700000000`, seuil 2024+) dès l'obtention de l'adresse IP.
  - Si l'horloge est déjà valide (redémarrage à chaud, maintien RTC ou réveil), l'attente bloquante SNTP est évitée (`sntpSyncMs = 0 ms`, gain direct de ~1000 ms), tout en maintenant la synchronisation en arrière-plan.
  - Si l'horloge n'est pas encore initialisée (démarrage à froid), maintien strict de la boucle d'attente SNTP avec timeout (5.0 s) pour garantir l'intégrité de la vérification de validité temporelle des certificats TLS.
- **Sécurité TLS Inviolée** :
  - Aucune désactivation de la validation des certificats TLS GoogleRoots.

### Optimisation Gemini Live & Session Controller
- **Conditionnement Strict de la Reconnexion au Réseau** :
  - Intégration d'un prédicat `isNetworkReady()` dans `LiveSessionController` vérifiant la connectivité IP avant toute tentative de connexion WebSocket.
  - Évite l'épuisement prématuré des tentatives de reconnexion (`_maxReconnectAttempts = 3`) lorsque le Wi-Fi est temporairement coupé.
  - Reconnexion immédiate à Gemini dès le rétablissement de la liaison réseau.
- **Suppression des Reconnexions Inutiles** :
  - Protection contre les appels redondants à `connect()` si la session est déjà connectée et prête (`setupComplete`).
  - Maintien absolu de la confirmation `setupComplete` avant d'émettre l'état `READY`.

### Instrumentation & Mesures des 5 Phases de Démarrage
- **Structure de Données `ConnectionMetrics`** :
  - `t_wifi_ms` (`wifiConnectMs`) : Démarrage Wi-Fi → adresse IP obtenue.
  - `t_sntp_ms` (`sntpSyncMs`) : Début SNTP → horloge système valide.
  - `t_ws_ms` (`wsConnectMs`) : Début connexion Gemini → WebSocket connecté.
  - `t_setup_ms` (`setupCompleteMs`) : WebSocket connecté → confirmation `setupComplete`.
  - `t_total_ready_ms` (`totalReadyMs`) : Démarrage total de la façade → `VoiceKit::isReady()`.
  - Compteurs d'essais et d'échecs (`wifiAttempts`, `geminiAttempts`, `connectionFailures`).
  - Indicateurs FastConnect (`fastConnectSuccess`, `sntpSkipped`, `channel`, `bssidStr`).
  - Nouvelle méthode publique : `const ConnectionMetrics& VoiceKit::getConnectionMetrics() const;`.

### Évolution de l'Exemple Officiel VoiceDiagnostics
- **5e Écran Tactile Dédié `CONNECTION` (`CONN`)** :
  - Affichage en temps réel des 5 durées mesurées avec indicateurs visuels `(FAST)` et `(IMMEDIAT)`.
  - Visualisation des tentatives, échecs et canal/BSSID actifs.
- **Journalisation Série Structurée et CSV** :
  - Ligne de synthèse horodatée `[CONN]` à chaque transition vers `READY`.
  - Format CSV directement exploitable `[CONN_CSV]` avec en-tête `[CONN_CSV_HEADER]`.

### Validation Matérielle & Non-Régression Totale
- **Mesures Authentiques sur CoreS3 (COM4)** :
  - Démarrage à froid (scan complet) : 5002 ms jusqu'à `READY`.
  - Démarrage rapide (FastConnect + RTC valide) : **3182 ms** jusqu'à `READY` (**gain net de 1820 ms / -36.4%**).
  - Liaison Wi-Fi seule : 2384 ms -> **946 ms** (**gain de 1438 ms / -60.3%**).
- **Compilation PlatformIO** : 8 exemples officiels compilés avec succès à 100%.
- **Banc de Tests Automatisés** : 60 suites de tests (Suites 56 à 60 ajoutées), 833 assertions réussies, 0 échec.

---

## [0.5.1] - 2026-10-25 (Intégration de VoiceDiagnostics aux exemples officiels)

### Nouvel Exemple Officiel `VoiceDiagnostics`
- **Application Autonome de Diagnostic Tactile (320 × 240)** :
  - Intégration du nouvel exemple officiel `examples/VoiceDiagnostics` pour M5Stack CoreS3.
  - Interface tactile fluide construite directement avec M5Unified, sans surcharge ni dépendance LVGL.
  - 4 onglets tactiles spécialisés :
    - `SYS` : État opérationnel VoiceKit, mode vocal actif, RAM libre et minimum observé, PSRAM libre (8 Mo), Uptime et caractéristiques du SoC ESP32-S3 (fréquence CPU, modèle, révision, taille Flash).
    - `AUDIO` : Niveau RMS du microphone acquis en temps réel via VoxCore, état de capture STT (`INACTIF`, `EN COURS`, `TRANSCRIPTION`), état de restitution TTS (`INACTIF`, `EN COURS`, `OCCUPE`), compteurs des débuts/fins de synthèse et d'écoute, transcriptions finales, et métriques du dernier tour conversationnel (`firstAudioLatencyMs`, `droppedSamples` si applicable).
    - `NET` : Connexion Wi-Fi (statut, IP, RSSI en dBm), état de session Gemini Live (`PRETE`, `CONNEXION...`, `NON PRETE`), compteurs de reconnexions et d'erreurs, et intitulé de la dernière anomalie.
    - `EVENTS` : Historique circulaire horodaté des 6 derniers événements vocaux (changements d'état, début/fin TTS, début/fin écoute, transcriptions reçues, erreurs), avec zéro allocation dynamique.
  - 2 boutons d'action permanents :
    - `TEST TTS` : Déclenche la prononciation vocale d'une phrase de test en français (« *Bonjour. Ceci est un test de diagnostic vocal VoiceKit sur Core S trois.* »).
    - `STOP AUDIO` : Interrompt immédiatement toute activité vocale en cours via `voice.stop()`.

### Sortie Série Structurée et Télémétrie
- **Format Série Standardisé `[DIAG]`** :
  - Émission périodique d'une ligne structurée et immédiatement exploitable par des scripts ou outils de monitoring :  
    `[DIAG] Voice=READY [DIAG] Mode=IDLE [DIAG] WiFi=CONNECTED [DIAG] RAM_FREE=152320 [DIAG] PSRAM_FREE=7845120 [DIAG] MIC_RMS=215 [DIAG] TTS_BUSY=0`
- **Sortie CSV Facultative (`ENABLE_CSV_OUTPUT`)** :
  - Activée via constante de compilation `#define ENABLE_CSV_OUTPUT 1` pour l'enregistrement direct des données dans des tableurs ou outils d'analyse temporelle.
- **Régulation Série en Streaming Audio** :
  - Espacement automatique des émissions série (3 secondes) lors de la restitution TTS ou de l'écoute micro pour supprimer toute interférence avec les transferts DMA et WebSocket.

### Intégrité du Cœur et Performances
- **Utilisation Exclusive de la Façade `VoiceKit`** :
  - Zéro second moteur vocal instancié, respect strict du pattern façade `VoiceKit voice;`.
- **Aucune Tâche FreeRTOS Supplémentaire** :
  - Le diagnostic s'exécute intégralement dans le fil applicatif principal (`setup()` / `loop()`).
- **Cadence d'Affichage Optimisée (5 Hz / 200 ms)** :
  - Rafraîchissement ciblé du panneau central sans scintillement (aucun effacement plein écran cyclique).
- **Protection des Identifiants** :
  - Clés API et mots de passe Wi-Fi strictement masqués (jamais affichés à l'écran ni transmis sur le port série).
- **Authenticité des Données** :
  - Aucune métrique simulée ; affichage explicite de `N/D` lorsque la donnée n'est pas disponible.

### Validation & Non-Régression
- **9 Exemples Officiels** : Compilation réussie sous PlatformIO (`VoiceDiagnostics`, `MinimalVoiceKit`, `ContinuousSTT`, etc.).
- **Suite de Tests MinGW** : 55 suites de tests automatisés validées à 100% (806 réussis, 0 échec).

---

## [0.5.0.1] - 2026-10-25 (Correctif de Stabilisation : FreeRTOS, Transitions de modes et Événements)

### Correction de la Synchronisation FreeRTOS
- **Remplacement de `volatile` par `std::atomic`** :
  - Élimination des accès concurrents non protégés sur `_state`, `_isActive`, `_isPaused`, `_speechEndRequested`, `_lastChunkRms` et `_mode`.
- **File SPSC (Single-Producer Single-Consumer) Lock-Free** :
  - Transfert léger et borné (128 blocs = 4,096 secondes d'audio) des valeurs RMS depuis le callback audio FreeRTOS Core 1 vers la méthode `update()` dans la boucle principale.
  - Zéro allocation dynamique, zéro mutex bloquant dans le chemin critique audio, zéro manipulation de `String` depuis la tâche FreeRTOS.
- **Unification du Contexte VAD & Callbacks** :
  - Traitement complet de l'algorithme VAD (`_vad.processRms`) exécuté exclusivement dans `update()`.
  - Tous les callbacks utilisateurs (`onSpeechStarted`, `onSpeechEnded`, `onSegmentComplete`, `onStateChanged`, etc.) s'exécutent de façon déterministe dans le fil d'exécution principal (`loop()`).
  - Seul le callback brut `onAudioChunk` demeure dans la tâche FreeRTOS audio et reste strictement non-bloquant.

### Sécurisation des Transitions de Modes
- **Synchronisation Rigoureuse de l'État dans la Façade `VoiceKit`** :
  - Correction de l'anomalie où `stopConversation()` basculait prématurément vers `VoiceKitMode::IDLE` même en cas d'échec de désactivation.
  - Distinction formelle entre une demande acceptée (`bool ok`) et une transition confirmée après réception de `setupComplete` depuis Gemini Live.
  - Refus strict des opérations incompatibles (`say`, `listen`, `startContinuousListening`, `startConversation`) lorsqu'une connexion ou transition est en cours (`isConnecting()`, `isModeSwitchPending()`).
  - Restauration automatique du dernier mode valide en cas d'échec de la reconnexion de mode.
  - Prévention totale des doubles demandes de reconnexion.

### Fiabilisation de `ContinuousListeningController` & Unicité des Événements
- **Identifiant Séquentiel Local de Segment (`_currentSegmentId`)** :
  - Chaque segment de parole possède un identifiant incrémental unique.
  - Unicité absolue de finalisation par segment : au maximum une seule émission de `onSegmentComplete` par segment.
  - Rejet systématique des notifications finales répétées ou dupliquées par le réseau/serveur.
- **Protection Anti-Hallucination & Rejet des Callbacks Tardifs** :
  - Interdiction stricte de convertir une transcription partielle (interim) en transcription finale.
  - Aucun texte final artificiel après expiration du timeout STT (texte vide et `confirmed = false`).
  - Rejet catégorique des callbacks tardifs d'un segment terminé arrivant après timeout ou pendant le segment suivant.
  - Reprise automatique fixée à 400 ms par défaut et réinitialisation complète de l'état VAD et RMS avant chaque segment.

### Validation & Non-Régression
- **Suite Complète de Tests Automatisés (806 assertions, 0 échec)** :
  - Conservation des 692 assertions de la version v0.5.0.
  - Ajout de 114 nouvelles assertions déterministes (Suites 53, 54 et 55) couvrant les blocs audio après arrêt, les pauses/reprises répétées, les événements dupliqués, les callbacks après timeout, les échecs de reconnexion, les refus de transitions occupées, le démarrage continu hors session et l'enchaînement rapide des 4 modes.
- **Validation PlatformIO Complète** :
  - Compilation réussie sans warning de l'ensemble des 7 exemples PlatformIO (`MinimalVoiceKit`, `ContinuousSTT`, `MinimalConversation`, `MinimalSTT`, `MinimalTTS`, `VoiceSettingsDemo`, `OrientationClockVoice`).

---

## [0.5.0] - 2026-10-25 (Simplification de l'API et Stabilisation)

### Nouvelle Façade Simplifiée `VoiceKit` (`VoiceKit.h`)
- **API Haut Niveau Intuitive et Unifiée** :
  - Introduction d'une classe façade optionnelle `VoiceKit` exposant une API claire et expressive pour piloter l'ensemble des modes vocaux à partir d'un seul objet :
    - Cycle de vie : `begin(config)`, `update()`, `stop()`, `isReady()`, `isBusy()`, `getState()`, `getMode()`, `getLastError()`.
    - Synthèse vocale (TTS) : `say(text)`, `isSpeaking()`.
    - Reconnaissance manuelle (STT) : `listen()`, `stopListening()`, `cancelListening()`, `isListening()`, `isTranscribing()`.
    - Reconnaissance continue (ContinuousSTT) : `startContinuousListening()`, `stopContinuousListening()`, `pauseContinuousListening()`, `resumeContinuousListening()`, `isContinuousListening()`, `isContinuousListeningPaused()`.
    - Conversation bidirectionnelle : `startConversation()`, `stopConversation()`, `isConversationActive()`.
    - Profil vocal & volume : `setVoiceProfile(...)`, `setVoice(...)`, `setVolume(...)`, `getVolume()`.
  - **Délégation Transparente sans Duplication** :
    - La façade délègue fidèlement toutes les opérations réseau, audio DMA et protocolaires au moteur existant `VoxCoreVoice` et au nouveau contrôleur `ContinuousListeningController`.
    - Zéro tâche FreeRTOS supplémentaire, zéro second buffer audio, zéro nouvelle allocation mémoire invasive.
  - **Contrôles Déterministes & Résultats Explicites** :
    - Toutes les méthodes acceptent ou refusent les requêtes avec un retour booléen explicite (`bool`) selon l'état du moteur et la politique d'exclusion mutuelle des modes.
    - Élimination des conflits : refus strict de lancer du TTS ou une conversation pendant une écoute continue, et réciproquement.

### Nouveau Contrôleur Réutilisable `ContinuousListeningController`
- **Composant Autonome Intégré à la Bibliothèque (`src/core/ContinuousListeningController.h`, `.cpp`)** :
  - Extrait et encapsule toute la logique de dictée continue mains libres précédemment démontrée dans l'exemple `ContinuousSTT`.
  - Intègre le détecteur d'activité vocale léger `VoiceActivityDetector` pour le calcul d'énergie RMS, l'estimation adaptative du plancher de bruit, l'hystérésis (seuil parole vs seuil maintien).
  - Paramètres déterministes par défaut :
    - Détection début de parole : 200 ms consécutifs au-dessus du seuil.
    - Détection fin de phrase : 1 200 ms de silence consécutif post-parole.
    - Durée maximale de sécurité par segment : 30 secondes.
    - Délai de relance automatique inter-segments : 400 ms.
  - **Thread-Safety & Intégrité Audio** :
    - Traitement des blocs PCM 16 kHz thread-safe (via `std::atomic<uint32_t>` pour le RMS et drapeaux d'état atomiques).
    - Zéro double capture micro : réutilise directement le flux DMA de `VoxCoreVoice` / `LiveAudioStreamer`.
    - Règle stricte anti-hallucination : en cas de timeout sans transcription finale authentique de Gemini Live, aucun texte factice n'est généré ni injecté.
    - Prise en charge des pauses et reprises manuelles sans altérer la session WebSocket TLS sous-jacente.

### Gestion des Événements & Unicité des Callbacks
- **Architecture de Callbacks Simplifiée et Cohérente** :
  - `voice.onReady(cb)` : Émis lorsque la session est prête pour une opération.
  - `voice.onError(cb)` : Émis en cas d'anomalie réseau, audio ou protocolaire.
  - `voice.onSpeechStarted(cb)` : Émis au premier échantillon audio restitué.
  - `voice.onSpeechFinished(cb)` : Émis à l'extinction complète de la restitution sonore (garanti exactement une fois par énonciation).
  - `voice.onTranscription(cb)` : Callback unifié avec distinction stricte du statut (`const String& text, bool isFinal`).
  - `voice.onConversationFinished(cb)` : Émis à la clôture exacte de chaque tour de conversation.
- **Contexte d'Exécution Sécurisé** :
  - Tous les callbacks de haut niveau sont exécutés dans le fil d'exécution de la boucle principale (`loop()`) lors des appels à `update()`.
  - Seul le callback brut d'acquisition `onAudioChunk` s'exécute dans la tâche audio FreeRTOS Core 1.
  - Garantie formelle d'unicité : aucune duplication d'événement de fin lors des boucles d'update suivantes.

### Nouvel Exemple PlatformIO `MinimalVoiceKit`
- **Application Démonstrateur Complète (`examples/MinimalVoiceKit/`)** :
  - Utilise exclusivement l'API simplifiée `VoiceKit` avec un unique objet `VoiceKit voice;`.
  - Interface tactile interactive fluide sur écran CoreS3 (320 × 240) sans aucune dépendance à LVGL (basée sur `M5Unified`).
  - 4 onglets tactiles pour basculer et tester successivement :
    1. **Synthèse vocale (TTS)** : Phrase de test en français avec accents.
    2. **Reconnaissance manuelle (STT)** : Push-to-Talk via boutons tactiles Écouter / Arrêter.
    3. **Dictée continue (ContinuousSTT)** : Écoute mains libres avec détection automatique de fin de parole et bouton Pause / Reprendre.
    4. **Conversation (Gemini Live)** : Échange bidirectionnel vocal interactif.
  - Pilotage également possible via commandes textuelles sur le port Série (115200 bauds).

### Préservation Totale de l'Existant (`VoxCoreVoice`)
- **Compatibilité Ascendante à 100%** :
  - La classe `VoxCoreVoice` et toutes ses méthodes publiques sont intégralement préservées à l'identique.
  - Les 7 exemples historiques compilent et s'exécutent sans la moindre modification de comportement :
    - `examples/MinimalTTS`
    - `examples/MinimalSTT`
    - `examples/ContinuousSTT`
    - `examples/MinimalConversation`
    - `examples/OrientationClockVoice`
    - `examples/VoiceSettingsDemo`
    - `examples/MinimalTTS_Arduino/MinimalTTS_Arduino.ino`

### Tests & Validations
- **Suites de Tests Automatisés (MinGW C++14/C++17)** :
  - 52 suites de tests exécutées avec succès (692 assertions validées, 0 échec).
  - Suites 47 à 52 dédiées aux spécifications v0.5.0 :
    - Suite 47 : Façade VoiceKit — Initialisation, cycle de vie, modes et délégation.
    - Suite 48 : Façade VoiceKit — Validation des opérations TTS, STT manuel, ContinuousSTT et Conversation.
    - Suite 49 : Façade VoiceKit — Refus stricts des opérations incompatibles et transitions de modes.
    - Suite 50 : ContinuousListeningController — VAD intégré, silence, timeout sans hallucination, pause et reprise.
    - Suite 51 : Intégrité des événements — Unicité des callbacks de fin et absence de duplication.
    - Suite 52 : Compatibilité ascendante intégrale — Coexistence VoxCoreVoice et VoiceKit sans régression.
- **Compilations PlatformIO Validées (8 applications)** :
  - `examples/MinimalTTS` : SUCCESS (14.78s)
  - `examples/MinimalSTT` : SUCCESS (15.15s)
  - `examples/ContinuousSTT` : SUCCESS (18.92s)
  - `examples/MinimalConversation` : SUCCESS (20.68s)
  - `examples/OrientationClockVoice` : SUCCESS (40.72s)
  - `examples/VoiceSettingsDemo` : SUCCESS (19.57s)
  - `examples/MinimalVoiceKit` : SUCCESS (81.31s)
  - `examples/MinimalTTS_Arduino` : Syntaxe et conformité Arduino IDE 1.5 validées

---

## [0.4.1.2] - 2026-10-24 (ContinuousSTT & VAD Détection Fin de Parole)

### Nouvel Exemple PlatformIO `ContinuousSTT`
- **Reconnaissance Vocale Continue Mains Libres** :
  - Nouveau sixième exemple autonome dans `examples/ContinuousSTT/` avec son propre `platformio.ini`, `no_ota.csv`, `secrets.example.h` et `README.md`.
  - Fonctionnement comparable à une dictée vocale moderne : écoute automatique dès que le moteur est prêt, détection de début et de fin de parole sans maintien de bouton, et réactivation automatique pour le segment suivant après finalisation.
  - Découpage naturel des interventions en segments transcrits via une session Gemini Live unique maintenue en continu.

### Détecteur d'Activité Vocale Léger (`VoiceActivityDetector`)
- **Composant Réutilisable et Non Bloquant (`src/core/VoiceActivityDetector.h`)** :
  - Analyse synchrone sur blocs PCM 16 kHz mono 16 bits (chunks de 512 échantillons = 32 ms).
  - Plancher de bruit ambiant adaptatif (`noiseFloor`) ajusté en continu en phase de silence via lissage exponentiel (EMA).
  - Hystérésis déterministe entre seuil d'attaque (parole : `noiseFloor * 2.0 + 250 RMS`) et seuil de relâchement (maintien : `noiseFloor * 1.3 + 120 RMS`).
  - Validation de durée minimale de parole : 200 ms consécutifs requis au-dessus du seuil haut pour éliminer les clics et bruits transitoires.
  - Détection automatique de fin d'intervention après 1 200 ms de silence consécutif post-parole avec déclenchement propre de `voice.stopListening()`.
  - Bornage de sécurité : durée maximale de segment limitée à 30 secondes.
  - Tolérance aux pauses inter-mots : un silence inférieur à 1 200 ms est réinitialisé dès que la voix reprend, sans interruption de phrase.

### Extensions de l'API Publique
- **`VoxCoreVoice::getAudioLevel()`** :
  - Retourne le niveau RMS instantané du dernier bloc micro PCM 16 kHz capturé.
- **`VoxCoreVoice::onAudioChunk(cb)`** :
  - Enregistre un callback léger invoqué sur chaque bloc audio acquis (pour VAD, visualiseur graphique ou télémétrie).
- **`LiveAudioStreamer::getLastChunkRms()`** & **`LiveAudioStreamer::onAudioChunk(cb)`** :
  - Accès au niveau audio sans modifier le chemin critique de streaming, les tampons DMA ni la tâche FreeRTOS.

### Interface Graphique M5Unified (CoreS3 320 × 240)
- **Visualisation Complète et Réactive sans LVGL** :
  - 7 états applicatifs distincts avec code couleur dédié : `ATTENTE`, `ECOUTE`, `PAROLE`, `SILENCE`, `TRANSCRIPTION`, `TERMINE`, `ERREUR`.
  - Barregraphe VU-mètre dynamique avec curseur de seuil de parole.
  - Jauge de silence dynamique visualisant le compte à rebours 0 à 1 200 ms avant clôture.
  - Compteur de segments transcrits et affichage du bruit ambiant.
  - Zone de texte scindée : texte définitif authentique mis en évidence et conservé à l'écran vs texte en direct (intérim).
  - Bouton tactile dynamique `PAUSE` / `REPRENDRE`.
  - Prise en charge intégrale de l'UTF-8 français via la police compatible `fonts::efontJA_12` (accents `é`, `è`, `à`, `ç`, `ê`, `ô`, `ù`, `û`).
  - Détection explicite des timeouts de transcription avec affichage `[Transcription non confirmée (délai dépassé)]` sans invention de texte final.

### Tests & Validations
- **Suites de Tests Automatisés (MinGW C++14/C++17)** :
  - Ajout des suites 43, 44, 45 et 46 dans `tests/test_voicekit_all.cpp` (567 assertions réussies, 0 échec).
  - Validation du VAD, des seuils adaptatifs, de l'hystérésis, des pauses inter-mots, de la durée maximale 30s, de l'enchaînement multi-segments, des timeouts sans invention et de la fidélité UTF-8.
- **Compilations PlatformIO Validées (6 applications)** :
  - `examples/MinimalTTS` : SUCCESS
  - `examples/MinimalSTT` : SUCCESS
  - `examples/MinimalConversation` : SUCCESS
  - `examples/VoiceSettingsDemo` : SUCCESS
  - `examples/OrientationClockVoice` : SUCCESS
  - `examples/ContinuousSTT` : SUCCESS (Flash: 71.4%, RAM: 20.8%)

---

## [0.4.1.1] - 2026-10-24 (Corrective de Stabilisation)

### Sécurisation des Profils Vocaux & Reconnexions Gemini Live
- **Validation Différée après `setupComplete`** :
  - Les méthodes de modification de profil (`setVoiceProfile`, `setVoice`, `setLanguage`, `setStyle`, `setTone`, `setAccent`) conservent intact le profil précédent dans `_config` jusqu'à la confirmation de prise en compte par le serveur Google via l'événement `setupComplete`.
  - Les accesseurs (`getVoice()`, `getLanguage()`, `getStyle()`, `getTone()`, `getAccent()`) reflètent le profil réellement actif et validé à tout instant.
  - Le callback `onReady()` est garanti de correspondre au profil définitivement négocié et prêt.
- **Restauration Cohérente en Cas d'Échec** :
  - En cas d'erreur de négociation TLS/WebSocket ou de timeout lors d'une reconnexion de profil, le profil précédent est automatiquement restauré dans `GeminiLiveClient` et maintenu dans `_config`.
- **Refus Strict et Déterminisme d'Accès** :
  - Refus immédiat (`false`) si le moteur est en cours d'activité vocale (`isBusy()` : émission audio, acquisition micro, attente transcription ou tour conversationnel actif).
  - Refus immédiat (`false`) si une connexion ou reconnexion est déjà en cours (`isConnecting()`), interdisant formellement toute double connexion WebSocket ou enchaînement incontrôlé de reconnexions successives.
- **Nouvelles Méthodes Utilitaires Publiques** :
  - `bool isConnecting() const` : Indique si une connexion ou reconnexion WebSocket vers Gemini Live est en cours.
  - `bool isProfileChangePending() const` : Indique si un changement de profil vocal est en cours de négociation.
  - `bool isModeSwitchPending() const` : Indique si un basculement TTS <-> Conversation est en cours de négociation.

### Fiabilisation de la Transition TTS / Conversation
- **Reconnexion Maîtrisée au Repos** :
  - `enableConversation(bool)` applique le nouveau mode (Mode B conversationnel vs mode unitaire TTS/STT) via une reconnexion propre et maîtrisée lorsque la session est connectée au repos.
  - L'instruction système spécifique (`conversationInstruction` vs `systemInstruction`) est injectée de manière garantie dans la nouvelle trame de setup WebSocket.
  - Préservation absolue des directives vocales (`buildVoiceDirectives()`) : la langue, le style, la tonalité et l'accent sont strictement conservés et transmis dans le nouveau mode.
  - Préservation intégrale des callbacks existants (`onReady`, `onTranscription`, `onUserText`, `onAssistantText`, etc.).
- **Verrouillage Pendant Tour Actif** :
  - Refus strict (`false`) de tout basculement de mode pendant qu'un tour est en cours (écoute micro, attente réponse, élocution).
  - En cas d'échec de la reconnexion de transition, restauration automatique du mode précédent.

### Clarification de la Compatibilité Linguistique
- **Documentation et Déterminisme dans `GeminiLiveClient`** :
  - Clarification explicite que le modèle Gemini Live actif oriente la langue d'élocution principalement au moyen des consignes textuelles système.
  - `isModelSpeechLanguageCodeSupported()` retourne systématiquement `false` afin de ne pas annoncer une détection automatique non implémentée et de garantir qu'aucun champ `speechConfig.languageCode` non validé pour le modèle actif ne soit transmis au serveur.
  - Conservation du schéma protocolaire éprouvé et compatible.

### Tests & Validation
- **Suites de Tests Automatisés Déterministes (MinGW C++14/C++17)** :
  - Ajout des suites 40, 41 et 42 dans `tests/test_voicekit_all.cpp` (485 tests réussis, 0 échec).
  - Validation exhaustive : changement de voix à froid/au repos, refus pendant parole, refus pendant transcription, refus pendant connexion active, échec simulé et rollback, passage TTS $\rightarrow$ Conversation, passage Conversation $\rightarrow$ TTS, refus pendant tour actif, préservation des directives vocales, et validation intégrale de la phrase française accentuée.
- **Compilations PlatformIO des 5 Applications Exemples** :
  - `examples/MinimalTTS` : SUCCESS
  - `examples/MinimalSTT` : SUCCESS
  - `examples/MinimalConversation` : SUCCESS
  - `examples/VoiceSettingsDemo` : SUCCESS
  - `examples/OrientationClockVoice` : SUCCESS

---

## [0.4.1] - 2026-10-24 (Configuration Vocale & Support Français UTF-8)

### Configuration Vocale Étendue (`VoiceKitConfig`)
- **Paramètres Vocaux Complets** :
  - `voice` : Voix pré-construite Gemini Live conservée (`Aoede`, `Charon`, `Fenrir`, `Kore`, `Puck`, défaut : `Aoede`).
  - `language` : Code de langue BCP-47 conforme aux standards internationaux (défaut : `fr-FR`).
  - `style` : Style d'élocution (`VoiceStyle` : `NATURAL`, `PROFESSIONAL`, `EXPRESSIVE`, `NARRATIVE` ; acceptation indifférenciée des énumérateurs ou des chaînes littérales).
  - `tone` : Tonalité vocale (`VoiceTone` : `NEUTRAL`, `CALM`, `WARM`, `CHEERFUL`, `SERIOUS`).
  - `accent` : Orientation de prononciation et accentuation régionale optionnelle (ex: `"parisien"`, `"méridional"`, `"québécois"`, `"british"`).
- **Rétrocompatibilité Totale** :
  - Préservation intégrale de tous les champs et valeurs par défaut antérieurs.
  - Wrappers transparents `VoiceStyleWrapper` et `VoiceToneWrapper` autorisant indifféremment l'assignation par énumérateur (`VoiceStyle::EXPRESSIVE`) ou par chaîne de caractères (`"expressive"`).

### Intégration Gemini Live & Protocole Setup
- **Préservation Strict du Schéma Protocolaire** :
  - Configuration `voiceConfig.prebuiltVoiceConfig.voiceName` préservée mot à mot.
  - Vérification de compatibilité de modèle (`isModelSpeechLanguageCodeSupported`) : exclusion de `speechConfig.languageCode` pour les modèles natifs Bidi (`gemini-3.8-live`, `gemini-2.0-flash`), évitant tout rejet de trame par le serveur Google.
  - Pour les modèles natifs, l'orientation de langue est transmise de manière déterministe via les consignes système.
- **Conversion Déterministe en Directives Vocales** :
  - Méthodes `buildVoiceDirectives()` et `buildEffectiveInstruction()` dans `VoiceKitConfig`.
  - Préservation stricte des instructions métier (`VOICEKIT_DEFAULT_TTS_INSTRUCTION`, `VOICEKIT_DEFAULT_STT_INSTRUCTION`, `VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION`).
  - Ajout des consignes d'élocution et de tonalité sous forme de directives structurées transmises lors du setup WebSocket.
- **Sécurité et Dynamique des Profils** :
  - Nouvelles méthodes dans `VoxCoreVoice` : `setVoiceProfile()`, `setVoice()`, `setLanguage()`, `setStyle()`, `setTone()`, `setAccent()`.
  - Protection absolue : toute modification demandée pendant une élocution, une transcription ou un tour conversationnel actif (`isBusy()`) est strictement refusée sans interrompre l'activité en cours.
  - Au repos, la modification entraîne une reconnexion propre et transparente négociant la nouvelle trame de setup avec Gemini Live.

### Chaîne UTF-8 Intégrale de Bout en Bout
- **Nouveau Module Centralisé `Utf8Utils` (`src/core/Utf8Utils.h`)** :
  - Validation stricte de conformité UTF-8 selon RFC 3629 (`isValidUtf8`).
  - Assainissement sans corruption : les textes valides sont préservés à 100% sans la moindre altération ; les octets mal formés sont remplacés de manière contrôlée par le caractère de substitution officiel Unicode `U+FFFD`.
  - Échappement JSON centralisé et conforme RFC 8259 (`escapeJson`) pour tous les flux textuels sortants (`setup`, `clientContent`, `toolResponse`, etc.).
  - Comptage des points de code Unicode réels (`unicodeLength`).
- **Support des Caractères Français, Ligatures et Ponctuations Typographiques** :
  - Tests unitaires et validation pour tous les caractères prescrits : `é`, `è`, `ê`, `ë`, `à`, `â`, `ù`, `û`, `î`, `ï`, `ô`, `ö`, `ç`, `œ`, `æ`, `É`, `À`, `Ç`, `’`, `«`, `»`.
  - Validation de bout en bout de la phrase obligatoire :
    *« À Noël, Élise prépare un café crème. L'été à Genève est agréable. François préfère un cœur léger. »*
- **Découplage Architecturel Display vs Transport** :
  - Retrait de la translittération ASCII intempestive dans `LiveSessionController` pour préserver le texte UTF-8 original.
  - Délimitation explicite du rôle de `DisplayTextNormalizer`, réservé uniquement aux écrans LCD/TFT à polices restreintes en 7-bits ASCII.

### Nouvelle Application Exemple (`examples/VoiceSettingsDemo/`)
- **Projet PlatformIO Autonome pour M5Stack CoreS3** :
  - Test successif de 5 profils vocaux combinant différentes voix (`Aoede`, `Charon`, `Fenrir`, `Kore`, `Puck`), langues (`fr-FR`, `de-DE`, `en-US`), styles et tons.
  - Affichage sur écran LCD CoreS3 : statut réseau, profil courant, phrase prononcée et bouton tactile « PROFIL SUIVANT ».
  - Pilotage parallèle possible via le moniteur série (touches `N`, `R`, `S`).

### Validation et Non-Régression
- **Compilation PlatformIO Validée pour les 5 Exemples** :
  - `examples/MinimalTTS` : SUCCESS
  - `examples/MinimalSTT` : SUCCESS
  - `examples/OrientationClockVoice` : SUCCESS
  - `examples/MinimalConversation` : SUCCESS
  - `examples/VoiceSettingsDemo` : SUCCESS
- **Suite de Tests Automatisés Natifs** :
  - 39 suites de tests automatisés (396 tests réussis, 0 échec).

---

## [0.4.0] - 2026-10-24 (Conversation Vocale Bidirectionnelle — Mode B Native Gemini Live)

### Conversation Vocale Bidirectionnelle (Mode B Native Gemini Live)
- **Architecture Native Gemini Live (Mode B)** :
  - Flux direct continu : Microphone CoreS3 (PCM 16 kHz) $\rightarrow$ Gemini Live $\rightarrow$ Réponse audio native (PCM 24 kHz) $\rightarrow$ Haut-parleur CoreS3.
  - Élimine tout aller-retour textuel intermédiaire superflu (pas de second appel Gemini).
  - Préservation des flux de transcription temps réel : l'application reçoit à la fois le texte transcrit de l'utilisateur (`inputTranscription`) et le texte de réponse du modèle (`modelTurn` / `parts`).
- **Nouveau Composant Léger `ConversationController`** :
  - `src/core/ConversationController.h`, `src/core/ConversationController.cpp` (avec en-têtes relais `include/ConversationController.h` et `include/core/ConversationController.h`).
  - Gestion unifiée et synchronisée du cycle de vie des tours : `READY` $\rightarrow$ `LISTENING` $\rightarrow$ `PROCESSING` (WAITING_RESPONSE) $\rightarrow$ `SPEAKING` $\rightarrow$ `READY`.
  - Intégration transparente avec `LiveSessionController`, `LiveAudioStreamer` et `LiveAudioPlayer` sans introduire de second WebSocket ni de tâche FreeRTOS superflue.
- **Protection Anti-Larsen & Préservation Dernière Syllabe** :
  - Désactivation automatique du haut-parleur pendant la capture microphone (`setPlaybackAllowed(false)`).
  - Réarmement automatique et immédiat dès le relâchement du bouton (« PARLER ») pour restituer la réponse audio native.
  - Préservation intégrale de la dernière syllabe : drainage complet des tampons DMA et PSRAM avant de déclarer la fin du tour.
- **Métriques Diagnostiques de Tour (`ConversationMetrics`)** :
  - Collecte précise de la durée de capture micro (`micCaptureDurationMs`), nombre de blocs audio émis (`audioBlocksSent`), latence jusqu'au premier son (`firstAudioLatencyMs`), nombre de blocs PCM reçus (`pcmBlocksReceived`), échantillons perdus (`droppedSamples`), durée totale du tour (`totalTurnDurationMs`), texte utilisateur et texte assistant.

### Extension de l'API Publique `VoxCoreVoice`
- **Méthodes Conversationnelles Dédiées** :
  - `void enableConversation(bool enable)` : Active ou désactive le mode conversationnel bidirectionnel (bascule automatiquement le prompt système vers `VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION`).
  - `bool isConversationEnabled() const` : Indique si le mode conversationnel est actif.
  - `void onUserText(TranscriptionCallback cb)` : Callback pour la transcription utilisateur (partielle et finale).
  - `void onAssistantText(AssistantTextCallback cb)` : Callback pour le texte de réponse de l'assistant Gemini.
  - `void onConversationFinished(VoidCallback cb)` : Callback déclenché à la fin complète d'un tour (après vidage audio).
  - `void onConversationTurnMetrics(TurnMetricsCallback cb)` : Callback notifiant les métriques diagnostiques du tour.
  - `const ConversationMetrics& getConversationMetrics() const` : Accès direct aux métriques du dernier tour.
- **Nouveaux États Opérationnels (`VoiceState`)** :
  - `VoiceState::PROCESSING` : Traitement de la demande par Gemini après relâchement du micro.
  - `VoiceState::STOPPED` : Tour interrompu par appui sur STOP.
  - `VoiceState::DISCONNECTED` : Session déconnectée.

### Nouvelle Application Exemple (`examples/MinimalConversation/`)
- **Démonstrateur Conversationnel Push-to-Talk pour M5Stack CoreS3** :
  - Interface visuelle claire sur écran LCD 320x240 M5Unified (sans dépendance LVGL requise).
  - En-tête : Badge d'état réseau et moteur vocal (`PRÊT`, `ÉCOUTE`, `RÉFLEXION`, `RÉPONSE`, `ERREUR`).
  - Centre : Carte transcription utilisateur (« Vous : ... ») et carte réponse Gemini (« Gemini : ... »).
  - Bas : Grand bouton tactile dynamique « PARLER » (vert) / « STOP » (orange/rouge).
  - Contrôle parallèle via port série (commandes `P` pour parler, `R` pour relâcher, `S` pour stop).
  - Projet autonome PlatformIO avec modèle de partition `no_ota.csv` et gestion des identifiants `secrets.example.h`.

### Non-Régression Intégrale v0.3.0 / v0.2.1
- Les 3 applications validées conservent un fonctionnement rigoureusement inchangé :
  - `examples/MinimalTTS` : SUCCESS (Flash 55.7%, RAM 20.7%).
  - `examples/MinimalSTT` : SUCCESS (Flash 56.2%, RAM 20.7%).
  - `examples/OrientationClockVoice` : SUCCESS (Flash 76.0%, RAM 48.7%).
  - `examples/MinimalConversation` : SUCCESS (Flash 56.2%, RAM 20.7%).
- **Validation Automatisée Native** :
  - 35 suites de tests automatisés (279 assertions passées avec succès, 0 échec).

---

## [0.3.0] - 2026-10-24 (Ajout Reconnaissance Vocale STT & Push-to-Talk)

### Reconnaissance Vocale STT (Speech-to-Text)
- **Intégration des composants éprouvés de VoxCore et VoxTranscribe** :
  - `LiveAudioStreamer` (`src/core/LiveAudioStreamer.h`, `src/core/LiveAudioStreamer.cpp`) : Gestionnaire d'acquisition et de streaming PCM 16 kHz vers Gemini Live.
  - `IAudioInput` (`src/hal/IAudioInput.h`) : Abstraction matérielle de l'entrée microphone.
  - `M5AudioInput` (`src/hal/M5AudioInput.h`, `src/hal/M5AudioInput.cpp`) : Pilote matériel pour le double microphone ES7210 du M5Stack CoreS3 via `M5.Mic`.
  - `HardwareManager` (`src/hal/HardwareManager.h`) : Arbitrage matériel centralisé pour le partage propre du bus I2S entre haut-parleur et microphone.
- **Acquisition & Streaming Découplé** :
  - Échantillonnage PCM 16 kHz, 16 bits mono avec double-buffering ping-pong DMA 512 échantillons (32 ms).
  - Pipeline FreeRTOS découplé (`live_mic_task` pour la capture, `live_tx_task` pour l'envoi réseau sans bloquer la boucle Arduino).
  - Protocole Gemini Live BidiGenerateContent complet avec signalisation officielle `activityStart` au début de l'écoute et `activityEnd` au relâchement.
- **Règles Strictes de Transcription & Résilience** :
  - Transcriptions partielles (`interimInputTranscription`) et finales (`inputTranscription`) transmises avec le booléen `isFinal`.
  - **Zéro hallucination** : Règle formelle interdisant d'inventer une transcription finale à partir de fragments partiels lors d'un silence ou d'une absence de retour serveur.
  - Surveillance automatique d'un timeout de silence paramétrable (`sttFinalTimeoutMs`, défaut 4000 ms) avec retour propre à l'état `READY`.
  - **Arbitrage I2S Anti-Larsen** : Coupure automatique du haut-parleur pendant l'écoute (`muteSpeakerDuringSTT = true`) et réarmement automatique en fin de dictée.

### Extension de l'API Publique `VoxCoreVoice`
- **Méthodes STT Non-Bloquantes** :
  - `bool startListening()` : Démarre la capture microphone et le streaming vers Gemini Live. Interrompt prioritairement toute synthèse TTS en cours.
  - `bool stopListening()` : Arrête l'acquisition, envoie `activityEnd` et attend la transcription finale.
  - `void cancelListening()` : Annulation immédiate de la dictée sans attendre de résultat.
  - `bool isListening() const` : Indique si le microphone est activement en cours de capture.
  - `bool isTranscribing() const` : Indique que la voix a été capturée et que la transcription finale est en attente.
  - `void onTranscription(TranscriptionCallback cb)` : Callback de réception des transcriptions `(const String& text, bool isFinal)`.
  - `void onListeningStarted(VoidCallback cb)` et `void onListeningFinished(VoidCallback cb)`.
- **Nouveaux États Opérationnels (`VoiceState`)** :
  - `VoiceState::LISTENING` : Écoute micro active (Push-to-Talk enfoncé).
  - `VoiceState::TRANSCRIBING` : Relâchement effectué, attente de transcription finale.

### Nouvelle Application Exemple (`examples/MinimalSTT/`)
- **Démonstrateur Push-to-Talk Autonome** :
  - Bouton tactile grand format « DICTER » sur l'écran LCD du CoreS3.
  - Acquisition pendant l'appui continu, arrêt et transmission au relâchement.
  - Machine d'état visuelle claire : `PRÊT` (vert), `ÉCOUTE` (rouge clignotant), `TRANSCRIPTION` (orange), `TERMINÉ` (vert foncé), `ERREUR` (rouge).
  - Affichage dynamique des transcriptions partielles et finales à l'écran et sur le port série (115200 bauds).
  - Configuration PlatformIO installable sans copie de sources (`lib_deps`).

### Non-Régression Absolue TTS v0.2.1
- **Empreintes SHA-256 identiques bit à bit** :
  - `LiveAudioPlayer.cpp`, `GeminiLiveClient.cpp`, `LiveSessionController.cpp`, `WiFiManager.cpp`, `AudioTraceLog.cpp`, `M5AudioOutput.cpp` et `GoogleRoots.h` sont strictement identiques à la version v0.2.1.
- **Compilation vérifiée avec succès** :
  - `examples/MinimalTTS` : SUCCESS (RAM 20.5%, Flash 55.5%).
  - `examples/OrientationClockVoice` : SUCCESS (RAM 48.6%, Flash 75.8%).
  - `examples/MinimalSTT` : SUCCESS (RAM 20.5%, Flash 55.9%).
- **Validation automatisée native** :
  - 25 suites de tests unitaires couvrant l'intégralité du cycle de vie TTS et STT (165 validations passées, 0 échec).

---

## [0.2.1] - 2026-10-24 (Conditionnement Bibliothèque Installable PlatformIO & Arduino IDE)

### Conditionnement & Structure de Bibliothèque
- **Conformité Arduino 1.5+ et PlatformIO Registry** :
  - Déplacement et organisation de l'ensemble des en-têtes publics et internes dans le répertoire `src/` (`src/VoxCoreVoice.h`, `src/VoiceKitConfig.h`, `src/core/`, `src/hal/`).
  - Suppression de la dépendance obligatoire à `-I../../include` : les projets consommateurs découvrent désormais automatiquement les en-têtes via la résolution native de bibliothèque de PlatformIO et de l'Arduino IDE.
  - Mise en place d'en-têtes relais (`#pragma once`) assurant une rétrocompatibilité totale ascendante et descendante.
- **Manifestes de Bibliothèque** :
  - `library.json` : Nommage unifié `"VoiceKit"`, version `0.2.1`, compatibilité plateforme `espressif32`, framework `arduino`, dépendances `M5CoreS3@1.0.0` et `M5Unified@0.2.23`, options de compilation `-std=gnu++17` et `-DVOXCORE_AUDIO_MODE=2`.
  - `library.properties` : Version `0.2.1`, catégorie `Audio`, architectures `esp32`, dépendances déclarées pour l'Arduino IDE.
- **Licence Officielle** :
  - Ajout du fichier `LICENSE` (Licence MIT).

### Compatibilité & Exemples
- **Exemple Arduino IDE Autonome (`examples/MinimalTTS_Arduino/`)** :
  - Ajout du croquis `MinimalTTS_Arduino.ino` directement compatible avec l'installateur de bibliothèque ZIP de l'Arduino IDE 2.x.
  - Documentation intégrée des prérequis matériels CoreS3 (PSRAM OPI, schéma de partition Huge APP / 16M).
- **Nettoyage des Exemples PlatformIO** :
  - Suppression des drapeaux de contournement `-I../../include` dans `examples/MinimalTTS/platformio.ini` et `examples/OrientationClockVoice/platformio.ini`.
  - Validation réussie de la compilation sans chemin d'accès absolu ou relatif forcé.
- **Validation Projet Externe (`tests/external-consumer/`)** :
  - Création d'un projet PlatformIO tiers indépendant validant l'installation de VoiceKit directement depuis GitHub via `lib_deps`.

### Sanctuaire Audio & Zéro Régression
- **Conservation stricte et intégrale du moteur VoxCore** :
  - Empreintes SHA-256 rigoureusement identiques à la version de référence v0.2.0 (`4670a27`) pour l'ensemble des fichiers critiques du moteur : `GeminiLiveClient.cpp`, `LiveAudioPlayer.cpp`, `LiveSessionController.cpp`, `WiFiManager.cpp`, `AudioTraceLog.cpp`, `M5AudioOutput.cpp` et `GoogleRoots.h`.
  - Zéro modification du transport TLS, de la gestion DMA audio, du tampon PSRAM ou du protocole BidiGenerateContent.
  - Zéro identifiant confidentiel (clés API, identifiants Wi-Fi) publié dans le dépôt.

---

## [0.2.0] - 2026-10-23 (Reconstruction sur la Base Stable de VoxCore)

### Changement Fondamental de Méthode
- **Abandon de l'ancienne implémentation v0.1.0.x** : Arrêt définitif des tentatives de réparation progressive de la pile expérimentale (qui introduisait des erreurs TLS `-0x2700`, des réassembleurs WebSocket superflus, des épuisements de heap et des coupures audio).
- **Extraction directe et sans réécriture de VoxCore** : Intégration fidèle des composants techniques stabilisés et testés avec succès sur M5Stack CoreS3 dans le dépôt de référence VoxCore (commits `edc2481` / `v0.2.2.0` et gel audio `e199733` / `v0.2.1.3.6`).

### Moteur Interne VoxCore Intégré
- **Protocole & WebSocket (`GeminiLiveClient.cpp`, `ILiveTransport.h`)** :
  - Transport WebSocket natif `esp_websocket_client` sur pile de 14 Ko.
  - Format BidiGenerateContent officiel avec `"generationConfig":{"responseModalities":["AUDIO"]}` et `"realtimeInputConfig":{"automaticActivityDetection":{"disabled":true}}`.
  - Élimination complète de la couche `WebSocketReassembler` et suppression du diagnostic TLS concurrent qui épuisait le heap interne.
  - Décodage Base64 RFC 4648 direct et gestion native des événements serveur (`setupComplete`, `turnComplete`, `interrupted`).
- **Certificats TLS (`GoogleRoots.h`)** :
  - Bundle officiel complet Google Trust Services de 21 certificats racines PEM (GTS Root R1 à R4, GlobalSign Root CA, etc.).
  - Validation stricte des certificats sans altération des options cryptographiques.
- **Sanctuaire Audio Éprouvé (`LiveAudioPlayer.cpp`, `LiveAudioPlayer.h`)** :
  - Mode 2 `DEDICATED_AUDIO_TASK` FreeRTOS sur Core 1 (priorité 4).
  - Tampon circulaire PSRAM de 120 000 échantillons 16 bits @ 24 kHz (~5 secondes de parole).
  - Quadruple buffer DMA (4 x 960 échantillons = 160 ms de marge acoustique).
  - Algorithme de vidage matériel exact `getHardwareDrainMs()` : le silence et `onSpeechFinished` ne sont confirmés qu'après la restitution physique complète de la dernière syllabe.
- **Traceur & Surveillance (`AudioTraceLog.cpp`)** :
  - Journaliseur circulaire d'événements POD temps réel avec horodatage microseconde, sans aucune allocation heap en cours de restitution.
- **Horloge & Réseau (`WiFiManager.cpp`)** :
  - Gestion Wi-Fi et synchronisation SNTP vérifiant `time(nullptr) >= 1700000000` avant le handshake TLS.
- **HAL Haut-Parleur (`M5AudioOutput.cpp`)** :
  - Pilotage direct de `M5.Speaker` en mode invité non-intrusif (`playRaw` 24 kHz stéréo/mono).
- **Machine à États Découplée (`LiveSessionController.cpp`)** :
  - Découplage complet de l'interface graphique : retrait strict de `UIManager` et LVGL du cœur de la bibliothèque.
  - Retrait du streamer microphone pour une bibliothèque purement dédiée au TTS.

### API Publique Simplifiée (`VoxCoreVoice.h`, `VoxCoreVoice.cpp`)
- Façade unifiée et ergonomique masquant la complexité interne du moteur VoxCore :
  - `bool begin(const VoiceKitConfig& config)`
  - `bool say(const String& text)`
  - `void update()`
  - `void stop()`
  - `bool isReady() const`, `bool isBusy() const`, `bool isSpeaking() const`, `String getLastError() const`
  - Callbacks asynchrones : `onReady`, `onSpeechStarted`, `onSpeechFinished`, `onError`.

### Exemples d'Application
- **`MinimalTTS`** : Exemple de test matériel minimaliste, sans LVGL, démontrant la connexion Wi-Fi/NTP, l'établissement de la session Gemini Live, l'annonce vocale d'accueil et le déclenchement tactile sur le CoreS3 physique.
- **`OrientationClockVoice`** : Intégration dans CoreS3 Orientation Clock (BMI270, RTC, LVGL 9) conservée intacte et validée à la compilation sans régression.

### Documentation de Livraison
- Ajout de `COMPARAISON_VOXCORE_VOICEKIT.md` (tableau d'équivalence composant par composant et justifications techniques).
- Ajout de `RAPPORT_LIVRAISON_v0.2.0.md` (rapport formel de livraison selon les étapes prescrites).

---

## [0.1.0.3] - 2026-10-23 (Correction TLS ciblée & Restauration du Comportement Validé VoxCore)

### Certificats TLS & Handshake mbedTLS (`GoogleRoots.h`, `GeminiLiveClient.cpp`)
- **Restauration intégrale du bundle officiel Google Trust Services (`GoogleRoots.h`)** : Réintégration complète des 21 certificats racines officiels issus de `https://pki.goog/roots.pem` tels que validés dans VoxCore (38 Ko). En particulier, réintégration de **`GTS Root R4`** (ECC P-384) et **`GlobalSign Root CA`** (R1), indispensables pour authentifier l'autorité intermédiaire `WE2` actuellement servie par `generativelanguage.googleapis.com:443`.
- **Résolution définitive de l'erreur `mbedtls_ssl_handshake returned -0x2700`** : Élimination de l'échec `MBEDTLS_ERR_X509_CERT_VERIFY_FAILED` causé par la troncature à deux racines (R1 & R2) effectuée précédemment, permettant un handshake TLS direct, authentifié et sans faille.
- **Étude de compatibilité `esp_crt_bundle_attach`** : Confirmation technique que sur ESP-IDF v4.4 (utilisé par Arduino-ESP32 v2.0.17 dans `espressif32@6.9.0`), `esp_websocket_client` ne supporte pas `crt_bundle_attach` (champ apparu avec ESP-IDF v5.0). Le passage du bundle PEM complet via `cfg.cert_pem` constitue la solution canonique et la plus performante.
- **Maintien strict de la sécurité TLS** : Préservation intégrale de la validation cryptographique du serveur et de son nom d'hôte (aucun contournement par `setInsecure` ni `skip_cert_common_name_check`).

### Diagnostics Explicites au Moniteur Série (`GeminiLiveClient.cpp`)
- **Séquence de diagnostic formalisée** :
  - `[TLS] Connexion à generativelanguage.googleapis.com`
  - `[TLS] Certificats : Google Trust Services (21 racines : GTS Root R1..R4, GlobalSign, DigiCert)`
  - `[TLS] Vérification : OK` (ou diagnostic détaillé des drapeaux X.509 : autorité inconnue, certificat expiré, horloge NTP non synchronisée, nom d'hôte incorrect)
  - `[Gemini] WebSocket : connecté`
  - `[Gemini] setupComplete : reçu`

### Résilience & Récupération après Erreur TLS / Réseau (`VoxCoreVoice.cpp`, `VoxCoreVoice.h`)
- **Machine à états avec reprise temporisée bornée** : En cas d'erreur de négociation TLS ou de coupure réseau, passage temporisé de `ERROR_STATE` vers `IDLE` après 5000 ms pour autoriser un nouvel essai propre.
- **Plafond de sécurité** : Nombre de tentatives automatiques borné à 3 (`MAX_RECONNECT_ATTEMPTS`).
- **Réinitialisation automatique** : Réinitialisation immédiate du compteur dès la confirmation `setupComplete` ou lors d'un appel explicite à `say()` ou `stop()`.
- **Correctif d'isolation transport** : Restauration propre du transport par défaut dans `setTransport(nullptr)` évitant tout pointeur pendant après injection de mock.

### Tests & Validation
- **Suite de tests automatisés** : Ajout de la Suite 15 (159 tests au total, 100% de réussite) validant l'intégrité du bundle TLS GoogleRoots et la temporisation de récupération d'erreur.
- **Compilation PlatformIO** : Validation complète sans erreur pour `examples/MinimalTTS` et `examples/OrientationClockVoice`.

---

## [0.1.0.2] - 2026-10-23 (Diagnostic et résolution de fermeture Gemini Live)

### Certificats TLS & Transport (`GoogleRoots.h`, `GeminiLiveClient.cpp`)
- **Réparation du certificat racine GTS Root R1** : Correction d'une troncature critique dans `GoogleRoots.h` où les dernières lignes de base64 et le marqueur `-----END CERTIFICATE-----` étaient absents, provoquant l'échec de la validation TLS mbedTLS sur ESP32.
- **Ajout de GTS Root R2** : Chaîne CA consolidée intégrant les deux certificats racines officiels de Google Trust Services (valides jusqu'en 2036).
- **Format d'URL standardisé** : Suppression du port `:443` explicite dans la chaîne d'URL (`wss://generativelanguage.googleapis.com/...`) afin de prévenir la pollution du champ SNI (*Server Name Indication*) lors du handshake TLS mbedTLS.

### Découpage & Réassemblage des Fragments WebSocket (`WebSocketReassembler.h`)
- **Nouvelle classe `WebSocketReassembler`** : Réassemblage dynamique des paquets fragmentés avant leur transmission au parseur JSON cJSON.
- **Prise en charge des fragments ESP-IDF** : Gestion des trames segmentées par le tampon réseau (8 Ko) via `payload_offset` et `payload_len`.
- **Prise en charge de la continuation RFC 6455** : Gestion transparente des opcodes `0x00` (continuation).
- **Protection anti-débordement mémoire (OOM)** : Plafond strict configurable (64 Ko par défaut) avec purge immédiate en cas de payload anormalement grand.
- **Fast-path sans copie** : Délivrance directe sans allocation pour les messages mono-trames non fragmentés.

### Diagnostic de Fermeture & Trames Close (`EspWebSocketTransport`, `ILiveTransport.h`)
- **Journalisation exhaustive des événements WebSocket** : Traçage systématique des événements `WEBSOCKET_EVENT_CONNECTED`, `WEBSOCKET_EVENT_DATA`, `WEBSOCKET_EVENT_CLOSED`, `WEBSOCKET_EVENT_DISCONNECTED` et `WEBSOCKET_EVENT_ERROR`.
- **Extraction des trames de fermeture CLOSE (Opcode `0x08`)** : Extraction du code de statut sur 2 octets big-endian (RFC 6455) et de la raison textuelle associée (ex: codes 1007, 1008).
- **Remontée contextuelle vers l'hôte** : Ajout du callback `CloseHandler` dans `ILiveTransport` et propagation du motif précis de fermeture au client et à l'application hôte au lieu d'un message générique.

### Extraction des Erreurs JSON Serveur (`GeminiLiveClient.cpp`)
- **Interprétation de l'objet `"error"`** : Détection et extraction systématique des champs `code`, `status` et `message` envoyés par Gemini en cas de rejet (ex: modèle inconnu, paramètres invalides, quota).
- **Interprétation de `"errorMessage"`** : Prise en charge des messages d'erreur textuels racine.

### Normalisation du Modèle & Robustesse
- **Préfixage automatique `"models/"`** : Normalisation automatique via `normalizeModelName()` pour accepter indifféremment `gemini-3.8-live` ou `models/gemini-3.8-live`, évitant le rejet serveur immédiat (code 1007 : *unexpected model name format*).
- **Vérification d'envoi du `setup`** : Contrôle du statut de transmission de la trame initiale de configuration et journalisation de la taille du payload.
- **Distinction des déconnexions intentionnelles** : Marquage explicite des fermetures locales (`stop()`, veille énergétique) pour ne plus les confondre avec des pannes réseau ou des fermetures inattendues, et préservation de l'état `ERROR_STATE` lors du teardown.
- **Confidentialité absolue** : Aucune journalisation des clés API ni des URL complètes.

### Tests Automatisés
- **Passage à 149 tests automatisés** (+48 nouveaux tests) couvrant le réassemblage, les limites mémoire, l'extraction JSON, les trames de fermeture, la normalisation et la robustesse de cycle de vie.
- **Validation matérielle** : Compilation réussie sous PlatformIO pour `examples/MinimalTTS` et `examples/OrientationClockVoice`.

---

## [0.1.0.1] - 2026-10-23 (Version corrective)

### Sécurité (Correction A)
- **Déréférencement des fichiers d'identifiants** : Retrait formel de l'index Git des fichiers `examples/MinimalTTS/include/secrets.h` et `examples/OrientationClockVoice/include/wifi_config.h` sans suppression locale (`git rm --cached`).
- **Règles `.gitignore` renforcées** : Ajout des règles d'exclusion pour `**/secrets.h`, `**/wifi_config.h`, `*.secret.h`, `.env`, et `.vscode/`.
- **Modèles de configuration d'exemple** : Maintien et alignement des fichiers `secrets.example.h` et `wifi_config.example.h` (ainsi que `wifi_config.h.example`) contenant des valeurs fictives documentées.
- **Assainissement des replis en dur** : Suppression des identifiants réels de repli qui figuraient dans `examples/OrientationClockVoice/include/WiFiTimeSync.h`.

### Modèle Gemini Live (Correction B)
- **Suppression du modèle expérimental obsolète** : Abandon de la référence imposée `models/gemini-2.0-flash-exp` (incompatible avec le endpoint WebSocket `bidiGenerateContent`).
- **Centralisation de la configuration** : Définition des constantes globales `VOICEKIT_DEFAULT_MODEL` (`"models/gemini-3.8-live"`, stabilisé dans VoxCore v0.2.2.0) et `VOICEKIT_DEFAULT_VOICE` (`"Aoede"`) dans `include/VoiceKitConfig.h`.
- **Configurabilité totale** : Possibilité de surcharger le modèle par constante `#define GEMINI_MODEL` ou dynamiquement via `vConfig.model`.

### Couche HAL & Haut-Parleur (Correction C)
- **Gestion explicite de la propriété audio** : Ajout de la méthode `isOwner()` dans `IAudioOutput` et `M5AudioOutput`.
- **Mode Invité respectueux** : Si l'application hôte a déjà initialisé `M5.Speaker` (détecté via `M5.Speaker.isRunning()`), VoiceKit n'arrête pas le haut-parleur physique sur `end()`, `reset()` ou à la destruction de l'objet (`~M5AudioOutput()`).
- **Isolement des canaux** : En mode invité, VoiceKit coupe uniquement son propre canal audio (canal 0) et détache son callback sans altérer la configuration matérielle de l'hôte.

### Application Horloge — Cohérence Horaire (Correction D)
- **Interrogation directe de `ClockManager`** : Ajout de la méthode `ClockManager::getLocalTime(int& hour, int& minute)`.
- **Alignement strict affichage / voix** : L'heure annoncée provient désormais directement de l'horloge système gérée par `ClockManager` sous le fuseau horaire `Europe/Zurich`, garantissant une parfaite correspondance entre les chiffres affichés et les mots prononcés.
- **Autonomie hors-ligne (RTC BM8563)** : L'heure reste disponible et fidèle même lorsque la radio Wi-Fi est éteinte.

### Machine à États & Robustesse Asynchrone (Correction E)
- **Gestion des requêtes concurrentes** : Élimination des connexions WebSocket concurrentes lors d'appels `say()` pendant la phase d'établissement (`CONNECTING_SERVICE`).
- **Protection contre les blocages permanents** : Ajout de timeouts asynchrones stricts pour la connexion (12 s) et la génération vocale (15 s).
- **Détection des coupures Wi-Fi en vol** : Interruption propre et transition en `ERROR_STATE` en cas de déconnexion réseau pendant une requête active.
- **Interruption sans callback fantôme** : La méthode `stop()` annule immédiatement les tampons et requêtes en cours sans émettre de fausse notification `onSpeechFinished`.
- **Réinitialisation propre après erreur** : Élimination des états bloquants permettant une relance immédiate après une anomalie distante.

### Coordination Wi-Fi (Correction F)
- **Verrouillage radio continu** : Maintien du Wi-Fi actif durant la connexion, l'envoi de prompt, la génération, la restitution audio et les demandes en file.
- **Maintien chaud 15 secondes** : Préservation de la radio active pendant 15 s après une annonce pour un réarmement immédiat lors d'appuis consécutifs.
- **Sécurité anti-blocage (Watchdog)** : Libération garantie du verrou Wi-Fi en cas d'erreur, timeout, interruption ou expiration du délai d'inactivité.

### Tests & Validation
- **Tests natifs unitaires** : Passage de 55 à 101 tests automatisés C++17 (100 % PASS).
- **Validation PlatformIO** : Compilation réussie de `examples/MinimalTTS` et `examples/OrientationClockVoice`.

---

## [0.1.0] - 2026-10-22 (Version initiale)
- Création de la bibliothèque VoxCore VoiceKit.
- Port du moteur audio haute fidélité PCM 24 kHz depuis VoxCore v0.2.1.3.6.
- Client WebSocket TLS Gemini Live.
- Exemples `MinimalTTS` et `OrientationClockVoice`.
