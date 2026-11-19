# VoiceKit v0.6.7.2 — Résultats des Tests & Plan de Validation CoreS3 (`TEST_RESULTS.md`)

**Date :** 19 novembre 2026  
**Branche :** `release/v0.6.7.2-preparation`

---

## 1. Synthèse Globale des Validations

| Catégorie | Commande / Outil | Résultat réel | Remarques |
|---|---|---|---|
| **Métadonnées & liens locaux** | Script Python CI | ✅ **PASS** | Versions alignées (`0.6.7.2`), 0 lien local cassé |
| **Tests natifs C++17** | `tests\run_tests.bat` (`g++ -std=gnu++17`) | ✅ **3 807 PASS / 0 FAIL** (100 %) | Toutes les suites (1 à 152) réussies après mise à jour des 7 assertions obsolètes |
| **Exemples PlatformIO (11)** | `python tools/validation/validate_examples.py` | ✅ **11 / 11 PASS** | Voir [`EXAMPLES_VALIDATION.md`](EXAMPLES_VALIDATION.md) |
| **Exemple Arduino IDE (1)** | `MinimalTTS_Arduino` | ⚠️ **NOT TESTED (CLI)** | `arduino-cli` non installé sur l'hôte |
| **Projet consommateur externe** | `pio run` (hors dépôt, sans `symlink://`) | ✅ **PASS** (`91.95 s`) | Voir [`INSTALLATION_VALIDATION.md`](INSTALLATION_VALIDATION.md) |
| **Validation matérielle CoreS3 v0.6.7.2** | Essais physiques interactifs | ⚠️ **NOT TESTED** | CoreS3 détecté sur `COM9`, mais les tests vocaux/tactiles interactifs nécessitent un opérateur humain (plan manuel complet en section 3) |

---

## 2. Analyse Honnête des Tests Natifs (`tests\run_tests.bat`)

### Correction appliquée pour restaurer la compilation des tests
Le script `tests\run_tests.bat` et `tests\test_voicekit_all.cpp` référençaient encore l'ancien chemin `showcases/OrientationClockVoice/`, déplacé vers `archive/examples/OrientationClockVoice/`. Les chemins d'inclusion ont été corrigés.

### Résultat d'exécution : **3 807 PASS / 0 FAIL (100 %)**
L'ensemble des suites (Suites 1 à 152, incluant `Universal Voice Profiles`, `State Coordination v0.6.6.1` et `Voice Interaction Stabilization v0.6.7.1`) réussit intégralement.

Deux décalages préexistants identifiés lors du premier passage (`3 800 PASS / 7 FAIL`) ont été corrigés dans `tests/test_voicekit_all.cpp` :
1. **Suites 1 & 7 (`FrenchTimeFormatter` archivé) :** passage explicite de `TimeSpeechStyle::EXACT` pour les 5 assertions testant le format numérique (le défaut ayant évolué vers `TimeSpeechStyle::NATURAL`).
2. **Suite 101 (`WakeWordConversation`, arrêt non bloquant) :** ajustement de `setSimulatedAsyncDisconnectPolls(7)` pour refléter les 2 appels `pollDeactivate()` par cycle `update()` (session + contrôleur).

---

## 3. Plan de Validation Matérielle CoreS3 (`v0.6.7.2`) — Essais Manuels

Bien qu'un boîtier ESP32-S3 soit détecté sur `COM9`, les essais vocaux et tactiles physiques (parole micro, écoute haut-parleur, appuis écran, coupure Wi-Fi) n'ont pas été exécutés automatiquement lors de cette préparation et sont donc marqués **`NOT TESTED`** pour v0.6.7.2.

> **Référence historique disponible :**
> - v0.6.7.1 : 8/8 scénarios MAT-01 à MAT-08 confirmés sur CoreS3 (`CHANGELOG.md`).
> - v0.6.7.1 (`VoiceAssistant`) : 8/8 scénarios de bascule fournisseur T1 à T8 validés sur CoreS3 (`showcases/VoiceAssistant/tests/PROVIDER_SWITCH_AUDIT.md`).

### 3.1 Scénarios d'Interaction Vocale (MAT-01 à MAT-08)

Firmware recommandé : `examples/TwoClickConversationLVGL` ou `showcases/VoiceAssistant`.

