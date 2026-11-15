# Changelog — VoxCore VoiceKit

Toutes les modifications notables apportées à ce projet sont documentées dans ce fichier.

Le format est basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.0.0/)
et ce projet adhère au [Semantic Versioning](https://semver.org/lang/fr/).

---

## [0.6.7.0] - 2026-11-15 (Voice Interaction Experimental)

Version expérimentale majeure introduisant l'extraction modulaire et la standardisation des mécanismes d'interaction vocale éprouvés dans `VoiceKit-SmartUI v0.1.0.6` (Option B de l'audit architectural `AUDIT_VOICE_INTERACTION.md`). Développement strictement additif préservant 100% du comportement de VoiceKit v0.6.6.2.

### Ajouté
- **`VoiceCaptureValidator` (`src/core/interaction/VoiceCaptureValidator.h`, `.cpp` et `src/VoiceCaptureValidator.h`)** :
  - Validateur d'activité vocale (VAD) holistique évaluant l'énergie globale d'une capture terminée.
  - Suivi adaptatif du bruit de fond par lissage exponentiel (EMA, $\alpha = 0.05$).
  - Seuil dynamique $\text{seuil} = 1.8 \times \text{noiseFloor} + 150$, avec plancher garanti à 380 RMS pour le microphone ES7210.
  - Rejet catégorique des captures sans parole exploitable (`TOO_SHORT` < 350 ms, `SILENCE`, `BRIEF_NOISE` < 120 ms continu ou < 250 ms cumulé, `LOW_ENERGY`).
  - Structure `VoiceCaptureMetrics` (alias `VadMetrics`) fournissant les causes explicites de rejet.
  - Configuration numérique personnalisable (`VoiceCaptureConfig`) sans imposer les valeurs CoreS3 à tous les matériels.
- **`VoiceInteractionController` (`src/core/interaction/VoiceInteractionController.h`, `.cpp` et `src/VoiceInteractionController.h`)** :
  - Machine à états d'interaction Push-to-Talk (PTT) à deux clics (PARLER / TERMINER), indépendante de LVGL.
  - Filtrage anti-rebond matériel configurable (120 ms par défaut).
  - Découplage des intentions tactiles/utilisateurs pour exécution non-bloquante dans la boucle `update()`.
  - Décision VAD au second clic : appel à `stopListening()` si parole valide, appel strict à `cancelListening()` si silence ou bruit bref, empêchant toute génération de trame réseau vers OpenAI / Gemini.
  - Commande STOP prioritaire absolue et universelle (accessible en écoute, traitement ou élocution), idempotente et non réentrante.
  - Watchdogs non bloquants intégrés : timeout microphone (15 s), timeout réponse serveur (15 s), stabilisation post-STOP (1,5 s), récupération d'erreur (15 s).
  - Callbacks d'observation IHM (`onSilenceDetected`, `onStateChanged`, `onCaptureValidated`).
  - API de notification explicite depuis les callbacks de `VoxCoreVoice` et méthode d'auto-liaison `attachVoiceCallbacks()`.
  - Compteurs d'observabilité et instrumentation pour les tests automatisés.
- **Exemple de référence `TwoClickConversationLVGL` (`examples/TwoClickConversationLVGL/`)** :
  - Exemple autonome et minimal pour M5Stack CoreS3 avec LVGL 9.
  - Démonstration du PTT à deux clics, indicateurs de connexion et d'état vocal, zone de transcription utilisateur et assistant, affichage de la cause de rejet VAD et bouton STOP prioritaire.
  - Prise en charge par défaut d'OpenAI Realtime avec commutateur de configuration pour Gemini Live.
- **Suite de tests automatisés Suite 152 (`tests/test_v0670_voice_interaction_suites.h`)** :
  - 20 scénarios exhaustifs validant tous les aspects de l'interaction (initialisation, transitions, deux clics, rejet silence/bruit/durée, double clic, STOP pendant écoute/restitution, idempotence, résilience timeouts et perte Wi-Fi, intégrité UTF-8).
- **Documentation complète** :
  - `docs/VOICE_INTERACTION.md` : Guide d'architecture, intégration, callbacks, guide de migration depuis SmartUI et exemple C++ complet.
  - `docs/reports/VOICE_INTERACTION_v0.6.7.0.md` : Rapport technique distinguant résultats logiciels, compilations matérielles et protocole de validation sur carte physique.

### Tests
- **Suite de tests unitaires et d'intégration** : **3 804 assertions réussies (0 échec)** sur compilateur C++17 hôte (`tests/run_tests.bat`).
- **Validation des compilations PlatformIO CoreS3** :
  - `examples/TwoClickConversationLVGL` : **SUCCESS** (RAM: 131 156 o, Flash: 2 042 465 o)
  - `examples/MinimalConversation` : **SUCCESS** (Non-régression)
  - `examples/OpenAIConversationLVGL` : **SUCCESS** (Non-régression)
  - `examples/IntegratedVoiceDemo` : **SUCCESS** (Non-régression)
  - `examples/VoiceDiagnostics` : **SUCCESS** (Non-régression)

---

## [0.6.6.2] - 2026-11-14 (Public Release Readiness)

Version de stabilisation, de validation de reproductibilité et de préparation de distribution pour mise à disposition publique sur M5Stack CoreS3. Aucun ajout fonctionnel ; consolidation stricte des acquis et préparation de la première version bêta publique (v0.7.0).

### Corrections
- **Alignement universel des constantes de version** : Mise à niveau synchrone de `library.json`, `library.properties`, `VOICEKIT_VERSION` (`src/core/VoiceHubConfig.h`), des bannières de démarrage dans `src/VoxCoreVoice.cpp` et `examples/IntegratedVoiceDemo/src/main.cpp`.
- **Hygiène du dépôt** : Nettoyage des dossiers de build `.pio` résiduels dans `tutorials/` et traçage du `.gitignore` pour `examples/IntegratedVoiceDemo`.

### Tests
- **Exécution et vérification intégrale de la suite de tests natifs** : **3 716 assertions unitaires réussies (0 échec, 0 ignoré)** sur compilateur GCC C++17 hôte (`tests/run_tests.bat`).
- **Validation automatisée de la compilation des exemples** : 10 exemples PlatformIO officiels compilés avec succès (**10/10 PASS**, 100 %) avec mesure des empreintes Flash et RAM via `tools/validation/validate_examples.py`.
- **Validation matérielle sur M5Stack CoreS3 physique (COM9)** : 19 scénarios sur 20 validés en conditions réelles (démarrage, Wi-Fi DHCP/SNTP, codec audio AW88298/ES7210, streaming PTT, STT Gemini Live, TTS 24 kHz, STOP prioritaire sous 50 ms avec rejet de blocs audio tardifs, multi-tours consécutifs, stabilité mémoire). 1 test annoté honnêtement `NOT TESTED` (OpenAI Realtime sur carte physique).

### Documentation
- **Refonte intégrale du README principal** : Structuration en 14 sections claires, épurées et didactiques pour développeurs tiers.
- **Création du Guide de Démarrage Rapide (`docs/QUICK_START.md`)** : Guide pas à pas d'installation, configuration et exécution du premier programme vocal en moins de 10 minutes.
- **Création du Référentiel des Limitations (`docs/KNOWN_LIMITATIONS.md`)** : Détail exhaustif des contraintes matérielles (Wi-Fi 2.4 GHz), réseau, quotas d'IA, fonctionnalités expérimentales (WakeNet) et compatibilités logicielles.
- **Politique de Sécurité et Confidentialité (`SECURITY.md`)** : Procédure de signalement de vulnérabilités, protection stricte des clés API, masquage dans les logs série et exclusion NVS.
- **Mise à jour des leçons d'apprentissage** : Correction des directives d'installation dans `tutorials/00-Installation/README.md` pour distinguer le développement local monorepo de l'intégration dans un projet autonome.

### Compatibilité
- **Validation d'intégration dans un projet externe isolé** : Démonstration complète de la compilation d'un projet PlatformIO tiers neuf avec résolution autonome des dépendances et inclusion directe de `<VoiceKit.h>` sans dépendance relative à l'arbre source.
- **Standard C++17** : Validation stricte des drapeaux de compilation `-std=gnu++17` et compatibilité avec le framework Arduino-ESP32 2.0.17+ et 3.x.

### Distribution
- **Audit de sécurité de l'historique Git** : Détection et signalement d'identifiants historiques dans le commit initial `aae5465` (supprimés dès `6fef6fe`), avec recommandation formelle de révocation de clé et nettoyage d'historique Git avant ouverture publique du dépôt.
- **Validation de licence** : Recensement de la licence MIT du projet et des composants tiers (ESP-SR sous Apache 2.0, cJSON sous MIT).
- **Intégration continue (CI)** : Création de la configuration GitHub Actions `.github/workflows/ci.yml` pour valider automatiquement les 3 716 tests natifs, la structure documentaire et la compilation des exemples de référence sur chaque push/PR.

### Limitations restantes
- Compilation Arduino IDE CLI non automatisée dans l'environnement hôte (`NOT TESTED (CLI)` pour `MinimalTTS_Arduino.ino`).
- Session matérielle physique limitée à Gemini Live lors des essais en direct (OpenAI Realtime validé à 100% sur simulateur natif, à tester physiquement sur carte avec clé OpenAI dédiée).

---

## [0.6.6.1] - 2026-11-14 (Corrective State Coordination)

Version corrective ciblée consacrée à la fiabilité des machines à états, à l'orchestration des tours vocaux, à la protection contre les événements obsolètes et à la cohérence du STOP prioritaire sans altération de l'architecture générale ni régression du fonctionnement validé sur M5Stack CoreS3.

### Corrigé
- **Fiabilisation du Push-to-Talk (PTT)** :
  - Idempotence stricte de `VoxCoreVoice::startListening()` : rejet immédiat et silencieux de tout nouvel appui PTT lorsque le moteur est déjà en cours d'écoute (`isListening()`) ou en cours de transcription finale (`isTranscribing()`).
  - Déduplication des transcriptions finales STT dans `handleIncomingTranscription()` : protection contre l'émission de doublons de fin de transcription lorsque le tour est déjà clos, tout en préservant l'étanchéité des tours annulés (`_cancelledCaptureTurnId`).
  - Suppression de la fausse transition `CONNECTED` dans `LiveSessionController::requestStop()` lors des phases de connexion (`CONNECTING`) ou reconnexion (`RECONNECTING`).
- **Protection contre les événements et callbacks obsolètes (STT & TTS)** :
  - `IntegratedVoicePipeline::onTranscriptionWithTurnId()` : rejet strict de tout événement sans `originTurnId` valide (`0`) ou ne correspondant pas exactement à `_activeTurnId`.
  - `VoxCoreVoice` : filtrage strict des notifications de fin de restitution audio orphelines ou annulées dans `_session.setOnPlaybackFinishedWithId` lorsque `_activeSpeechId == 0` ou lors d'un décalage d'identifiant.
  - Sécurisation de `triggerSpeechFinishedForTesting()` pour ignorer les notifications tardives sur un identifiant déjà clos.
- **Cohérence et priorité de la commande STOP** :
  - Réinitialisation propre de `_activeSpeechId = 0` et interruption immédiate des tampons audio DMA et streamer.
  - Évitement des transitions invalides `STOPPING -> READY` dans `IntegratedVoicePipeline` lorsque le moteur sous-jacent est en état d'erreur.
- **Diagnostics standardisés de transition** :
  - Traces ciblées intégrées sans surcharge des boucles audio : `[STATE][PIPELINE]`, `[STATE][STOP]`, `[STATE][STT]`, `[STATE][TTS]`.

### Ajouté
- **Suite de tests automatisés 151 (`tests/test_v0661_corrective_state_coordination_suites.h`)** :
  - 20 scénarios déterministes couvrant : Initialisation vers READY, Cycle PTT normal, Double appui PTT, Double relâchement, Transcription vide, Double transcription finale, Callback STT tardif après STOP, STOP pendant l'écoute, STOP pendant la génération IA, STOP pendant la restitution TTS, Callback TTS tardif après STOP, Timeout de transcription, Échec de démarrage microphone, Déconnexion réseau, Récupération après erreur, Exécution commande locale, Commande sensible en attente, Annulation confirmation, Changement profil moteur libre, Refus changement profil moteur occupé.
  - Total : **3716 tests réussis, 0 échec** sur l'ensemble des 151 suites de tests.

---

## [0.6.6] - 2026-11-13 (Universal Voice Profiles)

Version fonctionnelle introduisant le système unifié de **Profils Vocaux Universels (`VoiceProfileManager` + `VoiceProfile`)** compatible avec **Google Gemini Live** et **OpenAI Realtime**, utilisable depuis C++ ou LVGL 9, sans création de second moteur vocal ni régression de l'existant.

### Ajouté
- **Module indépendant `VoiceProfileManager` (`src/VoiceProfileManager.h/.cpp`)** :
  - Structure `VoiceProfile` (`provider`, `language`, `voiceId`, `preferredGender`, `style`, `tone`, `accent`) et énumération indicative `VoiceGender` (`ANY`, `FEMININE`, `MASCULINE`, `NEUTRAL`).
  - Catalogue extensible de voix prédéfinies : Gemini Live (`Aoede`, `Kore`, `Leda`, `Autonoe`, `Puck`, `Charon`, `Fenrir`, `Orus`, `Zephyr`, `Umbriel`) et OpenAI Realtime (`alloy`, `ash`, `ballad`, `coral`, `echo`, `sage`, `shimmer`, `verse`, `marin`, `cedar`) + `registerCustomVoice()` sans réallocation dynamique en boucle.
  - Catalogue multilingue BCP-47 (`fr-FR`, `fr-CH`, `en-US`, `en-GB`, `de-DE`, `it-IT`, `es-ES`) extensible via `registerCustomLanguage()` et validateur syntaxique RFC 5646 `isValidLanguageCode()`.
  - Persistance NVS sur ESP32-S3 (`Preferences`) avec numéro de schéma (`VOICEKIT_PROFILE_SCHEMA_VERSION = 1`), somme de contrôle CRC32, repli automatique sans blocage en cas de corruption, 4 emplacements favoris (`0..3`), et **exclusion absolue des clés API**.
- **Nouvelles API publiques dans `VoxCoreVoice` et la façade `VoiceKit`** :
  - `applyVoiceProfile()`, `setVoiceProfile(const VoiceProfile&)`, `getVoiceProfile()`, `setProvider()`, `getProviderType()`, `setPreferredGender()`, `getPreferredGender()`.
  - Gestion multi-clés : `geminiApiKey`, `openAiApiKey`, `setProviderApiKey()`, `getProviderApiKey()`, `hasProviderApiKey()`.
  - Persistance directe : `saveVoiceProfile()`, `loadVoiceProfile()`, `resetVoiceProfile()`, `hasSavedVoiceProfile()`, `saveFavoriteProfile()`, `loadFavoriteProfile()`.
  - Événements de confirmation et d'erreur : `onProfileApplied()` et `onProfileError()`.
- **Suite de tests automatisés 150 (`tests/test_v066_universal_voice_profiles_suites.h`)** : validation des 20 scénarios obligatoires (Gemini, OpenAI, styles/tons/accents, rejets croisés, verrouillage de voix OpenAI après audio, coupure réseau + rollback, NVS + corruption CRC/schéma, non-régression PTT/callbacks/outils locaux).

### Modifié
- **`VoiceKitConfig` (`src/VoiceKitConfig.h`)** : instructions système, conversationnelles et préfixes d'invites multilingues cohérents avec la langue choisie (`resolveSystemInstruction()`, `resolveConversationInstruction()`, `resolvePromptPrefix()`, `resolveAiPromptPrefix()`) sans écraser les consignes métier personnalisées ni traduire les textes TTS verbatim ; prise en charge native de `fr-CH`, `en-GB`, `it-IT`, `es-ES` dans `buildVoiceDirectives()`.
- **`OpenAIRealtimeClient` (`src/core/OpenAIRealtimeClient.h/.cpp`)** : fusion systématique des directives vocales lors des mises à jour dynamiques, séparation explicite entre langue de transcription d'entrée (`inputTranscriptionLanguage` ISO-639-1) et langue de synthèse, et suivi d'émission audio par session (`hasAudioEmittedInSession()`, `canUpdateVoiceInCurrentSession()`, `requestSessionUpdate()`) interdisant toute mutation de voix illégale sur une session ayant déjà produit de l'audio.
- **`LiveSessionController` (`src/core/LiveSessionController.h/.cpp`)** : nettoyage des callbacks de l'ancien fournisseur lors d'une bascule et support de `markPendingSessionUpdate()`.
- **`examples/VoiceSettingsDemo`** : migration vers une interface tactile **LVGL 9** (`320 × 240`, polices UTF-8 françaises) en 4 écrans (*Fournisseur & langue*, *Choix de la voix + Tester*, *Personnalisation + Enregistrer*, *Validation*).

---

## [Non publié] - 2026-11-10 (Réorganisation du dépôt & mise à jour de la documentation)

### Modifié
- Rapports techniques et de livraison (`RAPPORT_*.md`, v0.6.0 → v0.6.4.2) déplacés de la racine vers `docs/rapports/`.
- Scripts Python de validation matérielle (`validate_v06322.py`, `validate_lvgl_v010.py`, `test_voice_turn.py`) déplacés vers `tools/validation/`.
- Liens absolus locaux (`file:///c:/Users/...`) remplacés par des liens relatifs dans les rapports et dans `examples/OpenAIConversationLVGL/README.md`.
- `README.md` : arborescence mise à jour (dossier `src/core/voiceux/`, `docs/rapports/`, `tools/`, manifestes v0.6.4.2), liste des exemples complétée (16 exemples dont `VoiceUxLiveDemo`, `VoiceUxDemo`, `OpenAIConversationLVGL`) et renumérotée, bilan de tests et liens vers les rapports actualisés.

### Ajouté
- `docs/README.md` : index de la documentation et des rapports par version.
- `tools/validation/README.md` : mode d'emploi des scripts de validation série.

### Retiré
- `a-lire.txt` (notes personnelles) retiré du suivi Git et ajouté au `.gitignore` (le fichier reste disponible localement).
- Dossier racine `include/` supprimé (85 en-têtes relais redondants avec `src/`, dont 4 copies de `VoiceUxLiveBridge.h`). Aucun fichier du dépôt n'en dépendait : tous les en-têtes publics « à plat » restent disponibles dans `src/` (`#include <VoiceKit.h>`, `<ToolRegistry.h>`, `<VoiceUxLiveBridge.h>`, …), qui est le seul emplacement pris en compte par Arduino IDE.
  - Ajout de `src/GeminiLiveClient.h` et `src/LiveSessionController.h` pour conserver les deux seuls noms qui n'existaient que dans `include/`.
  - ⚠️ Les chemins d'alias non standard `include/core/*.h`, `include/core/tools/*.h`, `include/voiceux/*.h` et `include/hal/*.h` ne sont plus fournis : utiliser les noms à plat (`#include <ToolRegistry.h>`) ou les chemins réels (`#include <core/tools/ToolRegistry.h>`, `#include <core/voiceux/VoiceUxLiveBridge.h>`).
- `tests/run_tests.bat` : retrait de l'option `-Iinclude` devenue inutile.

---

## [0.6.5.2] - 2026-11-13 (Examples Cleanup, Validation & Transcription Assembly)

Version de consolidation consacrée au nettoyage, à la fiabilisation, à la documentation uniforme et à la validation des exemples de VoiceKit, accompagnée du correctif ciblé d'assemblage des transcriptions OpenAI Realtime sans aucune modification de l'architecture interne ni régression du moteur VoxCore.

### 1. Organisation et Standardisation des Exemples (11 Exemples Officiels)
- **11 Exemples Officiels Conservés** :
  1. `MinimalTTS` : Synthèse vocale pure unitaire (TTS PCM 24 kHz haute fidélité).
  2. `MinimalSTT` : Reconnaissance vocale Push-to-Talk (STT PCM 16 kHz) fiabilisée.
  3. `MinimalConversation` : Dialogue bidirectionnel direct (Mode B) avec Google Gemini Live.
  4. `MinimalTTS_Arduino` : Exemple autonome `.ino` pour l'IDE Arduino officiel.
  5. `WakeWordFoundation` : Diagnostic acoustique hors ligne « Bonjour ESP » (WakeNet ESP-SR).
  6. `WakeWordConversation` : Cycle mains libres complet Wake Word $\to$ Conversation multi-tours.
  7. `LocalToolRegistryDemo` : Registre local d'outils matériels hors ligne (`ToolRegistry`).
  8. `VoiceDiagnostics` : Télémétrie vocale, monitoring RAM/PSRAM et latences (6 onglets M5GFX).
  9. `VoiceSettingsDemo` : Démonstration multilingue (FR, DE, EN), profils vocaux et chaîne UTF-8.
  10. `OpenAIConversationLVGL` : Référence OpenAI Realtime avec interface graphique tactile LVGL 9.
  11. `IntegratedVoiceDemo` : Exemple phare illustrant l'orchestrateur `IntegratedVoicePipeline`.
- **Archivage et Reclassement (Zéro perte de code)** :
  - `archive/examples/MinimalVoiceKit/` : Ancien exemple composite archivé.
  - `archive/examples/ContinuousSTT/` : Mode continu unitaire archivé (intégré au pipeline).
  - `archive/examples/OpenAIConversation/` : Version M5GFX brute remplacée par LVGL 9.
  - `showcases/OrientationClockVoice/` : Reclassé en application vitrine gyroscopique complète.
  - `tests/manual/VoiceUxDemo/` : Reclassé en banc d'essai manuel hors ligne.
  - `tests/integration/VoiceUxLiveDemo/` : Reclassé en test d'intégration du pont temps réel.

### 2. Fiabilisation et Normalisation PlatformIO
- **Suppression des ports COM en dur** : Élimination de `upload_port = COMx` dans tous les `platformio.ini` officiels et archivés pour une détection automatique universelle.
- **`MinimalSTT` durci** : Démarrage du micro validé uniquement sur confirmation de `startListening()`, gestion des rejets, détection des sorties de zone tactile, protection anti-double commande, états stricts (`READY`, `LISTENING`, `TRANSCRIBING`, `ERROR`), et reprise après erreur par toucher tactile.
- **`IntegratedVoiceDemo` enrichi** : Ajout du fichier modèle `secrets.example.h`, support des commandes série de pause/reprise (`k`), répétition vocale et vérification effective des résultats d'outils.

### 3. Documentation Uniforme et Outils de Validation
- **Documentation 10/10** : Chaque exemple officiel possède un `README.md` complet structuré en 10 sections obligatoires.
- **Script automatisé `tools/validation/validate_examples.py`** : Audit complet des répertoires, vérification des fichiers obligatoires, des en-têtes secrets et compilation PlatformIO automatisée sous Windows 11.
- **Matrice de tests matériels (`tools/validation/MATRICE_TESTS_MATERIELS.md`)** : Protocole documenté pour les 14 tests physiques sur M5Stack CoreS3.

### 4. Préservation des Espaces Significatifs (`LiveSessionController::appendTranscriptionFragment`)
- **Préservation des deltas d'espaces isolés** : Le tokenizer OpenAI (`o200k_base`) émettant l'espace précédant un nombre sous forme de delta audio transcript dédié contenant uniquement `" "`, correction du test d'élagage (`fTrim.isEmpty()`) pour insérer un espace unique `' '` lorsque la cible ne se termine pas déjà par un blanc au lieu de jeter le delta.
- **Résolution du collage numérique** : Élimination du collage de mots et de nombres (ex: `"Nous sommes en2026"` $\to$ `"Nous sommes en 2026"`).
- **Anti-double espace** : Coalescence préventive des espaces consécutifs à la frontière entre deux fragments disjoints lorsque la cible se termine déjà par un espace et que le delta en débute par un.

### 2. Sécurisation de la Déduplication et de l'Overlap
- **Protection anti-fusion des chiffres consécutifs** : Interdiction formelle de tout chevauchement suffixe/préfixe portant sur des chiffres ASCII (`'0'` à `'9'`), empêchant la fusion erronée de nombres consécutifs (ex: `"20"` + `"06"` $\to$ `"2006"` au lieu de `"206"`, `"11"` + `"11"` $\to$ `"1111"` au lieu de `"111"`).
- **Seuil minimal d'overlap (k >= 3) et frontière de mot** : La recherche de chevauchement partiel requiert désormais une longueur minimale de 3 caractères et une frontière de mot dans la cible (`target`), interdisant l'amputation de lettres légitimes en bordure de token.
- **Troncature UTF-8 conforme RFC 3629** : Centralisation de `truncateUtf8` dans `Utf8Utils` pour garantir qu'aucune troncature sur `maxLength` ne coupe au milieu d'un codepoint UTF-8 multi-octets français (`é`, `è`, `à`, `œ`, etc.).

### 3. Distinction Propre entre Deltas Incrémentaux et Transcription Finale Faisant Foi
- **Nouveau callback rétrocompatible** : Ajout de `setOnOutputTranscriptionWithFinal(OutputTranscriptionWithFinalCallback cb)` dans `IVoiceProvider` avec implémentation par défaut vide, préservant 100% de compatibilité avec les mocks et implémentations tierces existantes.
- **Remplacement autoritaire sans duplication** : Dans `LiveSessionController` et `ConversationController`, la transcription finale issue de `response.output_audio_transcript.done` / `response.done` remplace directement le texte partiel consolidé du tour courant au lieu d'exécuter une concaténation aveugle `+=` en cas de divergence minime (espace, casse, ponctuation).
- **Idempotence des événements OpenAI Realtime** : Déduplication de `_lastDoneTranscript` dans `OpenAIRealtimeClient` pour interdire toute émission redondante entre `.done` et `response.done`.

### 4. Tests et Validation Automatisés (Suite 148)
- **Suite de Tests 148 (v0.6.5.2)** : 6 tests obligatoires couvrant :
  1. Reproduction exacte de la séquence OpenAI Realtime sur CoreS3 (`["Nous", " sommes", " en", " ", "202", "6", " cette", " année", "."]`, `.done`, `response.done`), validant l'absence de `"en2026"` et de répétition de phrase.
  2. Correction finale divergente avec remplacement propre autoritaire.
  3. Nombres et tokens courts consécutifs (`"Il est 20h00"`, `"En 2006"`, `"1111"`, bordures de lettres).
  4. Espaces multiples, deltas `" "` consécutifs et coalescence de frontières.
  5. Troncature UTF-8 exhaustive sur caractères accentués français.
  6. Non-régression Gemini Live (transcription cumulative et fragments partiels).

---

## [0.6.5.1] - 2026-11-11 (Integrated Pipeline Corrective & Stabilisation)

Version corrective et de stabilisation majeure de l'orchestrateur `IntegratedVoicePipeline` et de son exemple de référence `IntegratedVoiceDemo` sur M5Stack CoreS3 (ESP32-S3).

### 1. P0 — Parcours IA Complet et Instructions Système Duales
- **Consigne Système Duale** : Définition de `VOICEKIT_DEFAULT_INTEGRATED_INSTRUCTION` distinguant explicitement le mode TTS verbatim ("Prononce fidèlement...") et le mode conversationnel IA ("Réponds de manière concise, naturelle et fluide...").
- **Synchronisation Dynamique** : Ajout de `setSystemInstruction()` et `setAiPromptPrefix()` dans `VoxCoreVoice` pour garantir la cohérence des consignes sans dégrader la restitution littérale en mode TTS.
- **Capture Asynchrone de Réponse IA** : Abonnement interne `_voice->onAssistantText` dès l'initialisation du pipeline pour capturer le texte émis par Gemini Live ou OpenAI Realtime au fil de l'eau.
- **Règle de Clôture IA** : Séparation stricte de la réception du texte et de l'achèvement effectif du flux audio avant notification de fin de tour (`COMPLETED`).

### 2. P0 — Sécurisation de `speakOutput()` et des Démarrages TTS
- **Vérification Systématique du Retour de `say()`** : En cas de refus ou indisponibilité du moteur vocal, transition immédiate vers `READY` avec résultat `FAILED` sans bloquer le pipeline dans l'état `SPEAKING`.
- **Suivi Précis par `speechId`** : Filtrage strict des callbacks de fin TTS (`onSpeechFinishedWithId`) pour ignorer les annonces résiduelles ou antérieures.
- **Watchdog Temporel** : Ajout de `ttsStartTimeoutMs` (8 000 ms) et `ttsPlaybackTimeoutMs` (25 000 ms) dans `VoicePipelineConfig` pour prévenir tout blocage audio.

### 3. P0 — Idempotence Stricte des Terminaisons (`finishTurn`)
- **Verrou Terminal Idempotent** : Enregistrement de `_lastFinishedTurnId` interdisant toute émission multiple ou contradictoire de `onTurnFinished()` pour un même tour.
- **Élimination de la Double Terminaison** : Correction de l'expiration du délai de confirmation dans `update()` (`rejectPendingAction()` soldant déjà le tour, élimination de l'appel redondant à `finishTurn()`).
- **Annulation Active Prioritaire** : Annulation réelle du transport ou du lecteur lors des évènements `STOP`, timeout ou erreur.

### 4. P1 — Unification et Fiabilisation de `ReplyMemory`
- **Politique Unifiée** : Enregistrement systématique des réponses d'outils locaux (`LOCAL_TOOL`) et des réponses d'assistant IA (`AI_PROVIDER`) dans `ReplyMemory` uniquement après restitution vocale complète et non interrompue.
- **Exclusion des Messages de Contrôle** : Les répétitions (`REPEAT`, `PREVIOUS`), les messages système et les annonces de confirmation sensibles sont exclus pour préserver l'historique conversationnel utile.
- **Troncature UTF-8 Sécurisée** : `truncateUtf8Safe()` préserve les frontières de points de code multi-octets français et coupe sur les espaces pour éviter les caractères corrompus.

### 5. P1 — Gestion Effective de `PAUSE` et `RESUME`
- **Suspension Réelle du Pipeline** : Implémentation de `isPaused()` et `setPaused()` dans `IntegratedVoicePipeline`.
- **Filtrage des Commandes en Pause** : Rejet immédiat de toute commande ordinaire ou demande IA lorsque le pipeline est en pause (avec annonce "En pause."), tout en conservant `STOP` et `RESUME` ("Reprise.") actifs.

### 6. P1 — Durcissement du `VoiceCommandDispatcher`
- **Détection des Négations** : `hasNegation()` intercepte les tournures négatives françaises ("ne ... pas", "je ne veux pas", etc.) pour éviter d'exécuter des outils par inadvertance.
- **Filtrage des Requêtes d'Information** : `isInformationQuery()` empêche le déclenchement d'outils d'écriture sur des questions explicatives ("comment fonctionne...", "explique-moi...").
- **Suppression du Substring Matching Indiscriminé** : Remplacement de `indexOf >= 0` par une concordance stricte, par préfixe ou par suffixe sur mots complets.
- **Robustesse Numérique** : Prise en charge des nombres négatifs, des décimaux français (virgule ou point), du zéro et des valeurs supérieures à 100.

### 7. P1 — Robustesse des Actions Sensibles et Anti-Rejeu
- **Sanctuarisation SENSITIVE** : Si la politique sensible est requise, aucun outil `SENSITIVE` ne peut être exécuté sans jeton valide généré par `ToolRegistry`.
- **Maintien de l'État de Confirmation** : La fin de l'annonce vocale de confirmation ne fait pas basculer le pipeline à `READY`, mais le maintient en `WAITING_CONFIRMATION` jusqu'à validation utilisateur ou expiration.

### 8. P1 — Table de Partitions Flash 16 Mo pour CoreS3
- **Nouvelle Table `partitions_16MB.csv`** : Allocation d'une partition application `app0` de 8 Mo (0x800000) et SPIFFS de 7,875 Mo, éliminant la saturation de la partition `no_ota.csv` (2 Mo / 98.9% d'occupation avec polices françaises LVGL).
- **Vérification d'Initialisation dans `main.cpp`** : Contrôle systématique des retours de `s_voice.begin()`, `s_pipeline.begin()`, `registerTool()` et `registerMapping()` avec affichage visuel d'erreur sur l'écran tactile CoreS3.

