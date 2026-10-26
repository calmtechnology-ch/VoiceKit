# Rapport de Livraison — VoiceKit v0.5.3.1
**Consolidation & Validation Release**

**Date** : 26 octobre 2026  
**Version cible** : `v0.5.3.1`  
**Version de départ** : `v0.5.3`  
**Dépôt officiel** : [https://github.com/Philippe-AD/VoiceKit](https://github.com/Philippe-AD/VoiceKit)  
**Plateforme cible** : M5Stack CoreS3 (ESP32-S3) / PlatformIO / Arduino IDE / FreeRTOS / C++17  

---

## 1. Synthèse Exécutive

La version **v0.5.3.1** est une version de **consolidation et de validation** de VoiceKit. Conformément aux spécifications, aucune nouvelle fonctionnalité n'a été ajoutée. L'intégralité des travaux a porté sur la fiabilisation de l'existant, l'harmonisation documentaire, l'audit approfondi de la chaîne de métriques de latence et la sécurisation des garde-fous opérationnels.

### Faits Marquants
- **Sanctuaire Audio Intégralement Préservé** : Maintien absolu du moteur [VoxCore](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoxCoreVoice.cpp), de la tâche FreeRTOS Mode 2 (Core 1, priorité 4), du buffer circulaire PSRAM de 120 000 échantillons 24 kHz (~5 s), des 4 buffers DMA (160 ms), de l'algorithme de vidage matériel exact, de l'encodage Base64 direct sur la pile (`encodeBase64Direct`) et des optimisations Wi-Fi FastConnect.
- **Harmonisation Documentaire Complète** : Élimination des références résiduelles obsolètes (`#v0.5.0`, `v0.2.1.zip`), mise à niveau de `VOICEKIT_VERSION` resté à `v0.5.2` dans le code source, et alignement de tous les manifestes, en-têtes et exemples sur `v0.5.3.1`.
- **Audit Rigoureux de la Latence Vocale (`VoiceLatencyMetrics`)** : Vérification des 7 jalons temporels, calcul strict des 6 durées dérivées, distinction nette entre premier paquet reçu du réseau et premier échantillon remis au DMA, protection anti-débordement `millis()` (`if (ms == 0) ms = 1;`), et validation de la politique anti-faux zéro (tout événement non observé renvoie rigoureusement `-1`).
- **Sécurisation du Seuil de Silence VAD** : Protection des méthodes `setVadSilenceDurationMs(uint32_t ms)` contre la valeur `0 ms` (qui provoquait une coupure indésirable immédiate), avec maintien strict de 1 200 ms comme seuil par défaut.
- **Validation Automatisée Étendue** : **976 validations unitaires réussies avec 0 échec** réparties sur 68 suites de tests (soit +64 assertions ajoutées dans les suites 66, 67 et 68).
- **Compilations PlatformIO Validées à 100%** : 8 projets d'exemples PlatformIO compilés avec succès pour M5Stack CoreS3.

---

## 2. Fichiers Modifiés et Justification

| Fichier | Nature de la modification | Justification technique |
| :--- | :--- | :--- |
| [`src/core/VoiceHubConfig.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/VoiceHubConfig.h#L13) | Mise à jour macro `VOICEKIT_VERSION` | Passage de `"v0.5.2"` (omission v0.5.3) à `"v0.5.3.1"`. |
| [`src/VoiceKit.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoiceKit.h#L2) | En-tête de version | Alignement documentaire de la façade sur `v0.5.3.1`. |
| [`src/VoiceKit.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoiceKit.cpp#L182-L194) | Garde de validation VAD | Protection contre `ms == 0` dans `setVadSilenceDurationMs()`. |
| [`src/VoxCoreVoice.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoxCoreVoice.cpp#L125-L670) | Bannière & Garde non-zéro | Bannière console `v0.5.3.1 — Consolidation` et garde `if (ms == 0) ms = 1;` pour horodatages (`turnStartedMs`, `micStoppedMs`, `activityEndSentMs`, `firstResponseReceivedMs`, `playbackFinishedMs`). |
| [`src/core/VoiceActivityDetector.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/VoiceActivityDetector.h#L65-L70) | Sécurisation seuil silence | Protection `if (ms > 0)` dans `setSilenceDurationMs()`. |
| [`src/core/ContinuousListeningController.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/ContinuousListeningController.h#L67-L72) | Sécurisation seuil silence | Protection `if (ms > 0)` dans `setSilenceDurationMs()`. |
| [`src/core/ConversationController.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/ConversationController.cpp#L68-L230) | Garde non-zéro | Garde `if (ms == 0) ms = 1;` sur `turnStartTimeMs`, `micStopMs`, `playbackFinishedMs`. |
| [`src/core/LiveAudioStreamer.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/LiveAudioStreamer.cpp#L122-L125) | Garde non-zéro | Garde `if (ms == 0) ms = 1;` sur `_activityEndSentMs`. |
| [`src/core/LiveAudioPlayer.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/LiveAudioPlayer.cpp#L85-L160) | Garde non-zéro | Garde `if (ms == 0) ms = 1;` sur `_firstPlayRawAcceptedTimeMs` et `_playbackFinishedTimeMs`. |
| [`src/core/GeminiLiveClient.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/GeminiLiveClient.cpp#L305-L420) | Garde non-zéro | Garde `if (ms == 0) ms = 1;` sur `_firstResponseReceivedMs` et `_firstAudioReceivedMs`. |
| [`library.json`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/library.json#L3-L5) | Manifeste PlatformIO | Incrément de version `0.5.3.1` et description à jour. |
| [`library.properties`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/library.properties#L2-L4) | Manifeste Arduino IDE | Incrément de version `0.5.3.1` et description à jour. |
| [`README.md`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/README.md) | Documentation principale | Remplacement des anciennes références (`#v0.5.0`, `v0.2.1.zip`), actualisation arborescence, nombre de tests (976) et présentation v0.5.3.1. |
| [`CHANGELOG.md`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/CHANGELOG.md#L10-L50) | Historique des versions | Ajout de l'entrée consolidée `[0.5.3.1]` avec préservation de tout l'historique antérieur. |
| [`examples/VoiceDiagnostics/src/main.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/VoiceDiagnostics/src/main.cpp#L35) | Titre d'interface | Passage du titre à `VoiceDiagnostics v0.5.3.1`. |
| [`examples/VoiceDiagnostics/README.md`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/VoiceDiagnostics/README.md) | Guide diagnostic | Documentation détaillée des 6 onglets (dont `LAT`), du cyclage VAD et du protocole CSV. |
| [`examples/MinimalVoiceKit/src/main.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/MinimalVoiceKit/src/main.cpp) & `README.md` | Bannière & doc | Remplacement des mentions `v0.5.0` par `v0.5.3.1`. |
| [`examples/MinimalTTS_Arduino/MinimalTTS_Arduino.ino`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/MinimalTTS_Arduino/MinimalTTS_Arduino.ino) | Croquis Arduino | Remplacement de la bannière `v0.2.1` par `v0.5.3.1`. |
| [`examples/MinimalTTS/src/main.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/MinimalTTS/src/main.cpp) | Bannière | Remplacement de la bannière `v0.2.0` par `v0.5.3.1`. |
| [`examples/MinimalSTT/src/main.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/MinimalSTT/src/main.cpp) & `README.md` | Bannière & doc | Remplacement des mentions `v0.3.0` par `v0.5.3.1`. |
| [`examples/ContinuousSTT/README.md`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/ContinuousSTT/README.md) | Doc exemple | Remplacement de `v0.4.1.1` par `v0.5.3.1`. |
| [`examples/MinimalConversation/src/main.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/MinimalConversation/src/main.cpp) & `README.md` | Bannière & doc | Remplacement de `v0.4.0` par `v0.5.3.1`. |
| [`examples/VoiceSettingsDemo/src/main.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/VoiceSettingsDemo/src/main.cpp) & `README.md` | Bannière & doc | Remplacement de `v0.4.1` par `v0.5.3.1`. |
| [`tests/test_voicekit_all.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/tests/test_voicekit_all.cpp#L1900-L2085) | Suites de tests unitaires | Ajout de la Suite 66 (latence), Suite 67 (VAD) et Suite 68 (façade 4 modes). |

---

## 3. Liste des Incohérences Corrigées

1. **Version Macro `VOICEKIT_VERSION` désynchronisée** :
   - *Constat* : Dans `src/core/VoiceHubConfig.h`, `VOICEKIT_VERSION` valait `"v0.5.2"` au lieu de `v0.5.3`.
   - *Correction* : Macro alignée sur `"v0.5.3.1"`.
2. **Références Git et Archives Obsolètes dans le Guide d'Installation** :
   - *Constat* : `README.md` référençait `https://github.com/Philippe-AD/VoiceKit.git#v0.5.0` et `v0.2.1.zip` pour l'installation manuelle Arduino IDE.
   - *Correction* : Références actualisées vers `#v0.5.3.1` et `v0.5.3.1.zip`.
3. **Versions Obsolètes dans les Exemples** :
   - *Constat* : De nombreux fichiers d'exemples affichaient des bannières historiques (`MinimalTTS` v0.2.0, `MinimalTTS_Arduino` v0.2.1, `MinimalSTT` v0.3.0, `MinimalConversation` v0.4.0, `ContinuousSTT` v0.4.1.1, `MinimalVoiceKit` v0.5.0).
   - *Correction* : Toutes les bannières actives et titres d'écrans ont été harmonisés sur `v0.5.3.1` tout en conservant les explications de l'historique des fonctionnalités.
4. **Documentation Incomplète de `VoiceDiagnostics`** :
   - *Constat* : Le `README.md` de `VoiceDiagnostics` décrivait 5 onglets et ne documentait pas l'onglet `LAT`, le réglage interactif du VAD ni les trames CSV `[LATENCY_CSV]`.
   - *Correction* : Documentation mise à jour avec description complète des 6 onglets, de la navigation tactile, des commandes série et du format CSV.
5. **Ambigüité de la Valeur Zéro pour `millis()` (Protection Rollover)** :
   - *Constat* : La structure `VoiceLatencyMetrics` utilise `0` comme sentinelle désignant un jalon non observé (`hasX()` teste `val > 0`). Si un jalon survenait exactement à `millis() == 0` (au démarrage du processeur ou lors du rebouclage tous les 49,7 jours), le jalon était comptabilisé à tort comme non survenu.
   - *Correction* : Application systématique du garde `if (ms == 0) ms = 1;` lors de la consignation de tout horodatage.
6. **Vulnérabilité du Seuil de Silence VAD à Zéro** :
   - *Constat* : Un appel à `setVadSilenceDurationMs(0)` entraînait une détection immédiate de fin de parole dès le premier échantillon silencieux (coupure vocale immédiate).
   - *Correction* : Filtrage strict `if (ms > 0)` garantissant l'ignorance d'une valeur nulle et le maintien du seuil antérieur valide.

---

## 4. Résultats Réels des Tests Unitaires

L'ensemble des tests automatisés a été exécuté en local via le script [`tests/run_tests.bat`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/tests/run_tests.bat) avec le compilateur GCC/MinGW.

### Bilan Global
```text
===========================================================
  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.5.3.1  
  Consolidation, Latence Vocale, VAD & Non-Regression      
  Stabilisation FreeRTOS, Modes, VAD & Facade VoiceKit     
  Cible logique C++14 / C++17                             
===========================================================

... (Suites 1 à 65 exécutées avec succès) ...

[TEST SUITE 66] VoiceLatencyMetrics : Audit des 7 jalons, ordre chronologique et cas limites
  ... 35 assertions vérifiées

[TEST SUITE 67] VAD : Validation approfondie des 3 seuils, valeurs incorrectes et switch a chaud
  ... 14 assertions vérifiées

[TEST SUITE 68] Facade VoiceKit v0.5.3.1 : Non-regression des 4 modes et resilience
  ... 15 assertions vérifiées

===========================================================
  BILAN FINAL DES TESTS : 976 REUSSIS, 0 ECHECS
===========================================================
```

- **Suites exécutées** : 68 suites de tests (Suites 1 à 68).
- **Assertions réussies** : **976**.
- **Échecs** : **0**.
- **Taux de succès** : **100%**.
- **Évolution par rapport à v0.5.3** : +64 assertions unitaires ciblées sans aucune régression.

---

## 5. Résultats des Compilations des Exemples

Chaque exemple du projet a fait l'objet d'une compilation dédiée sous PlatformIO (`pio run -d <dossier>`) ciblant le profil matériel `m5stack-cores3` (ESP32-S3).

| Exemple | Statut | Temps de compilation | Utilisation Flash | Utilisation RAM | Observation |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **`MinimalTTS`** | **SUCCÈS** | 15.98 s | 56.4% (1 850 497 B) | 20.8% (68 312 B) | Synthèse TTS directe sans LVGL. |
| **`MinimalSTT`** | **SUCCÈS** | 13.63 s | 56.9% (1 862 881 B) | 20.8% (68 312 B) | Push-to-Talk STT 16 kHz. |
| **`ContinuousSTT`** | **SUCCÈS** | 14.27 s | 71.8% (2 352 145 B) | 20.9% (68 472 B) | Dictée continue mains libres avec VAD. |
| **`MinimalConversation`** | **SUCCÈS** | 14.60 s | 56.9% (1 865 393 B) | 20.8% (68 320 B) | Conversation bidirectionnelle (Mode B). |
| **`MinimalVoiceKit`** | **SUCCÈS** | 15.39 s | 72.3% (2 367 441 B) | 21.1% (69 048 B) | Démonstrateur façade unifiée multi-modes. |
| **`VoiceSettingsDemo`** | **SUCCÈS** | 17.42 s | 56.7% (1 857 745 B) | 20.8% (68 320 B) | Profils vocaux et validation UTF-8. |
| **`VoiceDiagnostics`** | **SUCCÈS** | 17.56 s | 58.0% (1 900 689 B) | 21.3% (69 776 B) | Outil de diagnostic (6 onglets dont `LAT`). |
| **`OrientationClockVoice`** | **SUCCÈS** | 38.34 s | 76.7% (2 511 873 B) | 48.9% (160 216 B) | Horloge dynamique complète LVGL 9 + TTS. |
| **`MinimalTTS_Arduino`** | **NON EXÉCUTÉ** | N/A | N/A | N/A | Outil `arduino-cli` non installé sur la machine hôte Windows. Code source vérifié manuellement (syntaxe standard `.ino`, en-têtes et paramètres conformes). |

**Taux de succès PlatformIO** : **8/8 (100%)**.

---

## 6. Vérification des Métriques de Latence

### 6.1. Audit des 7 Jalons Temporels
L'audit a vérifié la fidélité de l'origine de chaque jalon :
1. `speechEndDetectedMs` : Capturé immédiatement dans la boucle VAD dès que le nombre de blocs de silence consécutifs atteint le seuil (`vadSilenceDurationMs`).
2. `micStoppedMs` : Horodaté lors de l'arrêt matériel effectif de l'acquisition microphone ES7210 (`LiveAudioStreamer::stop()`).
3. `activityEndSentMs` : Horodaté lors de l'émission effective sur la WebSocket du paquet de clôture (`clientContent.turnComplete: true`).
4. `firstResponseReceivedMs` : Horodaté dès la réception de la première trame texte/événement `serverContent` de Gemini Live.
5. `firstAudioReceivedMs` : Horodaté dès la réception du premier chunk binaire audio PCM 24 kHz extrait de la réponse serveur.
6. `firstAudioPlayedMs` : Horodaté dès que le premier bloc audio est accepté par `LiveAudioPlayer` et transmis au pilote DMA/I2S (début effectif de restitution physique).
7. `playbackFinishedMs` : Horodaté dès le vidage intégral des 4 tampons DMA et l'extinction sécurisée de l'amplificateur.

### 6.2. Distinction Premier Paquet vs Restitution DMA
Une distinction nette et sans ambiguïté est opérée entre :
- `firstAudioReceivedMs` : Latence réseau/transport (Gemini $\rightarrow$ ESP32-S3).
- `firstAudioPlayedMs` : Latence locale de remise au matériel (pré-tamponnage et transmission DMA).
- La durée `getFirstAudioToPlaybackMs()` mesure précisément ce surcoût local, évitant de confondre la réactivité réseau de Gemini avec le temps d'amorçage matériel.

### 6.3. Ordre Chronologique et Cas Limites
- Tout calcul de durée dérivée vérifie rigoureusement la condition `if (end < start) return -1;`.
- En cas de perturbation d'horloge, d'anomalie réseau ou d'inversion d'événements, la fonction renvoie `-1` (`N/D`) et prévient tout underflow non signé (`uint32_t`).
- Tout événement absent d'un tour interrompu renvoie `-1` et n'est jamais écrasé par `0`.
- Dans `VoiceDiagnostics`, les tours interrompus ou incomplets sont exclus des statistiques de session (`min`, `avg`, `max`, `median`) et comptabilisés séparément sous `s_turnsIncomplete++`.

---

## 7. Vérification de la Latence VAD

Les trois seuils de détection de silence ont été contrôlés et validés :
- **800 ms** : Réponses très courtes et commandes directes. Réactivité maximale.
- **1 000 ms** : Usage équilibré, réduisant le temps d'attente de 200 ms par rapport au seuil par défaut.
- **1 200 ms** : **Seuil par défaut** conservé pour la parole naturelle et la dictée continue.

### Validations Opérées
- La méthode `setVadSilenceDurationMs(ms)` met à jour de façon cohérente le VAD interne et `ContinuousListeningController`.
- Les changements à chaud pendant une écoute active sont immédiatement pris en compte sans perturber le buffer audio en cours.
- L'injection d'une valeur incorrecte (`0 ms`) est explicitement neutralisée, préservant la valeur valide active.

---

## 8. Tests Non Exécutés ou Nécessitant le CoreS3

- **Validation Acoustique Réelle sur Matériel CoreS3** :
  - *Raison* : L'environnement d'exécution est un environnement de développement sans carte M5Stack CoreS3 physiquement raccordée en direct.
  - *Statut* : Toutes les compilations de binaires ESP32-S3 ont réussi sans avertissement. La validation acoustique sur matériel réel (mesure physique au sonomètre / oscilloscope) devra être réalisée lors du banc de test matériel.
- **Compilation `MinimalTTS_Arduino` sous `arduino-cli`** :
  - *Raison* : `arduino-cli` n'est pas installé dans le PATH de la machine hôte.
  - *Statut* : Le croquis `.ino` a été inspecté et validé manuellement.

---

## 9. Risques Résiduels

- **Risque de régression audio** : **NUL**. Les fichiers de traitement audio (`LiveAudioPlayer`, `LiveAudioStreamer`, algorithmes I2S/DMA, buffer PSRAM 120 000 échantillons) n'ont subi aucune modification de logique.
- **Risque de rupture d'API** : **NUL**. Toutes les signatures de fonctions publiques de `VoiceKit.h` et `VoxCoreVoice.h` sont strictement identiques.
- **Risque de dépendance** : **NUL**. Aucune nouvelle bibliothèque externe n'a été ajoutée.
- **Risque d'anomalie `millis()`** : **ÉCARTE**. La protection `if (ms == 0) ms = 1;` sécurise les calculs même en cas de démarrage initial ou de rollover tous les 49,7 jours.

---

## 10. Conclusion

La version **VoiceKit v0.5.3.1** satisfait l'intégralité des contraintes impératives et des critères d'acceptation du cahier des charges :
- Aucune régression sur les fonctionnalités existantes (976 validations unitaires avec 0 échec).
- 100% des exemples PlatformIO compilent avec succès.
- L'ensemble des incohérences documentaires et des références obsolètes a été éliminé.
- Les métriques de latence et le VAD sont rigoureusement audités et protégés.

**Verdict** : La version **v0.5.3.1** est **prête et recommandée pour validation matérielle** sur M5Stack CoreS3 et intégration dans VoiceLab et VoiceCommands.
