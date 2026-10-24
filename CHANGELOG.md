# Changelog — VoxCore VoiceKit

Toutes les modifications notables apportées à ce projet sont documentées dans ce fichier.

Le format est basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.0.0/)
et ce projet adhère au [Semantic Versioning](https://semver.org/lang/fr/).

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
