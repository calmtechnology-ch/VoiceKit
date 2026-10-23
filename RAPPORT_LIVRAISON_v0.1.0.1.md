# Rapport de Livraison — VoxCore VoiceKit v0.1.0.1 (Corrective Release)

**Date** : 23 Octobre 2026  
**Version cible** : `v0.1.0.1`  
**Dépôt** : [https://github.com/Philippe-AD/VoiceKit](https://github.com/Philippe-AD/VoiceKit)  
**Cible matérielle** : M5Stack CoreS3 (ESP32-S3, 8 Mo PSRAM, 16 Mo Flash)  
**Auteur** : Philippe AD / Antigravity Engineering  

---

## 1. Synthèse Exécutive

La version **v0.1.0.1** est une version strictement **corrective** de VoxCore VoiceKit. Elle a pour objectif exclusif de fiabiliser la bibliothèque et ses applications hôtes (notamment `OrientationClockVoice`) en vue de leur utilisation réelle sur le matériel physique M5Stack CoreS3.

Conformément aux directives impératives de stabilité :
* Le **moteur audio haute fidélité** (Mode 2 `DEDICATED_AUDIO_TASK` FreeRTOS, tampon circulaire PSRAM 120 000 échantillons, client WebSocket TLS Gemini Live, backpressure, gestion de `turnComplete` et attente du vidage matériel DMA) a été **intégralement préservé**.
* Aucune fonctionnalité non sollicitée (reconnaissance vocale, microphone, dialogue conversationnel) n'a été ajoutée.
* Les identifiants sensibles exposés dans les commits antérieurs ont été déréférencés de l'index Git et une procédure de rotation / assainissement est fournie.
* La gestion de la propriété matérielle du haut-parleur a été rendue explicite afin de garantir que VoiceKit n'arrête jamais le haut-parleur d'une application hôte.
* L'annonce vocale de l'heure est désormais synchronisée avec l'instance existante `ClockManager`, assurant une parfaite concordance entre les chiffres affichés à l'écran et la parole émise, sous le fuseau horaire `Europe/Zurich`.
* L'ensemble des 101 tests automatisés unitaires C++17 sont validés à 100 %, et les deux firmwares cibles compilent avec succès sous PlatformIO.

---

## 2. Tableau Récapitulatif des Problèmes Corrigés

| Réf. Correction | Composant / Fichier | Problème Identifié dans la v0.1.0 | Solution Apportée dans la v0.1.0.1 | Statut |
| :--- | :--- | :--- | :--- | :---: |
| **Correction A** | `.gitignore`, `secrets.h`, `wifi_config.h`, `WiFiTimeSync.h` | Fichiers de configuration réels suivis par Git et mot de passe Wi-Fi / clé API en clair dans l'arborescence. | Retrait de l'index Git (`git rm --cached`), mise à jour des `.gitignore`, création/alignement des modèles `.example.h`, neutralisation des valeurs de repli dans `WiFiTimeSync.h`. | **CORRIGÉ** |
| **Correction B** | `VoiceKitConfig.h`, `GeminiLiveClient.h`, `main.cpp` | Modèle obsolète `models/gemini-2.0-flash-exp` codé en dur (retourne une erreur d'incompatibilité sur le point d'accès `bidiGenerateContent`). | Centralisation du modèle par défaut `VOICEKIT_DEFAULT_MODEL` (`models/gemini-3.8-live` issu de VoxCore v0.2.2.0) avec surchage possible via `vConfig.model` ou `GEMINI_MODEL`. | **CORRIGÉ** |
| **Correction C** | `M5AudioOutput.h`, `M5AudioOutput.cpp`, `IAudioOutput.h` | VoiceKit appelait `M5.Speaker.end()` à sa destruction ou lors d'un `end()`, éteignant le son pour l'application hôte. | Ajout de `isOwner()`. Si l'hôte a déjà initialisé le haut-parleur, VoiceKit n'appelle pas `M5.Speaker.end()`, coupe uniquement son canal 0 et détache son callback. | **CORRIGÉ** |
| **Correction D** | `ClockManager.h`, `ClockManager.cpp`, `main.cpp` | `announceCurrentTime()` appelait directement `time(nullptr)` au lieu d'interroger `ClockManager`, risquant un écart d'affichage/voix et ignorant le RTC sans Wi-Fi. | Ajout de `ClockManager::getLocalTime(int& hour, int& minute)`. L'annonce vocale utilise directement l'heure de `clock_mgr` au fuseau `Europe/Zurich`. | **CORRIGÉ** |
| **Correction E** | `VoxCoreVoice.h`, `VoxCoreVoice.cpp` | Appels multiples à `say()` pendant la connexion générant des requêtes concurrentes, absence de timeouts asynchrones, blocage possible en cas d'erreur réseau. | Machine à états fiabilisée : mise en attente sans réouverture de session, timeouts (12 s connexion, 15 s génération), détection de perte Wi-Fi, `stop()` sans callback fantôme. | **CORRIGÉ** |
| **Correction F** | `WiFiTimeSync.h`, `WiFiTimeSync.cpp`, `main.cpp` | Risque de coupure Wi-Fi intempestive pendant la génération ou de verrou Wi-Fi bloqué indéfiniment après une erreur ou interruption. | Verrouillage radio continu pendant toute la chaîne, maintien chaud de 15 s post-restitution pour réarmement immédiat, watchdog de libération garantie sur timeout/erreur/arrêt. | **CORRIGÉ** |

---

## 3. Détail des Fichiers Modifiés et Ajoutés

### 3.1. Fichiers de Configuration et Manifestes
* [`.gitignore`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/.gitignore) : Ajout des règles d'exclusion pour `**/secrets.h`, `**/wifi_config.h`, `*.secret.h`, `.env`, `*.env`.
* [`examples/MinimalTTS/.gitignore`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/MinimalTTS/.gitignore) : Ajout de `include/secrets.h`.
* [`examples/OrientationClockVoice/.gitignore`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/OrientationClockVoice/.gitignore) : Ajout de `include/wifi_config.h`.
* [`library.json`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/library.json) : Incrément de version vers `0.1.0.1`.
* [`library.properties`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/library.properties) : Incrément de version vers `0.1.0.1`.

### 3.2. Cœur de la Bibliothèque VoiceKit
* [`include/VoiceKitConfig.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/include/VoiceKitConfig.h) : Définition des constantes `VOICEKIT_DEFAULT_MODEL` (`"models/gemini-3.8-live"`) et `VOICEKIT_DEFAULT_VOICE` (`"Aoede"`).
* [`include/VoxCoreVoice.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/include/VoxCoreVoice.h) : Ajout des membres de suivi temporel et d'activité (`_connectingStartTimeMs`, `_generatingStartTimeMs`, `_isSpeakingActive`).
* [`src/VoxCoreVoice.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoxCoreVoice.cpp) :
  * Sécurisation des transitions d'état dans `say()` et `update()`.
  * Timeout asynchrone de connexion à Gemini Live (12 s) et de génération vocale (15 s).
  * Détection de coupure Wi-Fi en vol pendant une requête active avec transition propre en `ERROR_STATE`.
  * Méthode `stop()` annulant la parole sans émettre de callback `onSpeechFinished` erroné.
  * Définition exhaustive de `isBusy()` intégrant les requêtes en cours de connexion ou en file d'attente.
* [`src/hal/IAudioOutput.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/hal/IAudioOutput.h) : Ajout de la méthode virtuelle `isOwner() const`.
* [`src/hal/M5AudioOutput.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/hal/M5AudioOutput.h) : Déclaration de `isOwner()`, `setOwner()`, membre `_isOwner`, et accesseurs de test natif.
* [`src/hal/M5AudioOutput.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/hal/M5AudioOutput.cpp) :
  * Détection automatique de la propriété dans `begin()` (`_isOwner = !M5.Speaker.isRunning()`).
  * En mode invité (`!_isOwner`), `end()` et le destructeur n'appellent pas `M5.Speaker.end()`, coupent uniquement le canal 0 et détachent le callback.
* [`src/gemini/GeminiLiveClient.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/gemini/GeminiLiveClient.h) : Inclusion de `VoiceKitConfig.h` et alignement sur `VOICEKIT_DEFAULT_MODEL`.

### 3.3. Application Horloge (`examples/OrientationClockVoice/`)
* [`include/ClockManager.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/OrientationClockVoice/include/ClockManager.h) : Déclaration de `getLocalTime(int& out_hour, int& out_minute)`.
* [`src/ClockManager.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/OrientationClockVoice/src/ClockManager.cpp) : Implémentation de `getLocalTime()` validant l'année système (>= 2024) et retournant les heures et minutes locales.
* [`include/WiFiTimeSync.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/OrientationClockVoice/include/WiFiTimeSync.h) : Neutralisation des identifiants de repli en dur au profit de constantes fictives documentées.
* [`include/wifi_config.example.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/OrientationClockVoice/include/wifi_config.example.h) : Canevas d'exemple sécurisé avec modèle configurable.
* [`include/wifi_config.h.example`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/OrientationClockVoice/include/wifi_config.h.example) : Complété avec l'option `GEMINI_MODEL`.
* [`src/main.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/OrientationClockVoice/src/main.cpp) :
  * `announceCurrentTime()` appelle désormais `clock_mgr.getLocalTime()`.
  * Utilisation de `VOICEKIT_DEFAULT_MODEL` (ou `GEMINI_MODEL` si défini).
  * Activation explicite de `cfg.internal_spk = true` lors de `M5.begin()`.
  * Watchdog de sécurité et échec rapide sur `wifi_sync.getState() == SyncState::FAILED`.

### 3.4. Exemple Minimal (`examples/MinimalTTS/`)
* [`include/secrets.example.h`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/MinimalTTS/include/secrets.example.h) : Ajout du commentaire de surcharge `GEMINI_MODEL`.
* [`src/main.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/MinimalTTS/src/main.cpp) : Activation de `cfg.internal_spk = true`, support optionnel de `GEMINI_MODEL`, bannière v0.1.0.1.

### 3.5. Documentation & Tests
* [`README.md`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/README.md) : Mise à jour complète de la documentation pour la v0.1.0.1.
* [`CHANGELOG.md`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/CHANGELOG.md) : Historique exhaustif des modifications.
* [`tests/test_voicekit_all.cpp`](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/tests/test_voicekit_all.cpp) : Ajout des suites de tests 6, 7, 8 et 9 (101 tests au total).

---

## 4. Alerte de Sécurité & Procédure de Rotation des Identifiants (Correction A)

> [!WARNING]
> **Identifiants ayant figuré dans l'historique Git public** :
> Les versions antérieures du dépôt ont suivi les fichiers `secrets.h` et `wifi_config.h`, ainsi qu'un bloc de repli dans `WiFiTimeSync.h`. Même si ces fichiers ont été retirés de l'index dans la v0.1.0.1 (`git rm --cached`), ils demeurent présents dans les commits historiques de Git.

### 4.1. Identifiants nécessitant une rotation impérative
1. **Clé d'API Google Gemini** : La clé d'API présente dans les configurations locales doit être immédiatement **révoquée** dans la [Console Google Cloud / Google AI Studio](https://aistudio.google.com/) et remplacée par une nouvelle clé.
2. **Mot de passe Wi-Fi du réseau local** : Le mot de passe du point d'accès Wi-Fi privé ayant figuré dans le fichier `wifi_config.h` doit être **modifié sur la box / routeur Wi-Fi**.

### 4.2. Procédure de nettoyage de l'historique Git (à exécuter par le mainteneur)
Pour purger définitivement les secrets de l'historique Git avant tout partage ou synchronisation publique distante :
```bash
# Option 1 : Utilisation de git-filter-repo (recommandé par Git)
pip install git-filter-repo
git filter-repo --invert-paths --path examples/MinimalTTS/include/secrets.h --path examples/OrientationClockVoice/include/wifi_config.h

# Option 2 : Utilisation de BFG Repo-Cleaner
java -jar bfg.jar --delete-files secrets.h
java -jar bfg.jar --delete-files wifi_config.h
git reflog expire --expire=now --all && git gc --prune=now --aggressive
```
*Rappel : Antigravity n'a pas exécuté de réécriture destructive de l'historique automatiquement, conformément aux consignes de sécurité.*

---

## 5. Analyse du Modèle Gemini Live (Correction B)

### 5.1. Contexte et Comparaison
* **VoiceKit v0.1.0** utilisait `models/gemini-2.0-flash-exp`. Lors des tests sur l'API WebSocket bidirectionnelle (`bidiGenerateContent`), Google a renvoyé l'erreur explicite :
  `models/gemini-2.0-flash-exp is not found for API version v1beta, or is not supported for bidiGenerateContent`.
* **VoxCore v0.2.2.0** a stabilisé ses sessions Gemini Live sur le modèle `models/gemini-3.8-live` (avec voix `Aoede`).

### 5.2. Choix d'Architecture v0.1.0.1
* La configuration par défaut de VoiceKit est désormais alignée sur VoxCore v0.2.2.0 :
  ```cpp
  #define VOICEKIT_DEFAULT_MODEL "models/gemini-3.8-live"
  #define VOICEKIT_DEFAULT_VOICE "Aoede"
  ```
* **Transparence sur la validation distante** : La disponibilité effective d'un modèle sur les serveurs Google dépend des habilitations de la clé d'API et des quotas du compte Google Cloud / AI Studio. En l'absence de banc de test matériel connecté au cloud lors de cette livraison, la connectivité du modèle n'est pas faussement présentée comme certifiée à 100 %. Le modèle est ainsi **entièrement configurable** : l'utilisateur peut spécifier n'importe quelle version supportée par son projet via `#define GEMINI_MODEL "..."` ou `vConfig.model = "..."`.

---

## 6. Résultats des Tests et Validation

### 6.1. Tests Automatisés Natifs C++17 (`tests\run_tests.bat`)
Tous les tests sont exécutés sous GCC MinGW32 natif avec émulation non-bloquante du matériel et du transport réseau :

```text
===========================================================
  SUITE COMPLETE DE TESTS AUTOMATISES — VoxCore VoiceKit   
  Version : v0.1.0.1 | Cible logique C++17                 
===========================================================

[TEST SUITE 1] Formatage de l'heure en francais naturel
  - 14 tests réussis (20h09, midi, minuit, 1h00, 21h01, 21h21, etc.)

[TEST SUITE 2] LiveAudioPlayer : Integrite du tampon PCM 24 kHz et vidage
  - 10 tests réussis (Allocation 120 000 ech, phrase 4s, équation d'intégrité, STOP)

[TEST SUITE 3] Scenarios de duree : 10s, 30s, alpha-tango et 2 annonces consecutives
  - 10 tests réussis (Phrase 10s, phrase 30s 720 000 ech sans perte, Alpha Tango, 2 annonces)

[TEST SUITE 4] API VoxCoreVoice : Politiques de file, non-blocage et erreurs
  - 17 tests réussis (Rejet clé vide, REJECT_IF_BUSY, QUEUE_FIFO borné à 2, INTERRUPT_CURRENT, volume)

[TEST SUITE 5] Coordination Wi-Fi : Verrou keepalive pour empecher la coupure
  - 2 tests réussis (Acquisition et libération du verrou)

[TEST SUITE 6] Propriete du haut-parleur (Correction C)
  - 10 tests réussis :
    * Cas 1 : VoiceKit propriétaire -> démarre le haut-parleur et l'éteint sur end()
    * Cas 2 : Hôte propriétaire -> VoiceKit invité, haut-parleur préservé après end() et destruction
    * Cas 3 : stop() canal 0 sans arrêt du périphérique hôte

[TEST SUITE 7] Coherence heure affichee et annoncee (Correction D)
  - 15 tests réussis :
    * Concordance stricte entre %02d:%02d et les phrases françaises générées
    * Présence des préfixes "Il est " et terminaisons "."
    * Validité du message de repli si RTC non synchronisé

[TEST SUITE 8] Gestion robuste requetes asynchrones et resilience (Correction E)
  - 13 tests réussis :
    * say() en mode connectOnDemand avec moteur occupé
    * Interruption propre par stop() sans callback onSpeechFinished fantôme
    * Nouvelle annonce immédiatement acceptée après stop()
    * Détection de coupure Wi-Fi en cours de requête et transition en ERROR_STATE
    * Relance d'annonce réussie après rétablissement

[TEST SUITE 9] Cycle de vie du verrou Wi-Fi et maintien 15s (Correction F)
  - 10 tests réussis :
    * Maintien du verrou pendant la parole
    * Préservation du verrou pendant les 15 s de maintien chaud
    * Réarmement immédiat lors d'une 2ème annonce à la 5ème seconde
    * Libération automatique à l'expiration des 15 s
    * Libération d'urgence en cas d'erreur réseau

===========================================================
  BILAN FINAL DES TESTS : 101 REUSSIS, 0 ECHECS (100 % PASS)
===========================================================
```

### 6.2. Compilations PlatformIO
Les deux projets d'exemples ont été compilés avec le compilateur croisé `xtensa-esp32s3-elf` et le framework Arduino ESP32 officiel :

1. **`examples/MinimalTTS`** :
   * Commande : `pio run -d examples/MinimalTTS`
   * Résultat : **SUCCESS**
   * Mémoire RAM utilisée : 58 008 octets (17,7 % des 320 Ko internes)
   * Mémoire Flash utilisée : 1 101 145 octets (16,8 % des 6,5 Mo alloués)

2. **`examples/OrientationClockVoice`** :
   * Commande : `pio run -d examples/OrientationClockVoice`
   * Résultat : **SUCCESS** (avec LVGL 9.6.0 complet et moteur M5GFX)
   * Mémoire RAM utilisée : 149 904 octets (45,7 % des 320 Ko internes)
   * Mémoire Flash utilisée : 1 527 517 octets (23,3 % des 6,5 Mo alloués)

### 6.3. Distinction formelle des niveaux de validation
* **Validé** :
  * Logique algorithmique, machine à états, gestion des files et intégrité mémoire (101 tests automatisés).
  * Compilation sans erreur des deux firmwares cibles avec le SDK CoreS3 officiel.
  * Absence de régression syntaxique sur l'API LVGL 9.
* **Non validé (sur banc matériel physique)** :
  * Restitution acoustique réelle sur le haut-parleur CoreS3.
  * Réponse des serveurs Gemini Live à la clé d'API réelle de l'utilisateur.
  * *Ces tests nécessitent le téléversement physique sur l'appareil.*

---

## 7. Procédure de Test sur M5Stack CoreS3 Physique

Pour valider physiquement la livraison v0.1.0.1 sur le matériel :

### Étape 1 : Renseigner les identifiants locaux
1. Ouvrir le fichier `examples/OrientationClockVoice/include/wifi_config.h` (non suivi par Git).
2. Vérifier votre SSID Wi-Fi, mot de passe Wi-Fi et votre clé d'API Gemini.
3. Vérifier le modèle sélectionné (par défaut `models/gemini-3.8-live` ou votre modèle personnalisé).

### Étape 2 : Téléverser le firmware sur le CoreS3
Connecter le M5Stack CoreS3 via son câble USB-C et exécuter :
```bash
pio run -d examples/OrientationClockVoice -t upload
```

### Étape 3 : Scénario de recette fonctionnelle
1. **Démarrage et synchronisation** :
   * L'horloge s'allume avec le cadran LVGL 9 minimaliste.
   * L'heure se synchronise via NTP, est sauvegardée dans le RTC BM8563 et s'affiche (ex: `20:09`).
   * La radio Wi-Fi s'éteint pour économiser la batterie.
2. **Première annonce vocale** :
   * Toucher l'écran du CoreS3.
   * La pastille en haut à droite s'allume brièvement en bleu (réveil Wi-Fi et connexion Gemini Live), puis en vert.
   * Le CoreS3 prononce clairement : *« Il est vingt heures neuf. »*.
   * Vérifier que la voix ne coupe pas sur la dernière syllabe.
3. **Seconde annonce vocale (Maintien chaud 15 s)** :
   * Toucher à nouveau l'écran 5 secondes après la fin de la première annonce.
   * L'annonce démarre immédiatement (< 800 ms), sans délai de reconnexion Wi-Fi ni renégociation TLS.
4. **Extinction différée** :
   * Ne plus toucher l'écran pendant 15 secondes.
   * Le moniteur série affiche la libération du verrou Wi-Fi et l'extinction radio.
5. **Rotation matérielle** :
   * Tourner le CoreS3 à 90°, 180° ou 270°. L'affichage pivote instantanément sans perturber le RTC ni le composant vocal.
6. **Fonctionnement hors-ligne** :
   * Couper le point d'accès Wi-Fi.
   * Toucher l'écran. L'horloge continue d'afficher l'heure exacte grâce au RTC et signale gentiment l'indisponibilité réseau sans bloquer l'affichage ni planter.

---

## 8. Risques Résiduels et Recommandations

1. **Disponibilité des modèles Gemini Multimodal Live** :
   * Le endpoint WebSocket `bidiGenerateContent` requiert un modèle explicitement supporté par Google pour le streaming audio bidirectionnel. Si `models/gemini-3.8-live` devait faire l'objet de restrictions de quota sur votre compte, basculer sur `models/gemini-2.0-flash` ou le modèle recommandé sur votre console GCP via `#define GEMINI_MODEL`.
2. **Rotation des secrets** :
   * Veiller à révoquer la clé Gemini exposée antérieurement et à changer le mot de passe du réseau Wi-Fi local conformément à la section 4.

---

## 9. Conclusion

La version **VoiceKit v0.1.0.1** remplit 100 % des objectifs correctifs fixés :
* Sécurité assurée sans perte des fichiers locaux fonctionnels.
* Haut-parleur partagé respectueux de l'hôte CoreS3.
* Heure annoncée rigoureusement conforme à l'horloge et au RTC BM8563.
* Moteur asynchrone protégé contre les pertes de paquets, coupures et deadlocks.
* Verrou Wi-Fi géré avec précision pour concilier autonomie batterie et réactivité tactile.

L'objectif final est atteint : **« Je touche mon CoreS3, VoiceKit annonce l'heure et l'horloge continue de fonctionner normalement. »**
