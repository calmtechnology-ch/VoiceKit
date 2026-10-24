# Rapport de Livraison — VoiceKit v0.4.0

> **Date** : 24 Octobre 2026  
> **Version** : v0.4.0  
> **Type de version** : Évolution majeure — Conversation vocale bidirectionnelle native (Mode B) & Non-régression totale TTS v0.2.1 / STT v0.3.0  
> **Dépôt** : `https://github.com/Philippe-AD/VoiceKit`  
> **Référence stable précédente** : tag `v0.3.0`  
> **Cible matérielle** : M5Stack CoreS3 (ESP32-S3FN8, ES7210 I2S Mic, AW88298 I2S Speaker, ILI9342C LCD)  

---

## 1. Synthèse Exécutive

La version **VoiceKit v0.4.0** dote VoiceKit de la **conversation vocale bidirectionnelle temps réel**, permettant un échange fluide et naturel de parole à parole entre l'utilisateur et Gemini Live sur le M5Stack CoreS3.

### 1.1 Architecture retenue : Choix du Mode B (Conversation Native Gemini Live)
Conformément aux spécifications du prompt, l'architecture retenue est le **Mode B — Conversation native Gemini Live** :
- **Flux continu direct** :  
  `Microphone CoreS3 (PCM 16 kHz) -> Gemini Live -> Réponse audio native (PCM 24 kHz) -> Haut-parleur CoreS3`