### 9. Validation et Tests Automatisés
- **Suite de Tests 145 (v0.6.5.1)** : 10 tests obligatoires (A à J) couvrant commande locale avec ReplyMemory, cycle IA complet simulé, refus TTS, idempotence de terminaison, pause/reprise, négations, parsing numérique, sécurité sensible, pannes réseau et test d'endurance de 30 tours consécutifs.
- **Bilan Global** : 3 016 assertions logiques réussies sur 3 016 (100% de succès, 0 échec).

---

## [0.6.5] - 2026-11-11 (Integrated Voice Pipeline)

Mise à niveau majeure introduisant l'orchestrateur centralisé `IntegratedVoicePipeline`, permettant de construire des applications vocales complètes sans devoir programmer manuellement la coordination entre STT, TTS, Voice UX, ToolRegistry et les fournisseurs d'IA (Gemini Live / OpenAI Realtime).

### 1. Nouveau Module `IntegratedVoicePipeline`
- **Orchestration Centralisée de Tour Vocal** : Machine à états finis séquentielle à 12 états (`IDLE`, `READY`, `LISTENING`, `TRANSCRIBING`, `ROUTING`, `LOCAL_PROCESSING`, `TOOL_EXECUTING`, `WAITING_CONFIRMATION`, `AI_PROCESSING`, `SPEAKING`, `STOPPING`, `ERROR`).
- **Règle A (Un Seul Moteur Audio)** : Réutilisation directe de `VoxCoreVoice` sans duplication de microphone, player, tâche FreeRTOS ou buffer PCM.
- **Règle B & C (Un Seul Tour Actif & Zéro Double Exécution)** : Corrélation monotone de bout en bout (`turnId` et `speechId`), rejet des notifications tardives et résiduelles.
- **Support des Intentions Voice UX Locales** : Intégration transparente de `TextGate`, `VoiceIntentRouter` et `ReplyMemory` (commandes conversationnelles `STOP`, `REPEAT`, `PREVIOUS`, `PAUSE`, `RESUME`).
- **En-têtes Publiques** : `#include <IntegratedVoicePipeline.h>` et `#include <core/pipeline/IntegratedVoicePipeline.h>`.

### 2. Nouveau Module `VoiceCommandDispatcher`
- **Liaison Déterministe Parole -> Commandes Locales** : Normalisation UTF-8 en français (minuscules, suppression accents, ponctuation, espaces multiples, suppression formules de politesse).
- **Extraction Typée des Paramètres** : Extraction automatique des arguments entiers et flottants à partir des formulations vocales.
- **Validation Sécuritaire ToolRegistry** : Respect strict des politiques `READ_ONLY`, `WRITE` et `SENSITIVE`.

### 3. Routage Vocal vers l'IA (`askAi`)
- **Méthode `askAi(question)` dans `VoxCoreVoice` & `VoiceKit`** : Permet de soumettre une question conversationnelle à l'IA sans préfixe directif de répétition TTS (`promptPrefix`).
- **Mémorisation dans `ReplyMemory`** : La réponse de l'assistant IA est enregistrée dans l'historique conversationnel local uniquement après restitution vocale complète.

### 4. Gestion Robuste des Actions Sensibles (`SENSITIVE`)
- **Cycle de Confirmation Sécurisé** : Jeton cryptographique à usage unique, expiration configurable (`confirmationTimeoutMs`), protection anti-rejeu.
- **Interface Utilisateur CoreS3 & Annonce Vocale** : Événement explicite `onConfirmationRequired()` permettant d'afficher une boîte de dialogue modale sur l'écran tactile sans bloquer la boucle coopérative.

### 5. Exemple de Référence et Validation
- **Exemple `examples/IntegratedVoiceDemo`** : Application complète pour M5Stack CoreS3 avec LVGL 9 (320x240 LCD tactile), 3 outils locaux + 1 outil sensible avec confirmation à l'écran, PTT et STOP.
- **Suite de Tests 144** : 10 tests obligatoires (A à J) validant l'ensemble des scénarios nominaux, limites, erreurs, concurrence et anti-rejeu.
- **Bilan Global** : 2945 tests automatisés réussis sur 2945 (100% de succès), zéro régression.

---

## [0.6.4.3.2] - 2026-11-11 (Corrective — Event Correlation : Corrélation Asynchrone STT, TTS, STOP & Statuts Terminaux)

Version corrective de stabilisation garantissant l'origine réelle et la propagation de bout en bout des identifiants d'opérations asynchrones STT et TTS, éliminant définitivement la lecture de variables globales actives dans l'application, et verrouillant le cycle de vie des actions.

### 1. P0 — Origine et Propagation Réelle des Identifiants STT & TTS (`VoxCoreVoice`, `VoiceUxLiveBridge`, `VoiceUxLiveDemo`)
- **Élimination des Variables Globales Fragiles** : Suppression totale de `s_currentCaptureTurnId` et `s_currentSpeechId` dans l'application `VoiceUxLiveDemo`.
- **Génération Monotone & Propagation dans `VoxCoreVoice`** :
  - Chaque `startListening()` incrémente un identifiant monotone de capture (`_sttTurnId`) et le notifie via `onListeningStartedWithTurnId(uint32_t turnId)`.
  - Lors de la réception d'une transcription serveur, `VoxCoreVoice` associe le paquet à son tour d'origine réel (`_activeCaptureTurnId` ou `_pendingTranscriptionTurnId`) et le transmet via le nouveau callback `onTranscriptionWithTurnId(const String& text, bool isFinal, uint32_t turnId)`.
  - En cas de `stopListening()`, le tour passe en attente de transcription finale (`_pendingTranscriptionTurnId`).
  - En cas de `cancelListening()` ou `stop()`, les tours actifs sont immédiatement invalidés (`_cancelledCaptureTurnId`), empêchant toute corrélation tardive erronée.
  - Chaque opération `say()` attribue un identifiant unique de restitution monotone (`speechId`). À l'achèvement de la lecture audio, `LiveAudioPlayer` déclenche `onSpeechFinishedWithId(uint32_t speechId)` portant l'identifiant exact de la restitution achevée.
- **Rétrocompatibilité Préservée** : Maintien intégral des signatures sans identifiant (`onListeningStarted`, `onTranscription`, `onSpeechFinished`) encapsulant les nouvelles signatures étendues.

### 2. P1 — Validation Stricte du Cycle de Vie STT (`VoiceUxLiveBridge`)
- **Phasage Explicite du STT (`VoiceUxSttPhase`)** :
  - États de phase : `IDLE`, `LISTENING`, `TRANSCRIBING`, `COMPLETED`, `CANCELLED`.
  - Rejet strict de toute transcription finale reçue en phase `IDLE` sans capture préalable autorisée.
  - Les simulations d'entrées (`feedSimulatedText()`) créent leur propre contexte de tour valide et atteignent `COMPLETED` sans contourner la sûreté des événements réels.
- **Dédoublonnage & Rejet des Tours Obsolètes** :
  - Détection prioritaire des tours antérieurs (`turnId < _activeTurnId`) classifiés et comptabilisés comme `staleTranscriptionsIgnored`.
  - Rejet des doubles finalisations pour un même tour (`duplicateTranscriptionsIgnored`).

### 3. P1 — Statuts Terminaux Uniques & Gestion des Erreurs Audio (`VoiceUxLiveBridge`)
- **Garantie d'Unique Statut Terminal** :
  - Chaque action créée (`VoiceUxBridgeAction`) ne peut recevoir qu'un seul statut terminal parmi `COMPLETED`, `REJECTED` ou `CANCELLED` (`_activeActionTerminated`).
  - Aucun événement tardif ne peut émettre un second statut terminal pour une action déjà achevée ou annulée.
