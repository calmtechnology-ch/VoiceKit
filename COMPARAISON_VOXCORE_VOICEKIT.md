# Comparaison Technique Approfondie : VoxCore vs VoiceKit v0.2.0

> **Date** : 23 Octobre 2026  
> **Auteur** : Antigravity (Google DeepMind)  
> **Dépôt source (Référence)** : `https://github.com/Philippe-AD/VoxCore`  
> **Dépôt cible (Bibliothèque)** : `https://github.com/Philippe-AD/VoiceKit`  
> **Commits de référence VoxCore** :  
> - Commit principal : `edc2481` (*feat(v0.2.2.0): consolidate framework, centralize diagnostics, and clean repository*)  
> - Commit gel audio TTS : `e199733` (*docs: add baseline documentation and freeze report for v0.2.1.3.6*)  
> - Cibles matérielles validées : `cores3-tts-test` et `cores3-basic`

---

## 1. Contexte et Objectif

La série VoiceKit v0.1.0.x a introduit des réécritures partielles de la pile réseau et audio (tentative de réassembleur de trames WebSocket, diagnostic TLS concurrent drainant la mémoire interne du heap, certificats incomplets provoquant `-0x2700`, coupures audio prématurées).

La version **VoiceKit v0.2.0** applique le principe fondamental :  
> **« Ne plus tenter de réparer progressivement l'ancienne implémentation VoiceKit. Reprendre les composants validés de VoxCore sans réécriture fonctionnelle. »**

VoiceKit v0.2.0 est une **façade légère (wrapper public)** qui encapsule le moteur original et éprouvé de VoxCore, conditionné sous forme de bibliothèque réutilisable PlatformIO / Arduino pour M5Stack CoreS3.

---

## 2. Tableau Comparatif Composant par Composant

