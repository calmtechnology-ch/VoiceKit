# VoiceKit v0.6.4.2 — Voice UX Live Bridge, Voice UX Foundation, Local Tool Registry & Architecture Multi-Fournisseurs (Gemini Live & OpenAI Realtime)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![Arduino IDE](https://img.shields.io/badge/Arduino%20IDE-Compatible-00979C.svg)](https://www.arduino.cc/)
[![ESP32-S3](https://img.shields.io/badge/Target-M5Stack%20CoreS3%20(ESP32--S3)-blue.svg)](https://docs.m5stack.com/en/core/CoreS3)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Bibliothèque C++/Arduino installable de **synthèse vocale (Text-To-Speech / TTS)**, de **reconnaissance vocale (Speech-To-Text / STT)**, de **dictée vocale continue mains libres (ContinuousSTT)**, de **conversation vocale bidirectionnelle native (Mode B)**, de **détection locale de mot de réveil (Wake Word Foundation hors ligne)** et de **registre local d'outils (Local Tool Registry)** pour **M5Stack CoreS3** (ESP32-S3).

Propulsée par une **architecture multi-fournisseurs unifiée (`IVoiceProvider`)** supportant à la fois **Google Gemini Live** (moteur par défaut, 100% rétrocompatible) et **OpenAI Realtime API** (`wss://api.openai.com/v1/realtime`), construite sur le moteur audio et réseau éprouvé de **[VoxCore](https://github.com/Philippe-AD/VoxCore)** et **[VoxTranscribe](https://github.com/Philippe-AD/VoxTranscribe)**.

La version **v0.6.4.2** introduit le pont applicatif **Voice UX Live Bridge (`VoiceUxLiveBridge`)**, reliant pour la première fois la reconnaissance vocale STT du microphone du M5Stack CoreS3, l'arbitrage d'intentions locales `VoiceUxController`, la mémoire de réponses `ReplyMemory` et la restitution sonore TTS haute fluidité :
- **Pont Applicatif `VoiceUxLiveBridge` (`src/core/voiceux/VoiceUxLiveBridge.h/.cpp`)** :
  - **Coordination Découplée STT $\to$ UX $\to$ MEM $\to$ TTS** : Reçoit les transcriptions finales STT, les transmet à `VoiceUxController` et met en attente des requêtes d'action structurées (`VoiceUxBridgeAction`) consommées en toute sécurité depuis `loop()`. Aucune opération TTS n'est exécutée dans un callback STT.
  - **Exécution Locale Déterministe** : Les commandes vocales de contrôle comme « Répète » (`REPEAT`) et « Réponse précédente » (`PREVIOUS`) rappellent directement les réponses mémorisées dans `ReplyMemory` et déclenchent la restitution TTS locale sans aucun appel à un modèle d'IA distant.
  - **Sûreté Audio & Machine à États** : Micro libéré avant toute restitution audio, interdiction de capture pendant la restitution TTS, dédoublonnage des transcriptions finales d'un même tour, rejet strict des événements tardifs ou annulés grâce au suivi par identifiants de tour (`turnId`), et priorité absolue de la commande `STOP` (tactile ou série).
  - **Gestion Explicite de Pause** : État `PAUSED` ignorant les demandes conversationnelles ordinaires et autorisant uniquement la reprise vocale (`RESUME` / « Continue ») ou tactile.
  - **Nouvelle Démonstration Matérielle CoreS3 (`examples/VoiceUxLiveDemo/`)** : Interface graphique M5Unified/M5GFX (320 × 240) sans LVGL 9, boutons tactiles `PARLER` (PTT) et `STOP`, console de diagnostic série avec mode hybride voix réelle (`[STT][REAL]`) et simulation (`[STT][SIM]`).

La version **v0.6.4.1** introduit la fondation logicielle expérimentale **Voice UX Foundation (`VoiceUxController`)**, permettant d'analyser localement les transcriptions textuelles et d'exécuter des décisions conversationnelles déterministes en français sans solliciter de modèle d'IA externe :
- **Fondation Voice UX Locale (`VoiceUxController`, `TextGate`, `VoiceIntentRouter`, `ReplyMemory`, `VoiceUxTypes`)** :
  - **Filtrage Déterministe `TextGate`** : Évalue chaque transcription (`ACCEPT`, `IGNORE`, `INCOMPLETE`). Rejette les entrées vides, la ponctuation seule et les hésitations isolées (« euh », « heu », « hum », « hmm »). Conserve intégralement les commandes courtes (« oui », « non », « stop », « répète ») et n'invalide jamais une phrase complexe contenant une hésitation. Détecte à titre purement informatif les phrases potentiellement incomplètes (points de suspension ou conjonction/préposition orpheline finale).
  - **Routeur d'Intentions Conversationnelles Françaises `VoiceIntentRouter`** : Reconnaissance locale instantanée (1 à 4 µs) de 7 intentions majeures : `REPEAT`, `PREVIOUS`, `STOP`, `PAUSE`, `RESUME`, `SLOWER`, `REPHRASE`. Tolère les formules de politesse en préfixe/suffixe (« s'il te plaît », « merci », « peux-tu »). Protège strictement contre les faux positifs : les énoncés avec complément d'objet (« Arrête la minuterie », « Continue cette équation », « Peux-tu répéter cette expérience scientifique ? ») ou questions complexes (« Pourquoi la voiture s'arrête-t-elle ? ») retournent strictement `NONE`.
  - **Mémoire Conversationnelle Bornée `ReplyMemory`** : Mémorise en RAM uniquement les 5 dernières réponses complètes de l'assistant (sans base de données ni écriture Flash). Éviction FIFO de la plus ancienne lors de la saturation. Permet le rappel immédiat sur intention `REPEAT` (`getLastReply()`) ou `PREVIOUS` (`getPreviousReply()`). Protège rigoureusement l'encodage UTF-8 RFC 3629 lors de la troncature à la taille maximale autorisée (1024 octets).
  - **Façade Découplée & Stricte Neutralité** : Désactivée par défaut (`_enabled = false`) pour préserver l'intégrité de tous les projets existants. N'exécute aucune action matérielle directe (`say`, `stop`, `listen`) : émet des événements structurés (`VoiceUxResult`) et laisse l'application maîtresse décider des actions.
  - **Démonstration Dédiée CoreS3 (`examples/VoiceUxDemo/`)** : Fonctionne à 100% hors-ligne sans Wi-Fi, sans clé API et sans capture audio, avec affichage M5GFX et pilotage série interactif (commandes `T`, `M`, `H`, `C`, `D`).

La version **v0.6.4** introduit une infrastructure de **registre local d'outils (`Local Tool Registry`)** permettant aux applications ESP32 d'enregistrer, découvrir, valider et exécuter des commandes locales de façon totalement indépendante du Wi-Fi, de Gemini Live et d'OpenAI Realtime :
- **Registre Local d'Outils Découplé (`ToolRegistry`, `ToolDefinition`, `ToolExecutor`, `ToolValidator`, `ToolEvents`)** :
  - **Enregistrement & Découverte** : Capacité configurable (16 commandes par défaut), vérification d'unicité des identifiants (rejet des doublons), validation de schéma au moment de l'enregistrement, énumération complète, recherche par identifiant et inspection des métadonnées/paramètres.
  - **Typage & Validation Stricte des Paramètres** : Validation des types (`STRING`, `INT`, `FLOAT`, `BOOLEAN`), vérification des paramètres obligatoires vs optionnels avec valeurs par défaut, bornage min/max numérique, et rejet systématique des paramètres inconnus (sauf autorisation explicite `allowUnknownParams`).
  - **Niveaux de Sécurité & Confirmation Sensible** : 3 niveaux de sécurité (`READ_ONLY`, `WRITE`, `SENSITIVE`). Une commande `SENSITIVE` exige une confirmation explicite liée à un jeton à usage unique (`CONF-xxxxxxxx`), protégé contre le rejeu et limité dans le temps (expiration configurable, 30 s par défaut).
  - **Exécution Contrôlée & Protection Anti-Réentrance** : Exécution protégée contre la réentrance concurrente (`BUSY`), gestion d'erreurs d'exécution (`EXECUTION_FAILED`), support d'exécution différée non bloquante (`enqueueDeferred`), et résultats structurés (`ToolResult`).
  - **Cycle de Vie & Événements** : 5 événements observables (`TOOL_REQUESTED`, `TOOL_STARTED`, `TOOL_COMPLETED`, `TOOL_FAILED`, `TOOL_CONFIRMATION_REQUIRED`) prêts pour l'intégration UI (VoiceKit-SmartUI).
  - **Fondation Découplée pour le Futur** : Socle autonome préparé pour de futurs appels d'outils IA et une interopérabilité MCP (sans annoncer de support prématuré dans cette version — voir [Documentation Architecture Registre, IA & MCP](docs/architecture_tools_registry_ai_mcp.md)).

La version **v0.6.3.1** est une version corrective qui fiabilise la latence de `voice.update()`, consolide les transitions audio et prépare la sélection explicite du modèle WakeNet sans refondre l'architecture existante :
- **Correctifs de Stabilité & WakeNet Configurable (v0.6.3.1)** :
  - **Élimination du Blocage Synchrone dans `voice.update()`** : Remplacement de l'appel bloquant `esp_websocket_client_stop()` (responsable du pic `582 701 µs` pendant `esp_transport_poll_read(1000)`) par un arrêt non bloquant en 2 phases (`requestDisconnect()` / `pollDisconnect()` et `requestDeactivate()` / `pollDeactivate()`) sans aucune tâche FreeRTOS supplémentaire. Séparation de `WakeNet::detect()` et de `stopLocalCapture()` via `requestStopLocalCaptureAsync()`, optimisation de `stopMicHardware()` et `restoreSpeakerAfterMic()`, et suppression de la trame `sendInterruption()` redondante avant fermeture (`abortTurnForSessionClose()`).
  - **Consolidation des Transitions Audio & Propriétaire Unique du Microphone** : Suivi explicite du propriétaire unique (`HardwareManager::getMicOwner()`) et compteur d'échecs de réservation (`micReservationFailures`), absence de double démarrage I2S, absence de reprise WakeNet pendant la conversation, absence de fermeture prématurée du lecteur audio et absence de WebSocket résiduel en fin de session.
  - **Sélection Configurable du Modèle WakeNet (`config.wakeWordModelName = "wn10_fr_bonjouresp"`)** : Conservation obligatoire de `config.wakeWordPhrase = "Bonjour ESP"` et de `wn10_fr_bonjouresp` par défaut, vérification stricte à 4 niveaux avant utilisation (support compilé, présence du modèle en partition Flash, initialisation, reconnaissance acoustique active), vérification de cohérence phrase/modèle (`isWakeWordPhraseConsistentWithModel()`), et absence totale de bascule implicite vers `TEST` en cas de modèle absent.
  - **Diagnostics Séparés par Sous-Système & Visibilité Immédiate UI** : Instrumentation fine de `voice.update()` (moyenne et max), `WakeWordConversationController::update()`, transitions d'état, `WakeWordManager::update()`, `LiveSessionController`, WebSocket, transitions microphone, arrêt de session et réactivation WakeNet, ainsi que affichage immédiat par événements réels des 7 transitions (`BONJOUR ESP DÉTECTÉ`, `CONNEXION`, `PRÊT — PARLEZ`, `ÉCOUTE`, `RÉPONSE`, `FIN DE CONVERSATION`, `RETOUR EN ÉCOUTE`).

La version **v0.6.3** introduit la **conversation vocale entièrement mains libres déclenchée par le mot de réveil local « Bonjour ESP » (`WakeWordConversationController`)** sur M5Stack CoreS3, tout en préservant intégralement la reconnaissance acoustique WakeNet et les modes existants :
- **Conversation Mains Libres Déclenchée par « Bonjour ESP » (v0.6.3)** :
  - **Composant Réutilisable `WakeWordConversationController`** : Machine à états non bloquante pilotée par `update()` (`WAKE_ARMED`, `WAKE_DETECTED`, `PREPARING_SESSION`, `CONNECTING`, `CONVERSATION_LISTENING`, `CONVERSATION_SPEAKING`, `ENDING_SESSION`, `RETURNING_TO_WAKE`, `RECOVERABLE_ERROR`), strictement séparée des états internes de `WakeWordManager`.
  - **Cycle Complet & Répétable Sans Redémarrage** : Écoute locale WakeNet $\to$ détection acoustique confirmée $\to$ suspension de WakeNet et libération contrôlée du microphone $\to$ connexion au fournisseur vocal configuré (`Gemini Live` par défaut ou `OpenAI Realtime`) $\to$ échanges vocaux bidirectionnels multi-tours sans répéter le mot de réveil $\to$ détection de fin de session (timeout de silence 30 s, durée maximale, arrêt manuel ou commande vocale confirmée sur transcription finale) $\to$ restitution intégrale des derniers échantillons audio et libération des ressources réseau/audio $\to$ réactivation de WakeNet $\to$ retour à `WAKE_ARMED`.
  - **Protections de Concurrence & Sûreté Audio** : Traitement différé dans `update()` (aucune opération longue dans `onWakeWordDetected()`), rejet des détections concurrentes, garde anti-réentrance, interdiction de réactivation prématurée de WakeNet, vérification stricte de `stopLocalCapture()` sans double réservation du microphone, et nettoyage des sessions WebSocket résiduelles.
  - **API Applicative & Compatibilité LVGL 9** : 8 callbacks dédiés exécutés sur la boucle principale (`onWakeConvDetected`, `onWakeConvConnecting`, `onWakeConvReady`, `onWakeConvListening`, `onWakeConvSpeaking`, `onWakeConvEnded`, `onWakeConvReturnedToWake`, `onWakeConvError`), file d'événements non bloquante (`pollWakeWordConversationEvent()`), diagnostics complets (`WakeWordConversationMetrics`) et nouvel exemple `examples/WakeWordConversation/`.

La version **v0.6.2.1** est une version corrective qui fiabilise et durcit la **fondation locale de détection de mot de réveil (« Bonjour ESP »)** avant son intégration dans une application LVGL 9 (`CoreS3-VoiceUI-Demo`) sur M5Stack CoreS3 :
- **Correctifs de Fiabilité & Performances Wake Word Foundation (v0.6.2.1)** :
  - **Sûreté Concurrente SPSC de `WakeWordAudioBridge`** : Désactivation propre de la politique `DROP_OLDEST` (repli automatique sur `DROP_NEWEST`) afin de garantir mathématiquement que le producteur audio ne modifie jamais `_tail` et n'invalide jamais un pointeur `peekFrame()` pendant `IWakeWordEngine::process()`. Ajout d'un compteur d'écrivains actifs sans verrou (`_activeWriters`) et d'une époque de génération (`_epoch`) protégeant `clear()`, `setEnabled(false)`, `begin()` et `end()` contre toute trame partiellement écrite ou fantôme.
  - **Budget CPU Borné & Fluidité LVGL (`WakeWordManager`)** : Introduction de `setWakeWordMaxFramesPerUpdate()` (défaut : `2`) et `setWakeWordProcessingBudgetUs()` (défaut : `5000 µs`) pour borner le temps consommé dans `voice.update()`, avec détection des dépassements (`processingBudgetExceeded`) et télémétrie fine (`lastUpdateProcessDurationUs`, `maxUpdateProcessDurationUs`, `lastWakeProcessDurationUs`, `maxWakeProcessDurationUs`).
  - **Vérification Stricte d'Arrêt Microphone (`stopLocalCapture`)** : Contrôle systématique du résultat de `stopLocalCapture()` lors des transitions vers STT, TTS, ContinuousSTT et Conversation ; refus propre en cas d'échec sans double initialisation du micro ES7210 (`captureStopFailures`), et récupération complète dès levée de quarantaine FreeRTOS sans blocage définitif du microphone.
  - **Durcissement de `WakeNetEngine` & Métriques RMS Authentiques** : Distinction stricte à 4 niveaux (`isWakeWordEngineCompiledSupportAvailable`, `isWakeWordEngineModelAvailable`, `isWakeWordEngineInitialized`, `isWakeWordRealRecognitionActive`), libération garantie des modèles ESP-SR sur tout échec partiel de `begin()`, suppression du score de confiance fictif (`0.0f` au lieu de `0.95f`), continuité sans perte de l'adaptateur `512 -> 480` sur détection, et suppression de l'injection de trame silencieuse fictive dans `triggerTestWakeWord()` pour préserver les métriques RMS réelles (`lastRms`, `peakRms`).
- **Fondation Wake Word Locale Hors Ligne (v0.6.2)** :
  - **Capture Audio Partagée Hors Ligne** : VoiceKit (`LiveAudioStreamer` + `HardwareManager`) demeure l'unique propriétaire du microphone CoreS3 ES7210 (PCM 16 kHz mono 16 bits signé), utilisable sans Wi-Fi ni connexion Gemini/OpenAI via `startLocalCapture()` sans aucune tâche FreeRTOS supplémentaire.
  - **Architecture Découplée (`IWakeWordEngine`, `WakeWordAudioBridge`, `WakeWordManager`)** : Pont audio temps réel à file SPSC bornée (1 seule copie DMA $\to$ slot, 0 copie au dépilage, politique sécurisée `DROP_NEWEST`), machine à états complète (`DISABLED` $\to$ `INITIALIZING` $\to$ `ARMED` $\to$ `DETECTED` $\to$ `SUSPENDED` $\to$ `ARMED`, `ERROR`), suspension automatique pendant TTS/STT/Conversation et reprise contrôlée après la réponse vocale.
  - **Étude & Adaptateur Espressif ESP-SR WakeNet10 (`WakeNetEngine`)** : Adaptateur pour le modèle français `wn10_fr_bonjouresp` avec recadrage sans perte `512 -> 480` échantillons (`32 ms -> 30 ms` @ 16 kHz). Rapporte honnêtement son indisponibilité (`ERROR`) sous Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7 sans jamais simuler silencieusement une reconnaissance vocale.
  - **Moteur de Test Déterministe (`TestWakeWordEngine`) & Compatibilité LVGL (`pollWakeWordEvent`)** : Validation complète du pipeline par commande série ou injection explicite, diagnostics mémoire/CPU (`WakeWordMetrics`) et exemple autonome `examples/WakeWordFoundation/`.
- **Correctifs de Stabilité Temps Réel (v0.6.1)** :
  - **Correction A (Métriques PCM Multi-Tours)** : Réinitialisation polymorphe des métriques audio RX via `IVoiceProvider` (`resetTurnRxMetrics`, `getTurnDecodedSamples`, `getPostTurnCompleteBlocks`), éliminant les fausses alertes d'ordre après `turnComplete` à partir du 2ᵉ tour tout en préservant la détection stricte des véritables blocs tardifs.
  - **Correction B (Ordonnancement FreeRTOS TLS)** : Ajustement de la priorité de `websocket_task` pendant le handshake mbedTLS (`prio = 1`, puis `2` une fois connecté) pour supprimer la préemption prolongée (~1,55 s) de `loopTask` (LVGL/tactile) et ajout de l'instrumentation complète TLS/FreeRTOS/RAM interne.
  - **Correction C (Wi-Fi FastConnect Non-Bloquant)** : Désactivation des écritures NVS synchrones redondantes (`WiFi.persistent(false)`) et déport des opérations de repli/reconnexion Wi-Fi sur une tâche ouvrière unique persistante (`vk_wifi_wkr`), éliminant le blocage de ~343 ms dans `WiFiManager::tick()` / `voice.update()`.
- **Architecture Multi-Fournisseurs (`IVoiceProvider`)** : Interface pure unifiée pour l'authentification, la négociation de session, le streaming audio bidirectionnel, les transcriptions STT/TTS et les interruptions STOP.
- **Intégration Native OpenAI Realtime** : Client WebSocket TLS direct ESP32-S3 (`OpenAIRealtimeClient`) avec support du modèle `gpt-realtime-2.1-mini`, authentification Bearer, négociation `session.update`, streaming PCM 24 kHz Base64 et transcriptions Whisper intégrées.
- **Sélection par Configuration Zéro-Code** : Bascule instantanée entre Gemini et OpenAI par simple configuration (`config.provider = VoiceProviderType::OPENAI;`). Aucune modification requise dans le code métier de l'application !
- **Rééchantillonneur Dédié 16 kHz $\to$ 24 kHz (`AudioResampler`)** : Algorithme linéaire ultra-rapide en virgule fixe entière (512 in $\to$ 768 out en moins de 15 µs sur Core 1), n'impactant aucunement la tâche audio critique.
- **Sanctuaire Audio VoxCore Intact** : Conservation intégrale de la tâche FreeRTOS Mode 2, du buffer circulaire PSRAM 120 000 échantillons (~5 s), des buffers DMA et du protocole de vidage exact sans coupure de la syllabe finale (*« alpha tango terminé »*).
- **Validation Exhaustive & Zéro Régression** : **2 579 tests unitaires réussis avec 0 échec** (138 suites de tests) et 100% des exemples PlatformIO compilés avec succès.

---

## Sommaire

- [**Apprendre VoiceKit — Tutoriel progressif**](#apprendre-voicekit--tutoriel-progressif)
1. [Fonctionnalités Principales](#1-fonctionnalités-principales)
2. [Architecture Multi-Fournisseurs](#2-architecture-multi-fournisseurs)
3. [Structure de la Bibliothèque](#3-structure-de-la-bibliothèque)
4. [Méthodes d'Installation](#4-méthodes-dinstallation)
   - [Méthode A — PlatformIO depuis GitHub](#méthode-a--platformio-depuis-github)
   - [Méthode B — Arduino IDE](#méthode-b--arduino-ide)
   - [Méthode C — Développement Local](#méthode-c--développement-local)
5. [Configuration & Choix du Fournisseur](#5-configuration--choix-du-fournisseur)
6. [Nouvelle API Simplifiée (`VoiceKit.h`)](#6-nouvelle-api-simplifiée-voicekith)
7. [Registre Local d'Outils (`ToolRegistry` — v0.6.4)](#7-registre-local-doutils-toolregistry--v064)
8. [Contrôleur Réutilisable `ContinuousListeningController`](#8-contrôleur-réutilisable-continuouslisteningcontroller)
9. [Référence de l'API Historique (`VoxCoreVoice`)](#9-référence-de-lapi-historique-voxcorevoice)
10. [Exemples Inclus](#10-exemples-inclus)
11. [Garanties de Non-Régression & Sécurité](#11-garanties-de-non-régression--sécurité)
12. [Architecture : Registre Local, IA Tool Calling & MCP](#12-architecture--registre-local-ia-tool-calling--mcp)

---

## Apprendre VoiceKit — Tutoriel progressif

Vous découvrez VoiceKit ou la programmation vocale sur ESP32-S3 ? Un **[parcours d'apprentissage progressif en français](tutorials/README.md)** est disponible dans le dossier `tutorials/`.

Conçu selon le principe de *la simplicité avant la sophistication*, il vous guide pas à pas à travers 12 leçons thématiques, depuis l'installation et votre premier programme jusqu'à la synthèse vocale, la reconnaissance vocale et le dialogue interactif :
- **[Leçon 00 — Préparer son environnement VoiceKit](tutorials/00-Installation/README.md)** : Prise en main du CoreS3, installation de PlatformIO, Wi-Fi et clé API Gemini Live.
- **[Leçon 01 — Mon premier programme VoiceKit](tutorials/01-HelloVoiceKit/README.md)** : Initialisation de la façade `VoiceKit`, configuration et connexion à Gemini Live.
- **[Leçon 02 — Faire parler le CoreS3](tutorials/02-TextToSpeech/README.md)** : Synthèse vocale asynchrone (`say`), suivi d'événements (`onSpeechStarted`, `onSpeechFinished`).
- **[Leçon 03 — Contrôler la synthèse vocale](tutorials/03-SpeechControl/README.md)** : Pilotage interactif série, volume sonore (0-100 %), arrêt d'urgence (`stop`) et diagnostic.

Consultez le **[sommaire complet du parcours (Leçons 00 à 11)](tutorials/README.md)** pour découvrir l'ensemble des leçons.

---

## 1. Fonctionnalités Principales

- **Zéro dépendance intrusive** : VoiceKit est headless. Il ne requiert ni LVGL, ni UIManager, et n'interfère pas avec l'écran ou les capteurs de l'hôte.
- **Configuration Vocale Complète (v0.4.1)** :
  - **Voix Gemini** : Sélection parmi les voix officielles (ex. `Aoede`, `Puck`, `Charon`, `Fenrir`, `Kore`).
  - **Langue BCP-47** : Paramétrable (`fr-FR` par défaut, `en-US`, `de-DE`, etc.). Orientation intelligente : instruction système systématique, plus paramètre de setup `speechConfig.languageCode` si le modèle le supporte.
  - **Styles d'élocution** : `natural`, `professional`, `expressive`, `narrative`.
  - **Tonalités expressives** : `neutral`, `calm`, `warm`, `cheerful`, `serious`.
  - **Accentuation optionnelle** : Directives de prononciation transmises sans altérer les instructions métier (TTS, STT, conversation).
  - **Reconnexion sécurisée** : Changement de profil dynamique appliqué uniquement à chaud lorsque le moteur est au repos (`!isBusy()`).
- **Chaîne UTF-8 & Français Intégrale (v0.4.1)** :
  - Préservation stricte de tous les caractères accentués français (`é`, `è`, `ê`, `ë`, `à`, `â`, `ù`, `û`, `î`, `ï`, `ô`, `ö`, `ç`, `É`, `À`, `Ç`).
  - Prise en charge des ligatures (`œ`, `æ`) et ponctuations typographiques (`’`, `«`, `»`).
  - Zéro translittération, suppression ou altération d'accent lors du transport audio et des requêtes/réponses Gemini.
  - Validation UTF-8 conforme RFC 3629 et échappement JSON centralisé conforme RFC 8259 (`Utf8Utils`).
- **Conversation Vocale Bidirectionnelle Native (Mode B)** :
  - Flux continu sans latence : Micro ES7210 (PCM 16 kHz) $\rightarrow$ Gemini Live $\rightarrow$ Haut-parleur AW88298 (PCM 24 kHz).
  - Élimination des allers-retours textuels superflus : Gemini génère directement sa réponse vocale native en streaming audio.
  - Coordination légère de session via `ConversationController` : gestion fine des tours sans second transport WebSocket ni duplication de tâches.
  - Callbacks de transcription intégrés pour afficher à la fois la question prononcée (`inputTranscription`) et la réponse de l'assistant (`modelTurn`).
- **Sanctuaire Audio TTS Mode 2 (FreeRTOS)** : 
  - Tâche audio dédiée sur le Core 1 (priorité 4).
  - Tampon circulaire en PSRAM de 120 000 échantillons 16 bits @ 24 kHz (~5 s de pré-tamponnage).
  - Quadruple buffer DMA (160 ms de marge acoustique).
  - Algorithme de vidage matériel exact : aucune coupure de syllabe en fin d'énonciation.
- **Reconnaissance Vocale STT Haute Fidélité (PCM 16 kHz)** :
  - Capture matérielle microphone CoreS3 (double micro ES7210).
  - Double-buffering ping-pong DMA 512 échantillons @ 16 kHz.
  - Streaming audio asynchrone découplé vers Gemini Live avec signalisation officielle `activityStart` / `activityEnd`.
  - Transcriptions partielles (interim) et finales avec flag `isFinal`.
  - Règle stricte anti-hallucination : aucune invention de transcription finale à partir de fragments partiels ; gestion d'un timeout de silence paramétrable.
  - Arbitrage matériel automatique : coupure automatique du haut-parleur pendant l'écoute pour éliminer le larsen.
- **Sécurité TLS & Google PKI** :
  - Connexion WebSocket native `esp_websocket_client`.
  - Bundle officiel de 21 certificats Google Trust Services (`GoogleRoots.h`).
  - Validation temporelle SNTP préalable automatique.
- **Conditionnement standard** : Compatible PlatformIO Registry / Git et Arduino IDE 1.5+ specification.

---

## 2. Architecture Multi-Fournisseurs

VoiceKit v0.6.0 introduit une architecture ouverte découplée reposant sur l'interface pure `IVoiceProvider` :

```text
                        ┌───────────────────────────────────────────────┐
                        │      Application Utilisateur / Exemples        │
                        └───────────────────────┬───────────────────────┘
                                                │
                                    ┌───────────▼───────────┐
                                    │   VoiceKit / VoxCore   │
                                    └─────┬───────────┬─────┘
                                          │           │
                 ┌────────────────────────┘           └────────────────────────┐
                 ▼                                                             ▼
    ┌─────────────────────────┐                                   ┌─────────────────────────┐
    │   GeminiVoiceProvider   │                                   │   OpenAIRealtimeClient  │
    │   (Google Gemini Live)  │                                   │  (OpenAI Realtime API)  │
    └────────────┬────────────┘                                   └────────────┬────────────┘
                 │ PCM 16k in / 24k out                                        │ PCM 24k in / 24k out
                 │                                                ┌────────────▼────────────┐
                 │                                                │      AudioResampler     │
                 │                                                │   (16 kHz -> 24 kHz)    │
                 │                                                └────────────┬────────────┘
                 ▼                                                             ▼
    ┌─────────────────────────┐                                   ┌─────────────────────────┐
    │  Google Live Transport  │                                   │  OpenAI TLS Transport   │
    │  (wss://generativelanguage...)                              │  (wss://api.openai.com) │
    └─────────────────────────┘                                   └─────────────────────────┘
```

- **`IVoiceProvider`** : Contrat standardisé pour la connexion, l'authentification Bearer, la configuration de session, l'envoi de chunks audio PCM, la réception de flux audio descendants, la transmission de texte, l'interruption STOP et les callbacks d'événements.
- **`GeminiVoiceProvider`** : Adaptateur sans surcoût enveloppant le client `GeminiLiveClient` existant (100% de rétrocompatibilité).
- **`OpenAIRealtimeClient`** : Client natif WebSocket TLS direct pour `wss://api.openai.com/v1/realtime`, gestion des trames `session.update`, Base64 streaming, et transcriptions Whisper.
- **`AudioResampler`** : Algorithme linéaire ultra-rapide à virgule fixe entière convertissant les blocs 16 kHz du micro CoreS3 vers les 24 kHz requis par OpenAI, en moins de 15 µs par bloc, sans blocage de la boucle audio.

---

## 3. Structure de la Bibliothèque

```text
VoiceKit/
├── library.json                 # Manifeste PlatformIO (v0.6.4.2)
├── library.properties           # Manifeste Arduino IDE (v0.6.4.2)
├── extra_script.py              # Script PlatformIO (liaison ESP-SR / WakeNet)
├── no_ota.csv                   # Table de partitions sans OTA (modèle à copier)
├── LICENSE                      # Licence MIT
├── README.md                    # Documentation complète
├── CHANGELOG.md                 # Historique des versions
├── docs/                        # Architecture & documentation approfondie
│   ├── README.md                # Index de la documentation
│   ├── architecture_tools_registry_ai_mcp.md # Registre Local, Tool Calling & MCP
│   └── rapports/                # Rapports techniques & de livraison (v0.6.0 → v0.6.4.2)
├── tools/
│   └── validation/              # Scripts Python de validation matérielle (port série)
├── tutorials/                   # Parcours d'apprentissage progressif (leçons 00 à 03)
├── src/                         # Sources et en-têtes (Standard Arduino 1.5)
│   ├── VoiceKit.h               # Façade simplifiée unifiée (TTS, STT, Continuous, Conversation, WakeWord)
│   ├── VoiceKit.cpp             # Implémentation de la façade simplifiée
│   ├── VoxCoreVoice.h           # En-tête public historique préservé
│   ├── VoxCoreVoice.cpp         # Moteur principal VoiceKit / VoxCore
│   ├── VoiceKitConfig.h         # Configuration, choix du fournisseur, profils & Wake Word
│   ├── Utf8Utils.h              # Utilitaires UTF-8 & échappement JSON RFC 8259
│   ├── core/                    # Moteur interne VoxCore, Fournisseurs & Wake Word
│   │   ├── voiceux/             # Voice UX Foundation (v0.6.4.1) & Live Bridge (v0.6.4.2)
│   │   │   ├── VoiceUxTypes.h   # Types partagés (intentions, décisions, résultats)
│   │   │   ├── TextGate (.h/.cpp)          # Filtrage déterministe des transcriptions
│   │   │   ├── VoiceIntentRouter (.h/.cpp) # Routeur d'intentions conversationnelles françaises
│   │   │   ├── ReplyMemory (.h/.cpp)       # Mémoire bornée des 5 dernières réponses
│   │   │   ├── VoiceUxController (.h/.cpp) # Façade Voice UX locale
│   │   │   └── VoiceUxLiveBridge (.h/.cpp) # Pont STT → UX → Mémoire → TTS (v0.6.4.2)
│   │   ├── tools/               # Registre Local d'Outils (v0.6.4)
│   │   │   ├── ToolDefinition.h # Types, arguments, résultats et définitions
│   │   │   ├── ToolEvents.h     # Événements du cycle de vie des commandes
│   │   │   ├── ToolValidator.h  # Validateur strict des types et contraintes
│   │   │   ├── ToolExecutor.h   # Exécuteur avec garde anti-réentrance et file différée
│   │   │   ├── ToolRegistry.h   # Registre principal (enregistrement, découverte, sécurité)
│   │   │   └── ToolRegistry.cpp # Implémentation du registre d'outils
│   │   ├── WakeWordConversationController (.h/.cpp) # Contrôleur de conversation déclenchée par mot de réveil (v0.6.3)
│   │   ├── IWakeWordEngine.h              # Interface abstraite des moteurs de mot de réveil (v0.6.2)
│   │   ├── WakeWordAudioBridge (.h/.cpp)  # Pont audio temps réel SPSC borné 16 kHz (v0.6.2)
│   │   ├── WakeWordManager (.h/.cpp)      # Machine à états & cycle de vie Wake Word (v0.6.2)
│   │   ├── TestWakeWordEngine.h           # Moteur de test déterministe simulé (v0.6.2)
│   │   ├── WakeNetEngine.h                # Adaptateur ESP-SR WakeNet10 wn10_fr_bonjouresp (v0.6.2)
│   │   ├── IVoiceProvider.h               # Interface unifiée des fournisseurs vocaux
│   │   ├── GeminiVoiceProvider (.h/.cpp)  # Adaptateur Gemini Live -> IVoiceProvider
│   │   ├── OpenAIRealtimeClient (.h/.cpp) # Client natif OpenAI Realtime WebSocket TLS
│   │   ├── AudioResampler (.h/.cpp)       # Rééchantillonneur linéaire 16k -> 24k
│   │   ├── OpenAIRoots.h                  # Bundle certificats TLS DigiCert pour OpenAI
│   │   ├── GoogleRoots.h                  # Bundle certificats Google Trust Services
│   │   ├── ContinuousListeningController (.h/.cpp) # Contrôleur autonome de dictée continue
│   │   ├── VoiceActivityDetector.h        # Détecteur VAD local 16 kHz
│   │   ├── GeminiLiveClient (.h/.cpp)     # Client WebSocket BidiGenerateContent
│   │   ├── LiveSessionController (.h/.cpp)# Automate de session
│   │   ├── ConversationController (.h/.cpp)# Contrôleur de conversation bidirectionnelle
│   │   ├── LiveAudioPlayer (.h/.cpp)      # Lecteur DMA PCM 24 kHz (TTS / Réponse)
│   │   ├── LiveAudioStreamer (.h/.cpp)    # Streamer micro PCM 16 kHz (STT + capture locale hors ligne)
│   │   ├── WiFiManager (.h/.cpp)          # Gestionnaire Wi-Fi & SNTP
│   │   ├── Utf8Utils (.h/.cpp)            # Validation & sanitisation UTF-8 RFC 3629
│   │   └── AudioTraceLog (.h/.cpp)        # Journaliseur d'événements audio
│   └── hal/                     # Couche d'abstraction matérielle
│       ├── IAudioOutput.h                 # Interface de sortie audio
│       ├── IAudioInput.h                  # Interface d'entrée microphone
│       ├── M5AudioOutput (.h/.cpp)        # HAL haut-parleur M5.Speaker
│       ├── M5AudioInput (.h/.cpp)         # HAL microphone M5.Mic (ES7210)
│       └── HardwareManager.h              # Arbitrage I2S centralisé HP / Micro
├── examples/                    # Exemples d'application (16 exemples)
│   ├── VoiceUxLiveDemo/         # NOUVEAU v0.6.4.2 : Pont vocal temps réel STT → Voice UX → TTS
│   ├── VoiceUxDemo/             # NOUVEAU v0.6.4.1 : Fondation Voice UX 100 % hors ligne
│   ├── LocalToolRegistryDemo/   # NOUVEAU v0.6.4 : Démonstrateur Registre Local d'outils hors ligne
│   ├── WakeWordConversation/    # NOUVEAU v0.6.3 : Conversation vocale mains libres (« Bonjour ESP »)
│   ├── WakeWordFoundation/      # NOUVEAU v0.6.2 : Fondation Wake Word hors ligne (sans LVGL ni réseau)
│   ├── OpenAIConversation/      # Conversation avec OpenAI Realtime
│   ├── OpenAIConversationLVGL/  # Conversation OpenAI Realtime avec interface LVGL 9 (orbe vocal, PTT)
│   ├── VoiceDiagnostics/        # Outil officiel de diagnostic (6 onglets dont LATENCY) & CSV
│   ├── MinimalVoiceKit/         # Démonstrateur façade simplifiée multi-modes
│   ├── ContinuousSTT/           # Dictée continue mains libres avec interface M5Unified
│   ├── VoiceSettingsDemo/       # Démonstrateur tactile profils & UTF-8
│   ├── MinimalConversation/     # Application conversation bidirectionnelle (Gemini Mode B)
│   ├── MinimalTTS/              # Exemple PlatformIO TTS sans LVGL
│   ├── OrientationClockVoice/   # Exemple PlatformIO horloge LVGL 9 + TTS
│   ├── MinimalSTT/              # Exemple PlatformIO dictée Push-to-Talk
│   └── MinimalTTS_Arduino/      # Exemple autonome Arduino IDE (.ino)
└── tests/
    ├── run_tests.bat            # Lanceur des tests automatisés MinGW
    ├── test_voicekit_all.cpp    # Suites de tests unitaires natifs (voir CHANGELOG pour les totaux)
    ├── test_voiceux_suites.h    # Suites de tests Voice UX / Live Bridge
    ├── native_mock/             # Simulacres Arduino / WiFi / cJSON pour la compilation native
    └── external-consumer/       # Projet consommateur externe (validation d'installation)
```

---

## 4. Méthodes d'Installation

### Méthode A — PlatformIO depuis GitHub

Cette méthode est recommandée pour les projets professionnels ou basés sur VS Code / PlatformIO.

#### Prérequis
- PlatformIO Core (CLI) ou extension PlatformIO dans Visual Studio Code.
- Câble USB-C connecté à un M5Stack CoreS3.

#### Configuration `platformio.ini`
Ajoutez VoiceKit dans `lib_deps` en ciblant la version Git :

```ini
[env:m5stack-cores3]
platform = espressif32@6.9.0
board = m5stack-cores3
framework = arduino
lib_ldf_mode = deep
board_build.partitions = no_ota.csv
build_unflags = -std=gnu++11
build_flags =
    -std=gnu++17
    -DCORE_DEBUG_LEVEL=1
    -DBOARD_M5STACK_CORES3=1
    -DVOXCORE_AUDIO_MODE=2
lib_deps =
    m5stack/M5CoreS3@1.0.0
    m5stack/M5Unified@0.2.23
    https://github.com/Philippe-AD/VoiceKit.git#v0.6.1
```

> **Note de partitionnement** : Le firmware VoiceKit et ses dépendances occupent environ 1,15 à 1,5 Mo. Utilisez une table de partitions comme `no_ota.csv` (fournie dans le dépôt) offrant au moins 2 à 3 Mo pour la partition application (`app0`).

#### Programme minimal (`src/main.cpp`)

```cpp
#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceKit.h>

VoiceKit voice;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    VoiceKitConfig config;
    config.apiKey = "VOTRE_CLE_API_GEMINI";
    config.wifiSSID = "VOTRE_SSID";
    config.wifiPassword = "VOTRE_MOT_DE_PASSE";
    config.voice = "Aoede";
    config.volume = 180;

    voice.begin(config);

    voice.onReady([]() {
        Serial.println("VoiceKit est prêt !");
    });

    voice.onSpeechFinished([]() {
        Serial.println("Élocution terminée.");
    });
}

void loop() {
    M5.update();
    voice.update();

    if (voice.isReady()) {
        static bool done = false;
        if (!done) {
            voice.say("Bonjour, VoiceKit fonctionne avec succès.");
            done = true;
        }
    }
}
```

#### Commande de compilation
```bash
pio run
# Pour téléverser et afficher la console :
pio run -t upload && pio device monitor -b 115200
```

#### Erreurs courantes & Solutions
- **`fatal error: VoxCoreVoice.h: No such file or directory`** : Vérifiez que `lib_ldf_mode = deep` est défini dans `platformio.ini`.
- **`Echec initialisation memoire LiveAudioPlayer`** : Le CoreS3 requiert la PSRAM active. Assurez-vous que le profil de carte est bien `m5stack-cores3`.

---

### Méthode B — Arduino IDE

Cette méthode permet d'utiliser VoiceKit avec l'environnement classique Arduino IDE 2.x.

#### Prérequis
- Arduino IDE 2.3+ avec le paquet de cartes **esp32** par Espressif (version 2.0.14+ ou 3.x).
- Télécharger l'archive ZIP de VoiceKit depuis GitHub :  
  `https://github.com/Philippe-AD/VoiceKit/archive/refs/tags/v0.6.1.zip`

#### Étapes d'installation
1. Ouvrez Arduino IDE.
2. Menu : **Croquis** → **Inclure une bibliothèque** → **Ajouter la bibliothèque .ZIP...**
3. Sélectionnez le fichier `VoiceKit-0.6.1.zip` téléchargé.
4. Ouvrez le Gestionnaire de bibliothèques (**Outils** → **Gérer les bibliothèques**) et installez :
   - `M5Unified` (version >= 0.2.23)
   - `M5CoreS3` (version >= 1.0.0)

#### Configuration indispensable de l'IDE
Dans le menu **Outils**, configurez impérativement :
- **Type de carte** : `M5Stack-CoreS3` (ou `ESP32S3 Dev Module`)
- **PSRAM** : `OPI PSRAM`  *(CRITIQUE : indispensable pour allouer le buffer audio 24 kHz en PSRAM)*
- **Partition Scheme** : `Huge APP (3MB No OTA)` ou `16M Flash (3MB APP/9.9MB FATFS)`
- **Flash Size** : `16MB (128Mb)`

#### Exemple prêt à l'emploi
Ouvrez directement dans Arduino IDE :  
**Fichier** → **Exemples** → **VoiceKit** → **MinimalTTS_Arduino** (ou `examples/MinimalTTS_Arduino/MinimalTTS_Arduino.ino`).

Renseignez votre SSID Wi-Fi et votre clé API Gemini, puis cliquez sur **Téléverser**.

---

### Méthode C — Développement Local

Cette méthode permet de modifier VoiceKit et de tester immédiatement vos évolutions dans une application hôte sans passer par GitHub.

#### Prérequis
- Dépôt VoiceKit cloné en local sur votre poste.
- Votre projet consommateur PlatformIO situé sur la même machine.

#### Configuration `platformio.ini`
Utilisez la directive `symlink://` de PlatformIO :

```ini
[env:m5stack-cores3]
platform = espressif32@6.9.0
board = m5stack-cores3
framework = arduino
lib_ldf_mode = deep
board_build.partitions = no_ota.csv
build_flags =
    -std=gnu++17
    -DVOXCORE_AUDIO_MODE=2
lib_deps =
    m5stack/M5CoreS3@1.0.0
    m5stack/M5Unified@0.2.23
    symlink://../../VoiceKit    # Chemin relatif vers la racine de VoiceKit
```

Grâce à la structure `src/` conforme au standard Arduino 1.5, **aucun paramètre `-I../../include` n'est nécessaire**. PlatformIO découvre automatiquement les en-têtes et compile les sources du moteur.

---

## 5. Configuration & Choix du Fournisseur

VoiceKit permet de sélectionner le fournisseur vocal désiré directement via `VoiceKitConfig` :

### 5.1 Configuration Google Gemini Live (Fournisseur par défaut)
```cpp
VoiceKitConfig config;
config.provider = VoiceProviderType::GEMINI; // Optionnel : GEMINI est la valeur par défaut
config.apiKey = GEMINI_API_KEY;
config.model = "models/gemini-3.8-live";      // Modèle officiel Gemini Live
config.voice = "Aoede";                      // Aoede, Puck, Charon, Fenrir, Kore
config.language = "fr-FR";
config.wifiSSID = WIFI_SSID;
config.wifiPassword = WIFI_PASS;
```

### 5.2 Configuration OpenAI Realtime API
```cpp
VoiceKitConfig config;
config.provider = VoiceProviderType::OPENAI; // Sélection OpenAI Realtime
config.apiKey = OPENAI_API_KEY;              // Clé secrète sk-...
config.model = "gpt-realtime-2.1-mini";       // Modèle officiel Realtime
config.voice = "alloy";                      // alloy, echo, shimmer, etc.
config.language = "fr-FR";                   // Instructions et transcriptions en français
config.wifiSSID = WIFI_SSID;
config.wifiPassword = WIFI_PASS;
```

> **Sécurité absolue des clés** : Ne committez jamais vos clés secrètes d'API dans votre dépôt Git. Utilisez un fichier séparé `secrets.h` ignoré par `.gitignore`. VoiceKit masque automatiquement toutes les clés dans les journaux console.

---

## 6. Nouvelle API Simplifiée (`VoiceKit.h`)

La version **v0.5.0** introduit une façade unifiée et haut niveau optionnelle `VoiceKit`, accessible via `#include <VoiceKit.h>`. Elle permet de piloter l'intégralité des fonctionnalités vocales à l'aide d'un unique objet :

```cpp
#include <VoiceKit.h>

VoiceKit voice;
```

### 6.1 Vue d'ensemble des méthodes

| Méthode | Description | Retour | Caractère |
| :--- | :--- | :---: | :---: |
| `bool begin(config)` | Initialise le matériel, le Wi-Fi, SNTP et la session du fournisseur choisi. | `bool` | Asynchrone / Non bloquant |
| `void update()` | Traite la machine d'état et les contrôleurs audio. À appeler dans `loop()`. | `void` | Non bloquant (< 1 ms) |
| `bool say(text)` | Déclenche la synthèse vocale (TTS) de la phrase donnée. | `bool` | Non bloquant |
| `bool listen()` | Démarre la reconnaissance vocale manuelle (STT Push-to-Talk). | `bool` | Non bloquant |
| `bool stopListening()` | Arrête l'acquisition micro et attend la transcription finale. | `bool` | Non bloquant |
| `void cancelListening()` | Annule immédiatement l'écoute sans attendre de transcription. | `void` | Immédiat |
| `bool startContinuousListening()` | Active la dictée continue mains libres avec détection automatique de fin de parole. | `bool` | Non bloquant |
| `bool stopContinuousListening()` | Désactive la dictée continue et replace la façade en mode `IDLE`. | `bool` | Immédiat |
| `bool pauseContinuousListening()` | Met en pause temporaire la dictée continue. | `bool` | Immédiat |
| `bool resumeContinuousListening()` | Reprend la dictée continue après une pause. | `bool` | Non bloquant |
| `bool startConversation()` | Active le mode conversationnel bidirectionnel natif (Gemini Live ou OpenAI Realtime). | `bool` | Sécurisé / Non bloquant |
| `bool stopConversation()` | Désactive le mode conversationnel et replace la façade en mode `IDLE`. | `bool` | Immédiat |
| `void stop()` | Interrompt immédiatement toute opération en cours (TTS, écoute, conversation). | `void` | Immédiat |
| `bool isReady() const` | Indique si le système est prêt à recevoir une commande. | `bool` | Non bloquant |
| `bool isBusy() const` | Indique si une opération vocale (son, micro, réseau) est en cours. | `bool` | Non bloquant |
| `bool isSpeaking() const` | Indique si le haut-parleur restitue du son. | `bool` | Non bloquant |
| `bool isListening() const` | Indique si le microphone est en cours d'enregistrement. | `bool` | Non bloquant |
| `bool isTranscribing() const` | Indique que le micro est coupé et que la transcription finale est en attente. | `bool` | Non bloquant |
| `bool isContinuousListening() const` | Indique si le mode dictée continue est actif. | `bool` | Non bloquant |
| `bool isContinuousListeningPaused() const`| Indique si la dictée continue est en pause. | `bool` | Non bloquant |
| `bool isConversationActive() const` | Indique si le mode conversationnel est actif. | `bool` | Non bloquant |
| `VoiceState getState() const` | Retourne l'état opérationnel interne du moteur VoxCore. | `VoiceState` | Non bloquant |
| `VoiceKitMode getMode() const` | Retourne le mode de la façade (`IDLE`, `CONTINUOUS_LISTENING`, `CONVERSATION`). | `VoiceKitMode` | Non bloquant |
| `const ConnectionMetrics& getConnectionMetrics() const` | Retourne les métriques et temps de connexion mesurés (Wi-Fi, SNTP, WS, Setup, Total READY). | `const ConnectionMetrics&` | Non bloquant |
| `const VoiceLatencyMetrics& getLatencyMetrics() const` | Retourne les métriques de latence vocale détaillées du dernier tour vocal (7 jalons, 6 durées). | `const VoiceLatencyMetrics&` | Non bloquant |
| `void setVadSilenceDurationMs(uint32_t ms)` | Configure le seuil de silence VAD local (800, 1000, 1200 ms). Alias : `setSilenceDurationMs()`. | `void` | Immédiat |
| `uint32_t getVadSilenceDurationMs() const` | Retourne le seuil de silence VAD actuel en ms. Alias : `getSilenceDurationMs()`. | `uint32_t` | Non bloquant |
| `String getLastError() const` | Retourne le dernier message d'erreur survenu. | `String` | Non bloquant |

### 6.2 Événements & Callbacks Unifiés

Les callbacks de la façade `VoiceKit` sont exécutés dans le contexte de la boucle principale `loop()` lors des appels à `voice.update()`, garantissant l'absence de blocage ou d'accès concurrent :

```cpp
// 1. Session prête
voice.onReady([]() {
    Serial.println("[EVENT] VoiceKit est prêt");
});

// 2. Erreur survenue
voice.onError([](const String& err) {
    Serial.printf("[EVENT] Erreur : %s\n", err.c_str());
});

// 3. Début de restitution vocale (TTS ou Conversation)
voice.onSpeechStarted([]() {
    Serial.println("[EVENT] Restitution vocale démarrée");
});

// 4. Fin de restitution vocale (émis exactement une fois par énonciation)
voice.onSpeechFinished([]() {
    Serial.println("[EVENT] Restitution vocale achevée");
});

// 5. Transcription unifiée (STT manuel, ContinuousSTT et Conversation)
voice.onTranscription([](const String& text, bool isFinal) {
    if (isFinal) {
        Serial.printf("[TRANSCRIPTION FINALE] \"%s\"\n", text.c_str());
    } else {
        Serial.printf("[TRANSCRIPTION INTERIM] \"%s\"\n", text.c_str());
    }
});

// 6. Fin d'un tour de conversation
voice.onConversationFinished([]() {
    Serial.println("[EVENT] Tour conversationnel achevé");
});

// 7. Détection locale de mot de réveil (Wake Word Foundation — v0.6.2)
voice.setWakeWordEngine(WakeWordEngineType::TEST); // ou WakeWordEngineType::WAKENET sous IDF >= 5.0
voice.onWakeWordDetected([](const String& word) {
    Serial.println(word); // ex: "Bonjour ESP"
});
voice.enableWakeWord(true);

// 8. Conversation mains libres déclenchée par « Bonjour ESP » (Wake Word Conversation — v0.6.3)
voice.onWakeConvDetected([](const String& word, float conf, bool isSim) {
    Serial.printf("[WAKE CONV] Détection %s : %s\n", isSim ? "SIMULÉE" : "ACOUSTIQUE", word.c_str());
});
voice.onWakeConvConnecting([]() { Serial.println("[WAKE CONV] Connexion au fournisseur..."); });
voice.onWakeConvReady([]()      { Serial.println("[WAKE CONV] Session prête — parlez"); });
voice.onWakeConvListening([]()  { Serial.println("[WAKE CONV] Écoute utilisateur active"); });
voice.onWakeConvSpeaking([]()   { Serial.println("[WAKE CONV] Réponse vocale en cours"); });
voice.onWakeConvEnded([](WakeWordSessionEndReason r) {
    Serial.printf("[WAKE CONV] Fin de session : %s\n", wakeWordSessionEndReasonToString(r));
});
voice.onWakeConvReturnedToWake([]() { Serial.println("[WAKE CONV] Retour à WAKE_ARMED"); });
voice.onWakeConvError([](const String& err) { Serial.printf("[WAKE CONV] Erreur : %s\n", err.c_str()); });
voice.enableWakeWordConversation(true);
```

---

## 7. Registre Local d'Outils (`ToolRegistry` — v0.6.4)

La version **v0.6.4** introduit une infrastructure de registre local de commandes, entièrement autonome (sans Wi-Fi, sans Gemini Live, sans OpenAI Realtime et sans initialisation préalable du moteur vocal) pour ESP32-S3 / M5Stack CoreS3.

### 7.1 Architecture & Composants Découplés

Le registre d'outils est structuré en modules complémentaires dans `src/core/tools/` :
- **`ToolDefinition.h`** : Définition des métadonnées, paramètres typés (`STRING`, `INT`, `FLOAT`, `BOOLEAN`), arguments (`ToolArgs`), valeurs par défaut, politiques de sécurité (`READ_ONLY`, `WRITE`, `SENSITIVE`) et statuts structurés (`ToolExecutionStatus`, `ToolResult`).
- **`ToolValidator.h`** : Validation stricte préalable : existence de la commande, présence des paramètres obligatoires, conformité des types, respect des bornes minimales/maximales, et rejet catégorique des paramètres inconnus (sauf autorisation explicite `allowUnknownParams`).
- **`ToolExecutor.h`** : Exécuteur d'outils avec protection anti-réentrance concurrente (`BUSY`), interception sûre des exceptions et gestion d'une file d'exécution différée (`enqueueDeferred`, `update`).
- **`ToolEvents.h`** : Cycle de vie complet des commandes sous forme d'événements observables (`TOOL_REQUESTED`, `TOOL_STARTED`, `TOOL_COMPLETED`, `TOOL_FAILED`, `TOOL_CONFIRMATION_REQUIRED`), prêt pour l'intégration UI (VoiceKit-SmartUI).
- **`ToolRegistry.h / .cpp`** : Registre central orchestrant l'enregistrement (capacité initiale 16 commandes configurables, rejet des doublons et déclarations invalides), la découverte, la validation, les autorisations et le cycle de confirmation sensible.

### 7.2 Niveaux de Sécurité & Confirmation Sensible

1. **`READ_ONLY`** : Lecture d'état sans modification (ex. `system.get_status`).
2. **`WRITE`** : Modification d'état autorisée sans confirmation supplémentaire (ex. `audio.set_volume`, `display.set_brightness`).
3. **`SENSITIVE`** : Opération critique (ex. `system.factory_reset`, effacement mémoire). **Une commande sensible ne s'exécute jamais automatiquement**. Le registre génère un jeton sécurisé unique (`CONF-xxxxxxxx`) avec expiration paramétrable (30 secondes par défaut) et protection stricte contre le rejeu. L'exécution est débloquée uniquement par l'appel explicite de `executeWithConfirmation("commande", args, token)`.

### 7.3 Exemple d'Utilisation C++

```cpp
#include <ToolRegistry.h>

ToolRegistry registry;

// 1. Déclaration d'une commande de lecture
ToolDefinition statusTool;
statusTool.id = "system.get_status";
statusTool.description = "Retourne l'état du CoreS3 (batterie, mémoire)";
statusTool.security = ToolSecurityPolicy::READ_ONLY;
statusTool.handler = [](const ToolArgs& args) -> ToolResult {
    return ToolResult::ok("{\"battery\": 98, \"free_psram\": 4194304}");
};
registry.registerTool(statusTool);

// 2. Déclaration d'une commande d'écriture avec paramètre borné
ToolDefinition volTool;
volTool.id = "audio.set_volume";
volTool.description = "Règle le volume sonore (0-100)";
volTool.security = ToolSecurityPolicy::WRITE;
volTool.parameters.push_back(
    ToolParam::requiredInt("volume", "Niveau sonore en pourcent")
        .withRange(0, 100)
);
volTool.handler = [](const ToolArgs& args) -> ToolResult {
    int vol = args.getInt("volume");
    // Appliquer au matériel...
    return ToolResult::ok("Volume réglé à " + String(vol) + "%");
};
registry.registerTool(volTool);

// 3. Exécution d'une commande
ToolArgs args;
args.set("volume", 75);
ToolResult res = registry.execute("audio.set_volume", args);
if (res.isSuccess()) {
    Serial.println(res.output);
}
```

### 7.4 Méthodes Publiques Clés

| Méthode | Description |
| :--- | :--- |
| `bool registerTool(const ToolDefinition& def)` | Enregistre une commande (vérifie validité, unicité et capacité max). |
| `bool unregisterTool(const String& id)` | Désenregistre une commande et libère son emplacement. |
| `size_t getToolCount() const` | Retourne le nombre d'outils enregistrés. |
| `bool hasTool(const String& id) const` | Vérifie l'existence d'une commande par son identifiant. |
| `const ToolDefinition* findTool(const String& id) const` | Recherche une commande pour consulter sa description et ses paramètres. |
| `std::vector<String> getToolIds() const` | Liste l'ensemble des identifiants des commandes enregistrées. |
| `ToolResult execute(const String& id, const ToolArgs& args)` | Valide et exécute une commande de manière synchrone et sécurisée. |
| `ToolResult executeWithConfirmation(id, args, token)` | Valide et exécute une commande sensible avec son jeton de confirmation. |
| `bool cancelConfirmation(const String& token)` | Révoque immédiatement un jeton de confirmation en attente. |
| `void setEventListener(ToolEventCallback cb)` | Souscrit aux événements du cycle de vie des outils. |
| `void setAuthorizationChecker(checker)` | Définit un hook d'autorisation personnalisée avant toute exécution. |
| `void update()` | Traite la file des commandes différées et nettoie les jetons expirés. |

---

## 8. Contrôleur Réutilisable `ContinuousListeningController`

Intégré au cœur de la bibliothèque (`src/core/ContinuousListeningController.h` / `.cpp`), ce contrôleur abstrait toute la complexité d'une dictée vocale continue mains libres :
- **Détection VAD Intégrée** : Analyse en direct les blocs PCM 16 kHz capturés par le microphone matériel.
- **Paramètres Temporels Déterministes** :
  - **Attaque parole** : 200 ms consécutifs au-dessus du seuil haut.
  - **Silence de clôture** : 1 200 ms de silence consécutif post-parole.
  - **Durée maximale de segment** : 30 secondes (bornage strict anti-blocage).
  - **Délai de réarmement automatique** : 400 ms entre deux segments.
- **Zéro Double Capture Microphone** : Se branche directement sur les chunks audio du streamer existant via `_voice->onAudioChunk()`.
- **Règle Anti-Hallucination Absolue** : Si le délai de transcription finale expire sans réponse du serveur, aucun texte factice n'est généré.
- **Support des Pauses & Reprises** : `pause()` et `resume()` permettent de suspendre la dictée sans déconnecter la session WebSocket TLS.

---

## 9. Référence de l'API Historique (`VoxCoreVoice`)

Pour assurer une rétrocompatibilité ascendante stricte à 100%, la classe `VoxCoreVoice` (`VoxCoreVoice.h`) reste intégralement disponible avec toutes ses méthodes publiques historiques :

### API Synthèse Vocale (TTS)
- `bool begin(const VoiceKitConfig& config)`
- `bool say(const String& text)`
- `void update()`
- `void stop()`
- `bool isReady() const`, `bool isBusy() const`, `bool isSpeaking() const`
- `void setVolume(uint8_t volume)`

### API Reconnaissance Vocale (STT)
- `bool startListening()`, `bool stopListening()`, `void cancelListening()`
- `bool isListening() const`, `bool isTranscribing() const`
- `void onTranscription(TranscriptionCallback cb)`
- `void onListeningStarted(VoidCallback cb)`, `void onListeningFinished(VoidCallback cb)`
- `uint32_t getAudioLevel() const`, `void onAudioChunk(AudioChunkCallback cb)`

### API Conversation Vocale Bidirectionnelle
- `bool enableConversation(bool enable)`
- `bool isConversationEnabled() const`, `bool isModeSwitchPending() const`
- `void onUserText(TranscriptionCallback cb)`, `void onAssistantText(AssistantTextCallback cb)`
- `void onConversationFinished(VoidCallback cb)`, `void onConversationTurnMetrics(TurnMetricsCallback cb)`
- `const ConversationMetrics& getConversationMetrics() const`
- `const VoiceLatencyMetrics& getLatencyMetrics() const`
- `void setVadSilenceDurationMs(uint32_t ms)`, `uint32_t getVadSilenceDurationMs() const`

### API Fournisseurs & Modèles (v0.6.0)
- `IVoiceProvider* getActiveProvider() const`
- `VoiceProviderType getProviderType() const`
- `GeminiLiveClient* getClient()` *(rétrocompatibilité)*

### API Profil Vocal & Langue
- `bool setVoiceProfile(voice, lang, style, tone, accent)`
- `bool setVoice(const String& voice)`, `bool setLanguage(const String& lang)`
- `bool setStyle(VoiceStyle / const String&)`, `bool setTone(VoiceTone / const String&)`, `bool setAccent(const String& accent)`
- `bool isConnecting() const`, `bool isProfileChangePending() const`

---

## 10. Exemples Inclus

La bibliothèque fournit 16 exemples complets prêts à l'emploi :

1. **`examples/VoiceUxLiveDemo/`** (PlatformIO) — **NOUVEAU v0.6.4.2** :  
   Démonstration matérielle du pont vocal temps réel `VoiceUxLiveBridge` (STT → `VoiceUxController` → `ReplyMemory` → TTS). Interface M5Unified/M5GFX (320 × 240) sans LVGL, boutons tactiles `PARLER` (PTT) et `STOP`, console série avec mode hybride voix réelle (`[STT][REAL]`) et simulation (`[STT][SIM]`).

2. **`examples/VoiceUxDemo/`** (PlatformIO) — **NOUVEAU v0.6.4.1** :  
   Démonstrateur de la fondation Voice UX locale (`TextGate`, `VoiceIntentRouter`, `ReplyMemory`, `VoiceUxController`), 100 % hors ligne sans Wi-Fi, sans clé API et sans capture audio, avec affichage M5GFX et pilotage série interactif (commandes `T`, `M`, `H`, `C`, `D`).

3. **`examples/LocalToolRegistryDemo/`** (PlatformIO) — **NOUVEAU v0.6.4** :  
   Démonstrateur autonome de registre local d'outils sur M5Stack CoreS3 **sans Wi-Fi, sans Gemini Live ni OpenAI**, avec écran de diagnostic M5GFX (320 × 240) et menu interactif série. Enregistre trois commandes matérielles réelles (`system.get_status`, `audio.set_volume`, `display.set_brightness`) et une commande sensible d'exemple (`system.factory_reset`) avec validation stricte de types/bornes et confirmation par jeton à usage unique.

4. **`examples/WakeWordConversation/`** (PlatformIO) — **NOUVEAU v0.6.3** :  
   Application de conversation vocale entièrement mains libres sur M5Stack CoreS3 (affichage M5GFX 320 × 240, sans LVGL), déclenchée par le mot de réveil local « Bonjour ESP ». Affiche clairement les 9 états en français (`EN ATTENTE — « Bonjour ESP »`, `MOT DE RÉVEIL DÉTECTÉ`, `CONNEXION GEMINI / OPENAI`, `PRÊT — PARLEZ`, `ÉCOUTE`, `RÉPONSE VOCALE`, `FIN DE CONVERSATION`, `RETOUR EN ÉCOUTE`, `ERREUR / RÉCUPÉRATION`), les compteurs de sessions, détections, reconnexions et erreurs, et distingue strictement une détection acoustique réelle (`WAKENET`) d'une détection simulée (`TEST`).

5. **`examples/WakeWordFoundation/`** (PlatformIO) — **NOUVEAU v0.6.2** :  
   Exemple autonome sur M5Stack CoreS3 **sans écran LVGL et sans connexion réseau**. Valide la capture PCM 16 kHz / 16 bits partagée du microphone ES7210, le pont audio borné `WakeWordAudioBridge`, la machine à états `WakeWordManager` (`DISABLED` $\to$ `INITIALIZING` $\to$ `ARMED` $\to$ `DETECTED` $\to$ `SUSPENDED` $\to$ `ARMED`), l'état explicite d'indisponibilité de `WAKENET` sous IDF 4.4 et le moteur de test déterministe `TEST`.
6. **`examples/OpenAIConversation/`** (PlatformIO) :  
   Démonstration épurée et headless de conversation vocale bidirectionnelle en français avec l'API **OpenAI Realtime** (`gpt-realtime-2.1-mini`). Acquisition microphone ES7210 16 kHz rééchantillonnée à chaud en 24 kHz via `AudioResampler`, streaming Base64, VAD local, restitution haut-parleur AW88298 24 kHz et interruption instantanée STOP sur appui tactile.
7. **`examples/OpenAIConversationLVGL/`** (PlatformIO) :  
   Conversation vocale OpenAI Realtime avec interface tactile dark mode **LVGL 9** (orbe vocal animé, cartes de transcription, bouton Push-to-Talk), polices françaises intégrées et synchronisation inter-threads sans allocation dynamique.
8. **`examples/VoiceDiagnostics/`** (PlatformIO) :  
   Outil officiel de diagnostic et de télémétrie vocale pour M5Stack CoreS3. Interface tactile M5Unified (320 × 240) à 6 onglets (`SYS`, `AUDIO`, `LAT`, `NET`, `CONN`, `EVT`). Journalisation série `[LATENCY]` et export CSV `[LATENCY_CSV]`.
9. **`examples/MinimalVoiceKit/`** (PlatformIO) :  
   Démonstrateur de référence de la façade simplifiée `VoiceKit`. Permet de tester successivement les 4 modes vocaux (TTS, STT manuel, dictée continue et conversation) sur l'écran tactile du M5Stack CoreS3 (320 × 240) avec diagnostic en temps réel.
10. **`examples/ContinuousSTT/`** (PlatformIO) :  
    Application autonome de dictée vocale mains libres avec interface graphique complète M5Unified (7 états, VU-mètre RMS, compte à rebours de silence, bouton Pause/Reprendre, accents français UTF-8).
11. **`examples/VoiceSettingsDemo/`** (PlatformIO) :  
    Démonstrateur tactile multi-profils et validation UTF-8 pour M5Stack CoreS3. Permet de faire défiler et tester 5 scénarios complets avec accents français, allemand et anglais.
12. **`examples/MinimalConversation/`** (PlatformIO) :  
    Application conversationnelle vocale bidirectionnelle native (Mode B) avec Gemini Live pour M5Stack CoreS3. Bouton tactile dynamique (« PARLER » / « STOP ») et cartes d'affichage.
13. **`examples/MinimalTTS/`** (PlatformIO) :  
    Validation physique TTS minimale sans LVGL. Démonstration de synthèse vocale au démarrage puis déclenchement tactile sur l'écran.
14. **`examples/MinimalSTT/`** (PlatformIO) :  
    Démonstration de reconnaissance vocale Push-to-Talk STT pour M5Stack CoreS3 avec transcriptions intérimaires et finales.
15. **`examples/OrientationClockVoice/`** (PlatformIO) :  
    Horloge dynamique complète avec **LVGL 9**, accéléromètre **BMI270** (rotation 360°), horloge RTC **BM8563**, et annonce vocale de l'heure en français.
16. **`examples/MinimalTTS_Arduino/`** (Arduino IDE) :  
    Croquis autonome `.ino` prêt à l'emploi avec documentation pas-à-pas des menus Arduino IDE.

---

## 11. Garanties de Non-Régression & Sécurité

La version **v0.6.4.2** garantit une non-régression absolue sur l'ensemble des modules préexistants :
- **Intégrité absolue du moteur audio VoxCore** : Aucune réduction arbitraire du buffer circulaire PSRAM (120 000 échantillons @ 24 kHz) ni des marges DMA (160 ms). Préservation intégrale de la tâche FreeRTOS Mode 2 et du vidage exact sans coupure de syllabe.
- **Validation Repère Acoustique (« alpha tango terminé »)** : Vérification rigoureuse que les derniers mots émis (par Gemini Live comme par OpenAI) sont intégralement restitués sans troncature, y compris lors de la réception de `response.done`.
- **Rééchantillonnage 16k $\to$ 24k en arithmétique entière** : Exécution en < 15 µs sur ESP32-S3 Core 1 sans blocage de la boucle de capture audio.
- **Zéro identifiant en clair** : Aucun secret, SSID Wi-Fi ni clé API n'est publié dans le dépôt. Masquage systématique des clés dans les logs.
- **Suite de tests automatisés** : 138 suites de tests unitaires et 2 579 tests validés avec 0 échec (`tests/test_voicekit_all.exe`).
- **Autonomie du Registre Local** : Aucune dépendance réseau, aucune allocation audio ni tâche FreeRTOS superflue pour `ToolRegistry`.
- **Compilations d'exemples validées à 100%** : Tous les exemples cibles PlatformIO compilent sans aucune erreur.

Consultez les rapports techniques regroupés dans [`docs/rapports/`](docs/rapports/) — notamment le [Rapport Technique v0.6.4.2](docs/rapports/RAPPORT_v0.6.4.2.md), le [Rapport Technique v0.6.4.1](docs/rapports/RAPPORT_v0.6.4.1.md), le [Rapport Technique v0.6.4](docs/rapports/RAPPORT_v0.6.4.md), le [Rapport Technique v0.6.3](docs/rapports/RAPPORT_v0.6.3.md) et le [Rapport de Livraison v0.6.0](docs/rapports/RAPPORT_LIVRAISON_v0.6.0.md) — pour les détails d'architecture, les mesures de compilation et les métriques exposées. L'index complet est disponible dans [docs/README.md](docs/README.md).

---

## 12. Architecture : Registre Local, IA Tool Calling & MCP

Un document d'architecture détaillé est disponible dans le dossier `docs/` :  
**[Architecture : Registre Local d'Outils, Appels d'Outils IA et MCP](docs/architecture_tools_registry_ai_mcp.md)**.

Il clarifie les responsabilités et frontières conceptuelles entre :
1. **Le Registre Local d'Outils (VoiceKit v0.6.4)** : Couche embarquée temps réel sur ESP32, déterministe, synchrone, sans dépendance réseau.
2. **L'Appel d'Outils par l'IA (IA Tool Calling — versions futures)** : Traduction bidirectionnelle de schémas JSON Schema pour Gemini Live (`functionDeclarations`) et OpenAI Realtime (`session.update.tools`), avec boucle d'exécution asynchrone des résultats vers le LLM.
3. **Le Model Context Protocol (MCP — versions futures)** : Protocole de découverte et d'échange client/serveur standardisé via JSON-RPC 2.0 pour agents et outils distribués.

> **Note importante** : VoiceKit v0.6.4 ne prétend pas être un serveur MCP ni déclarer de compatibilité directe avec les schémas d'outils Gemini/OpenAI dans cette version. Il en pose les fondations architecturales rigoureuses.