- **Gestion Robuste des Erreurs pendant TTS** :
  - `notifyError()` termine proprement l'action TTS active avec `REJECTED` et les actions en attente avec `CANCELLED`.
  - Aucune action orpheline n'est laissée en mémoire.
  - Les réponses interrompues par erreur ne sont jamais mémorisées dans `ReplyMemory`.
  - La récupération via `recoverFromError()` réarme proprement le pont sans résidu d'erreur.

### 4. P1 — Préservation du Mode Pause à Travers PTT (`VoiceUxLiveBridge`)
- **Maintien de l'État Pause** :
  - Lorsque le microphone est activé temporairement en mode `PAUSED`, l'état sous-jacent `_stateBeforePause` est préservé.
  - Les intentions ordinaires restent rejetées avec retour immédiat à l'état `PAUSED`, tandis que les intentions de reprise (`RESUME`) et d'arrêt (`STOP`) sont exécutées nominalement.

### 5. P2 — Validation Complète & Bilan de Non-Régression
- **Suite de Tests Dédiée Suite 142** :
  - Test A : Corrélation STT et rejet des transcriptions retardées.
  - Test B : Ancien événement TTS après nouveau TTS (rejet de terminaison obsolète).
  - Test C : Rejet des transcriptions sans capture (phase `IDLE`).
  - Test D : Gestion des erreurs pendant TTS et statut terminal unique `REJECTED`.
  - Test E : Concurrence STOP / fin audio et idempotence terminale.
  - Test F : Attribution réelle et propagation depuis `VoxCoreVoice`.
- **Résultats** : 2 813 tests unitaires réussis (0 échec), compilation PlatformIO de `VoiceUxLiveDemo` sur M5Stack CoreS3 (ESP32-S3) 100% propre (RAM 24.2%, Flash 82.3%).

---

## [0.6.4.3.1] - 2026-11-10 (Corrective — Event Safety : Sécurisation STT, TTS, STOP & ToolRegistry)

Version exclusivement corrective garantissant la robustesse événementielle asynchrone sans modifier l'architecture ni dégrader les fonctionnalités existantes.

### 1. P0 — Sécurisation des Transcriptions STT (`VoiceUxLiveBridge`, `VoiceUxLiveDemo`)
- **Génération Unique & Cycle de Capture** : Chaque session d'écoute initialise une génération de tour déterministe (`_activeTurnId`), réinitialise le statut d'annulation (`_turnCancelled = false`) et réarme la protection contre les doublons (`_finalReceivedForTurn = false`).
- **Suppression de l'Attribution Implicite de Tour** : Dans `feedTranscription()`, une transcription sans identifiant (`turnId == 0`) ne réalloue plus arbitrairement de nouveau tour si la capture est annulée ou terminée.
- **Rejet des Transcriptions Tardives après STOP** : Tout paquet de transcription arrivant après une annulation ou un STOP est rejeté silencieusement, protégeant l'état applicatif.
- **Rejet Déterministe des Doublons Finals** : Toute seconde transcription finale arrivant pour un même tour est immédiatement rejetée (`_finalReceivedForTurn`), éliminant l'exécution en double d'une action.
- **Préservation des Transcriptions Intermédiaires** : Les transcriptions partielles (`isFinal == false`) continuent d'être reçues et transmises pour l'affichage en direct.
- **Corrélation à la Frontière Applicative** : Dans `VoiceUxLiveDemo`, `s_currentCaptureTurnId` assure la correspondance stricte entre les événements moteur et l'injection dans le pont.

### 2. P0 — Sécurisation des Événements TTS (`VoiceUxLiveBridge`, `VoiceUxLiveDemo`)
- **Corrélation Générationnelle des Restitutions Vocales** : Introduction d'un identifiant de restitution incrémental (`_currentSpeechId`) et de la signature rétrocompatible `notifySpeechFinished(bool completed = true, uint32_t speechId = 0)`.
- **Immunité aux Terminaisons Obsolètes** : Une notification de fin appartenant à un ancien TTS interrompu ou annulé est ignorée si une nouvelle restitution est en cours (`speechId != _currentSpeechId`).
- **Protection de l'Historique Conversationnel** : Une réponse interrompue par STOP n'est jamais marquée `COMPLETED` et n'est jamais mémorisée dans `ReplyMemory`.
- **Rétrocompatibilité Totale** : Les appels existants à `notifySpeechFinished(bool)` sans identifiant restent valides et fonctionnels.

### 3. P1 — Cohérence de STOP et des Statuts d'Action (`VoiceUxLiveBridge`, `VoiceUxLiveDemo`)
- **Distinction Explicite des Entités lors d'un STOP** : Différenciation formelle entre l'action STOP elle-même, la capture audio interrompue (`cancelPendingAction()`), et la restitution TTS écourtée.
- **Statut Déterministe** : La commande STOP exécutée avec succès reçoit `COMPLETED`. Les actions utilisateur interrompues par le STOP reçoivent `CANCELLED`.
- **Règle d'Intégrité des Statuts** : Aucune action non exécutée ne peut recevoir le statut `COMPLETED`.

### 4. P1 — Sécurité du Registre d'Outils (`ToolRegistry`, `ToolDefinition`)
- **Génération Interne de Définition d'Outil** : Attribution d'un numéro de génération incrémental (`generation`) à chaque enregistrement d'outil (`_toolGenerationCounter`).
- **Invalidation Immédiate lors de `unregisterTool()`** : Toutes les confirmations en attente pour un outil supprimé voient leur référence invalidée.
- **Protection Anti-Réutilisation après Réenregistrement** : Toute tentative d'utiliser un jeton émis pour une ancienne définition après suppression et réenregistrement d'un outil avec le même identifiant est strictement rejetée avec `PERMISSION_DENIED`.
- **Préservation Intégrale des Garanties v0.6.4.3** : Requête immuable, vérification stricte des arguments, protection anti-rejeu, gestion de `BUSY` et file bornée (`_maxPendingConfirmations`).

### 5. P2 — Cohérence des Événements Applicatifs (`VoiceUxLiveDemo`)
- **Régularisation du Statut `FORWARD_AI`** : Dans l'attente de l'intégration IA complète en v0.6.5, les actions `FORWARD_AI` ne sont plus notifiées comme `COMPLETED`, mais signalées comme différées/rejetées (`notifyActionRejected`) avec journalisation claire.

### 6. P2 — Robustesse des Scripts Windows (`tests/run_tests.bat`)
- **Résolution Multi-Compilateurs** : Détection dans l'ordre de `%CXX%`, `%GCC%`, `where.exe g++`, et de la chaîne d'outils MinGW de PlatformIO. Prise en charge des chemins avec espaces.
- **Gestion Sécurisée d'ERRORLEVEL** : Élimination des pièges d'expansion retardée par l'usage systématique de `if not errorlevel 1`.

---

## [0.6.4.3] - 2026-11-10 (Corrective & Stabilisation — Sécurité ToolRegistry, VoiceUxLiveBridge & PTT Déterministe)

### 1. Sécurisation P0 du Registre d'Outils (`ToolRegistry`, `ToolExecutor`)
- **Requête Immuable & Protection Contre l'Altération des Paramètres** :
  - Chaque demande de confirmation sensible conserve une copie immuable de l'identifiant et des arguments d'origine (`PendingConfirmation::args`).
  - L'utilisation d'un jeton avec des arguments différents est strictement interdite dans `execute(id, args, token)` et retourne immédiatement `PERMISSION_DENIED`.
  - L'exécution confirmée via `confirm(token)` ou `execute(id, args, token)` utilise prioritairement la requête conservée et validée lors de la demande.
- **Revalidation Systématique des Autorisations & Cycle de Vie** :
  - `confirm()` revalide systématiquement l'existence de l'outil (`findTool`) et réapplique la politique d'autorisation (`AuthorizationChecker`) avec les arguments conservés.
  - Rejet déterministe avec `NOT_FOUND` si la commande a été désenregistrée avant sa confirmation.
  - Rejet avec `PERMISSION_DENIED` si les autorisations ont été révoquées entre la demande et la confirmation.
- **Consommation Unique et Protection Anti-Rejeu Étendue** :
  - Le jeton est marqué comme consommé (`used = true`) dès que l'exécuteur accepte la commande.
  - Garantit qu'une confirmation ne déclenche jamais deux exécutions, y compris en cas d'erreur de la fonction d'exécution (`EXECUTION_FAILED`).
- **Gestion Déterministe de la Concurrence (`BUSY`) & File Bornée** :
  - Si l'exécuteur est occupé (`isBusy()`), `confirm()` et `execute()` retournent `BUSY` sans consommer le jeton, permettant une confirmation ultérieure dès libération.
  - Limitation de la file de confirmations en attente via `_maxPendingConfirmations` (défaut : 8) pour empêcher l'épuisement de la RAM sur ESP32-S3.
  - Nettoyage des confirmations expirées lors des nouvelles demandes.
- **Annulation des Tâches Différées sur Exécuteur Personnalisé** :
  - Ajout de `virtual void cancelDeferred() {}` dans `IToolExecutor` et implémentation dans `ToolExecutor`.
  - `ToolRegistry::cancelPending()` et `ToolRegistry::clear()` délèguent l'annulation à l'exécuteur actif via `getExecutor().cancelDeferred()`.
  - Documentation claire de l'exécution différée synchrone dans `update()` (Option A) imposant des handlers courts (< 10-20 ms).

### 2. Fiabilisation P1 de `VoiceUxLiveBridge`
- **Cycle de Vie des Métadonnées TTS (Défaut A)** :
  - Conservation des métadonnées actives dans `_activeAction` (`isRecall`, `isFeedback`, `turnId`, texte) lors de `consumePendingAction()`.
  - `notifySpeechStarted()` consulte ces attributs préservés et initialise fidèlement `_currentSpeechIsRecall` et `_currentSpeechIsFeedback`.
  - Une réponse répétée (`REPEAT`) n'est plus ré-enregistrée comme une nouvelle réponse dans `ReplyMemory`.
  - Les réponses système et de feedback (« Aucune réponse à répéter. », « Aucune réponse précédente disponible. ») ne polluent plus la mémoire conversationnelle.
  - Une restitution vocale interrompue par STOP n'est jamais enregistrée comme terminée.
- **Machine à États Robuste (Défaut B)** :
  - Audit et sécurisation des transitions `READY → LISTENING → TRANSCRIBING → ACTION_PENDING → SPEAKING → READY`.
  - Transitions fiabilisées lors d'un STOP pendant LISTENING, TRANSCRIBING, ACTION_PENDING ou SPEAKING.
  - Gestion déterministe d'un refus de démarrage TTS via `notifySpeechRefused(reason)` rétablissant immédiatement l'état `READY`.
  - Récupération déterministe après erreur réseau via `recoverFromError()`.
  - Dédoublonnage strict des transcriptions finales et rejet des transcriptions d'un tour annulé ou antérieur.
- **Statuts Fins d'Exécution d'Action (Défaut C)** :
  - Distinction explicite des étapes via `VoiceUxActionStatus` : `CREATED`, `DISPATCHED`, `ACCEPTED`, `COMPLETED`, `REJECTED`, `CANCELLED`.
  - `_actionExecutedCb` n'est plus émis prématurément lors du dépilage de l'action, mais uniquement lors de son achèvement effectif (`notifySpeechFinished(true)` ou `notifyActionCompleted()`).
  - Ajout du callback d'observation de statut `onActionStatus()`.

### 3. PTT Déterministe & Source Unique de Vérité (`examples/VoiceUxLiveDemo`)
- **Source Unique de Vérité pour les Transitions d'Écoute** :
  - `s_bridge.onListeningStarted()` et `s_bridge.onListeningFinished()` sont désormais notifiés exclusivement depuis les callbacks confirmés du moteur vocal (`s_voice.onListeningStarted()`, `s_voice.onListeningFinished()`).
  - Suppression des notifications directes redondantes depuis les gestionnaires tactiles ou la console série.
- **Sécurisation des Variables d'Interface** :
  - `s_touchListening` ne bascule à `true` que si `s_voice.startListening()` retourne `true`.
  - Contrôle systématique des valeurs de retour de `startListening()`, `stopListening()` et `say()`.
  - Si `s_voice.say()` échoue ou est refusé, appel immédiat de `s_bridge.notifySpeechRefused()`, empêchant tout blocage en état `SPEAKING`.

### 4. Tests & Distribution
- **Nouvelles Suites de Non-Régression** :
  - `TEST SUITE 139` : Validation complète de la sécurité de `ToolRegistry` (immuabilité des arguments, rejeu, expiration, réutilisation après erreur, exécuteur occupé, file bornée, annulation custom executor).
  - `TEST SUITE 140` : Validation complète de `VoiceUxLiveBridge` (cycle TTS avec métadonnées, non-pollution de mémoire sur REPEAT/feedback/interruption, transitions STOP, refus TTS, dédoublonnage, échecs réseau, statuts d'action).
  - Total : **140 suites de tests automatiques réussies (2662 assertions, 0 échec)**.
- **Compilateur Portable & Validation Externe** :
  - `tests/run_tests.bat` : Résolution dynamique du compilateur C++ (`%CXX%`, `%GCC%`, `PATH`, puis `.platformio`) éliminant le chemin absolu utilisateur.
  - `tests/external-consumer` : Actualisation vers la version courante de VoiceKit via symlink PlatformIO, éliminant la dépendance au tag obsolète v0.3.0.
  - Validation réussie de compilation PlatformIO pour M5Stack CoreS3 (`VoiceUxLiveDemo`, `LocalToolRegistryDemo`, `external-consumer`).

---

### 1. Pont Applicatif Voice UX Live Bridge (`VoiceUxLiveBridge.h/.cpp`)
- **Adaptateur Découplé Indépendant (`src/core/voiceux/VoiceUxLiveBridge.h/.cpp`)** :
  - Orchestration temps réel entre la reconnaissance vocale STT du microphone CoreS3, l'arbitrage d'intentions locales `VoiceUxController`, l'historique conversationnel `ReplyMemory` et la restitution vocale TTS de VoiceKit.
  - Découplage strict des tâches : aucune opération audio (TTS) n'est exécutée directement depuis un callback STT ou une tâche réseau ; les actions sont mises en attente sous forme de requêtes structurées (`VoiceUxBridgeAction`) et consommées depuis la boucle principale applicative (`loop()`).
  - Ne prend pas possession du microphone (la gestion matérielle reste assurée exclusivement par VoiceKit).
- **Exécution Locale Déterministe des Commandes Conversationnelles** :
  - `REPEAT` : « Répète » récupère instantanément la dernière réponse complète mémorisée dans `ReplyMemory` et demande sa restitution TTS locale sans appel au modèle d'IA distant.
  - `PREVIOUS` : « Réponse précédente » récupère l'avant-dernière réponse et déclenche la synthèse vocale locale.
  - `STOP` : Commande prioritaire tactile, série ou vocale interrompant immédiatement toute opération audio en cours.
  - `PAUSE` / `RESUME` : Définition formelle de l'état `PAUSED` ignorant les requêtes conversationnelles ordinaires et n'autorisant que la reprise vocale (« Continue ») ou tactile.
  - `SLOWER` / `REPHRASE` : Intentions reconnues et loguées pour diagnostic sans simulation d'altération de vitesse.
- **Machine à États Robuste & Sûreté Audio** :
  - États explicites : `READY`, `LISTENING`, `TRANSCRIBING`, `VOICE_UX_PROCESSING`, `ACTION_PENDING`, `SPEAKING`, `PAUSED`, `ERROR`.
  - Aucune capture micro concurrente (`canStartListening()` verrouillé).
  - Aucune restitution TTS pendant une capture active (`canSpeak()` verrouillé).
  - Suivi par identifiant de génération de tour (`turnId`) : rejet strict des transcriptions partielles (intérim), dédoublonnage des transcriptions finales d'un même tour, et rejet des événements tardifs ou annulés.
  - Mémorisation exclusive des restitutions complètes validées : non-mémorisation des réponses partielles, annulées ou répétitions.
  - Gestion d'erreur non-bloquante avec récupération contrôlée (`recoverFromError()`).

### 2. Nouvelle Démonstration Matérielle CoreS3 (`examples/VoiceUxLiveDemo/`)
- Application complète pour M5Stack CoreS3 (ESP32-S3) combinant capture vocale réelle et arbitrage local.
- Interface M5Unified / M5GFX (320 × 240) épurée, sans dépendance LVGL 9, avec boutons tactiles `PARLER` (PTT) et `STOP`.
- Pré-chargement des deux réponses déterministes de test prescrites par le cahier des charges :
  1. « La température actuelle est de vingt-deux degrés. »
  2. « L'humidité relative est de quarante-huit pour cent. »
- Console de diagnostic série à 115 200 bauds avec mode hybride voix réelle (`[STT][REAL]`) et simulation (`[STT][SIM]`).
- Compilation réussie sous PlatformIO `espressif32@6.9.0` (RAM: 24.2%, Flash: 81.9%).

### 3. Validation Exhaustive & Non-Régression
- **4 Nouvelles Suites de Tests Unitaires (Suites 135 à 138)** :
  - Suite 135 : Transmission STT, filtrage intérim, commandes `REPEAT` (mémoire vide et avec mémoire), `PREVIOUS` et `STOP`.
  - Suite 136 : Gestion de la pause, rejet des demandes ordinaires en pause, reprise vocale et tactile (`PAUSE` / `RESUME`).
  - Suite 137 : Sécurité, concurrence, dédoublonnage de tour, événements tardifs, annulations et `FORWARD_AI`.
  - Suite 138 : Machine à états complète, cycle TTS, non double restitution, conservation des réponses validées et résilience d'erreur.
- **Bilan Global** : **2 579 tests unitaires réussis avec 0 échec** (138 suites de tests).
- Compilation validée à 100% sur les 8 exemples cibles : `VoiceUxLiveDemo`, `VoiceUxDemo`, `MinimalSTT`, `ContinuousSTT`, `MinimalTTS`, `WakeWordConversation`, `OpenAIConversationLVGL`, `LocalToolRegistryDemo`.

---

## [0.6.4.1] - 2026-11-06 (Voice UX Foundation — Filtrage TextGate, Intentions Locales & ReplyMemory)

### 1. Sous-Système Voice UX (`src/core/voiceux/`)
- **Architecture Modulaire et Découplée (`VoiceUxController`, `TextGate`, `VoiceIntentRouter`, `ReplyMemory`, `VoiceUxTypes`)** :
  - Couche logicielle expérimentale autonome permettant d'analyser localement les transcriptions textuelles finales et de gérer les interactions conversationnelles courantes en français sans appel IA ni connexion Internet.
  - Fonctionne de manière déterministe en quelques microsecondes sur ESP32-S3 (1 à 4 µs mesurés en natif).
  - Désactivée par défaut (`_enabled = false`) : aucun effet sur les applications existantes qui ne l'utilisent pas.
  - Neutralité matérielle absolue : `VoiceUxController` n'appelle jamais directement `say()`, `stop()`, `listen()` ou les contrôleurs audio ; il émet des événements structurés et laisse l'application maîtresse décider des actions.
- **Filtre Déterministe de Transcriptions (`TextGate.h/.cpp`)** :
  - Décisions : `ACCEPT` (texte exploitable), `IGNORE` (transcription vide, bruit ou hésitation isolée), `INCOMPLETE` (informatif).
  - Rejet des chaînes vides et espaces/ponctuation seuls.
  - Rejet des hésitations isolées (« euh », « heu », « hum », « hmm », « hm », etc.) y compris combinées ou ponctuées (« Euh... », « hum ! », « euh hum »).
  - Préservation intégrale des commandes courtes valides (« oui », « non », « stop », « répète », « pause »).
  - Ne rejette jamais une phrase entière sur la simple présence d'une hésitation (« Euh quelle heure est-il ? » -> `ACCEPT`).
  - Détection informative des énoncés incomplets (points de suspension ou conjonction/préposition orpheline en fin de phrase).
  - Normalisation lexicale interne pour l'analyse avec conservation scrupuleuse de la chaîne originale (`rawText`).
- **Routeur d'Intentions Conversationnelles Françaises (`VoiceIntentRouter.h/.cpp`)** :
  - Détection locale de 7 intentions majeures : `REPEAT`, `PREVIOUS`, `STOP`, `PAUSE`, `RESUME`, `SLOWER`, `REPHRASE` (et `NONE`).
  - Tolérance stricte des formules de politesse en préfixe ou suffixe (« s'il te plaît », « merci », « peux-tu », « s'il vous plaît »).
  - Insensibilité à la casse, à la ponctuation et aux accents français.
  - Protection absolue contre les faux positifs : les énoncés avec complément d'objet (« Arrête la minuterie », « Continue cette équation », « Peux-tu répéter cette expérience scientifique ? ») ou questions complexes (« Pourquoi la voiture s'arrête-t-elle ? ») retournent strictement `NONE`.