| Composant | Fichier source VoxCore (`edc2481` / `e199733`) | Fichier dans VoiceKit v0.2.0 | Différences fonctionnelles | Différences d'intégration | Justification technique |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Certificats TLS Google** | `include/core/GoogleRoots.h` | `include/GoogleRoots.h` | **AUCUNE** | Aucune | Restauration du bundle officiel complet Google PKI (21 certificats racines dont GTS Root R1 à R4 et GlobalSign). Indispensable pour éliminer l'erreur `-0x2700` (`MBEDTLS_ERR_X509_CERT_VERIFY_FAILED`). |
| **Interface Transport** | `include/core/ILiveTransport.h` | `include/ILiveTransport.h` | **AUCUNE** | Aucune | Maintien de l'interface abstraite (`connect`, `disconnect`, `send`, `setHandlers`) pour le découplage réseau. |
| **Client Gemini Live** | `include/core/GeminiLiveClient.h`<br>`src/core/GeminiLiveClient.cpp` | `include/core/GeminiLiveClient.h`<br>`include/GeminiLiveClient.h`<br>`src/core/GeminiLiveClient.cpp` | **AUCUNE** | Ajustement des chemins d'inclusion relatifs. | Utilisation du transport `EspWebSocketTransport` natif sur `esp_websocket_client`, pile de 14 Ko, Base64 RFC 4648, `automaticActivityDetection: {disabled: true}` et payload BidiGenerateContent exact de VoxCore. Suppression du `WebSocketReassembler` instable. |
| **Moteur Audio (Mode 2)** | `include/core/LiveAudioPlayer.h`<br>`src/core/LiveAudioPlayer.cpp` | `include/core/LiveAudioPlayer.h`<br>`include/LiveAudioPlayer.h`<br>`src/core/LiveAudioPlayer.cpp` | **AUCUNE** | Ajustement des chemins d'inclusion relatifs. | Préservation intégrale du pipeline audio gelé en v0.2.1.3.6 : tâche FreeRTOS dédiée (priorité 4, Core 1), tampon PSRAM de 120 000 échantillons (~5 s), quadruple buffer DMA (4x960 échantillons = 160 ms), vidage matériel complet (`getHardwareDrainMs()`) évitant toute troncature du dernier mot. |
| **Traceur Audio Diagnostic** | `include/core/AudioTraceLog.h`<br>`src/core/AudioTraceLog.cpp` | `include/core/AudioTraceLog.h`<br>`include/AudioTraceLog.h`<br>`src/core/AudioTraceLog.cpp` | **AUCUNE** | Aucune | Journalisation circulaire POD sans allocation dynamique durant la lecture, fournissant des métriques précises (overruns, underruns, DMA push). |
| **Gestionnaire Wi-Fi / NTP** | `include/core/WiFiManager.h`<br>`src/core/WiFiManager.cpp` | `include/core/WiFiManager.h`<br>`include/WiFiManager.h`<br>`src/core/WiFiManager.cpp` | **AUCUNE** | Aucune | Connexion robuste et synchronisation SNTP horloge avec seuil de validité `time(nullptr) >= 1700000000`, condition absolue pour la validation des dates de validité des certificats TLS X.509. |
| **Abstraction Sortie Audio** | `include/hal/IAudioOutput.h`<br>`include/hal/M5AudioOutput.h`<br>`src/hal/M5AudioOutput.cpp` | `include/hal/IAudioOutput.h`<br>`include/IAudioOutput.h`<br>`include/hal/M5AudioOutput.h`<br>`src/hal/M5AudioOutput.cpp` | **AUCUNE** | Aucune | Pilotage direct de `M5.Speaker` via `playRaw` (stéréo/mono, 24 kHz, DMA) avec gestion du volume et propriétaire invité non-intrusif. |
| **Contrôleur de Session** | `include/core/LiveSessionController.h`<br>`src/core/LiveSessionController.cpp` | `include/core/LiveSessionController.h`<br>`include/LiveSessionController.h`<br>`src/core/LiveSessionController.cpp` | **Découplage UI & Micro** | Retrait des inclusions de `UIManager`, LVGL et `MicrophoneStreamer`. | VoiceKit est une bibliothèque TTS headless (sans interface graphique imposée). Le contrôleur gère la machine à états, le backpressure et le turnComplete sans forcer l'inclusion de LVGL ni de la capture micro. |
| **Types & Constantes Partagés** | `include/core/VoiceHubConfig.h`<br>`include/core/VoiceHubTypes.h`<br>`include/core/VoiceConfig.h`<br>`include/core/VoiceEvents.h`<br>`include/hal/HardwareManager.h` | `include/VoiceHubConfig.h`<br>`include/VoiceHubTypes.h`<br>`include/VoiceConfig.h`<br>`include/VoiceEvents.h`<br>`include/hal/HardwareManager.h` | **AUCUNE** | Aucune | Définitions unifiées des événements, statistiques audio et protocoles. |
| **API Publique Façade** | *(Dans VoxCore : instanciation manuelle dans l'application `main.cpp`)* | `include/VoxCoreVoice.h`<br>`src/VoxCoreVoice.cpp`<br>`include/VoiceKitConfig.h` | **NOUVEAU** (Spécifique VoiceKit) | Wrapper unifié | Expose l'API minimale demandée : `begin(config)`, `say(text)`, `update()`, `stop()`, `isReady()`, `isBusy()`, `isSpeaking()`, `getLastError()`, et les callbacks `onReady`, `onSpeechStarted`, `onSpeechFinished`, `onError`. |

---

## 3. Détail des Différences et Justifications Techniques

### 3.1. Élimination du `WebSocketReassembler`
* **Dans VoiceKit v0.1.0.x** : Une classe personnalisée `WebSocketReassembler` tentait de concaténer manuellement les paquets WebSocket entrants avant de les parser en JSON.
* **Problème identifié** : Cette couche ajoutait des copies mémoire superflues et introduisait des risques d'épuisement du heap lors des rafales de blocs audio PCM base64.
* **Résolution v0.2.0** : Retour au code VoxCore original où `esp_websocket_client` gère les trames de manière native avec un buffer de réception dimensionné à 16 Ko, sans composant intermédiaire.

### 3.2. Élimination du diagnostic TLS concurrent
* **Dans VoiceKit v0.1.0.3** : La routine `diagnoseTlsVerification()` ouvrait une connexion TLS directe synchrone (`esp_tls_conn_new_sync`) pendant que Wi-Fi et NTP étaient en cours d'initialisation.
* **Problème identifié** : Consommation de plus de 45 Ko de heap interne au même moment, faisant chuter la mémoire disponible sous le seuil critique nécessaire à la négociation mbedTLS, provoquant l'erreur `-0x2700`.
* **Résolution v0.2.0** : Suppression totale de la double connexion. Le certificat officiel Google Trust Services est fourni directement à la configuration d'`esp_websocket_client`, exactement comme dans VoxCore `cores3-tts-test`.

### 3.3. Découplage de LVGL et de l'UI
* **Dans VoxCore** : `LiveSessionController` interagissait directement avec `UIManager` pour mettre à jour des bulles de texte, des icônes de statut et des barres d'énergie audio sur l'écran CoreS3 via LVGL 9.
* **Dans VoiceKit v0.2.0** : VoiceKit est strictement **headless**. L'application hôte (qu'il s'agisse de `MinimalTTS` sans LVGL, ou d'`OrientationClockVoice` avec LVGL 9 et BMI270) conserve l'entière maîtrise de son affichage. Les notifications d'état sont transmises par callbacks non-bloquants (`onReady`, `onSpeechStarted`, `onSpeechFinished`, `onError`).

### 3.4. Préservation du Sanctuaire Audio Mode 2
* **Moteur audio** : `LiveAudioPlayer` opère avec une tâche FreeRTOS dédiée sur le Core 1 (priorité 4), un tampon PSRAM de 120 000 échantillons (240 000 octets), et un quadruple buffer DMA de 4 x 960 échantillons.
* **Garantie de non-troncature** : L'estimation de vidage matériel via `getHardwareDrainMs()` garantit que le silence final de fin de phrase n'est signalé qu'après émission réelle de la dernière onde acoustique par la membrane du haut-parleur.

---

## 4. Matrice de Compatibilité et Paramètres de Compilation

Les paramètres PlatformIO de VoiceKit v0.2.0 sont strictement alignés sur ceux validés dans VoxCore `edc2481` :

```ini
platform = espressif32@6.9.0
board = m5stack-cores3
framework = arduino
platform_packages = 
    framework-arduinoespressif32 @ https://github.com/espressif/arduino-esp32.git#3.20017.241212
board_build.partitions = no_ota.csv
build_flags =
    -std=gnu++17
    -DVOXCORE_AUDIO_MODE=2
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DCORE_DEBUG_LEVEL=3
```

---

## 5. Conclusion comparative

VoiceKit v0.2.0 réalise l'alignement parfait avec VoxCore :
1. **Zéro réinvention** du protocole WebSocket, du handshake TLS ou du décodage audio.
2. **Zéro régression** sur la stabilité et la fluidité sonore.
3. **Architecture claire** : un moteur interne VoxCore pur, surmonté d'une API publique simple et modulaire.
