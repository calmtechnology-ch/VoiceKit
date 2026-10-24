# Rapport de Livraison — VoiceKit v0.3.0

> **Date** : 24 Octobre 2026  
> **Version** : v0.3.0  
> **Type de version** : Évolution majeure — Ajout de la reconnaissance vocale STT (Speech-to-Text) & Préservation intégrale TTS v0.2.1  
> **Dépôt** : `https://github.com/Philippe-AD/VoiceKit`  
> **Référence stable précédente** : tag `v0.2.1`  
> **Cible matérielle** : M5Stack CoreS3 (ESP32-S3FN8, ES7210 I2S Mic, AW88298 I2S Speaker, ILI9342C LCD)  

---

## 1. Synthèse Exécutive

La version **VoiceKit v0.3.0** enrichit VoiceKit de la **reconnaissance vocale continue STT (Speech-to-Text)** connectée au protocole Gemini Live WebSocket (`models/gemini-3.8-live`), tout en garantissant une **stricte non-régression** sur le moteur de synthèse vocale (TTS) validé en v0.2.1.

Les composants matériels et logiciels éprouvés issus de **VoxCore** et **VoxTranscribe** ont été directement réutilisés et adaptés sans introduire de duplication inutile de code :
- **Capture microphone CoreS3** : Acquisition I2S PCM 16 kHz 16-bit mono via `M5AudioInput`.
- **Arbitrage I2S matériel** : `HardwareManager` gère la commutation exclusive entre le microphone et le haut-parleur afin d'éliminer les conflits de bus I2S et l'effet Larsen.
- **Flux temps réel** : `LiveAudioStreamer` assure le double-buffering et l'encapsulation en paquets Base64 pour l'API Gemini Live (`realtimeInput.mediaChunks`).
- **Gestion des transcriptions** : Découplage strict entre transcriptions partielles (*interim*) et transcriptions finales (*final*), avec surveillance par temporisateur d'inactivité (*timeout*). **Règle absolue : aucune transcription finale n'est inventée à partir d'un fragment partiel.**

---

## 2. Preuve de Non-Régression Cryptographique (SHA-256 vs v0.2.1)

Pour certifier mathématiquement qu'aucune altération ni régression n'a été introduite dans les composants critiques du moteur vocal et réseau v0.2.1, chaque fichier a été confronté au tag Git `v0.2.1` :

| Composant | Fichier source | Empreinte SHA-256 (v0.2.1) | Empreinte SHA-256 (v0.3.0) | Conformité v0.2.1 |
| :--- | :--- | :--- | :--- | :---: |
| Client WebSocket TLS | `src/core/GeminiLiveClient.cpp` | `67840B069E82E43FE01744CE941D7B501ACB78C582456679B1BFF0DE4DBF2F84` | `67840B069E82E43FE01744CE941D7B501ACB78C582456679B1BFF0DE4DBF2F84` | **IDENTIQUE** (100%) |
| Lecteur Audio DMA | `src/core/LiveAudioPlayer.cpp` | `AD37EE304790DD288CE12F7826095BEBC0B7E70E1D3D32756FB8B7F3B2362D85` | `AD37EE304790DD288CE12F7826095BEBC0B7E70E1D3D32756FB8B7F3B2362D85` | **IDENTIQUE** (100%) |
| Contrôleur de Session | `src/core/LiveSessionController.cpp` | `C3FF8FA0BB447D5C4E544F4F8737AAD15CBA31E8B9A137E06645F94461402AA7` | `C3FF8FA0BB447D5C4E544F4F8737AAD15CBA31E8B9A137E06645F94461402AA7` | **IDENTIQUE** (100%) |
| Gestionnaire Wi-Fi / SNTP | `src/core/WiFiManager.cpp` | `8468785612E7CD261A645D68658F4AD1D86478C22DF83AAFF7EC55AB67071906` | `8468785612E7CD261A645D68658F4AD1D86478C22DF83AAFF7EC55AB67071906` | **IDENTIQUE** (100%) |
| Journaliseur Temps Réel | `src/core/AudioTraceLog.cpp` | `5A03A20AF3769E5E6D7DC3192D77571BE61F89A4F8A63E012D69D2BC84749515` | `5A03A20AF3769E5E6D7DC3192D77571BE61F89A4F8A63E012D69D2BC84749515` | **IDENTIQUE** (100%) |
| HAL M5.Speaker | `src/hal/M5AudioOutput.cpp` | `5A7E1ECA64A6639FA104D98B8FCE9112D5C7C52CD4097CFEEAFAB1682834DB02` | `5A7E1ECA64A6639FA104D98B8FCE9112D5C7C52CD4097CFEEAFAB1682834DB02` | **IDENTIQUE** (100%) |
| Bundle Certificats TLS | `src/core/GoogleRoots.h` | `03139270D421F26DA9DE461905BF8167BCF9AD77BF719A6DF50E28349113AD2D` | `03139270D421F26DA9DE461905BF8167BCF9AD77BF719A6DF50E28349113AD2D` | **IDENTIQUE** (100%) |