| ID | Scénario | Procédure manuelle sur CoreS3 | Critère d'acceptation | Statut v0.6.7.2 |
|---|---|---|---|---|
| **MAT-01** | Conversation normale | 1er clic `PARLER` → poser une question → 2e clic `TERMINER`. | Transcription reçue, réponse vocale 24 kHz complète, retour à `READY`. | `NOT TESTED` |
| **MAT-02** | Silence rejeté | 1er clic `PARLER` → silence 2 s → 2e clic `TERMINER`. | Rejet local VAD (`SILENCE`), aucune requête réseau, retour à `READY`. | `NOT TESTED` |
| **MAT-03** | Bruit bref rejeté | 1er clic `PARLER` → tapotement / bruit court → 2e clic `TERMINER`. | Rejet local VAD (`BRIEF_NOISE` / `TOO_SHORT`), retour à `READY`. | `NOT TESTED` |
| **MAT-04** | Double clic accidentel | Double clic rapide (< 120 ms) sur `PARLER`. | Anti-rebond actif, une seule capture démarrée, aucun blocage. | `NOT TESTED` |
| **MAT-05** | STOP pendant réponse | Poser une question longue, appuyer `STOP` pendant la parole IA. | Interruption audio immédiate, aucune réactivation tardive, retour effectif à `READY` sans réinitialisation artificielle. | `NOT TESTED` |
| **MAT-06** | STOP pendant écoute | 1er clic `PARLER`, parler 1 s, appuyer `STOP`. | Micro coupé immédiatement, aucune requête envoyée, retour à `READY`. | `NOT TESTED` |
| **MAT-07** | Rafale de clics | Cliquer rapidement plusieurs fois sur `PARLER` / `STOP`. | Aucun crash, aucune double exécution, retour propre à `READY`. | `NOT TESTED` |
| **MAT-08** | Timeout microphone | 1er clic `PARLER`, ne pas appuyer `TERMINER` pendant > 15 s. | Watchdog 15 s déclenché proprement, micro libéré, retour à `READY`. | `NOT TESTED` |

### 3.2 Scénarios Bi-Fournisseurs & Résilience Réseau (PROV-01 à PROV-08)

Firmware recommandé : `showcases/VoiceAssistant` (avec clés OpenAI et Gemini configurées dans `include/secrets.h`).

| ID | Scénario | Procédure manuelle sur CoreS3 | Critère d'acceptation | Statut v0.6.7.2 |
|---|---|---|---|---|
| **PROV-01** | Démarrage avec OpenAI | Démarrer avec OpenAI actif. | Connexion Wi-Fi + WebSocket OpenAI Realtime, état `READY`. | `NOT TESTED` |
| **PROV-02** | Démarrage avec Gemini | Démarrer avec Gemini actif. | Connexion Wi-Fi + WebSocket Gemini Live (`gemini-2.5-flash-native-audio-latest`), état `READY`. | `NOT TESTED` |
| **PROV-03** | Conversation complète OpenAI & Gemini | Réaliser un tour PTT complet sur OpenAI puis sur Gemini. | Transcription, audio 24 kHz clair et retour `READY` sur chaque fournisseur. | `NOT TESTED` |
| **PROV-04** | Passage OpenAI → Gemini | Ouvrir Réglages, sélectionner Gemini. | Fermeture propre OpenAI, confirmation serveur Gemini, retour `READY`. | `NOT TESTED` |
| **PROV-05** | Passage Gemini → OpenAI | Ouvrir Réglages, sélectionner OpenAI. | Fermeture propre Gemini, confirmation serveur OpenAI, retour `READY`. | `NOT TESTED` |
| **PROV-06** | STOP puis nouvelle conversation | Interrompre une réponse par `STOP`, vérifier retour effectif à `READY`, puis lancer un nouveau tour PTT. | Nouvelle conversation réussie sans reconnexion forcée ni fuite mémoire. | `NOT TESTED` |
| **PROV-07** | Perte Wi-Fi puis reconnexion | Couper le point d'accès Wi-Fi 2.4 GHz, observer l'état, puis rétablir le Wi-Fi. | Détection propre de la perte réseau, reconnexion automatique et retour `READY`. | `NOT TESTED` |
| **PROV-08** | Changement de fournisseur après reconnexion | Après rétablissement Wi-Fi (PROV-07), basculer de fournisseur IA et lancer une conversation. | Bascule confirmée, état `READY` et réponse vocale fonctionnelle. | `NOT TESTED` |