- **Mémoire Conversationnelle Bornée en RAM (`ReplyMemory.h/.cpp`)** :
  - Mémorisation des 5 dernières réponses complètes de l'assistant (capacité configurable).
  - Stockage en RAM uniquement (zéro écriture Flash, zéro base de données).
  - Éviction FIFO de la réponse la plus ancienne en cas de saturation.
  - Consultation de la dernière réponse (`getLastReply()`), de la précédente (`getPreviousReply()`) ou parcours d'historique (`getReplyAt()`).
  - Borne stricte de taille (1024 octets par défaut) avec troncature sécurisée respectant les limites des séquences UTF-8 RFC 3629 (aucun point de code scindé).
- **Façade Unifiée (`VoiceUxController.h/.cpp`)** :
  - API claire : `begin()`, `setEnabled()`, `isEnabled()`, `processFinalText()`, `rememberReply()`, `update()`.
  - Notification par callback d'événement : `onResult(ResultCallback)`.
  - Compteurs statistiques de production (`processedCount`, `acceptedCount`, `ignoredCount`, `recognizedIntentCount`).

### 2. Nouvel Exemple Officiel `examples/VoiceUxDemo/`
- Exemple autonome sur M5Stack CoreS3 **sans Wi-Fi, sans clé API et sans capture audio**.
- Interface double écran M5GFX (320 × 240) et moniteur série interactif (115 200 bauds).
- Commandes série : `T <texte>` (analyser), `M <reponse>` (mémoriser), `H` (historique), `C` (effacer), `D` (activer/désactiver).
- Bascule tactile directe par appui sur l'écran du CoreS3.
- Compilation réussie sous PlatformIO `espressif32@6.9.0` (RAM: 7.0%, Flash: 25.2%).

### 3. Tests Automatisés & Non-Régression
- **5 Nouvelles Suites de Tests Unitaires (Suites 130 à 134)** :
  - Suite 130 : Validation complète de TextGate (chaîne vide, hésitations, commandes courtes, accents, phrases incomplètes).
  - Suite 131 : Validation de VoiceIntentRouter (toutes les intentions, politesse, casse, accents, faux positifs critiques, commandes inconnues).
  - Suite 132 : Cycle de vie de ReplyMemory (capacité 5, éviction FIFO, recul, clear, troncature et intégrité UTF-8).
  - Suite 133 : Façade VoiceUxController (activation/désactivation, événements, neutralité audio, rappels de mémoire).
  - Suite 134 : Performances, déterminisme et stabilité sur 100 requêtes consécutives (latences mesurées de 1 à 4 µs).
- **Bilan Global** : **2 487 tests réussis avec 0 échec** (134 suites de tests).
- Zéro régression sur `LocalToolRegistryDemo`, `ContinuousSTT` et l'ensemble de la chaîne audio de VoiceKit.

---

## [0.6.4] - 2026-11-03 (Local Tool Registry — Registre Local d'Outils pour M5Stack CoreS3)

### 1. Registre Local d'Outils (`src/core/tools/`)
- **Infrastructure Autonome Découplée (`ToolRegistry`, `ToolDefinition`, `ToolExecutor`, `ToolValidator`, `ToolEvents`)** :
  - Permet aux applications ESP32-S3 d'enregistrer, de découvrir, de valider et d'exécuter des commandes locales de façon déterministe et autonome.
  - Fonctionne sans Wi-Fi, sans initialisation audio, et indépendamment de Gemini Live et d'OpenAI Realtime.
  - Conçu comme la fondation d'un futur système d'appels d'outils IA et d'une éventuelle interopérabilité MCP (sans annoncer de support prématuré dans cette version).