> **Conclusion** : Le transport WebSocket, le handshake TLS avec la PKI Google Trust Services, la pile FreeRTOS et le décodage audio 24 kHz restent 100% préservés bit-à-bit.

---

## 3. Architecture Technique STT

### 3.1 Diagramme des Flux Audio & Réseau

```
+-------------------------------------------------------------------------------+
|                             M5Stack CoreS3                                   |
|                                                                               |
|  [ Microphone I2S ES7210 ]                                                   |
|             |                                                                 |
|             v (16 kHz, 16-bit mono)                                           |
|       M5AudioInput  <---+                                                     |
|             |           | Arbitrage I2S : HardwareManager                     |
|             v           | (prepareMicCapture / restoreSpeakerAfterMic)        |
|     LiveAudioStreamer --+                                                     |
|             |                                                                 |
|             v (Base64 PCM chunks)                                             |
|     GeminiLiveClient -------- TLS WebSocket (wss://...) -----------------+   |
+--------------------------------------------------------------------------|---+
                                                                           |
                                                                           v
                                                            [ Google Gemini Live ]
                                                            models/gemini-3.8-live
                                                                           |
                                                                           v (JSON response)
+--------------------------------------------------------------------------|---+
|     GeminiLiveClient <---------------------------------------------------+   |
|             |                                                                 |
|             v (serverContent.modelTurn.parts[].text)                          |
|     LiveSessionController                                                     |
|             |                                                                 |
|             v                                                                 |
|        VoxCoreVoice                                                           |
|             |                                                                 |
|             +---> onTranscription(text, isFinal)                              |
|             +---> Transition d'état : LISTENING -> TRANSCRIBING -> READY      |
+-------------------------------------------------------------------------------+
```

### 3.2 Arbitrage I2S et Gestion Matérielle
Sur l'ESP32-S3 du M5Stack CoreS3, le microphone (ES7210) et le haut-parleur (AW88298) partagent des ressources matérielles I2S et d'alimentation.
`HardwareManager` applique le séquencement suivant :
1. **Début d'écoute (`prepareMicCapture`)** :
   - Mise en pause / sourdine du haut-parleur (`M5.Speaker.setVolume(0)`).
   - Réveil et initialisation du microphone (`M5.Mic.begin()`).
   - Bascule du flag d'arbitrage matériel pour interdire toute écriture audio concurrente.
2. **Fin d'écoute (`restoreSpeakerAfterMic`)** :
   - Arrêt propre du microphone (`M5.Mic.end()`).
   - Rétablissement du volume et de l'état du haut-parleur.

---

## 4. API Publique VoiceKit v0.3.0

L'interface de `VoxCoreVoice` conserve l'intégralité de l'API TTS v0.2.1 et introduit les méthodes STT non-bloquantes suivantes :

```cpp
// --- Méthodes de capture vocale (STT) ---
bool startListening();              // Démarre l'acquisition micro et le flux vers Gemini
bool stopListening();               // Arrête la capture et bascule en attente de transcription finale
void cancelListening();             // Annule la dictée en cours sans attendre de résultat

bool isListening() const;           // true pendant la capture micro active
bool isTranscribing() const;        // true pendant l'attente du résultat final Gemini

// --- Callbacks d'événements STT ---
void onTranscription(TranscriptionCallback cb);  // cb(const String& text, bool isFinal)
void onListeningStarted(VoidCallback cb);        // Notifié dès l'ouverture du micro
void onListeningFinished(VoidCallback cb);       // Notifié à l'arrêt de la capture
```

### Machine à États Globale

```
+-------------+      say(...)      +---------------+
|    READY    | -----------------> |   SPEAKING    |
+-------------+                    +---------------+
     |   ^                                |
     |   |                                | startListening() [interruption PTT]
     |   | transcription finale / timeout v
     |   +--------------------- +-------------------+
     |                          |   TRANSCRIBING    |
     |                          +-------------------+
     |                                    ^
     | startListening()                   | stopListening()
     v                                    |
+-------------+                           |
|  LISTENING  | --------------------------+
+-------------+
```

---

## 5. Nouvelle Application : `examples/MinimalSTT`

Un exemple autonome et complet d'application Push-to-Talk pour M5Stack CoreS3 a été créé dans `examples/MinimalSTT` :

### Fonctionnalités de MinimalSTT :
- **Bouton tactile grand format "DICTER"** affiché sur l'écran LCD du CoreS3 via `M5Unified`.
- **Mode Push-to-Talk instantané** :
  - Début de l'appui tactile : déclenchement de `voice.startListening()`, écran vert "ÉCOUTE EN COURS...".
  - Maintien : transmission audio 16 kHz en continu vers Gemini Live.
  - Relâchement tactile : appel de `voice.stopListening()`, écran orange "TRANSCRIPTION...".
