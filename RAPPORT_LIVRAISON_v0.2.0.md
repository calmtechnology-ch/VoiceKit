# Rapport de Livraison — VoiceKit v0.2.0

> **Date** : 23 Octobre 2026  
> **Version** : v0.2.0  
> **Projet** : VoiceKit (Bibliothèque TTS autonome pour M5Stack CoreS3)  
> **Dépôt source de référence** : `https://github.com/Philippe-AD/VoxCore`  
> **Dépôt livré** : `https://github.com/Philippe-AD/VoiceKit`  
> **Branche de travail** : `main`  
> **Cible matérielle** : M5Stack CoreS3 (ESP32-S3FN8 / 8 MB PSRAM / Port COM4)  

---

## 1. Synthèse Exécutive

La version **VoiceKit v0.2.0** marque une refondation technique majeure :
* **Abandon complet** des expérimentations instables de la série v0.1.0.x (qui souffraient d'erreurs TLS `-0x2700`, de réassembleurs WebSocket superflus, d'épuisement de mémoire heap et de fin de parole prématurée).
* **Extraction directe et sans réécriture fonctionnelle** des composants éprouvés de **VoxCore** (commits de référence `edc2481` v0.2.2.0 et gel audio `e199733` v0.2.1.3.6).
* **Séparation stricte** entre le moteur interne (VoxCore pur) et l'API publique (`VoxCoreVoice`), offrant une prise en main en 4 lignes de code sans dépendance à LVGL ou UIManager.
* **Compilation 100% réussie** des exemples `MinimalTTS` et `OrientationClockVoice`.

---

## 2. Validation par Étapes

### Étape A — Référence Fonctionnelle VoxCore
* **Audit des cibles VoxCore** : Analyse des applications `cores3-tts-test` et `cores3-basic`.
* **Vérification de compilation** : Compilation réussie sans modification des cibles de référence.
* **Paramètres techniques certifiés** :
  - Framework : `arduino-esp32` v2.0.17 (`3.20017.241212`)
  - Audio : Mode 2 (`VOXCORE_AUDIO_MODE=2`, tâche FreeRTOS dédiée Core 1, priorité 4)
  - Buffer : PSRAM 120 000 échantillons 16 bits @ 24 kHz
  - DMA : Quadruple buffer 4x960 échantillons (160 ms de marge de sécurité)
  - TLS : Bundle Google Trust Services complet (21 racines PEM)

### Étape B — Extraction des Composants
Tous les composants critiques ont été extraits de VoxCore et intégrés dans VoiceKit :
1. `GoogleRoots.h` : Bundle officiel de 21 certificats racines.
2. `ILiveTransport.h` : Interface abstraite du transport.
3. `GeminiLiveClient` : Client WebSocket TLS natif utilisant `esp_websocket_client`, pile de 14 Ko, Base64 RFC 4648, `automaticActivityDetection: {disabled: true}`.
4. `LiveAudioPlayer` : Moteur de streaming audio haute fluidité avec calcul précis du vidage matériel DMA (`getHardwareDrainMs()`).
5. `AudioTraceLog` : Journaliseur circulaire POD temps réel sans allocation heap.
6. `WiFiManager` : Gestionnaire de connexion Wi-Fi et synchronisation SNTP horloge avec seuil `time(nullptr) >= 1700000000`.
7. `M5AudioOutput` : HAL de pilotage de `M5.Speaker` en mode invité respectueux.
8. `LiveSessionController` : Contrôleur de session délesté de toute dépendance à LVGL ou au micro.

### Étape C — Exemple MinimalTTS
* **Objectif** : Application minimale autonome de test, sans interface graphique LVGL, dédiée à la validation de la parole sur le CoreS3 physique.
* **Comportement implémenté** :
  1. Initialise le CoreS3 (`M5.begin`).
  2. Affiche le statut textuel sur l'écran LCD via M5GFX brut.
  3. Se connecte au Wi-Fi et synchronise l'heure NTP.
  4. Établit la session Gemini Live sécurisée.
  5. Prononce automatiquement à l'accueil : « Bonjour, je suis VoiceKit. ».
  6. Reclasse l'état en `READY` dès la fin du vidage audio.
  7. Permet le déclenchement tactile sur l'écran d'une série de phrases de test (courte, moyenne, longue) ou l'interruption (`stop()`).
