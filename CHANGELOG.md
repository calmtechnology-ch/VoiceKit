# Changelog — VoxCore VoiceKit

Toutes les modifications notables apportées à ce projet sont documentées dans ce fichier.

Le format est basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.0.0/)
et ce projet adhère au [Semantic Versioning](https://semver.org/lang/fr/).

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