- **Affichage à double niveau** :
  - **Partiel (interim)** : affiché en temps réel en jaune au fur et à mesure que Gemini renvoie du texte.
  - **Final** : affiché en vert éclatant dès réception de la trame finale.
  - **Sortie Moniteur Série** : journalisation temps réel à 115200 bauds.
- **Résilience** : Gestion des erreurs de connexion, timeout en cas d'absence de parole, retour automatique à l'état `PRÊT`.

---

## 6. Résultats Réels de Compilation PlatformIO

Les trois projets firmware de la bibliothèque ont été compilés avec succès sous PlatformIO (`espressif32@6.9.0`, framework `arduino`, ESP32-S3FN8) :

| Projet | Cible PlatformIO | Statut | RAM Utilisée | Flash Utilisée | Notes |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **`MinimalTTS`** | `pio run -d examples/MinimalTTS` | **SUCCESS** | 67 232 octets (20.5%) | 1 164 553 octets (55.5%) | Exemple de synthèse vocale TTS pur |
| **`OrientationClockVoice`** | `pio run -d examples/OrientationClockVoice` | **SUCCESS** | 159 104 octets (48.6%) | 1 590 097 octets (75.8%) | Horloge parlante LVGL + TTS v0.2.1 |
| **`MinimalSTT`** | `pio run -d examples/MinimalSTT` | **SUCCESS** | 67 284 octets (20.5%) | 1 172 029 octets (55.9%) | Nouveau Push-to-Talk STT v0.3.0 |

---

## 7. Bilan de la Suite de Tests Automatisés

L'exécutable de validation logique `test_voicekit_all.exe` intègre **25 suites de tests** couvrant l'intégralité du TTS et du STT :

```text
===========================================================
  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.3.0    
  Validation TTS + STT | Cible logique C++14 / C++17       
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
[TEST SUITE 10] LiveAudioStreamer : Etats des tampons circulaires et file d'emission
[TEST SUITE 11] GeminiLiveClient : Extraction des erreurs JSON renvoyees par le serveur
[TEST SUITE 12] GeminiLiveClient : Trame de fermeture serveur (Close code 1007/1008 & raison)
[TEST SUITE 13] Normalisation automatique du nom de modele Gemini
[TEST SUITE 14] VoxCoreVoice : Deconnexion intentionnelle vs erreur de cloture
[TEST SUITE 15] GoogleRoots : Integrite du bundle TLS et recuperation apres erreur
[TEST SUITE 16] Initialisation microphone et arbitrage materiel HAL
[TEST SUITE 17] Acquisition PCM 16 kHz, double-buffering et streaming
[TEST SUITE 18] Cycle Push-to-Talk (startListening -> stopListening)
[TEST SUITE 19] Scenarios de dictee courte et longue
[TEST SUITE 20] Transcriptions partielles (interim) et finales
[TEST SUITE 21] Enchainement de dictees consecutives
[TEST SUITE 22] Interruption et annulation pendant l'ecoute
[TEST SUITE 23] Perte Wi-Fi / transport pendant la dictee
[TEST SUITE 24] Absence de parole et timeout de transcription finale (Zero invention)
[TEST SUITE 25] Coexistence TTS et STT sans interferences mutuelles

===========================================================
  BILAN FINAL DES TESTS : 165 REUSSIS, 0 ECHECS
===========================================================
```

---

## 8. Instructions pour la Validation Matérielle Finale

> [!IMPORTANT]
> **Règle impérative de déploiement** :  
> Le tag Git `v0.3.0` ne doit **PAS** être créé avant la validation physique sur M5Stack CoreS3.  
> Aucun téléversement automatique n'a été exécuté.

### Procédure de validation sur M5Stack CoreS3 :
1. Configurer vos identifiants dans `examples/MinimalSTT/include/secrets.h` :
   ```cpp
   #define WIFI_SSID     "VOTRE_WIFI_SSID"
   #define WIFI_PASSWORD "VOTRE_WIFI_PASSWORD"
   #define GEMINI_API_KEY "VOTRE_CLE_GEMINI"
   ```
2. Téléverser le firmware de démonstration STT :
   ```bash
   pio run -d examples/MinimalSTT -t upload
   ```
3. Ouvrir le moniteur série :
   ```bash
   pio device monitor -d examples/MinimalSTT -b 115200
   ```
4. **Scénario d'acceptation utilisateur** :
   - Attendre l'écran bleu `PRÊT`.
   - Poser le doigt sur le bouton tactile `DICTER`. L'écran passe au vert `ÉCOUTE EN COURS...`.
   - Prononcer une phrase distincte (ex : *"Bonjour, ceci est un test de reconnaissance vocale sur CoreS3."*).
   - Relâcher le doigt. L'écran passe à l'orange `TRANSCRIPTION...`.
   - Constater l'apparition du texte exact en vert sur l'écran LCD et dans le moniteur série.
   - Vérifier qu'une nouvelle dictée peut être lancée immédiatement.
5. Re-tester `examples/MinimalTTS` et `examples/OrientationClockVoice` pour confirmer le bon fonctionnement de la parole.