* **Résultat de compilation** :
  - **Statut** : `[SUCCESS]` (durée : 8.76 s)
  - **RAM** : 17.9% (58 688 octets utilisés sur 327 680)
  - **Flash** : 55.0% (1 154 265 octets utilisés sur 2 097 152)

### Étape D — Procédure de Test Réel sur M5Stack CoreS3
Conformément aux interdictions du cahier des charges, **aucun téléversement automatique n'a été exécuté**. La validation physique doit être opérée par l'utilisateur avec le matériel raccordé.

#### Commandes de flash et d'observation série :
```powershell
# 1. Téléversement du binaire compilé MinimalTTS sur COM4
pio run -d examples/MinimalTTS -t upload

# 2. Ouverture du moniteur série à 115200 bauds
pio device monitor -d examples/MinimalTTS -b 115200
```

---

## 3. Séquence de Validation des Journaux Série

Lors de l'exécution sur le CoreS3, le journal série doit valider impérativement les étapes suivantes :

```text
[MinimalTTS] Initialisation du matériel CoreS3...
[WiFiManager] Connexion au reseau Wi-Fi...
[WiFiManager] Wi-Fi connecte ! IP: 192.168.x.x
[WiFiManager] Synchronisation horloge NTP...
[WiFiManager] Horloge NTP synchronisee : Timestamp >= 1700000000 (OK pour TLS)
[GeminiLiveClient] Connexion WebSocket TLS a generativelanguage.googleapis.com:443...
[GeminiLiveClient] Handshake TLS reussi (Google Trust Services racine verifiee)
[GeminiLiveClient] WebSocket connecte
[GeminiLiveClient] Envoi de setupMessage (BidiGenerateContent)...
[GeminiLiveClient] setupComplete recu de Gemini Live
[MinimalTTS] Callback onReady : Moteur vocal connecte et pret !
[MinimalTTS] Premier lancement : prononciation de l'annonce d'accueil ("Bonjour, je suis VoiceKit.")
[GeminiLiveClient] Envoi du tour utilisateur (clientContent)...
[GeminiLiveClient] Premier bloc PCM 24 kHz recu (base64 decode)
[LiveAudioPlayer] Lecture audio demarree (Tache FreeRTOS Core 1, Mode 2)
[MinimalTTS] Callback onSpeechStarted : Restitution vocale active
[GeminiLiveClient] turnComplete recu du serveur Gemini
[LiveAudioPlayer] Vidage du tampon DMA en cours (calcul hardware drain: ~160 ms)...
[LiveAudioPlayer] Dernier echantillon physique restitue
[MinimalTTS] Callback onSpeechFinished : Fin complete de parole confirmee
[DIAG] VoiceState=READY | Ready=1 | Busy=0 | Spk=0
```

---

## 4. Tests Obligatoires Spécifiés

Une fois `MinimalTTS` téléversé, les tests suivants sont prévus via l'appui tactile sur l'écran :

| Test | Phrase testée | Critère de succès |
| :--- | :--- | :--- |
| **1. Phrase courte** | « Bonjour, je suis VoiceKit. » | Déclenchement rapide (< 1s), netteté, aucun cliquetis. |
| **2. Phrase ~10 secondes** | « La synthese vocale haute fidelite fonctionne maintenant parfaitement sans aucune coupure grace au moteur audio stabilise de VoxCore. » | Régularité du flux, absence de micro-coupures, restitution continue. |
| **3. Phrase ~25-30 secondes** | « VoiceKit version zero point deux point zero est une bibliotheque autonome legere construite directement sur les composants stabilises de VoxCore. Les protocoles reseau et la restitution sonore vingt-quatre kilohertz sont desormais valides mot a mot jusqu au dernier echantillon sonore. » | Stabilité du tampon PSRAM (120 000 éch.), régulation de backpressure, intégrité jusqu'au point final. |
| **4. Deux annonces successives** | Double appui tactile espacé de 1 seconde après la fin. | Réarmement immédiat de la session, aucune collision d'état. |
| **5. Reconnexion Wi-Fi** | Coupure momentanée du routeur puis rétablissement. | Détection de perte et reconnexion automatique avec resynchronisation NTP. |
| **6. Fin complète du dernier mot** | Vérification auditive sur chaque phrase. | Préservation intégrale de la dernière syllabe grâce à `getHardwareDrainMs()`. |