- **Capacité, Enregistrement & Découverte (`ToolRegistry.h/.cpp`)** :
  - Capacité initiale configurable (16 commandes par défaut), extensible dynamiquement (`maxTools`).
  - Refus catégorique des identifiants dupliqués (`registerTool` renvoie `false`).
  - Refus des déclarations invalides (`isValid()` vérifie l'identifiant non vide et le gestionnaire non nul).
  - Méthodes d'inspection et de découverte : `getToolCount()`, `hasTool(id)`, `findTool(id)`, `getToolIds()`.
- **Validation Stricte des Paramètres (`ToolValidator.h`)** :
  - Typage des paramètres : `ToolParamType` (`STRING`, `INT`, `FLOAT`, `BOOLEAN`).
  - Paramètres obligatoires et optionnels avec valeurs par défaut injectées automatiquement si omises.
  - Bornage numérique automatique : `withRange(min, max)` pour entiers et flottants.
  - Rejet systématique des arguments inconnus non déclarés, sauf autorisation explicite (`allowUnknownParams = true`).
  - Échec immédiat de la validation si le type, l'existence ou les bornes ne correspondent pas : le gestionnaire d'exécution n'est jamais appelé si la validation échoue.
- **Politiques de Sécurité & Confirmation Sensible (`ToolSecurityPolicy`)** :
  - Trois niveaux stricts : `READ_ONLY` (lecture/télémétrie), `WRITE` (modification autorisée), `SENSITIVE` (opération critique).
  - Une commande `SENSITIVE` ne s'exécute jamais automatiquement : un appel direct `execute()` retourne `CONFIRMATION_REQUIRED` et génère un jeton cryptographiquement robuste (`CONF-xxxxxxxx`).
  - Jeton lié à une requête précise (identifiant d'outil + arguments), limité dans le temps (expiration configurable, 30 s par défaut) et protégé contre le rejeu (invalidation immédiate après usage).
  - Méthode d'exécution confirmée : `executeWithConfirmation(id, args, token)` et méthode d'annulation explicite : `cancelConfirmation(token)`.
  - Hook d'autorisation personnalisé : `setAuthorizationChecker(checker)` permettant de rejeter une commande en amont avec le statut `PERMISSION_DENIED`.
- **Exécution Contrôlée & Protection Anti-Réentrance (`ToolExecutor.h`)** :
  - Garde atomique anti-réentrance (`_isBusy.exchange(true)`) : toute exécution concurrente sur le même exécuteur retourne immédiatement `BUSY`.
  - Encadrement d'exécution avec interception des exceptions (`EXECUTION_FAILED`).
  - File d'exécution différée (`enqueueDeferred`, `update()`) pour différer les opérations matérielles longues hors des contextes critiques.
- **Cycle de Vie & Événements Applicatifs (`ToolEvents.h`)** :
  - 5 événements standardisés émis vers `ToolEventCallback` :
    - `TOOL_REQUESTED` : Dès la réception de la demande d'exécution.
    - `TOOL_STARTED` : Dès le démarrage effectif du gestionnaire après validation.
    - `TOOL_COMPLETED` : Lors du succès de l'exécution avec le résultat associé.
    - `TOOL_FAILED` : Lors d'un échec (validation, non trouvé, non autorisé, erreur d'exécution).
    - `TOOL_CONFIRMATION_REQUIRED` : Lorsqu'une commande sensible attend son jeton.
  - Prêt pour l'intégration de retours visuels dans VoiceKit-SmartUI.

### 2. Nouvel Exemple Officiel `examples/LocalToolRegistryDemo/`
- Exemple autonome sur M5Stack CoreS3 **sans Wi-Fi, sans Gemini Live ni OpenAI**.
- Enregistrement de 4 commandes réelles :
  - `system.get_status` (`READ_ONLY`) : Télémétrie CoreS3 (tension batterie, Heap libre, PSRAM libre, uptime).
  - `audio.set_volume` (`WRITE`) : Réglage du volume audio matériel (0-100) avec confirmation sonore (bip AW88298).
  - `display.set_brightness` (`WRITE`) : Réglage du rétroéclairage LCD matériel (0-255).
  - `system.factory_reset` (`SENSITIVE`) : Exemple pédagogique de commande critique exigeant un jeton de confirmation.
- Tableau de bord graphique M5GFX (320 × 240) et moniteur série interactif (options 1 à 9 et commande help).
- Compilation réussie sous PlatformIO `espressif32@6.9.0` (RAM: 7.1%, Flash: 26.4%).

### 3. Tests Automatisés & Non-Régression
- **17 Nouvelles Suites de Tests Unitaires (Suites 113 à 129)** :
  - Couverture exhaustive des 16 exigences du cahier des charges :
    1. Enregistrement valide.
    2. Identifiant dupliqué.
    3. Capacité maximale.
    4. Recherche d'une commande inexistante.
    5. Exécution réussie.
    6. Paramètre manquant.
    7. Paramètre de type incorrect.
    8. Valeur hors limites.
    9. Paramètre inconnu (rejet et acceptation).
    10. Commande non autorisée.
    11. Confirmation sensible requise et exécution confirmée.
    12. Confirmation expirée ou réutilisée (anti-rejeu).
    13. Erreur de fonction d'exécution.
    14. Double demande d'exécution (protection anti-réentrance BUSY).
    15. Ordre correct des événements émis.
    16. Libération des ressources et désenregistrement.
    17. Validation des trois commandes de l'exemple (`system.get_status`, `audio.set_volume`, `display.set_brightness`).
- **Résultat Global** : **2 320 tests unitaires validés avec 0 échec** (contre 2 100 en v0.6.3.1).
- **Zéro Régression** : Vérification de la compilation sans erreur des exemples préexistants (`MinimalTTS`, `OrientationClockVoice` avec LVGL 9, `MinimalVoiceKit`).
- **Sanctuaire Audio Intact** : Pipeline VoxCore, WakeNet et liaisons WebSocket TLS Gemini/OpenAI inchangés.

### 4. Documentation
- Création du document d'architecture : `docs/architecture_tools_registry_ai_mcp.md` détaillant les distinctions et synergies entre Registre Local, IA Tool Calling et MCP.
- Mise à jour de `library.json` et `library.properties` en version `0.6.4`.
- Mise à jour complète du `README.md`.

---

## [0.6.3.1] - 2026-10-29 (Corrective de stabilité et WakeNet configurable — M5Stack CoreS3)

### 1. Correction Prioritaire — Latence de `voice.update()` & Transitions Non Bloquantes
- **Identification de la Cause Racine du Pic `voice.update() = 582 701 µs` (`controller.update() = 582 454 µs`)** :
  - L'analyse du désassemblage de `libesp_websocket_client.a` (ESP-IDF v4.4) démontre que `esp_websocket_client_stop()` place `client->run = 0` puis bloque de manière synchrone sur `xEventGroupWaitBits(client->status_bits, STOPPED_BIT, pdFALSE, pdTRUE, portMAX_DELAY)` tant que la tâche interne `websocket_task` est bloquée dans `esp_transport_poll_read(1000)` (`select()` avec timeout de 1 000 ms).
  - En outre, dans `ENDING_SESSION`, `_returnToWakeStartMs` était initialisé avec le timestamp capturé *avant* l'appel à `_session->deactivate()`, ce qui faisait que `582 ms >= 250 ms` court-circuitait la garde acoustique de `250 ms` et l'affichage de `RETOUR EN ÉCOUTE` dès le tick suivant.
- **Arrêt WebSocket Non Bloquant en Deux Phases (`EspWebSocketTransport`, `IVoiceProvider`, `LiveSessionController`)** :
  - Ajout de `requestDisconnect()`, `pollDisconnect()` et `isDisconnectPending()` sur `ILiveTransport` / `EspWebSocketTransport`, propagés à `IVoiceProvider`, `GeminiVoiceProvider`, `GeminiLiveClient`, `OpenAIRealtimeClient` et `LiveSessionController` (`requestDeactivate()`, `pollDeactivate()`, `isDeactivating()`).
  - Phase 1 (`requestDisconnect()`) : signale l'arrêt à `websocket_task` en $< 2\text{ µs}$ sans attente bloquante.
  - Phase 2 (`pollDisconnect()`) : interroge `STOPPED_BIT` avec un délai de `0` tick à chaque appel `voice.update()` et détruit le client en $< 100\text{ µs}$ dès que `websocket_task` est sortie de `poll_read`, **sans créer aucune tâche FreeRTOS supplémentaire**.
  - Ancrage de `_returnToWakeStartMs` sur l'horloge système *après* achèvement effectif de la désactivation, préservant systématiquement la garde acoustique de `250 ms` et la visibilité de `RETOUR EN ÉCOUTE`.
- **Optimisation des Transitions Microphone et de Fermeture de Session** :
  - Séparation de la détection acoustique `WakeNet::detect()` (tick $N$, avec signalement asynchrone immédiat `requestStopLocalCaptureAsync()`) et de la finalisation `ensureCaptureStopped()` (tick $N+1$ dans `WAKE_DETECTED`).
  - Dans `LiveAudioStreamer::stopMicHardware()` et `HardwareManager::restoreSpeakerAfterMic()`, appel de `_audioInput->end()` avant la boucle de vérification `isRecording() != 0` et suppression du `delay(5)` redondant.
  - Ajout de `ConversationController::abortTurnForSessionClose()` / `LiveSessionController::abortTurnLocally()` pour éviter l'émission d'une trame WebSocket `sendInterruption()` inutile juste avant la fermeture de la session.
  - Dans `LiveSessionController::deactivate()` et `requestDeactivate()`, `_player->stop()` n'est invoqué que si le lecteur audio est réellement actif ou contient des échantillons en tampon.

### 2. Consolidation des Transitions Audio & Propriétaire Unique du Microphone
- **Suivi Explicite du Propriétaire Unique (`HardwareManager`)** :
  - Ajout du suivi du propriétaire courant (`_micOwner`, `getMicOwner()`) dans `HardwareManager::prepareMicCapture(owner)` et du compteur d'échecs de réservation (`getMicReservationFailureCount()`, `recordMicReservationFailure()`).
  - Garantie sur tout le cycle `WAKE_ARMED -> WAKE_DETECTED -> PREPARING_SESSION -> CONNECTING -> CONVERSATION_LISTENING -> CONVERSATION_SPEAKING -> ENDING_SESSION -> RETURNING_TO_WAKE -> WAKE_ARMED` : un seul propriétaire du microphone, aucun double démarrage I2S, aucune reprise WakeNet pendant la conversation, aucune fermeture prématurée du lecteur audio, aucun WebSocket résiduel après une session terminée, et préservation intégrale des protections de `WakeWordAudioBridge` v0.6.2.1.

### 3. Sélection Configurable du Modèle WakeNet & Vérification à 4 Niveaux
- **Configuration Explicite du Modèle (`VoiceKitConfig`, `WakeNetEngine`, `WakeWordManager`)** :
  - Conservation obligatoire de `config.wakeWordPhrase = "Bonjour ESP"` et de `config.wakeWordModelName = "wn10_fr_bonjouresp"` comme valeurs par défaut.
  - Ajout de `setWakeWordModelName(const String&)`, `getWakeWordModelName()`, `getResolvedWakeWordModelName()` et `isWakeWordPhraseConsistentWithModel()` sur `WakeWordManager`, `VoxCoreVoice` et `VoiceKit`.
  - Vérification stricte en 4 étapes avant toute activation dans `WakeWordManager::enable(true)` : (1) support WakeNet compilé, (2) présence effective du modèle sélectionné dans la partition Flash `srmodels`, (3) initialisation du modèle, (4) reconnaissance acoustique réellement active.
  - Aucun modèle fictif ajouté, aucune supposition qu'une modification de `wakeWordPhrase` entraîne l'apprentissage d'une nouvelle phrase acoustique, aucun remplacement implicite d'un modèle absent par le moteur `TEST`, et extensibilité conservée pour un futur moteur `microWakeWord` (`IWakeWordEngine`).

### 4. Diagnostics Enrichis Non Bloquants & Interface `WakeWordConversation`
- **Instrumentation Séparée par Sous-Système (`WakeWordConversationMetrics`)** :
  - Durée moyenne, maximale et nombre d'appels de `voice.update()` (`avgVoiceUpdateDurationUs`, `maxVoiceUpdateDurationUs`, `voiceUpdateCallCount`).
  - Durées maximales séparées de `WakeWordConversationController::update()`, des transitions d'état (`maxStateTransitionDurationUs`), de `WakeWordManager::update()`, de `LiveSessionController::tick()`, des traitements WebSocket (`maxWsProcessDurationUs`), des transitions microphone (`maxMicTransitionDurationUs`), de l'arrêt de session (`maxSessionStopDurationUs`) et de la réactivation WakeNet (`maxWakeReactivationDurationUs`).
  - Compteurs de sessions réussies (`successfulSessionCount`), de reprises après erreur (`recoveryCount`), d'échecs de réservation microphone (`micReservationFailures`), de saturations (`wakeQueueSaturations`) et de pertes de trames Wake Word (`wakeFramesDropped`).
- **Interface `WakeWordConversation` Pilotée par Événements Réels** :
  - Ajout de `getUiTransitionBannerText()` / `getWakeWordConversationBannerText()` et émission immédiate de `USER_LISTENING` dès la première trame de parole confirmée par le VAD local, rendant immédiatement visibles les 7 transitions requises (`BONJOUR ESP DÉTECTÉ`, `CONNEXION`, `PRÊT — PARLEZ`, `ÉCOUTE`, `RÉPONSE`, `FIN DE CONVERSATION`, `RETOUR EN ÉCOUTE`) sans aucun délai arbitraire.
- **5 Nouvelles Suites de Tests Unitaires Correctifs (Suites 101 à 105)** : **2 100 validations réussies avec 0 échec** sur 105 suites.

---

## [0.6.3] - 2026-10-29 (Wake Word Driven Conversation — Conversation vocale mains libres déclenchée par « Bonjour ESP »)

### Nouveau Composant Réutilisable `WakeWordConversationController` (`src/core/WakeWordConversationController.h/.cpp`)
- **Orchestration Mains Libres Non Bloquante (`update()`)** :
  - Implémentation du cycle complet et répétable sans redémarrage sur M5Stack CoreS3 :
    1. Écoute locale hors ligne de « Bonjour ESP » via `WakeWordManager` (`WAKE_ARMED`).
    2. Détection acoustique confirmée (`WAKE_DETECTED`).
    3. Suspension de WakeNet et libération contrôlée du microphone ES7210 (`PREPARING_SESSION`).
    4. Connexion au fournisseur vocal configuré (`CONNECTING` — Gemini Live par défaut ou OpenAI Realtime).
    5. Activation automatique de la conversation après confirmation `setupComplete` (`CONVERSATION_LISTENING`).
    6. Échanges vocaux bidirectionnels multi-tours (`CONVERSATION_LISTENING` $\leftrightarrow$ `CONVERSATION_SPEAKING`).
    7. Détection de la fin de session (timeout de silence 30 s, durée maximale, arrêt manuel ou commande vocale confirmée).
    8. Arrêt complet de la conversation après restitution intégrale des derniers échantillons audio et libération des ressources réseau/audio (`ENDING_SESSION`).
    9. Réactivation contrôlée de WakeNet (`RETURNING_TO_WAKE`).
    10. Retour à l'état `WAKE_ARMED`.
  - Séparation stricte des états de la session conversationnelle (`WakeWordConversationState`) et des états internes du détecteur (`WakeWordState` dans `WakeWordManager`).
  - Réutilisation intégrale des composants existants (`LiveAudioStreamer`, `LiveSessionController`, `ConversationController`, `LiveAudioPlayer`, `IVoiceProvider`) sans second gestionnaire microphone, sans second transport WebSocket, sans seconde chaîne de lecture audio et sans tâche FreeRTOS supplémentaire.

### Gestion des Événements & Protections de Concurrence
- **Traitement Différé dans `update()`** :
  - Le callback `onWakeWordDetected()` (via le hook interne `setInternalEventHook()` de `WakeWordManager`) n'exécute aucune opération longue : il enregistre uniquement une demande atomique de démarrage (`_wakeRequestPending`), traitée ensuite par `WakeWordConversationController::update()`.
- **Protections Systématiques** :
  - Rejet et comptabilisation des détections simultanées ou concurrentes pendant une connexion ou une session active (`ignoredConcurrentDetections`).
  - Garde anti-réentrance (`_inUpdate`) protégeant tous les callbacks utilisateur (`stopWakeWordConversation()`, `requestEndWakeWordConversationAfterTurn()`, `disableWakeWordConversation()`).
  - Interdiction de réactivation prématurée de WakeNet tant que la capture réseau, la lecture audio ou la quarantaine FreeRTOS ne sont pas formellement terminées.
  - Vérification stricte de `stopLocalCapture()` et de `HardwareManager::isMicCaptureReserved()` avant toute ouverture de capture conversationnelle.
  - Nettoyage automatique des sessions WebSocket résiduelles avant connexion et en fin de session.

### Conversation Multi-Tours, Fournisseurs Vocaux & Gestion Audio
- **Compatibilité `IVoiceProvider` (Gemini Live & OpenAI Realtime)** :
  - Fournisseur par défaut : `VoiceProviderType::GEMINI` (`GeminiVoiceProvider`).
  - Fournisseur alternatif : `VoiceProviderType::OPENAI` (`OpenAIRealtimeClient`).
  - Démarrage de la capture conversationnelle uniquement lorsque `isConnected() && isSetupComplete()` sont confirmés.
- **Session Multi-Tours & Conditions de Fin** :
  - La fin d'un tour de réponse TTS (`CONVERSATION_SPEAKING` $\to$ `CONVERSATION_LISTENING`) relance automatiquement l'écoute du tour suivant sans répéter « Bonjour ESP » et n'est jamais confondue avec la fin de la session.
  - Timeout configurable de silence / d'inactivité (30 000 ms par défaut), durée maximale de session configurable, arrêt manuel explicite (`stopWakeWordConversation(immediate)`) et fermeture par commande vocale (*« au revoir »*, *« arrête la conversation »*, *« fin de conversation »*, *« stop »*, *« quitter »*) **uniquement** sur transcription finale confirmée (`isFinal == true`).
  - En fermeture normale (commande vocale ou fin après tour), attente de la restitution intégrale de tous les échantillons PCM 24 kHz dans `LiveAudioPlayer` avant la fermeture de session.

### Interface Applicative, Compatibilité LVGL 9, Nouvel Exemple & Diagnostics
- **API Rétrocompatible sur `VoxCoreVoice` et `VoiceKit`** :
  - Activation/désactivation (`enableWakeWordConversation()`, `disableWakeWordConversation()`, `config.enableWakeWordConversation`), 8 callbacks dédiés exécutés sur la boucle principale (`onWakeConvDetected`, `onWakeConvConnecting`, `onWakeConvReady`, `onWakeConvListening`, `onWakeConvSpeaking`, `onWakeConvEnded`, `onWakeConvReturnedToWake`, `onWakeConvError`) et file d'événements non bloquante (`pollWakeWordConversationEvent()`) pour LVGL 9.
- **Nouvel Exemple `examples/WakeWordConversation/`** :
  - Application M5Stack CoreS3 avec affichage M5GFX (sans LVGL) présentant les 9 bandeaux d'état requis en français, la distinction stricte entre reconnaissance acoustique réelle (`WAKENET`) et détection simulée (`TEST`), et les compteurs de sessions, détections, reconnexions et erreurs.
- **Métriques Diagnostiques (`WakeWordConversationMetrics`)** :
  - Durées `wakeToConnectStartMs`, `wakeToConversationReadyMs`, `wsConnectMs`, `lastSessionDurationMs`, `totalSessionDurationMs`, `returnToArmedMs`, compteurs d'échecs microphone (`micTransitionFailures`) et réseau (`networkFailures`), mémoire RAM interne et PSRAM libre/minimale, et temps maximal dans `voice.update()` et `controller.update()`.
- **6 Nouvelles Suites de Tests Unitaires (Suites 95 à 100)** : Couverture complète des 12 scénarios de validation et non-régression (**1 989 validations réussies avec 0 échec** sur 100 suites).

---

## [0.6.2.1] - 2026-10-28 (Corrective Wake Word Foundation — Fiabilité et performances)

### Correction Critique — Sûreté Concurrente de `WakeWordAudioBridge`
- **Protection de la Durée de Vie des Trames Consultées (`peekFrame`)** :
  - Désactivation propre de la politique `DROP_OLDEST` dans `setOverflowPolicy()` et `pushAudioChunk()` avec repli automatique sur `DROP_NEWEST` (et compteur de diagnostic `getUnsupportedPolicyRequestCount()`).
  - Garantie mathématique SPSC : seul le consommateur (`update()`) modifie `_tail` via `consumeFrame()`, et lorsque `used < _capacity`, `head % _capacity != tail % _capacity`. Le producteur audio (`live_mic_task`) ne peut donc jamais écraser ni invalider un slot détenu par `peekFrame()` pendant `IWakeWordEngine::process()`.
- **Synchronisation Sans Verrou Bloquant lors des Suspensions et Réinitialisations** :
  - Ajout d'un compteur atomique d'écrivains actifs (`_activeWriters`) avec garde RAII et d'un compteur d'époque (`_epoch`) dans `WakeWordAudioBridge`.
  - Si `setEnabled(false)`, `clear()`, `begin()` ou `end()` est appelé concurremment pendant une écriture `pushAudioChunk()`, l'incrémentation de `_epoch` invalide l'écriture en cours à sa sortie, empêchant toute visibilité d'une trame partiellement écrite ou post-suspension.
  - Protection de `end()` : refus de libérer `_slots` si `_activeWriters > 0`.

### Budget CPU Borné & Fluidité LVGL (`WakeWordManager`)
- **Contrôle du Temps Passé dans `voice.update()`** :
  - Introduction de `setMaxFramesPerUpdate(size_t)` (défaut : `2` trames au lieu de `8`) et `setProcessingBudgetUs(uint32_t)` (défaut : `5000 µs`), exposés sur `WakeWordManager`, `VoxCoreVoice`, `VoiceKit` et `VoiceKitConfig`.
  - Mesure du temps réel consommé par trame (`lastWakeProcessDurationUs`, `maxWakeProcessDurationUs`) et par appel `update()` (`lastUpdateProcessDurationUs`, `maxUpdateProcessDurationUs`).
  - Détection et comptabilisation des dépassements de budget (`processingBudgetExceeded`) avec arrêt anticipé de la boucle de dépilement dès que le budget est atteint.

### Arrêt et Reprise du Microphone (`LiveAudioStreamer` & `WakeWordManager`)
- **Gestion Explicite des Échecs de `stopLocalCapture()`** :
  - Vérification systématique du retour de `stopLocalCapture()` dans `LiveAudioStreamer::start()`, `WakeWordManager::stopCaptureIfNeeded()`, `VoxCoreVoice::say()`, `VoxCoreVoice::startListening()` et `VoiceKit::startContinuousListening()`.
  - En cas d'échec d'arrêt de la capture locale : refus propre du nouveau mode sans seconde initialisation du microphone ES7210, transition vers un état d'erreur explicite (`WakeWordState::ERROR` / `VoiceState::ERROR_STATE`) et incrémentation de `captureStopFailures`.
  - Dans `LiveAudioStreamer::stopLocalCapture()` et `checkQuarantinedTasks()`, `stopMicHardware()` n'est appelé qu'après terminaison confirmée de `live_mic_task`, et la levée ultérieure de quarantaine libère les tampons et la réservation microphone afin que le microphone ne reste jamais bloqué définitivement.

### Machine à États Wake Word, `WakeNetEngine` & Capture Locale
- **Machine à États & Sûreté Mémoire (`WakeWordManager`)** :
  - Protection anti-réentrance dans `update()` (`_inUpdate`) et respect des transitions déclenchées depuis les callbacks (`listen()`, `say()`, `resumeWakeWord()`, `disableWakeWord()`).
  - Idempotence des appels répétés à `enableWakeWord()`, `disableWakeWord()`, `suspendWakeWord()`, `resumeWakeWord()` et transition propre `ERROR -> DISABLED (INACTIVE) -> INITIALIZING -> ARMED`.
  - Lors de `disableWakeWord()`, `_bridge.end()` et `_activeEngine->end()` ne sont appelés qu'après confirmation de l'arrêt du producteur audio.
- **Durcissement de `WakeNetEngine` (`src/core/WakeNetEngine.h`)** :
  - Distinction stricte à 4 niveaux : (1) support compilé (`isCompiledSupportAvailable`), (2) modèle disponible en flash (`isModelAvailable`), (3) moteur initialisé (`isInitialized`), (4) reconnaissance acoustique réellement active (`isRealRecognitionActive`).
  - Libération systématique de `esp_srmodel_deinit(_models)` sur tous les chemins d'échec partiel de `begin()`.
  - Suppression du score de confiance fictif (`res.confidence = 0.0f` au lieu de `0.95f`).
  - Continuité bit-à-bit de l'adaptateur `512 -> 480` sans perte d'échantillons aux frontières même lors d'une détection en milieu de bloc, et remise à zéro complète de `_accumulator` dans `reset()`.
- **Métriques RMS Authentiques, Compteurs & Clarification de l'Affichage `WakeWordFoundation`** :
  - Suppression de l'injection de trame silencieuse fictive (`rms = 0`) dans `triggerTestDetection()` et `handleSerialCommand()`, garantissant que `lastRms`, `peakRms` et `framesReceived` reflètent exclusivement le signal acquis sur le microphone ES7210.
  - Ajout du compteur de réarmements (`rearmCount` dans `WakeWordMetrics`, incrémenté lors de chaque retour à l'état `ARMED` après l'activation initiale), en complément des compteurs de détections (`detectionCount`, comptabilisées une seule fois par événement réel) et d'erreurs (`errorCount`).
  - Clarification de l'affichage série dans `examples/WakeWordFoundation/src/main.cpp` :
    - Encadré très visible `MOT DE RÉVEIL DÉTECTÉ` affichant le texte `Bonjour ESP`, le moteur actif (`RECONNAISSANCE RÉELLE` ou `SIMULATION (TEST)`) et les compteurs de détections, de réarmements et d'erreurs.
    - Distinction explicite des états en français : `ARMED — En écoute`, `DETECTED — Mot reconnu`, `SUSPENDED — Détection suspendue`, puis `ARMED — Écoute réactivée`.
    - Conservation de l'affichage complet des métriques mémoire (RAM/PSRAM) et CPU via la commande série `'m'`, et de la commande `'c'` de cycles d'activation/désactivation.
    - Zéro journal répétitif dans `loop()` (affichage uniquement sur transitions, détections ou commandes) et zéro impression série dans le callback audio critique.
- **6 Nouvelles Suites de Tests Unitaires (Suites 89 à 94)** : **1 844 validations réussies avec 0 échec** (94 suites au total).

---

## [0.6.2] - 2026-10-28 (Wake Word Foundation — M5Stack CoreS3)

### Capture Audio Partagée Hors Ligne (`LiveAudioStreamer` & `VoxCoreVoice`)
- **Propriété Unique du Microphone CoreS3 ES7210** :
  - Ajout du mode d'acquisition locale hors ligne (`startLocalCapture()`, `stopLocalCapture()`, `isLocalCapture()`) dans `LiveAudioStreamer` sans nécessiter de connexion Wi-Fi ni de session WebSocket Gemini/OpenAI.
  - Exécution de `live_mic_task` seule en mode local (sans créer `live_tx_task`, économisant 10 Ko de pile FreeRTOS et zéro tâche supplémentaire).
  - Ajout d'un canal dédié non bloquant (`setWakeWordAudioCallback`) pour alimenter le pont Wake Word en parallèle du callback utilisateur `onAudioChunk()` sans duplication du flux I2S/DMA.
  - Bascule transparente et sans conflit de la capture locale vers le streaming réseau lorsque `start(provider)` est invoqué (STT / Conversation), et reprise automatique à la fin de l'opération vocale.
  - Prise en charge de l'initialisation hors ligne dans `VoxCoreVoice::begin(config)` lorsque `config.apiKey` est vide.

### Nouvelle Architecture Wake Word (`IWakeWordEngine`, `WakeWordAudioBridge`, `WakeWordManager`)
- **Interface Abstraite de Moteur (`IWakeWordEngine.h`)** :
  - Découplage complet entre la fondation audio/logicielle de VoiceKit et le moteur de détection (`WakeNetEngine`, `TestWakeWordEngine`, futur `microWakeWord` ou moteur personnalisé).
  - Distinction stricte et honnête entre : fonction activée (`isWakeWordEnabled`), moteur disponible (`isWakeWordEngineAvailable`), moteur initialisé (`isWakeWordEngineInitialized`), écoute réellement active (`isWakeWordListening`), détection simulée (`isWakeWordSimulated`) et reconnaissance acoustique réelle (`isWakeWordRealRecognitionActive`).
- **Pont Audio Temps Réel Borné (`WakeWordAudioBridge.h/.cpp`)** :
  - File circulaire SPSC sans verrou (8 slots de 512 échantillons = 256 ms @ 16 kHz 16-bit mono, ~8,3 Ko).
  - Contrôle strict des copies : exactement 1 copie (`memcpy` DMA $\to$ slot) dans la tâche audio (< 15 µs), 0 copie supplémentaire côté consommateur (`peekFrame()` / `consumeFrame()` dans `voice.update()`).
  - Zéro allocation dynamique et zéro écriture `Serial` dans le callback audio critique.
  - Politiques explicites de saturation (`DROP_NEWEST` par défaut ou `DROP_OLDEST`) et métriques atomiques de saturation/abandon.
- **Machine à États & Coexistence (`WakeWordManager.h/.cpp`)** :
  - États : `DISABLED` $\to$ `INITIALIZING` $\to$ `ARMED` $\to$ `DETECTED` $\to$ `SUSPENDED` $\to$ `ARMED`, ainsi que `ERROR`.
  - Suspension automatique pendant toute activité vocale (TTS `say()`, STT `listen()`, `ContinuousSTT`, `Conversation`) afin d'éviter tout conflit d'accès I2S et tout faux réveil provoqué par le haut-parleur AW88298.
  - Reprise contrôlée vers `ARMED` après la fin de la réponse vocale (avec délai d'extinction acoustique configurable, 250 ms par défaut).
  - La détection n'ouvre jamais automatiquement une connexion réseau.
- **Étude & Adaptateur Espressif ESP-SR WakeNet10 (`WakeNetEngine.h`)** :
  - Analyse complète d'ESP-SR et du modèle français `wn10_fr_bonjouresp` (« Bonjour ESP »).
  - Intégration d'un adaptateur de cadrage sans perte `512 -> 480` échantillons (`32 ms -> 30 ms` @ 16 kHz) garantissant la continuité bit-à-bit pour WakeNet10 (`15 * 512 == 16 * 480`).
  - Dans l'environnement actuel Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7 (`espressif32@6.9.0`), `WakeNetEngine::isAvailable()` retourne `false` et passe explicitement en état `ERROR` sans jamais simuler une reconnaissance vocale.
- **Détecteur de Test Déterministe (`TestWakeWordEngine.h`)** :
  - Moteur léger implémentant `IWakeWordEngine` pour valider la capture PCM 16 kHz, le pont audio, la machine à états, les callbacks, la suspension/reprise et les erreurs par commande série (`"bonjour esp"`, `"wake"`) ou appel explicite (`triggerTestWakeWord()`), toujours identifié comme simulé (`isSimulatedEngine() == true`).
- **Compatibilité Future LVGL & Diagnostics (`pollWakeWordEvent` & `WakeWordMetrics`)** :
  - File d'événements thread-safe (`pollWakeWordEvent`) permettant à `CoreS3-VoiceUI-Demo` de consommer un événement Wake Word dans la boucle LVGL principale sans dépendance LVGL dans VoiceKit.
  - Métriques à faible coût : RAM interne libre/minimale, PSRAM libre/minimale, temps max push audio, temps max traitement Wake Word, trames reçues/traitées/abandonnées, saturations, détections, erreurs et transitions d'état.

### Nouvel Exemple & Tests
- **Exemple Autonome `examples/WakeWordFoundation/`** : Démonstration sans LVGL et sans réseau sur M5Stack CoreS3.
- **6 Nouvelles Suites de Tests Unitaires (Suites 83 à 88)** : **1 457 validations réussies avec 0 échec** (88 suites au total).

---

## [0.6.1] - 2026-10-28 (Correctif de Stabilité — Gemini Live, WebSocket TLS et Wi-Fi FastConnect)

### Correction A — Réinitialisation Polymorphe des Métriques PCM entre Tours (`IVoiceProvider` & `LiveSessionController`)
- **Indépendance de `_legacyClient` et Support Multi-Fournisseurs** :
  - Correction de l'anomalie où `VoxCoreVoice::begin()` initialisait `LiveSessionController` via `IVoiceProvider*` en laissant `_legacyClient` à `nullptr`, ce qui empêchait la réinitialisation de `_turnCompleteReceived` entre deux tours et provoquait de fausses alertes `[LiveClient] ALERTE ORDRE: Bloc PCM recu APRES turnComplete` ainsi qu'un bilan `0 ech decodes` à partir du 2ᵉ tour.
  - Extension rétrocompatible de `IVoiceProvider` avec implémentations virtuelles par défaut : `resetTurnRxMetrics()`, `getTurnDecodedSamples()`, `getPostTurnCompleteBlocks()`, `getPostTurnCompleteSamples()`, `isTurnCompleteReceived()`.
  - Implémentation complète dans `GeminiVoiceProvider`, `GeminiLiveClient` et `OpenAIRealtimeClient`.
  - Conversion des compteurs de réception par tour en variables `std::atomic` pour éliminer tout risque de concurrence entre la tâche WebSocket (`websocket_task`) et la boucle principale (`loopTask`).
  - Réinitialisation déterministe au début de chaque tour (`sendTextMessage`, `beginAudioTurn`, `sendActivityStart`, `connect`) tout en conservant la détection stricte des véritables blocs PCM tardifs arrivant après `turnComplete`.

### Correction B — Ordonnancement FreeRTOS pendant le Handshake TLS (`EspWebSocketTransport`)
- **Suppression de la Famine CPU de `loopTask` durant mbedTLS** :
  - Analyse de l'API `esp_websocket_client_config_t` de l'ESP-IDF v4.4 (`framework-arduinoespressif32`) : absence de champ `task_core` (`websocket_task` créée avec `tskNO_AFFINITY`). Avec `cfg.task_prio = 5`, la tâche WebSocket préemptait `loopTask` (Cœur 1, priorité 1) pendant toute la durée du calcul cryptographique mbedTLS (~1,55 s d'interruption de la boucle applicative LVGL/tactile).
  - Démarrage de `websocket_task` à la priorité `DEFAULT_HANDSHAKE_TASK_PRIO = 1` (égale à `loopTask` priorité 1) pendant la connexion TCP et le handshake TLS : l'ordonnanceur FreeRTOS SMP exécute le handshake sur le Cœur 0 lorsqu'il est disponible et partage le Cœur 1 en Round-Robin (1 ms) avec `loopTask` sans jamais bloquer l'interface.
  - Relèvement automatique de la priorité à `DEFAULT_STEADY_TASK_PRIO = 2` dès réception de `WEBSOCKET_EVENT_CONNECTED` (après le handshake TLS) pour garantir une réception réactive des trames PCM 24 kHz (< `AudioPlayerTask` priorité 4), et retour automatique à `1` sur `WEBSOCKET_EVENT_DISCONNECTED` avant toute reconnexion TLS.
- **Instrumentation Non-Bloquante TLS / FreeRTOS / Mémoire (`ConnectionMetrics`)** :
  - Ajout des diagnostics de cœur et priorité (`wsTaskCore`, `wsTaskPriority`, `wsHandshakeTaskPriority`, `wsSteadyTaskPriority`, `loopTaskCore`, `loopTaskPriority`), des intervalles d'interruption apparente de la boucle principale (`lastLoopGapMs`, `maxLoopGapMs`, `maxLoopGapDuringTlsMs`), et de la mémoire interne libre pendant TLS (`freeInternalHeapDuringTls`, `minFreeInternalHeapDuringTls`, `largestInternalBlockDuringTls`).

### Correction C — Repli Wi-Fi FastConnect et Reconnexion Asynchrones (`WiFiManager`)
- **Élimination du Blocage Synchrone (~343 ms) sur la Tâche Principale** :
  - Désactivation de l'écriture flash NVS synchrone redondante du pilote Wi-Fi via `WiFi.persistent(false)` dans `WiFiManager::begin()`, et suppression des réécritures NVS `"vk_wifi"` lorsque le BSSID et le canal n'ont pas changé.
  - Création d'une machine à états explicite (`WiFiManagerState`) et d'une tâche ouvrière FreeRTOS unique persistante (`vk_wifi_wkr`, Cœur 0, priorité 1, file de commandes non-bloquante) chargée d'exécuter en arrière-plan les opérations Wi-Fi bloquantes (`fallbackToNormalScan`, `WiFi.reconnect`, sauvegarde NVS `"vk_wifi"`), réduisant la durée de `WiFiManager::tick()` sur `loopTask` de ~343 ms à < 1 ms.
  - Libération propre des ressources (`end()` et `~WiFiManager()`) et protection par drapeau atomique `_asyncOpInProgress` contre tout appel Wi-Fi concurrent.
  - Mesure séparée des durées d'exécution de `WiFiManager::tick()` (`lastWifiTickDurationUs`, `maxWifiTickDurationUs`) et de `VoiceKit::update()` / `VoxCoreVoice::update()` (`lastUpdateDurationUs`, `maxUpdateDurationUs`).

### Validation & Non-Régression
- **Sanctuaire Audio VoxCore Intact** : Zéro modification des tampons PSRAM (120 000 échantillons @ 24 kHz), des 4 buffers DMA (160 ms), de la tâche `AudioPlayerTask` Mode 2 ni de la politique de vidange matérielle complète.
- **3 Nouvelles Suites de Tests Unitaires (Suites 80 à 82)** : **1 227 validations réussies avec 0 échec**.

---

## [0.6.0] - 2026-10-26 (Multi-Fournisseurs — Intégration OpenAI Realtime & Préservation VoxCore)

### Architecture Multi-Fournisseurs (`IVoiceProvider`)
- **Contrat d'Abstraction Unifié** :
  - Création de l'interface pure `IVoiceProvider` définissant le cycle de vie complet d'un fournisseur vocal : initialisation/connexion TLS (`connect`, `disconnect`, `isConnected`), configuration dynamique (`updateSessionConfig`), émission audio (`sendAudioChunk`, `commitAudio`), requêtes textuelles (`sendTextMessage`), contrôle de conversation (`createResponse`, `sendInterruption`), et enregistrement de callbacks standardisés (transcriptions STT/TTS, audio streaming RX, fin de réponse, fermeture, statut de session et erreurs).
  - Fournisseur par défaut : `VoiceProviderType::GEMINI` (Google Gemini Live) pour une compatibilité ascendante stricte à 100%.
  - Fournisseur alternatif : `VoiceProviderType::OPENAI` (OpenAI Realtime API).
  - Sélection transparente via `VoiceKitConfig` : `config.provider = VoiceProviderType::OPENAI;` sans aucune modification nécessaire dans le code applicatif.

### Client Natif OpenAI Realtime (`OpenAIRealtimeClient`)
- **Connexion WebSocket TLS Directe ESP32-S3** :
  - Connexion sécurisée sur l'endpoint officiel `wss://api.openai.com/v1/realtime?model=<model>` (par défaut `gpt-realtime-2.1-mini`).
  - Authentification sécurisée par en-têtes HTTP `Authorization: Bearer <OPENAI_API_KEY>` et `OpenAI-Beta: realtime=v1`.
  - Intégration du bundle de certificats officiels DigiCert (`OpenAIRoots.h`).
  - Masquage systématique des clés d'API dans les logs de débogage et protection des secrets.
- **Protocole Realtime Bidi** :
  - Négociation de session via `session.update` dès réception de `session.created` (désactivation du turn_detection serveur pour laisser la priorité au VAD local matériel de VoiceKit, voix `alloy`, modèle de transcription `whisper-1`).
  - Streaming audio montant : encodage Base64 direct et émission d'événements `input_audio_buffer.append`.
  - Contrôle de tour : envoi automatique de `input_audio_buffer.commit` suivi de `response.create` lors de la détection de fin de parole.
  - Streaming audio descendant : réception des événements `response.audio.delta`, décodage Base64 optimisé et injection dans le tampon PSRAM du lecteur VoxCore.
  - Gestion des transcriptions : extraction asynchrone des transcriptions utilisateur Whisper (`conversation.item.input_audio_transcription.completed`) et assistant (`response.audio_transcript.delta` / `done`).
  - Interruption prioritaire STOP : émission immédiate de `response.cancel` et `input_audio_buffer.clear` avec vidage instantané du buffer de restitution.

### Rééchantillonnage Audio Dédié (`AudioResampler`)
- **Conversion 16 kHz vers 24 kHz Non-Bloquante** :
  - Conception d'un rééchantillonneur linéaire haute performance en arithmétique entière à virgule fixe (`(x0 + 2*x1)/3`, `(2*x1 + x2)/3`).
  - Conversion par blocs de capture micro (512 échantillons 16 kHz $\to$ 768 échantillons 24 kHz pour OpenAI).
  - Traitement en moins de 15 µs sur ESP32-S3 Core 1, n'impactant aucunement la tâche audio critique FreeRTOS.

### Préservation Intégrale du Moteur Audio VoxCore
- **Stabilité Audio & Zéro Coupure Finale** :
  - Conservation intégrale de la tâche FreeRTOS audio dédiée (Mode 2), des buffers DMA I2S et du tampon circulaire PSRAM de 120 000 échantillons (~5 secondes d'audio).
  - L'événement `response.done` (OpenAI comme Gemini) signale la fin de la génération serveur mais ne coupe jamais la restitution : vidage exact jusqu'au tout dernier échantillon PCM.
  - Préservation certifiée du test de prononciation « Cette phrase se termine par alpha tango terminé » sans coupure de la syllabe finale.

### Nouvel Exemple `OpenAIConversation`
- **Démonstration Minimaliste sans LVGL** :
  - `examples/OpenAIConversation/` : conversation bidirectionnelle en français avec OpenAI Realtime sur M5Stack CoreS3.
  - Indicateur d'état, capture microphone continue avec VAD local, restitution haut-parleur AW88298, transcription console et interruption par bouton tactile.

### Validation & Non-Régression
- **11 Nouvelles Suites de Tests (Suites 69 à 79)** :
  - Validation de `IVoiceProvider`, `AudioResampler`, `OpenAIRealtimeClient`, négociation `session.update`, encodage/décodage Base64, streaming bidirectionnel, transcriptions Whisper, arrêt STOP, gestion des erreurs et reconnexion.
  - **1047 tests unitaires réussis avec 0 échec**.
  - Compilation PlatformIO validée à 100% sur `OpenAIConversation`, `MinimalConversation`, `MinimalTTS` et `MinimalSTT`.

---

## [0.5.3.1] - 2026-10-26 (Consolidation & Validation — Fiabilisation, Documentation & Métriques)

### Consolidation & Correction des Incohérences Documentaires
- **Alignement Global des Versions** :
  - Mise à jour de toutes les références de version vers `v0.5.3.1` (`library.json`, `library.properties`, `VoiceHubConfig.h`, `VoiceKit.h`, bannières console et exemples).
  - Correction de `VOICEKIT_VERSION` resté à `v0.5.2` dans `src/core/VoiceHubConfig.h`.
  - Correction des anciennes références Git obsolètes dans le `README.md` (`#v0.5.0` -> `#v0.5.3.1`, archive `v0.2.1.zip` -> `v0.5.3.1.zip`).
  - Alignement des versions documentées dans l'ensemble des exemples (`MinimalTTS`, `MinimalSTT`, `ContinuousSTT`, `MinimalConversation`, `VoiceSettingsDemo`, `MinimalVoiceKit`, `VoiceDiagnostics`, `MinimalTTS_Arduino`).
  - Documentation exhaustive des 6 onglets de `VoiceDiagnostics` (dont `LAT`), du cyclage interactif VAD et du format de télémétrie `[LATENCY_CSV]`.

### Fiabilisation & Audit des Métriques de Latence (`VoiceLatencyMetrics`)
- **Audit des 7 Jalons & 6 Durées Dérivées** :
  - Vérification de l'origine exacte et de la précision temporelle de chaque jalon : `speechEndDetectedMs`, `micStoppedMs`, `activityEndSentMs`, `firstResponseReceivedMs`, `firstAudioReceivedMs`, `firstAudioPlayedMs`, `playbackFinishedMs`.
  - Distinction formelle et rigoureuse entre `firstAudioReceivedMs` (réception réseau du 1er paquet PCM 24 kHz) et `firstAudioPlayedMs` (remise effective du 1er échantillon au DMA/AW88298).
  - Protection contre les anomalies d'ordre chronologique : toute inversion temporelle renvoie strictement `-1` sans provoquer d'underflow entier 32 bits non signé.
  - Préservation absolue de la politique anti-faux zéro : tout événement non observé renvoie `-1` (`N/D`) et n'est jamais remplacé artificiellement par `0`.
- **Protection Anti-Débordement `millis()`** :
  - Introduction d'un garde non-zéro (`if (ms == 0) ms = 1;`) lors de la capture des horodatages à la milliseconde (`turnStartedMs`, `activityEndSentMs`, `micStoppedMs`, `firstResponseReceivedMs`, `firstAudioReceivedMs`, `firstAudioPlayedMs`, `playbackFinishedMs`).
  - Empêche un horodatage valide capturé à l'instant précis `millis() == 0` (démarrage du microcontrôleur ou rebouclage après ~49.7 jours) d'être interprété à tort comme un événement non observé (`0 == unobserved`).

### Contrôle & Sécurisation du Seuil de Silence VAD
- **Protection contre les Valeurs Invalides** :
  - Sécurisation de `setVadSilenceDurationMs(uint32_t ms)` dans `VoiceKit`, `VoiceActivityDetector` et `ContinuousListeningController` par le filtre `if (ms > 0)`.
  - Rejet des valeurs nulles (`0 ms`) qui provoqueraient une coupure immédiate de parole dès le premier chunk de silence ; conservation transparente du seuil antérieur valide (1 200 ms par défaut).
- **Validation des 3 Paliers d'Usage** :
  - 800 ms : Commandes et interactions courtes ultra-réactives.
  - 1 000 ms : Équilibre optimal pour interactions vocales fluides.
  - 1 200 ms : Parole naturelle avec pauses (seuil par défaut).
- **Changement à Chaud Sécurisé** :
  - Vérification du comportement lors de modifications dynamiques à chaud et pendant une écoute active.

### Renforcement des Tests & Non-Régression
- **Ajout de 3 Nouvelles Suites de Tests Unitaires (64 nouvelles assertions)** :
  - **Suite 66** : Audit approfondi de `VoiceLatencyMetrics` (7 jalons, 6 durées dérivées, ordre chronologique, rejet des inversions, cas limites, distinction paquet vs DMA, garde non-zéro).
  - **Suite 67** : Validation des 3 seuils VAD, rejet strict de la valeur 0, persistance de la configuration et bascule à chaud pendant l'écoute.
  - **Suite 68** : Non-régression complète de la façade `VoiceKit` sur les 4 modes (TTS, STT, ContinuousSTT, Conversation), transitions inter-modes, rejet des requêtes concurrentes (`isBusy()`), unicité des callbacks et résilience aux déconnexions/reconnexions.
- **Bilan Global des Tests** : 976 validations réussies avec 0 échec (contre 912 en v0.5.3).
- **Compilations PlatformIO** : 100% de réussite sur les 8 exemples cibles CoreS3 (`MinimalTTS`, `MinimalSTT`, `ContinuousSTT`, `MinimalConversation`, `MinimalVoiceKit`, `VoiceSettingsDemo`, `VoiceDiagnostics`, `OrientationClockVoice`).

---

## [0.5.3] - 2026-10-26 (Voice Latency Optimization — Réactivité des interactions vocales & Instrumentation)

### Instrumentation Précise de la Latence Vocale (7 Jalons / 6 Durées Dérivées)
- **Structure Complète `VoiceLatencyMetrics`** :
  - `speechEndDetectedMs` : Horodatage à la milliseconde de détection/validation de fin de parole par le VAD local.
  - `micStoppedMs` : Arrêt matériel effectif de la capture microphone ES7210.
  - `activityEndSentMs` : Envoi de la notification `clientContent.turnComplete: true` sur la WebSocket Gemini Live.
  - `firstResponseReceivedMs` : Réception du premier événement `serverContent` de Gemini Live.
  - `firstAudioReceivedMs` : Réception du premier paquet PCM 24 kHz extrait du flux Bidi.
  - `firstAudioPlayedMs` : Remise du premier échantillon au pilote DMA I2S / AW88298 (début effectif de restitution).
  - `playbackFinishedMs` : Fin matérielle et acoustique de la restitution audio.
- **Calcul Strict des Durées Dérivées (Politique Anti-Faux Zéro)** :
  - Fin de parole $\rightarrow$ fin de capture : `getSpeechEndToMicStoppedMs()`.
  - Fin de capture $\rightarrow$ notification Gemini : `getMicStoppedToActivityEndMs()`.
  - Notification Gemini $\rightarrow$ premier audio reçu : `getActivityEndToFirstAudioMs()`.
  - Premier audio reçu $\rightarrow$ début restitution : `getFirstAudioToPlaybackMs()`.
  - Fin de parole $\rightarrow$ début restitution (latence perçue) : `getResponseLatencyMs()`.
  - Durée totale du tour vocal : `getTotalTurnDurationMs()`.
  - **Règle absolue** : Tout événement non observé ou non applicable renvoie explicitement `-1` (`N/D`), éliminant tout faux `0 ms`.
  - Intégration transparente dans `ConversationMetrics` et accesseur public direct `VoiceKit::getLatencyMetrics()`.

### Optimisations Locales & Suppression des Allocations Dynamiques
- **Encodage Base64 Direct sur Pile (`GeminiLiveClient::encodeBase64Direct`)** :
  - Encodage Base64 direct sans aucune allocation dynamique d'objet `String` sur le tas FreeRTOS toutes les 32 ms.
  - Suppression de la fragmentation mémoire du tas et élimination des saccades audio pendant le streaming microphone continu.
- **Décodage Base64 Optimisé (`GeminiLiveClient::decodeBase64`)** :
  - Décodage direct dans le vecteur cible sans allocations intermédiaires de chaînes temporaires.
- **Sérialisation Légère des Trames JSON** :
  - Réduction de l'empreinte mémoire des trames `realtimeInput` et `clientContent`.
- **Formats Audio Strictement Préservés** :
  - Capture microphone : PCM 16 kHz 16 bits mono.
  - Restitution haut-parleur : PCM 24 kHz 16 bits mono.

### Contrôleur VAD Configurable & Réduction du Délai de Fin de Parole
- **Seuil de Silence VAD Paramétrable** :
  - Configuration de `vadSilenceDurationMs` dans `VoiceKitConfig` (1 200 ms par défaut).
  - Nouvelles méthodes de contrôle dynamique : `voice.setVadSilenceDurationMs(uint32_t ms)` et `voice.getVadSilenceDurationMs()` (avec alias `setSilenceDurationMs`/`getSilenceDurationMs`).
  - Validation des paliers **800 ms**, **1 000 ms** et **1 200 ms** :
    - 1 200 ms : Référence par défaut pour pauses longues et dictée continue.
    - 1 000 ms : Équilibre optimal réduisant la latence perçue de 200 ms sans coupure.
    - 800 ms : Mode ultra-réactif pour questions directes courtes.

### Enrichissement de l'Outil de Diagnostic `VoiceDiagnostics`
- **6e Onglet Tactile Dédié `LATENCY` (`LAT`)** :
  - Interface compacte 320 × 240 avec 6 onglets de navigation (`SYS`, `AUDIO`, `LAT`, `NET`, `CONN`, `EVT`).
  - Affichage temps réel : Dernière latence perçue, délai Gemini, délai local DMA, durée de capture microphone.
  - Statistiques de session : Tours mesurés, tours incomplets, min, moyenne, max et médiane glissante.
  - Réglage interactif du seuil VAD (800 / 1 000 / 1 200 ms) par touche tactile et par commande série `VAD <ms>`.
- **Journalisation Série Structurée et Format CSV** :
  - Émission de synthèses horodatées `[LATENCY]` à chaque fin de tour vocal.
  - Export CSV direct `[LATENCY_CSV]` avec en-tête `[LATENCY_CSV_HEADER]` pour analyse comparative.

### Sanctuaire Audio & Garanties de Non-Régression
- Maintien intégral du buffer circulaire en PSRAM de 120 000 échantillons (~5 s) et du quadruple buffer DMA (160 ms).
- Préservation absolue des garanties de vidage de `LiveAudioPlayer` : aucun arrêt prématuré du haut-parleur, restitution vérifiée jusqu'au dernier mot (« alpha tango terminé »).
- 912 assertions unitaires automatisées validées (65 suites de tests, +79 assertions en v0.5.3).
- 100% des exemples compilés sous PlatformIO sans erreur.

---

## [0.5.2] - 2026-10-26 (Fast Connection — Optimisation des temps de démarrage Wi-Fi & Gemini Live)

### Optimisation WiFiManager & FastConnect
- **Connexion Wi-Fi Rapide Facultative (FastConnect)** :
  - Mémorisation persistante en NVS (`Preferences`) du dernier canal RF et du BSSID du point d'accès Wi-Fi.
  - Connexion directe via `WiFi.begin(ssid, pass, channel, bssid)` évitant le balayage complet des 13 canaux RF.
  - Réduction mesurée du temps d'obtention IP sur CoreS3 de 2384 ms à 946 ms (-60.3%).
  - Repli automatique et transparent vers le scan multi-canaux classique en cas d'échec ou d'expiration du délai FastConnect (3.5 s).
  - Sauvegarde automatique des nouvelles coordonnées réseau à chaque association réussie sans altération des identifiants enregistrés.
- **Réduction de la Première Reconnexion & Backoff Progressif** :
  - Première tentative de reconnexion accélérée à 1.5 s (au lieu de 2.0 s) dès détection de perte réseau.
  - Backoff progressif avec multiplicateur 2x plafonné à 30 s (1.5s, 3s, 6s, 12s, 24s, 30s max).
  - Verrou d'exclusion mutuelle évitant tout lancement de reconnexions concurrentes.

### Optimisation de la Synchronisation SNTP
- **Détection d'Horloge Système Déjà Valide** :
  - Évaluation de l'horloge système (`time(nullptr) >= 1700000000`, seuil 2024+) dès l'obtention de l'adresse IP.
  - Si l'horloge est déjà valide (redémarrage à chaud, maintien RTC ou réveil), l'attente bloquante SNTP est évitée (`sntpSyncMs = 0 ms`, gain direct de ~1000 ms), tout en maintenant la synchronisation en arrière-plan.
  - Si l'horloge n'est pas encore initialisée (démarrage à froid), maintien strict de la boucle d'attente SNTP avec timeout (5.0 s) pour garantir l'intégrité de la vérification de validité temporelle des certificats TLS.
- **Sécurité TLS Inviolée** :
  - Aucune désactivation de la validation des certificats TLS GoogleRoots.

### Optimisation Gemini Live & Session Controller
- **Conditionnement Strict de la Reconnexion au Réseau** :
  - Intégration d'un prédicat `isNetworkReady()` dans `LiveSessionController` vérifiant la connectivité IP avant toute tentative de connexion WebSocket.
  - Évite l'épuisement prématuré des tentatives de reconnexion (`_maxReconnectAttempts = 3`) lorsque le Wi-Fi est temporairement coupé.
  - Reconnexion immédiate à Gemini dès le rétablissement de la liaison réseau.
- **Suppression des Reconnexions Inutiles** :
  - Protection contre les appels redondants à `connect()` si la session est déjà connectée et prête (`setupComplete`).
  - Maintien absolu de la confirmation `setupComplete` avant d'émettre l'état `READY`.

### Instrumentation & Mesures des 5 Phases de Démarrage
- **Structure de Données `ConnectionMetrics`** :
  - `t_wifi_ms` (`wifiConnectMs`) : Démarrage Wi-Fi → adresse IP obtenue.
  - `t_sntp_ms` (`sntpSyncMs`) : Début SNTP → horloge système valide.
  - `t_ws_ms` (`wsConnectMs`) : Début connexion Gemini → WebSocket connecté.
  - `t_setup_ms` (`setupCompleteMs`) : WebSocket connecté → confirmation `setupComplete`.
  - `t_total_ready_ms` (`totalReadyMs`) : Démarrage total de la façade → `VoiceKit::isReady()`.
  - Compteurs d'essais et d'échecs (`wifiAttempts`, `geminiAttempts`, `connectionFailures`).
  - Indicateurs FastConnect (`fastConnectSuccess`, `sntpSkipped`, `channel`, `bssidStr`).
  - Nouvelle méthode publique : `const ConnectionMetrics& VoiceKit::getConnectionMetrics() const;`.

### Évolution de l'Exemple Officiel VoiceDiagnostics
- **5e Écran Tactile Dédié `CONNECTION` (`CONN`)** :
  - Affichage en temps réel des 5 durées mesurées avec indicateurs visuels `(FAST)` et `(IMMEDIAT)`.
  - Visualisation des tentatives, échecs et canal/BSSID actifs.
- **Journalisation Série Structurée et CSV** :
  - Ligne de synthèse horodatée `[CONN]` à chaque transition vers `READY`.
  - Format CSV directement exploitable `[CONN_CSV]` avec en-tête `[CONN_CSV_HEADER]`.

### Validation Matérielle & Non-Régression Totale
- **Mesures Authentiques sur CoreS3 (COM4)** :
  - Démarrage à froid (scan complet) : 5002 ms jusqu'à `READY`.
  - Démarrage rapide (FastConnect + RTC valide) : **3182 ms** jusqu'à `READY` (**gain net de 1820 ms / -36.4%**).
  - Liaison Wi-Fi seule : 2384 ms -> **946 ms** (**gain de 1438 ms / -60.3%**).
- **Compilation PlatformIO** : 8 exemples officiels compilés avec succès à 100%.
- **Banc de Tests Automatisés** : 60 suites de tests (Suites 56 à 60 ajoutées), 833 assertions réussies, 0 échec.

---

## [0.5.1] - 2026-10-25 (Intégration de VoiceDiagnostics aux exemples officiels)

### Nouvel Exemple Officiel `VoiceDiagnostics`
- **Application Autonome de Diagnostic Tactile (320 × 240)** :
  - Intégration du nouvel exemple officiel `examples/VoiceDiagnostics` pour M5Stack CoreS3.
  - Interface tactile fluide construite directement avec M5Unified, sans surcharge ni dépendance LVGL.
  - 4 onglets tactiles spécialisés :
    - `SYS` : État opérationnel VoiceKit, mode vocal actif, RAM libre et minimum observé, PSRAM libre (8 Mo), Uptime et caractéristiques du SoC ESP32-S3 (fréquence CPU, modèle, révision, taille Flash).
    - `AUDIO` : Niveau RMS du microphone acquis en temps réel via VoxCore, état de capture STT (`INACTIF`, `EN COURS`, `TRANSCRIPTION`), état de restitution TTS (`INACTIF`, `EN COURS`, `OCCUPE`), compteurs des débuts/fins de synthèse et d'écoute, transcriptions finales, et métriques du dernier tour conversationnel (`firstAudioLatencyMs`, `droppedSamples` si applicable).
    - `NET` : Connexion Wi-Fi (statut, IP, RSSI en dBm), état de session Gemini Live (`PRETE`, `CONNEXION...`, `NON PRETE`), compteurs de reconnexions et d'erreurs, et intitulé de la dernière anomalie.
    - `EVENTS` : Historique circulaire horodaté des 6 derniers événements vocaux (changements d'état, début/fin TTS, début/fin écoute, transcriptions reçues, erreurs), avec zéro allocation dynamique.
  - 2 boutons d'action permanents :
    - `TEST TTS` : Déclenche la prononciation vocale d'une phrase de test en français (« *Bonjour. Ceci est un test de diagnostic vocal VoiceKit sur Core S trois.* »).
    - `STOP AUDIO` : Interrompt immédiatement toute activité vocale en cours via `voice.stop()`.

### Sortie Série Structurée et Télémétrie
- **Format Série Standardisé `[DIAG]`** :
  - Émission périodique d'une ligne structurée et immédiatement exploitable par des scripts ou outils de monitoring :  
    `[DIAG] Voice=READY [DIAG] Mode=IDLE [DIAG] WiFi=CONNECTED [DIAG] RAM_FREE=152320 [DIAG] PSRAM_FREE=7845120 [DIAG] MIC_RMS=215 [DIAG] TTS_BUSY=0`
- **Sortie CSV Facultative (`ENABLE_CSV_OUTPUT`)** :
  - Activée via constante de compilation `#define ENABLE_CSV_OUTPUT 1` pour l'enregistrement direct des données dans des tableurs ou outils d'analyse temporelle.
- **Régulation Série en Streaming Audio** :
  - Espacement automatique des émissions série (3 secondes) lors de la restitution TTS ou de l'écoute micro pour supprimer toute interférence avec les transferts DMA et WebSocket.

### Intégrité du Cœur et Performances
- **Utilisation Exclusive de la Façade `VoiceKit`** :
  - Zéro second moteur vocal instancié, respect strict du pattern façade `VoiceKit voice;`.
- **Aucune Tâche FreeRTOS Supplémentaire** :
  - Le diagnostic s'exécute intégralement dans le fil applicatif principal (`setup()` / `loop()`).
- **Cadence d'Affichage Optimisée (5 Hz / 200 ms)** :
  - Rafraîchissement ciblé du panneau central sans scintillement (aucun effacement plein écran cyclique).
- **Protection des Identifiants** :
  - Clés API et mots de passe Wi-Fi strictement masqués (jamais affichés à l'écran ni transmis sur le port série).
- **Authenticité des Données** :
  - Aucune métrique simulée ; affichage explicite de `N/D` lorsque la donnée n'est pas disponible.

### Validation & Non-Régression
- **9 Exemples Officiels** : Compilation réussie sous PlatformIO (`VoiceDiagnostics`, `MinimalVoiceKit`, `ContinuousSTT`, etc.).
- **Suite de Tests MinGW** : 55 suites de tests automatisés validées à 100% (806 réussis, 0 échec).

---

## [0.5.0.1] - 2026-10-25 (Correctif de Stabilisation : FreeRTOS, Transitions de modes et Événements)

### Correction de la Synchronisation FreeRTOS
- **Remplacement de `volatile` par `std::atomic`** :
  - Élimination des accès concurrents non protégés sur `_state`, `_isActive`, `_isPaused`, `_speechEndRequested`, `_lastChunkRms` et `_mode`.
- **File SPSC (Single-Producer Single-Consumer) Lock-Free** :
  - Transfert léger et borné (128 blocs = 4,096 secondes d'audio) des valeurs RMS depuis le callback audio FreeRTOS Core 1 vers la méthode `update()` dans la boucle principale.
  - Zéro allocation dynamique, zéro mutex bloquant dans le chemin critique audio, zéro manipulation de `String` depuis la tâche FreeRTOS.
- **Unification du Contexte VAD & Callbacks** :
  - Traitement complet de l'algorithme VAD (`_vad.processRms`) exécuté exclusivement dans `update()`.
  - Tous les callbacks utilisateurs (`onSpeechStarted`, `onSpeechEnded`, `onSegmentComplete`, `onStateChanged`, etc.) s'exécutent de façon déterministe dans le fil d'exécution principal (`loop()`).
  - Seul le callback brut `onAudioChunk` demeure dans la tâche FreeRTOS audio et reste strictement non-bloquant.

### Sécurisation des Transitions de Modes
- **Synchronisation Rigoureuse de l'État dans la Façade `VoiceKit`** :
  - Correction de l'anomalie où `stopConversation()` basculait prématurément vers `VoiceKitMode::IDLE` même en cas d'échec de désactivation.
  - Distinction formelle entre une demande acceptée (`bool ok`) et une transition confirmée après réception de `setupComplete` depuis Gemini Live.
  - Refus strict des opérations incompatibles (`say`, `listen`, `startContinuousListening`, `startConversation`) lorsqu'une connexion ou transition est en cours (`isConnecting()`, `isModeSwitchPending()`).
  - Restauration automatique du dernier mode valide en cas d'échec de la reconnexion de mode.
  - Prévention totale des doubles demandes de reconnexion.

### Fiabilisation de `ContinuousListeningController` & Unicité des Événements
- **Identifiant Séquentiel Local de Segment (`_currentSegmentId`)** :
  - Chaque segment de parole possède un identifiant incrémental unique.
  - Unicité absolue de finalisation par segment : au maximum une seule émission de `onSegmentComplete` par segment.
  - Rejet systématique des notifications finales répétées ou dupliquées par le réseau/serveur.
- **Protection Anti-Hallucination & Rejet des Callbacks Tardifs** :
  - Interdiction stricte de convertir une transcription partielle (interim) en transcription finale.
  - Aucun texte final artificiel après expiration du timeout STT (texte vide et `confirmed = false`).
  - Rejet catégorique des callbacks tardifs d'un segment terminé arrivant après timeout ou pendant le segment suivant.
  - Reprise automatique fixée à 400 ms par défaut et réinitialisation complète de l'état VAD et RMS avant chaque segment.

### Validation & Non-Régression
- **Suite Complète de Tests Automatisés (806 assertions, 0 échec)** :
  - Conservation des 692 assertions de la version v0.5.0.
  - Ajout de 114 nouvelles assertions déterministes (Suites 53, 54 et 55) couvrant les blocs audio après arrêt, les pauses/reprises répétées, les événements dupliqués, les callbacks après timeout, les échecs de reconnexion, les refus de transitions occupées, le démarrage continu hors session et l'enchaînement rapide des 4 modes.
- **Validation PlatformIO Complète** :
  - Compilation réussie sans warning de l'ensemble des 7 exemples PlatformIO (`MinimalVoiceKit`, `ContinuousSTT`, `MinimalConversation`, `MinimalSTT`, `MinimalTTS`, `VoiceSettingsDemo`, `OrientationClockVoice`).

---

## [0.5.0] - 2026-10-25 (Simplification de l'API et Stabilisation)

### Nouvelle Façade Simplifiée `VoiceKit` (`VoiceKit.h`)
- **API Haut Niveau Intuitive et Unifiée** :
  - Introduction d'une classe façade optionnelle `VoiceKit` exposant une API claire et expressive pour piloter l'ensemble des modes vocaux à partir d'un seul objet :
    - Cycle de vie : `begin(config)`, `update()`, `stop()`, `isReady()`, `isBusy()`, `getState()`, `getMode()`, `getLastError()`.
    - Synthèse vocale (TTS) : `say(text)`, `isSpeaking()`.
    - Reconnaissance manuelle (STT) : `listen()`, `stopListening()`, `cancelListening()`, `isListening()`, `isTranscribing()`.
    - Reconnaissance continue (ContinuousSTT) : `startContinuousListening()`, `stopContinuousListening()`, `pauseContinuousListening()`, `resumeContinuousListening()`, `isContinuousListening()`, `isContinuousListeningPaused()`.
    - Conversation bidirectionnelle : `startConversation()`, `stopConversation()`, `isConversationActive()`.
    - Profil vocal & volume : `setVoiceProfile(...)`, `setVoice(...)`, `setVolume(...)`, `getVolume()`.
  - **Délégation Transparente sans Duplication** :
    - La façade délègue fidèlement toutes les opérations réseau, audio DMA et protocolaires au moteur existant `VoxCoreVoice` et au nouveau contrôleur `ContinuousListeningController`.
    - Zéro tâche FreeRTOS supplémentaire, zéro second buffer audio, zéro nouvelle allocation mémoire invasive.
  - **Contrôles Déterministes & Résultats Explicites** :
    - Toutes les méthodes acceptent ou refusent les requêtes avec un retour booléen explicite (`bool`) selon l'état du moteur et la politique d'exclusion mutuelle des modes.
    - Élimination des conflits : refus strict de lancer du TTS ou une conversation pendant une écoute continue, et réciproquement.

### Nouveau Contrôleur Réutilisable `ContinuousListeningController`
- **Composant Autonome Intégré à la Bibliothèque (`src/core/ContinuousListeningController.h`, `.cpp`)** :
  - Extrait et encapsule toute la logique de dictée continue mains libres précédemment démontrée dans l'exemple `ContinuousSTT`.
  - Intègre le détecteur d'activité vocale léger `VoiceActivityDetector` pour le calcul d'énergie RMS, l'estimation adaptative du plancher de bruit, l'hystérésis (seuil parole vs seuil maintien).
  - Paramètres déterministes par défaut :
    - Détection début de parole : 200 ms consécutifs au-dessus du seuil.
    - Détection fin de phrase : 1 200 ms de silence consécutif post-parole.
    - Durée maximale de sécurité par segment : 30 secondes.
    - Délai de relance automatique inter-segments : 400 ms.
  - **Thread-Safety & Intégrité Audio** :
    - Traitement des blocs PCM 16 kHz thread-safe (via `std::atomic<uint32_t>` pour le RMS et drapeaux d'état atomiques).
    - Zéro double capture micro : réutilise directement le flux DMA de `VoxCoreVoice` / `LiveAudioStreamer`.
    - Règle stricte anti-hallucination : en cas de timeout sans transcription finale authentique de Gemini Live, aucun texte factice n'est généré ni injecté.
    - Prise en charge des pauses et reprises manuelles sans altérer la session WebSocket TLS sous-jacente.

### Gestion des Événements & Unicité des Callbacks
- **Architecture de Callbacks Simplifiée et Cohérente** :
  - `voice.onReady(cb)` : Émis lorsque la session est prête pour une opération.
  - `voice.onError(cb)` : Émis en cas d'anomalie réseau, audio ou protocolaire.
  - `voice.onSpeechStarted(cb)` : Émis au premier échantillon audio restitué.
  - `voice.onSpeechFinished(cb)` : Émis à l'extinction complète de la restitution sonore (garanti exactement une fois par énonciation).
  - `voice.onTranscription(cb)` : Callback unifié avec distinction stricte du statut (`const String& text, bool isFinal`).
  - `voice.onConversationFinished(cb)` : Émis à la clôture exacte de chaque tour de conversation.
- **Contexte d'Exécution Sécurisé** :
  - Tous les callbacks de haut niveau sont exécutés dans le fil d'exécution de la boucle principale (`loop()`) lors des appels à `update()`.
  - Seul le callback brut d'acquisition `onAudioChunk` s'exécute dans la tâche audio FreeRTOS Core 1.
  - Garantie formelle d'unicité : aucune duplication d'événement de fin lors des boucles d'update suivantes.

### Nouvel Exemple PlatformIO `MinimalVoiceKit`
- **Application Démonstrateur Complète (`examples/MinimalVoiceKit/`)** :
  - Utilise exclusivement l'API simplifiée `VoiceKit` avec un unique objet `VoiceKit voice;`.
  - Interface tactile interactive fluide sur écran CoreS3 (320 × 240) sans aucune dépendance à LVGL (basée sur `M5Unified`).
  - 4 onglets tactiles pour basculer et tester successivement :
    1. **Synthèse vocale (TTS)** : Phrase de test en français avec accents.
    2. **Reconnaissance manuelle (STT)** : Push-to-Talk via boutons tactiles Écouter / Arrêter.
    3. **Dictée continue (ContinuousSTT)** : Écoute mains libres avec détection automatique de fin de parole et bouton Pause / Reprendre.
    4. **Conversation (Gemini Live)** : Échange bidirectionnel vocal interactif.
  - Pilotage également possible via commandes textuelles sur le port Série (115200 bauds).

### Préservation Totale de l'Existant (`VoxCoreVoice`)
- **Compatibilité Ascendante à 100%** :
  - La classe `VoxCoreVoice` et toutes ses méthodes publiques sont intégralement préservées à l'identique.
  - Les 7 exemples historiques compilent et s'exécutent sans la moindre modification de comportement :
    - `examples/MinimalTTS`
    - `examples/MinimalSTT`
    - `examples/ContinuousSTT`
    - `examples/MinimalConversation`
    - `examples/OrientationClockVoice`
    - `examples/VoiceSettingsDemo`
    - `examples/MinimalTTS_Arduino/MinimalTTS_Arduino.ino`

### Tests & Validations
- **Suites de Tests Automatisés (MinGW C++14/C++17)** :
  - 52 suites de tests exécutées avec succès (692 assertions validées, 0 échec).
  - Suites 47 à 52 dédiées aux spécifications v0.5.0 :
    - Suite 47 : Façade VoiceKit — Initialisation, cycle de vie, modes et délégation.
    - Suite 48 : Façade VoiceKit — Validation des opérations TTS, STT manuel, ContinuousSTT et Conversation.
    - Suite 49 : Façade VoiceKit — Refus stricts des opérations incompatibles et transitions de modes.
    - Suite 50 : ContinuousListeningController — VAD intégré, silence, timeout sans hallucination, pause et reprise.
    - Suite 51 : Intégrité des événements — Unicité des callbacks de fin et absence de duplication.
    - Suite 52 : Compatibilité ascendante intégrale — Coexistence VoxCoreVoice et VoiceKit sans régression.
- **Compilations PlatformIO Validées (8 applications)** :
  - `examples/MinimalTTS` : SUCCESS (14.78s)
  - `examples/MinimalSTT` : SUCCESS (15.15s)
  - `examples/ContinuousSTT` : SUCCESS (18.92s)
  - `examples/MinimalConversation` : SUCCESS (20.68s)
  - `examples/OrientationClockVoice` : SUCCESS (40.72s)
  - `examples/VoiceSettingsDemo` : SUCCESS (19.57s)
  - `examples/MinimalVoiceKit` : SUCCESS (81.31s)
  - `examples/MinimalTTS_Arduino` : Syntaxe et conformité Arduino IDE 1.5 validées

---

## [0.4.1.2] - 2026-10-24 (ContinuousSTT & VAD Détection Fin de Parole)

### Nouvel Exemple PlatformIO `ContinuousSTT`
- **Reconnaissance Vocale Continue Mains Libres** :
  - Nouveau sixième exemple autonome dans `examples/ContinuousSTT/` avec son propre `platformio.ini`, `no_ota.csv`, `secrets.example.h` et `README.md`.
  - Fonctionnement comparable à une dictée vocale moderne : écoute automatique dès que le moteur est prêt, détection de début et de fin de parole sans maintien de bouton, et réactivation automatique pour le segment suivant après finalisation.
  - Découpage naturel des interventions en segments transcrits via une session Gemini Live unique maintenue en continu.

### Détecteur d'Activité Vocale Léger (`VoiceActivityDetector`)
- **Composant Réutilisable et Non Bloquant (`src/core/VoiceActivityDetector.h`)** :
  - Analyse synchrone sur blocs PCM 16 kHz mono 16 bits (chunks de 512 échantillons = 32 ms).
  - Plancher de bruit ambiant adaptatif (`noiseFloor`) ajusté en continu en phase de silence via lissage exponentiel (EMA).
  - Hystérésis déterministe entre seuil d'attaque (parole : `noiseFloor * 2.0 + 250 RMS`) et seuil de relâchement (maintien : `noiseFloor * 1.3 + 120 RMS`).
  - Validation de durée minimale de parole : 200 ms consécutifs requis au-dessus du seuil haut pour éliminer les clics et bruits transitoires.
  - Détection automatique de fin d'intervention après 1 200 ms de silence consécutif post-parole avec déclenchement propre de `voice.stopListening()`.
  - Bornage de sécurité : durée maximale de segment limitée à 30 secondes.
  - Tolérance aux pauses inter-mots : un silence inférieur à 1 200 ms est réinitialisé dès que la voix reprend, sans interruption de phrase.

### Extensions de l'API Publique
- **`VoxCoreVoice::getAudioLevel()`** :
  - Retourne le niveau RMS instantané du dernier bloc micro PCM 16 kHz capturé.
- **`VoxCoreVoice::onAudioChunk(cb)`** :
  - Enregistre un callback léger invoqué sur chaque bloc audio acquis (pour VAD, visualiseur graphique ou télémétrie).
- **`LiveAudioStreamer::getLastChunkRms()`** & **`LiveAudioStreamer::onAudioChunk(cb)`** :
  - Accès au niveau audio sans modifier le chemin critique de streaming, les tampons DMA ni la tâche FreeRTOS.

### Interface Graphique M5Unified (CoreS3 320 × 240)
- **Visualisation Complète et Réactive sans LVGL** :
  - 7 états applicatifs distincts avec code couleur dédié : `ATTENTE`, `ECOUTE`, `PAROLE`, `SILENCE`, `TRANSCRIPTION`, `TERMINE`, `ERREUR`.
  - Barregraphe VU-mètre dynamique avec curseur de seuil de parole.
  - Jauge de silence dynamique visualisant le compte à rebours 0 à 1 200 ms avant clôture.
  - Compteur de segments transcrits et affichage du bruit ambiant.
  - Zone de texte scindée : texte définitif authentique mis en évidence et conservé à l'écran vs texte en direct (intérim).
  - Bouton tactile dynamique `PAUSE` / `REPRENDRE`.
  - Prise en charge intégrale de l'UTF-8 français via la police compatible `fonts::efontJA_12` (accents `é`, `è`, `à`, `ç`, `ê`, `ô`, `ù`, `û`).
  - Détection explicite des timeouts de transcription avec affichage `[Transcription non confirmée (délai dépassé)]` sans invention de texte final.

### Tests & Validations
- **Suites de Tests Automatisés (MinGW C++14/C++17)** :
  - Ajout des suites 43, 44, 45 et 46 dans `tests/test_voicekit_all.cpp` (567 assertions réussies, 0 échec).
  - Validation du VAD, des seuils adaptatifs, de l'hystérésis, des pauses inter-mots, de la durée maximale 30s, de l'enchaînement multi-segments, des timeouts sans invention et de la fidélité UTF-8.
- **Compilations PlatformIO Validées (6 applications)** :
  - `examples/MinimalTTS` : SUCCESS
  - `examples/MinimalSTT` : SUCCESS
  - `examples/MinimalConversation` : SUCCESS
  - `examples/VoiceSettingsDemo` : SUCCESS
  - `examples/OrientationClockVoice` : SUCCESS
  - `examples/ContinuousSTT` : SUCCESS (Flash: 71.4%, RAM: 20.8%)

---

## [0.4.1.1] - 2026-10-24 (Corrective de Stabilisation)

### Sécurisation des Profils Vocaux & Reconnexions Gemini Live
- **Validation Différée après `setupComplete`** :
  - Les méthodes de modification de profil (`setVoiceProfile`, `setVoice`, `setLanguage`, `setStyle`, `setTone`, `setAccent`) conservent intact le profil précédent dans `_config` jusqu'à la confirmation de prise en compte par le serveur Google via l'événement `setupComplete`.
  - Les accesseurs (`getVoice()`, `getLanguage()`, `getStyle()`, `getTone()`, `getAccent()`) reflètent le profil réellement actif et validé à tout instant.
  - Le callback `onReady()` est garanti de correspondre au profil définitivement négocié et prêt.
- **Restauration Cohérente en Cas d'Échec** :
  - En cas d'erreur de négociation TLS/WebSocket ou de timeout lors d'une reconnexion de profil, le profil précédent est automatiquement restauré dans `GeminiLiveClient` et maintenu dans `_config`.
- **Refus Strict et Déterminisme d'Accès** :
  - Refus immédiat (`false`) si le moteur est en cours d'activité vocale (`isBusy()` : émission audio, acquisition micro, attente transcription ou tour conversationnel actif).
  - Refus immédiat (`false`) si une connexion ou reconnexion est déjà en cours (`isConnecting()`), interdisant formellement toute double connexion WebSocket ou enchaînement incontrôlé de reconnexions successives.
- **Nouvelles Méthodes Utilitaires Publiques** :
  - `bool isConnecting() const` : Indique si une connexion ou reconnexion WebSocket vers Gemini Live est en cours.
  - `bool isProfileChangePending() const` : Indique si un changement de profil vocal est en cours de négociation.
  - `bool isModeSwitchPending() const` : Indique si un basculement TTS <-> Conversation est en cours de négociation.

### Fiabilisation de la Transition TTS / Conversation
- **Reconnexion Maîtrisée au Repos** :
  - `enableConversation(bool)` applique le nouveau mode (Mode B conversationnel vs mode unitaire TTS/STT) via une reconnexion propre et maîtrisée lorsque la session est connectée au repos.
  - L'instruction système spécifique (`conversationInstruction` vs `systemInstruction`) est injectée de manière garantie dans la nouvelle trame de setup WebSocket.
  - Préservation absolue des directives vocales (`buildVoiceDirectives()`) : la langue, le style, la tonalité et l'accent sont strictement conservés et transmis dans le nouveau mode.
  - Préservation intégrale des callbacks existants (`onReady`, `onTranscription`, `onUserText`, `onAssistantText`, etc.).
- **Verrouillage Pendant Tour Actif** :
  - Refus strict (`false`) de tout basculement de mode pendant qu'un tour est en cours (écoute micro, attente réponse, élocution).
  - En cas d'échec de la reconnexion de transition, restauration automatique du mode précédent.

### Clarification de la Compatibilité Linguistique
- **Documentation et Déterminisme dans `GeminiLiveClient`** :
  - Clarification explicite que le modèle Gemini Live actif oriente la langue d'élocution principalement au moyen des consignes textuelles système.
  - `isModelSpeechLanguageCodeSupported()` retourne systématiquement `false` afin de ne pas annoncer une détection automatique non implémentée et de garantir qu'aucun champ `speechConfig.languageCode` non validé pour le modèle actif ne soit transmis au serveur.
  - Conservation du schéma protocolaire éprouvé et compatible.

### Tests & Validation
- **Suites de Tests Automatisés Déterministes (MinGW C++14/C++17)** :
  - Ajout des suites 40, 41 et 42 dans `tests/test_voicekit_all.cpp` (485 tests réussis, 0 échec).
  - Validation exhaustive : changement de voix à froid/au repos, refus pendant parole, refus pendant transcription, refus pendant connexion active, échec simulé et rollback, passage TTS $\rightarrow$ Conversation, passage Conversation $\rightarrow$ TTS, refus pendant tour actif, préservation des directives vocales, et validation intégrale de la phrase française accentuée.
- **Compilations PlatformIO des 5 Applications Exemples** :
  - `examples/MinimalTTS` : SUCCESS
  - `examples/MinimalSTT` : SUCCESS
  - `examples/MinimalConversation` : SUCCESS
  - `examples/VoiceSettingsDemo` : SUCCESS
  - `examples/OrientationClockVoice` : SUCCESS

---

## [0.4.1] - 2026-10-24 (Configuration Vocale & Support Français UTF-8)

### Configuration Vocale Étendue (`VoiceKitConfig`)
- **Paramètres Vocaux Complets** :
  - `voice` : Voix pré-construite Gemini Live conservée (`Aoede`, `Charon`, `Fenrir`, `Kore`, `Puck`, défaut : `Aoede`).
  - `language` : Code de langue BCP-47 conforme aux standards internationaux (défaut : `fr-FR`).
  - `style` : Style d'élocution (`VoiceStyle` : `NATURAL`, `PROFESSIONAL`, `EXPRESSIVE`, `NARRATIVE` ; acceptation indifférenciée des énumérateurs ou des chaînes littérales).
  - `tone` : Tonalité vocale (`VoiceTone` : `NEUTRAL`, `CALM`, `WARM`, `CHEERFUL`, `SERIOUS`).
  - `accent` : Orientation de prononciation et accentuation régionale optionnelle (ex: `"parisien"`, `"méridional"`, `"québécois"`, `"british"`).
- **Rétrocompatibilité Totale** :
  - Préservation intégrale de tous les champs et valeurs par défaut antérieurs.
  - Wrappers transparents `VoiceStyleWrapper` et `VoiceToneWrapper` autorisant indifféremment l'assignation par énumérateur (`VoiceStyle::EXPRESSIVE`) ou par chaîne de caractères (`"expressive"`).

### Intégration Gemini Live & Protocole Setup
- **Préservation Strict du Schéma Protocolaire** :
  - Configuration `voiceConfig.prebuiltVoiceConfig.voiceName` préservée mot à mot.
  - Vérification de compatibilité de modèle (`isModelSpeechLanguageCodeSupported`) : exclusion de `speechConfig.languageCode` pour les modèles natifs Bidi (`gemini-3.8-live`, `gemini-2.0-flash`), évitant tout rejet de trame par le serveur Google.
  - Pour les modèles natifs, l'orientation de langue est transmise de manière déterministe via les consignes système.
- **Conversion Déterministe en Directives Vocales** :
  - Méthodes `buildVoiceDirectives()` et `buildEffectiveInstruction()` dans `VoiceKitConfig`.
  - Préservation stricte des instructions métier (`VOICEKIT_DEFAULT_TTS_INSTRUCTION`, `VOICEKIT_DEFAULT_STT_INSTRUCTION`, `VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION`).
  - Ajout des consignes d'élocution et de tonalité sous forme de directives structurées transmises lors du setup WebSocket.
- **Sécurité et Dynamique des Profils** :
  - Nouvelles méthodes dans `VoxCoreVoice` : `setVoiceProfile()`, `setVoice()`, `setLanguage()`, `setStyle()`, `setTone()`, `setAccent()`.
  - Protection absolue : toute modification demandée pendant une élocution, une transcription ou un tour conversationnel actif (`isBusy()`) est strictement refusée sans interrompre l'activité en cours.
  - Au repos, la modification entraîne une reconnexion propre et transparente négociant la nouvelle trame de setup avec Gemini Live.

### Chaîne UTF-8 Intégrale de Bout en Bout
- **Nouveau Module Centralisé `Utf8Utils` (`src/core/Utf8Utils.h`)** :
  - Validation stricte de conformité UTF-8 selon RFC 3629 (`isValidUtf8`).
  - Assainissement sans corruption : les textes valides sont préservés à 100% sans la moindre altération ; les octets mal formés sont remplacés de manière contrôlée par le caractère de substitution officiel Unicode `U+FFFD`.
  - Échappement JSON centralisé et conforme RFC 8259 (`escapeJson`) pour tous les flux textuels sortants (`setup`, `clientContent`, `toolResponse`, etc.).
  - Comptage des points de code Unicode réels (`unicodeLength`).
- **Support des Caractères Français, Ligatures et Ponctuations Typographiques** :
  - Tests unitaires et validation pour tous les caractères prescrits : `é`, `è`, `ê`, `ë`, `à`, `â`, `ù`, `û`, `î`, `ï`, `ô`, `ö`, `ç`, `œ`, `æ`, `É`, `À`, `Ç`, `’`, `«`, `»`.
  - Validation de bout en bout de la phrase obligatoire :
    *« À Noël, Élise prépare un café crème. L'été à Genève est agréable. François préfère un cœur léger. »*
- **Découplage Architecturel Display vs Transport** :
  - Retrait de la translittération ASCII intempestive dans `LiveSessionController` pour préserver le texte UTF-8 original.
  - Délimitation explicite du rôle de `DisplayTextNormalizer`, réservé uniquement aux écrans LCD/TFT à polices restreintes en 7-bits ASCII.

### Nouvelle Application Exemple (`examples/VoiceSettingsDemo/`)
- **Projet PlatformIO Autonome pour M5Stack CoreS3** :
  - Test successif de 5 profils vocaux combinant différentes voix (`Aoede`, `Charon`, `Fenrir`, `Kore`, `Puck`), langues (`fr-FR`, `de-DE`, `en-US`), styles et tons.
  - Affichage sur écran LCD CoreS3 : statut réseau, profil courant, phrase prononcée et bouton tactile « PROFIL SUIVANT ».
  - Pilotage parallèle possible via le moniteur série (touches `N`, `R`, `S`).

### Validation et Non-Régression
- **Compilation PlatformIO Validée pour les 5 Exemples** :
  - `examples/MinimalTTS` : SUCCESS
  - `examples/MinimalSTT` : SUCCESS
  - `examples/OrientationClockVoice` : SUCCESS
  - `examples/MinimalConversation` : SUCCESS
  - `examples/VoiceSettingsDemo` : SUCCESS
- **Suite de Tests Automatisés Natifs** :
  - 39 suites de tests automatisés (396 tests réussis, 0 échec).

---

## [0.4.0] - 2026-10-24 (Conversation Vocale Bidirectionnelle — Mode B Native Gemini Live)

### Conversation Vocale Bidirectionnelle (Mode B Native Gemini Live)
- **Architecture Native Gemini Live (Mode B)** :
  - Flux direct continu : Microphone CoreS3 (PCM 16 kHz) $\rightarrow$ Gemini Live $\rightarrow$ Réponse audio native (PCM 24 kHz) $\rightarrow$ Haut-parleur CoreS3.
  - Élimine tout aller-retour textuel intermédiaire superflu (pas de second appel Gemini).
  - Préservation des flux de transcription temps réel : l'application reçoit à la fois le texte transcrit de l'utilisateur (`inputTranscription`) et le texte de réponse du modèle (`modelTurn` / `parts`).
- **Nouveau Composant Léger `ConversationController`** :
  - `src/core/ConversationController.h`, `src/core/ConversationController.cpp` (avec en-têtes relais `include/ConversationController.h` et `include/core/ConversationController.h`).
  - Gestion unifiée et synchronisée du cycle de vie des tours : `READY` $\rightarrow$ `LISTENING` $\rightarrow$ `PROCESSING` (WAITING_RESPONSE) $\rightarrow$ `SPEAKING` $\rightarrow$ `READY`.
  - Intégration transparente avec `LiveSessionController`, `LiveAudioStreamer` et `LiveAudioPlayer` sans introduire de second WebSocket ni de tâche FreeRTOS superflue.
- **Protection Anti-Larsen & Préservation Dernière Syllabe** :
  - Désactivation automatique du haut-parleur pendant la capture microphone (`setPlaybackAllowed(false)`).
  - Réarmement automatique et immédiat dès le relâchement du bouton (« PARLER ») pour restituer la réponse audio native.
  - Préservation intégrale de la dernière syllabe : drainage complet des tampons DMA et PSRAM avant de déclarer la fin du tour.
- **Métriques Diagnostiques de Tour (`ConversationMetrics`)** :
  - Collecte précise de la durée de capture micro (`micCaptureDurationMs`), nombre de blocs audio émis (`audioBlocksSent`), latence jusqu'au premier son (`firstAudioLatencyMs`), nombre de blocs PCM reçus (`pcmBlocksReceived`), échantillons perdus (`droppedSamples`), durée totale du tour (`totalTurnDurationMs`), texte utilisateur et texte assistant.

### Extension de l'API Publique `VoxCoreVoice`
- **Méthodes Conversationnelles Dédiées** :
  - `void enableConversation(bool enable)` : Active ou désactive le mode conversationnel bidirectionnel (bascule automatiquement le prompt système vers `VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION`).
  - `bool isConversationEnabled() const` : Indique si le mode conversationnel est actif.
  - `void onUserText(TranscriptionCallback cb)` : Callback pour la transcription utilisateur (partielle et finale).
  - `void onAssistantText(AssistantTextCallback cb)` : Callback pour le texte de réponse de l'assistant Gemini.
  - `void onConversationFinished(VoidCallback cb)` : Callback déclenché à la fin complète d'un tour (après vidage audio).
  - `void onConversationTurnMetrics(TurnMetricsCallback cb)` : Callback notifiant les métriques diagnostiques du tour.
  - `const ConversationMetrics& getConversationMetrics() const` : Accès direct aux métriques du dernier tour.
- **Nouveaux États Opérationnels (`VoiceState`)** :
  - `VoiceState::PROCESSING` : Traitement de la demande par Gemini après relâchement du micro.
  - `VoiceState::STOPPED` : Tour interrompu par appui sur STOP.
  - `VoiceState::DISCONNECTED` : Session déconnectée.

### Nouvelle Application Exemple (`examples/MinimalConversation/`)
- **Démonstrateur Conversationnel Push-to-Talk pour M5Stack CoreS3** :
  - Interface visuelle claire sur écran LCD 320x240 M5Unified (sans dépendance LVGL requise).
  - En-tête : Badge d'état réseau et moteur vocal (`PRÊT`, `ÉCOUTE`, `RÉFLEXION`, `RÉPONSE`, `ERREUR`).
  - Centre : Carte transcription utilisateur (« Vous : ... ») et carte réponse Gemini (« Gemini : ... »).
  - Bas : Grand bouton tactile dynamique « PARLER » (vert) / « STOP » (orange/rouge).
  - Contrôle parallèle via port série (commandes `P` pour parler, `R` pour relâcher, `S` pour stop).
  - Projet autonome PlatformIO avec modèle de partition `no_ota.csv` et gestion des identifiants `secrets.example.h`.

### Non-Régression Intégrale v0.3.0 / v0.2.1
- Les 3 applications validées conservent un fonctionnement rigoureusement inchangé :
  - `examples/MinimalTTS` : SUCCESS (Flash 55.7%, RAM 20.7%).
  - `examples/MinimalSTT` : SUCCESS (Flash 56.2%, RAM 20.7%).
  - `examples/OrientationClockVoice` : SUCCESS (Flash 76.0%, RAM 48.7%).
  - `examples/MinimalConversation` : SUCCESS (Flash 56.2%, RAM 20.7%).
- **Validation Automatisée Native** :
  - 35 suites de tests automatisés (279 assertions passées avec succès, 0 échec).

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