- **Justification technique** :
  1. **Zéro latence textuelle intermédiaire** : Aucun aller-retour de retraitement textuel ou second appel d'API n'est nécessaire. Gemini génère directement son flux audio au débit natif.
  2. **Réutilisation sans modification de la chaîne audio VoxCore** : Le décodage Base64, la file DMA et le lissage PSRAM 120 000 échantillons de `LiveAudioPlayer` restituent la voix de Gemini sans altération.
  3. **Double visibilité textuelle préservée** : Grâce aux événements `inputTranscription` (question prononcée par l'utilisateur) et `modelTurn.parts` (texte de réponse de Gemini), l'application affiche simultanément les deux bulles de conversation à l'écran tout en diffusant l'audio.

### 1.2 Composant Coordinateur Léger : `ConversationController`
Au lieu de créer un second client WebSocket ou de dupliquer des tâches FreeRTOS, un contrôleur léger `ConversationController` (`src/core/ConversationController.h/.cpp`) a été intégré :
- Machine à états conversationnelle : `READY` $\rightarrow$ `LISTENING` $\rightarrow$ `PROCESSING` (WAITING_RESPONSE) $\rightarrow$ `SPEAKING` $\rightarrow$ `READY`.
- Protection anti-larsen matérielle : Désactivation immédiate du haut-parleur pendant l'écoute (`setPlaybackAllowed(false)`), réarmement automatique dès le relâchement pour la restitution de la réponse.
- Préservation de la dernière syllabe : Attente active du vidage complet DMA/PSRAM avant confirmation de la fin du tour.
- Diagnostic en temps réel : Métriques complètes collectées par tour (`ConversationMetrics`).

---

## 2. Preuve de Non-Régression Cryptographique (SHA-256 vs v0.3.0 / v0.2.1)

Pour certifier qu'aucune régression n'a été introduite dans les composants critiques du moteur audio et réseau, chaque fichier a été confronté aux empreintes de référence :

| Composant | Fichier source | Empreinte SHA-256 | Conformité vs v0.3.0 / v0.2.1 |
| :--- | :--- | :--- | :---: |
| Client WebSocket TLS | `src/core/GeminiLiveClient.cpp` | `67840B069E82E43FE01744CE941D7B501ACB78C582456679B1BFF0DE4DBF2F84` | **IDENTIQUE (100%)** |
| Lecteur Audio DMA | `src/core/LiveAudioPlayer.cpp` | `AD37EE304790DD288CE12F7826095BEBC0B7E70E1D3D32756FB8B7F3B2362D85` | **IDENTIQUE (100%)** |
| Streamer Micro PCM 16k | `src/core/LiveAudioStreamer.cpp` | `20A40FAC8FE45058B246618D3B817AAF2A9C4C617060A0D3E157FE5880C78A47` | **IDENTIQUE (100%)** |
| Gestionnaire Wi-Fi / SNTP | `src/core/WiFiManager.cpp` | `8468785612E7CD261A645D68658F4AD1D86478C22DF83AAFF7EC55AB67071906` | **IDENTIQUE (100%)** |
| Journaliseur Temps Réel | `src/core/AudioTraceLog.cpp` | `5A03A20AF3769E5E6D7DC3192D77571BE61F89A4F8A63E012D69D2BC84749515` | **IDENTIQUE (100%)** |
| HAL M5.Speaker | `src/hal/M5AudioOutput.cpp` | `5A7E1ECA64A6639FA104D98B8FCE9112D5C7C52CD4097CFEEAFAB1682834DB02` | **IDENTIQUE (100%)** |
| HAL M5.Mic (ES7210) | `src/hal/M5AudioInput.cpp` | `569511CC0A33947DE5D81A2F06D6B2BD75FF359BAA465B49257BAAEBBDCF2B0D` | **IDENTIQUE (100%)** |
| Bundle Certificats TLS | `src/core/GoogleRoots.h` | `03139270D421F26DA9DE461905BF8167BCF9AD77BF719A6DF50E28349113AD2D` | **IDENTIQUE (100%)** |
| Contrôleur de Session | `src/core/LiveSessionController.cpp` | `61B176A274BD5A60AE885EDD2C8EE42A2351F6F507F5ECF601CE7E898A161DE6` | *Extension ciblée* (voir 2.1) |

### 2.1 Justification de l'extension de `LiveSessionController`
Deux méthodes utilitaires ont été ajoutées pour autoriser le tour conversationnel sans altérer la gestion TTS existante :
- `bool beginAudioTurn()` : Réinitialise les métriques et active le tour sans envoyer de prompt texte préalable.
- `void finishAudioCapture()` : Marque la fin de parole et bascule l'état d'attente réponse.
Les fonctions fondamentales (`begin`, `handleIncomingMessage`, `finishTurn`, `requestStop`) conservent rigoureusement leur logique validée.

---

## 3. Nouvelle Application Démonstrateur : `examples/MinimalConversation/`

Une nouvelle application complète et autonome a été créée sous `examples/MinimalConversation/` :
- **Écran 320x240 LCD CoreS3 (sans LVGL)** :
  - **En-tête** : Badge dynamique de l'état système (`PRÊT`, `ÉCOUTE`, `RÉFLEXION`, `RÉPONSE`, `ERREUR`).
  - **Carte Utilisateur** : Cadre affichant la transcription en temps réel (« Vous : ... »).
  - **Carte Assistant** : Cadre affichant la réponse textuelle de Gemini (« Gemini : ... »).
  - **Zone d'interaction** : Grand bouton tactile dynamique « PARLER » (vert) devenant « STOP » (orange/rouge) pendant le traitement et la réponse vocale.
- **Contrôle parallèle série** : Commandes `P` (Parler), `R` (Relâcher), `S` (Stop) à 115200 bauds.
- **Partitionnement sécurisé** : Utilisation de `no_ota.csv` (10 Mo d'espace application pour accueillir le firmware et les tampons PSRAM).

---

## 4. Résultats des Compilations PlatformIO (4 / 4 Validées)

Toutes les applications du dépôt ont été compilées sur le framework cible Arduino-ESP32 (`m5stack-cores3`) :

| Application | Statut | Empreinte Flash | Empreinte RAM | Temps de compilation |
| :--- | :---: | :---: | :---: | :---: |
| `examples/MinimalTTS` (v0.2.1) | **SUCCESS** | 55.7% (1 168 649 o / 2 Mo) | 20.7% (67 720 o / 320 Ko) | 9.16 s |
| `examples/MinimalSTT` (v0.3.0) | **SUCCESS** | 56.2% (1 177 993 o / 2 Mo) | 20.7% (67 772 o / 320 Ko) | 10.38 s |
| `examples/OrientationClockVoice` | **SUCCESS** | 76.0% (1 594 541 o / 2 Mo) | 48.7% (159 592 o / 320 Ko) | 22.76 s |
| `examples/MinimalConversation` (v0.4.0) | **SUCCESS** | 56.2% (1 179 349 o / 2 Mo) | 20.7% (67 788 o / 320 Ko) | 9.64 s |

---

## 5. Résultats de la Suite Complète de Tests Automatisés (35 Suites / 279 Validations)

Le banc de test autonome exécutable sous MinGW (`tests/run_tests.bat`) couvre désormais 35 suites de tests :

```
===========================================================
  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.4.0    
  Validation TTS + STT + Conversation (Mode B Native)     
  Cible logique C++14 / C++17                             
===========================================================

[TEST SUITE 1]  Formatage de l'heure en francais naturel                                 [PASS]
[TEST SUITE 2]  LiveAudioPlayer : Integrite du tampon PCM 24 kHz et vidage               [PASS]
[TEST SUITE 3]  Scenarios de duree : 10s, 30s, alpha-tango et 2 annonces consecutives   [PASS]
[TEST SUITE 4]  API VoxCoreVoice : Politiques de file, non-blocage et erreurs           [PASS]
[TEST SUITE 5]  Coordination Wi-Fi : Verrou keepalive pour empecher la coupure           [PASS]
[TEST SUITE 6]  Propriete du haut-parleur (Correction C)                                [PASS]
[TEST SUITE 7]  Coherence heure affichee et annoncee (Correction D)                     [PASS]
[TEST SUITE 8]  Gestion robuste requetes asynchrones et resilience (Correction E)       [PASS]
[TEST SUITE 9]  Cycle de vie du verrou Wi-Fi et maintien 15s (Correction F)             [PASS]
[TEST SUITE 10] LiveAudioStreamer : Etats des tampons circulaires et file d'emission     [PASS]
[TEST SUITE 11] GeminiLiveClient : Extraction des erreurs JSON renvoyees par le serveur  [PASS]
[TEST SUITE 12] GeminiLiveClient : Trame de fermeture serveur (Close code 1007/1008)     [PASS]
[TEST SUITE 13] Normalisation automatique du nom de modele Gemini                       [PASS]
[TEST SUITE 14] VoxCoreVoice : Deconnexion intentionnelle vs erreur de cloture          [PASS]
[TEST SUITE 15] GoogleRoots : Integrite du bundle TLS et recuperation apres erreur      [PASS]
[TEST SUITE 16] Initialisation microphone et arbitrage materiel HAL                     [PASS]
[TEST SUITE 17] Acquisition PCM 16 kHz, double-buffering et streaming                   [PASS]
[TEST SUITE 18] Cycle Push-to-Talk (startListening -> stopListening)                    [PASS]
[TEST SUITE 19] Scenarios de dictee courte et longue                                    [PASS]
[TEST SUITE 20] Transcriptions partielles (interim) et finales                          [PASS]
[TEST SUITE 21] Enchainement de dictees consecutives                                    [PASS]
[TEST SUITE 22] Interruption et annulation pendant l'ecoute                             [PASS]
[TEST SUITE 23] Perte Wi-Fi / transport pendant la dictee                               [PASS]
[TEST SUITE 24] Absence de parole et timeout de transcription finale (Zero invention)   [PASS]
[TEST SUITE 25] Coexistence TTS et STT sans interferences mutuelles                     [PASS]
[TEST SUITE 26] Activation mode conversationnel et instructions systeme (Mode B)        [PASS]
[TEST SUITE 27] Cycle conversationnel complet (READY -> LISTENING -> PROCESSING -> ...)  [PASS]
[TEST SUITE 28] Transcriptions utilisateur (interim + final) et transcription assistant [PASS]
[TEST SUITE 29] Restitution audio native 24 kHz et preservation derniere syllabe        [PASS]
[TEST SUITE 30] Enchainement de 10 tours conversationnels successifs                    [PASS]
[TEST SUITE 31] Interruption STOP prioritaire pendant l'ecoute et pendant la reponse    [PASS]
[TEST SUITE 32] Resilience : Deconnexion Wi-Fi pendant un tour et reprise propre        [PASS]
[TEST SUITE 33] Absence de parole et gestion du cycle a vide                            [PASS]
[TEST SUITE 34] Metriques diagnostiques completes du tour de conversation               [PASS]
[TEST SUITE 35] Non-regression integrale : TTS pur et STT pur preserves                 [PASS]

===========================================================
  BILAN FINAL DES TESTS : 279 REUSSIS, 0 ECHECS
===========================================================
```

---

## 6. Procédure de Test Physique sur M5Stack CoreS3

> [!IMPORTANT]
> **Règle absolue** : Conformément à la directive d'acceptation, **aucun tag Git `v0.4.0` n'est créé** tant que la validation physique n'a pas été confirmée sur le matériel réel.

Pour procéder au test physique sur votre M5Stack CoreS3 :

1. **Configurer vos identifiants** :
   Dans `examples/MinimalConversation/include/`, créez `secrets.h` (copie de `secrets.example.h`) :
   ```cpp
   #pragma once
   #define WIFI_SSID     "Votre_SSID"
   #define WIFI_PASSWORD "Votre_Mot_De_Passe"
   #define GEMINI_API_KEY "Votre_Cle_API_Gemini"
   ```
2. **Connecter le CoreS3 en USB-C** et téléverser :
   ```bash
   pio run -d examples/MinimalConversation -t upload
   ```
3. **Scénario de validation en 6 étapes** :
   - [ ] **Étape 1 (Initialisation)** : L'écran affiche `PRÊT` (badge vert) et l'adresse IP Wi-Fi.
   - [ ] **Étape 2 (Capture de question)** : Appuyez sur le bouton « PARLER », posez la question : *« Bonjour, comment vas-tu ? »*, puis relâchez. L'écran passe à `ÉCOUTE` (rouge clignotant) puis `RÉFLEXION` (orange).
   - [ ] **Étape 3 (Transcription utilisateur)** : Le texte transcrit apparaît dans le cadre « Vous : ... ».
   - [ ] **Étape 4 (Réponse vocale Gemini)** : L'écran passe à `RÉPONSE` (bleu). La voix de Gemini est restituée clairement par le haut-parleur CoreS3. La dernière syllabe est parfaitement audible (pas de coupure abrupte).
   - [ ] **Étape 5 (Réarmement)** : Le système revient automatiquement à l'état `PRÊT`. Vous pouvez immédiatement déclencher un second tour.
   - [ ] **Étape 6 (Interruption STOP)** : Pendant une réponse longue, appuyez sur le bouton (qui affiche `STOP`). La parole s'interrompt instantanément et le système retourne à `PRÊT`.
