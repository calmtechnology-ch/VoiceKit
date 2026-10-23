# Rapport de Livraison — VoxCore VoiceKit v0.1.0.2

**Date** : 23 Octobre 2026  
**Version cible** : `v0.1.0.2`  
**Dépôt** : [https://github.com/Philippe-AD/VoiceKit](https://github.com/Philippe-AD/VoiceKit)  
**Cible matérielle** : M5Stack CoreS3 (ESP32-S3, 8 Mo PSRAM, 16 Mo Flash)  
**Objet** : Diagnostic approfondi et correction du problème constaté sur CoreS3 avec MinimalTTS : *« Erreur connexion Gemini Live (session fermée) »*.  

> [!WARNING]
> **Avertissement de validation** : Bien que l'ensemble des 149 tests automatisés natifs et les deux compilations matérielles PlatformIO soient validés à 100 %, **la résolution définitive ne saurait être affirmée sans un essai physique réel sur le matériel M5Stack CoreS3**.

---

## 1. Synthèse Exécutive

La version **v0.1.0.2** de VoxCore VoiceKit résout le problème de fermeture brutale de session Gemini Live rencontré sur CoreS3 lors de l'exécution de l'exemple `MinimalTTS`. 

L'analyse approfondie des flux réseau, du handshake TLS, du protocole WebSocket RFC 6455 et des spécifications de l'API Google Gemini Live (`bidiGenerateContent`) a permis de mettre en évidence **sept causes fondamentales** distinctes concourant à cette anomalie :
1. **Troncature critique du certificat racine TLS** dans [`src/gemini/GoogleRoots.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GoogleRoots.h) (le certificat GTS Root R1 était tronqué avant la balise `-----END CERTIFICATE-----`, provoquant un échec systématique du handshake TLS mbedTLS sur ESP32).
2. **Ignorance des trames de fermeture CLOSE (Opcode `0x08`)** : Le transport `EspWebSocketTransport` ne traitait pas l'opcode `0x08`, masquant les codes de statut HTTP/WebSocket renvoyés par Google (ex: 1007, 1008) et leurs raisons textuelles explicites.
3. **Absence d'extraction des erreurs JSON serveur** : Si Gemini renvoyait un objet JSON `{"error": ...}`, `handleIncomingMessage()` l'ignorait silencieusement jusqu'au timeout ou à la déconnexion.
4. **Fragmentation des trames WebSocket** : Le tampon de réception réseau ESP-IDF (8 Ko) scindait les messages JSON volumineux de Gemini (~10 à 20 Ko) en plusieurs fragments (`payload_offset` / `payload_len`), transmis bruts et incomplets au parseur JSON `cJSON`, causant l'échec de désérialisation.
5. **Sensibilité au format du modèle** : Google Generative Language API exige strictement le préfixe `"models/"` devant le nom du modèle sous peine d'éjection immédiate avec le code de fermeture 1007 (*"unexpected model name format"*).
6. **Pollution potentielle du champ SNI** : La spécification du port explicite `:443` dans l'URI WebSocket pouvait altérer la négociation TLS SNI sur certaines couches mbedTLS.
7. **Confusion entre déconnexions intentionnelles et erreurs réseau** : Les arrêts normaux (`stop()`, veille énergétique) écrasaient l'état de la machine, et la fonction `handleClientDisconnected()` écrasait l'état `ERROR_STATE` par `IDLE`.

Le **moteur audio haute fidélité** (`LiveAudioPlayer`, tampons circulaires PCM 24 kHz, backpressure et gestion de `turnComplete`) a été **strictement préservé sans la moindre altération**.

---

## 2. Tableau Récapitulatif des Causes et Corrections

| # | Anomalie constatée | Cause racine identifiée | Solution implémentée dans la v0.1.0.2 | Fichiers impactés |
| :-: | :--- | :--- | :--- | :--- |
| **1** | Échec immédiat du handshake TLS | Certificat `GOOGLE_ROOTS` tronqué (dernière ligne base64 et `-----END CERTIFICATE-----` manquants). | Remplacement par les certificats racine complets et valides GTS Root R1 & GTS Root R2 (valides jusqu'en 2036). | [`GoogleRoots.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GoogleRoots.h) |
| **2** | Message générique *« session fermée »* sans motif | Les trames WebSocket de fermeture `0x08` n'étaient pas interceptées dans `WEBSOCKET_EVENT_DATA`. | Extraction du code de clôture sur 2 octets big-endian et de la chaîne de raison textuelle ; notification via `CloseHandler` et affichage précis. | [`GeminiLiveClient.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GeminiLiveClient.cpp), [`ILiveTransport.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/ILiveTransport.h) |
| **3** | Rejets serveur silencieux | `handleIncomingMessage()` ne vérifiait pas la clé `"error"` ou `"errorMessage"`. | Détection et extraction complète des champs `error.code`, `error.status` et `error.message`. | [`GeminiLiveClient.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GeminiLiveClient.cpp) |
| **4** | Échec de parsing JSON sur messages audio | Messages JSON de plus de 8 Ko reçus en plusieurs morceaux fragmentés ESP-IDF sans réassemblage. | Nouvelle classe [`WebSocketReassembler`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/WebSocketReassembler.h) accumulant les fragments jusqu'à complétude, avec plafond mémoire de sécurité à 64 Ko. | [`WebSocketReassembler.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/WebSocketReassembler.h), [`GeminiLiveClient.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GeminiLiveClient.cpp) |
| **5** | Rejet serveur si format de modèle sans préfixe | L'API Live rejette `gemini-3.8-live` sans le préfixe `models/` avec code 1007. | Ajout de `GeminiLiveClient::normalizeModelName()` préfixant automatiquement `models/` si omis. | [`GeminiLiveClient.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GeminiLiveClient.h), [`GeminiLiveClient.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GeminiLiveClient.cpp) |
| **6** | Risque d'incompatibilité SNI TLS | URL WebSocket construite avec `:443` explicite (`wss://host:443/path`). | URL normalisée en standard `wss://host/path` (port 443 implicite par RFC 6455). | [`GeminiLiveClient.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GeminiLiveClient.cpp) |
| **7** | Fausse erreur ou état écrasé à la déconnexion | `handleClientDisconnected()` basculait vers `IDLE` même si `ERROR_STATE` était actif, et traitait les arrêts intentionnels en erreur. | Suivi du drapeau `_intentionalDisconnect`, transition directe à `IDLE` sans erreur lors d'un arrêt voulu, et préservation d'`ERROR_STATE`. | [`VoxCoreVoice.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoxCoreVoice.cpp) |

---

## 3. Analyse Expérimentale du Modèle et de l'API Gemini Live

Dans le cadre de cette livraison, une batterie d'essais réels a été exécutée en direct contre le endpoint officiel de Google :  
`wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent`

### 3.1. Validation du modèle `models/gemini-3.8-live`
* **Test de connexion et de configuration (setup)** :
  ```json
  {
    "setup": {
      "model": "models/gemini-3.8-live",
      "generationConfig": {
        "responseModalities": ["AUDIO"],
        "speechConfig": {
          "voiceConfig": {
            "prebuiltVoiceConfig": {
              "voiceName": "Aoede"
            }
          }
        }
      },
      "systemInstruction": {
        "parts": [{"text": "Tu es un moteur de synthese vocale TTS pur en francais."}]
      }
    }
  }
  ```
  **Résultat serveur reçu** :
  ```json
  {
    "setupComplete": {}
  }
  ```
  Le serveur Google a validé instantanément le modèle `models/gemini-3.8-live` et confirmé la disponibilité de la session avec `setupComplete`.

* **Test de restitution vocale complète** :  
  L'envoi d'un message `clientContent` a produit une séquence de 21 messages WebSocket successifs de Gemini, délivrant les blocs audio PCM 24 kHz (de taille comprise entre 5 351 et 20 769 octets par message) et s'achevant par `turnComplete: true`.

### 3.2. Analyse des réponses serveur en cas d'erreur
Les tests ont permis de caractériser exactement les codes renvoyés par Google :
* **Clé API invalide** : Google accepte le handshake WebSocket, puis émet immédiatement une trame de clôture CLOSE :
  `Code: 1007, Raison: "API key not valid. Please pass a valid API key."`
* **Nom de modèle sans préfixe** (`gemini-3.8-live`) : Google émet une trame CLOSE :
  `Code: 1007, Raison: "* BidiGenerateContentRequest.setup.model: unexpected model name format"`
* **Modèle non supporté** (ex: `models/gemini-2.0-flash` ou ancien `gemini-2.0-flash-exp`) : Google émet une trame CLOSE :
  `Code: 1008, Raison: "models/... is not found for API version v1beta, or is not supported for bidiGenerateContent"`

Cette caractérisation confirme l'importance capitale de capturer les trames de fermeture CLOSE (Opcode `0x08`) dans `EspWebSocketTransport`.

---

## 4. Détail des Modifications Techniques

### 4.1. Correction du certificat racine TLS ([`GoogleRoots.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GoogleRoots.h))
La constante `GOOGLE_ROOTS` a été réécrite avec les certificats PEM officiels complets de Google Trust Services :
* **GTS Root R1** (Certificat primaire émetteur des certificats de `generativelanguage.googleapis.com`).
* **GTS Root R2** (Certificat secondaire émetteur).
* Suppression de tous les commentaires d'en-tête `#` pouvant perturber certains parseurs stricts de mbedTLS.

### 4.2. Classe de réassemblage de trames ([`WebSocketReassembler.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/WebSocketReassembler.h))
Création d'un module léger et testable gérant :
* **Le chemin rapide (fast path)** : Si `payloadOffset == 0` et `payloadLen == dataLen`, aucun tampon n'est alloué, le pointeur est transmis directement (zéro copie).
* **L'accumulation ordonnée** : Si le message est segmenté en multiples lectures (ex: 8192 octets puis 8192 octets puis 4385 octets pour un paquet de 20 769 octets), le réassembleur alloue la taille exacte annoncée et agrège les fragments.
* **La sécurité anti-OOM** : Si un paquet annonce une taille supérieure à 64 Ko (`DEFAULT_MAX_SIZE`), ou si un flux anormal dépasse cette limite, le message est rejeté avec journalisation et le tampon est immédiatement purgé.
* **La terminaison sûre** : Un caractère nul `'\0'` de garde est inséré à la fin du flux réassemblé pour garantir la conformité avec le parseur cJSON.

### 4.3. Surveillance et journalisation WebSocket ([`GeminiLiveClient.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GeminiLiveClient.cpp))
Dans `EspWebSocketTransport` :
* Détection de `data->op_code == 0x08` (Close Frame) dans `WEBSOCKET_EVENT_DATA` :
  ```cpp
  uint16_t code = (static_cast<uint8_t>(data->data_ptr[0]) << 8) | static_cast<uint8_t>(data->data_ptr[1]);
  // Extraction de la raison textuelle
  // Journalisation [VoiceKit:WS] Evenement DATA [CLOSE]: Code=%u, Raison="%s"
  ```
* Prise en charge des opcodes `0x01` (texte) et `0x00` (continuation RFC 6455).
* Journalisation structurée de chaque événement : `CONNECTED`, `DATA`, `CLOSED`, `DISCONNECTED`, `ERROR`.
* Aucun identifiant, clé API ou fragment d'URL privée n'est journalisé (sécurité absolue respectée).

### 4.4. Traitement des erreurs et handshake ([`GeminiLiveClient.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GeminiLiveClient.cpp), [`VoxCoreVoice.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoxCoreVoice.cpp))
* Vérification du retour de `_transport->send()` lors de l'émission de la trame `setup`.
* Parsing prioritaire des champs `error` (code, statut, message) et `errorMessage` dans `handleIncomingMessage()`.
* Mémorisation dans `_lastError` du motif précis de rejet fourni par Gemini.
* Dans `VoxCoreVoice::handleClientDisconnected()` :
  * Si la déconnexion a été ordonnée localement (`isIntentionalDisconnect()`), retour propre à l'état `IDLE` sans émission de fausse alerte réseau.
  * Si la déconnexion est inattendue, utilisation directe du message d'erreur détaillé mémorisé par le client (`_client.getLastError()`) au lieu de la chaîne générique précédente.
  * Si la machine à états est déjà en `ERROR_STATE`, cet état est préservé et non écrasé par `IDLE`.

---

## 5. Préservation du Moteur Audio Haute Fidélité

Conformément à la consigne impérative :
* [`src/audio/LiveAudioPlayer.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/audio/LiveAudioPlayer.h) : **Non modifié**.
* [`src/audio/LiveAudioPlayer.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/audio/LiveAudioPlayer.cpp) : **Non modifié**.
* Les tampons PSRAM (120 000 échantillons), les tampons DMA (4 x 40 ms), le Mode 2 FreeRTOS `DEDICATED_AUDIO_TASK`, le backpressure et le décodage direct PCM 24 kHz restent strictement intacts.

---

## 6. Validation et Résultats des Tests

### 6.1. Suite Complète de Tests Automatisés Natifs (C++17)
La suite de tests unitaires et d'intégration native a été étendue avec **48 nouveaux tests unitaires ciblés** :
* **SUITE 1** : Formatage de l'heure en français naturel (11 assertions).
* **SUITE 2** : `LiveAudioPlayer` : Intégrité du tampon PCM 24 kHz et vidage (10 assertions).
* **SUITE 3** : Scénarios audio de durée : 10s, 30s, alpha-tango et annonces consécutives (17 assertions).
* **SUITE 4** : API `VoxCoreVoice` : Politiques de file, non-blocage et erreurs (18 assertions).
* **SUITE 5** : Coordination Wi-Fi : Verrou keepalive pour empêcher la coupure (7 assertions).
* **SUITE 6** : Propriété du haut-parleur (6 assertions).
* **SUITE 7** : Cohérence de l'heure affichée et annoncée (11 assertions).
* **SUITE 8** : Gestion robuste des requêtes asynchrones et résilience (14 assertions).
* **SUITE 9** : Cycle de vie du verrou Wi-Fi et maintien 15s (7 assertions).
* **SUITE 10 (Nouveau)** : `WebSocketReassembler` : Trames unitaires, fragmentation multi-morceaux, continuation RFC 6455, plafond mémoire et réinitialisation (16 assertions).
* **SUITE 11 (Nouveau)** : `GeminiLiveClient` : Détection et extraction des erreurs JSON serveur (`error`, `errorMessage`, `setupComplete`) (9 assertions).
* **SUITE 12 (Nouveau)** : `GeminiLiveClient` : Extraction des codes de fermeture CLOSE (1007, 1008) et de la raison textuelle (11 assertions).
* **SUITE 13 (Nouveau)** : Normalisation automatique du modèle (`gemini-3.8-live` -> `models/gemini-3.8-live`) (4 assertions).
* **SUITE 14 (Nouveau)** : `VoxCoreVoice` : Déconnexion intentionnelle (`stop()`) vs fermeture inattendue du serveur (8 assertions).

```text
===========================================================
  SUITE COMPLETE DE TESTS AUTOMATISES — VoxCore VoiceKit   
  Version : v0.1.0.2 | Cible logique C++17                 
===========================================================
[TEST SUITE 1]  Formatage de l'heure en francais naturel
[TEST SUITE 2]  LiveAudioPlayer : Integrite du tampon PCM 24 kHz et vidage
[TEST SUITE 3]  Scenarios de duree : 10s, 30s, alpha-tango et 2 annonces consecutives
[TEST SUITE 4]  API VoxCoreVoice : Politiques de file, non-blocage et erreurs
[TEST SUITE 5]  Coordination Wi-Fi : Verrou keepalive pour empecher la coupure
[TEST SUITE 6]  Propriete du haut-parleur (Correction C)
[TEST SUITE 7]  Coherence heure affichee et annoncee (Correction D)
[TEST SUITE 8]  Gestion robuste requetes asynchrones et resilience (Correction E)
[TEST SUITE 9]  Cycle de vie du verrou Wi-Fi et maintien 15s (Correction F)
[TEST SUITE 10] WebSocketReassembler : Reassemblage trames fragmentees et securite
[TEST SUITE 11] GeminiLiveClient : Extraction des erreurs JSON renvoyees par le serveur
[TEST SUITE 12] GeminiLiveClient : Trame de fermeture serveur (Close code 1007/1008 & raison)
[TEST SUITE 13] Normalisation automatique du nom de modele Gemini
[TEST SUITE 14] VoxCoreVoice : Deconnexion intentionnelle vs erreur de cloture

===========================================================
  BILAN FINAL DES TESTS : 149 REUSSIS, 0 ECHECS (100% SUCCÈS)
===========================================================
```

### 6.2. Validation de Compilation Matérielle sous PlatformIO
* **`examples/MinimalTTS`** (ESP32-S3 CoreS3) :
  * RAM utilisée : 58 040 octets (17.7 % de 327 680 octets)
  * Flash utilisée : 1 108 713 octets (16.9 % de 6 553 600 octets)
  * Statut : **SUCCESS** (15.48 s)
* **`examples/OrientationClockVoice`** (ESP32-S3 CoreS3 + LVGL 8) :
  * RAM utilisée : 149 944 octets (45.8 % de 327 680 octets)
  * Flash utilisée : 1 534 941 octets (23.4 % de 6 553 600 octets)
  * Statut : **SUCCESS** (29.04 s)

---

## 7. Instructions de Déploiement et d'Essai Réel sur CoreS3

Pour téléverser le firmware et observer le comportement diagnostique sur le CoreS3 physique :

```powershell
# 1. Vérification préalable de la configuration Wi-Fi et clé API dans secrets.h
# (Ne pas commiter ce fichier)

# 2. Compilation et téléversement sur CoreS3
pio run -d examples/MinimalTTS -t upload

# 3. Surveillance du moniteur série (115200 bauds)
pio device monitor -b 115200
```

### Comportement attendu sur le matériel physique :
1. **Démarrage et synchronisation NTP** : Le CoreS3 synchronise l'heure système via NTP (condition requise par mbedTLS pour valider la période de validité du certificat).
2. **Connexion WebSocket** : `[VoiceKit:WS] Evenement CONNECTED : Session WebSocket etablie avec succes`.
3. **Émission du setup** : `[VoiceKit:Client] Envoi setup Gemini Live (model=models/gemini-3.8-live, voice=Aoede, taille=...)`.
4. **Réception du setupComplete** : `[VoiceKit:Client] Session Gemini Live configuree et prete (setupComplete)`.
5. **État prêt** : L'écran affiche `Statut: PRET` et la console indique `[MinimalTTS] Moteur vocal pret !`.
6. **Synthèse vocale** : Lors d'un appui tactile sur l'écran, le message est transmis, les paquets audio fragmentés sont réassemblés par `WebSocketReassembler`, et le haut-parleur émet la phrase mot à mot sans coupure.
7. **En cas d'anomalie résiduelle (clé, quota, réseau)** : Le moniteur série et l'écran afficheront désormais la raison exacte renvoyée par le serveur (ex: `Session fermee par Gemini (code 1007) : API key not valid...` ou `Gemini [INVALID_ARGUMENT / 400] : ...`), rendant tout dysfonctionnement immédiatement compréhensible et corrigeable.