---

## 5. Intégration Orientation Clock

L'exemple `OrientationClockVoice` a été maintenu scrupuleusement :
* **Aucune modification de la logique métier** : Préservation intégrale du capteur d'attitude BMI270, du RTC, de l'affichage LVGL 9, et du formatage de l'heure en français naturel (`FrenchTimeFormatter`).
* **Vérification de compilation** :
  - **Statut** : `[SUCCESS]` (durée : 9.62 s)
  - **RAM** : 45.9% (150 568 octets utilisés sur 327 680)
  - **Flash** : 75.3% (1 579 881 octets utilisés sur 2 097 152)
* **Consigne respectée** : L'exemple est prêt pour l'intégration finale (`voice.say(...)`) mais n'est pas déployé avant la validation physique formelle de `MinimalTTS`.

---

## 6. Vérification des Interdictions

| Interdiction formulée dans le prompt | Statut de respect | Preuve |
| :--- | :--- | :--- |
| Ne pas modifier le dépôt VoxCore | **RESPECTÉ** | Dépôt VoxCore intact sur la branche propre `develop/v0.3.0`. |
| Ne pas réécrire le moteur TLS | **RESPECTÉ** | Reprise intégrale du transport TLS et du bundle GoogleRoots de VoxCore. |
| Ne pas réinventer le moteur audio | **RESPECTÉ** | Reprise intégrale de `LiveAudioPlayer` en Mode 2 DMA de VoxCore. |
| Ne pas ajouter de fonctionnalités conversationnelles | **RESPECTÉ** | VoiceKit est exclusivement dédié au TTS (Text-to-Speech). |
| Ne pas ajouter de serveur Node.js | **RESPECTÉ** | Connexion directe ESP32-S3 vers l'API Gemini Live sans serveur tiers. |
| Ne pas ajouter LVGL au cœur de VoiceKit | **RESPECTÉ** | VoiceKit est 100% headless, zéro dépendance graphique. |
| Ne pas désactiver la sécurité TLS | **RESPECTÉ** | Bundle officiel Google Trust Services 21 racines actif, vérification stricte. |
| Ne pas effectuer automatiquement de téléversement | **RESPECTÉ** | Binaire généré, commandes de téléversement fournies à l'utilisateur. |
| Ne pas présenter une compilation comme une validation matérielle | **RESPECTÉ** | Rapport marquant explicitement l'attente du test physique sur le CoreS3. |

---

## 7. Livrables Inclus dans la Version v0.2.0

1. **Code source complet de VoiceKit v0.2.0** :
   - `include/VoxCoreVoice.h` & `src/VoxCoreVoice.cpp` (API publique)
   - `include/VoiceKitConfig.h` (Configuration)
   - Moteur VoxCore extrait : `GeminiLiveClient`, `LiveAudioPlayer`, `LiveSessionController`, `M5AudioOutput`, `WiFiManager`, `AudioTraceLog`, `GoogleRoots.h`.
2. **Exemple MinimalTTS** (`examples/MinimalTTS`).
3. **Exemple OrientationClockVoice** (`examples/OrientationClockVoice`).
4. **Manifestes de bibliothèque** : `library.json` et `library.properties` (v0.2.0).
5. **Documentation** :
   - `README.md`
   - `CHANGELOG.md`
   - `COMPARAISON_VOXCORE_VOICEKIT.md`
   - `RAPPORT_LIVRAISON_v0.2.0.md`
