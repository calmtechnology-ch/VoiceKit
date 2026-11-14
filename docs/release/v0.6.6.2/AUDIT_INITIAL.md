# VoiceKit v0.6.6.2 — Rapport d'Audit Initial
**Date de l'audit :** 14 novembre 2026  
**Auteur :** Antigravity / Senior Software Engineer  
**Version de départ :** v0.6.6.1  
**Version cible :** v0.6.6.2 (Public Release Readiness)  
**Dépôt :** https://github.com/Philippe-AD/VoiceKit  
**Branche auditée :** `main` (commit de départ : `e95012b`)

---

## 1. État Réel du Dépôt

### 1.1 Structure et organisation
Le dépôt présente une structure claire et cohérente adaptée à une bibliothèque C++/Arduino/PlatformIO pour M5Stack CoreS3 :
- `src/` : Code source complet de la bibliothèque (façade `VoiceKit`, moteur `VoxCoreVoice`, pipeline `IntegratedVoicePipeline`, gestionnaire de profils vocaux `VoiceProfileManager`, registre d'outils locaux `ToolRegistry`, composants HAL audio M5Stack et sous-système WakeNet/ESP-SR).
- `examples/` : 11 exemples officiels dont 10 pour PlatformIO (`MinimalTTS`, `MinimalSTT`, `MinimalConversation`, `WakeWordFoundation`, `WakeWordConversation`, `LocalToolRegistryDemo`, `VoiceDiagnostics`, `VoiceSettingsDemo`, `OpenAIConversationLVGL`, `IntegratedVoiceDemo`) et 1 autonome pour Arduino IDE (`MinimalTTS_Arduino`).
- `tests/` : Suite complète de tests natifs exécutables sur machine hôte (`tests/run_tests.bat`, `tests/test_voicekit_all.cpp`) totalisant 3 716 assertions unitaires et scénarios de non-régression.
- `tutorials/` : Série de 4 leçons d'apprentissage progressif (`00-Installation` à `03-SpeechControl`).
- `tools/` : Outils de validation automatisée (`validate_examples.py`, matrices de validation matérielle).
- `docs/` : Documentation technique, rapports de versions historiques et notes d'architecture.
- `archive/` & `showcases/` : Projets de référence et démonstrateurs historiques conservés.

### 1.2 Cohérence des versions et manifestes
- `library.json` : Indiquait la version `0.6.6.1`.
- `library.properties` : Indiquait la version `0.6.6.1`.
- `src/core/VoiceHubConfig.h` : Déclarait `static constexpr const char* VOICEKIT_VERSION = "v0.6.6";`.
- `src/VoxCoreVoice.cpp` : Ligne 186 affichait la bannière `VoxCore VoiceKit v0.6.6 — Universal Voice Profiles`.
- `README.md` : Titré `VoiceKit v0.6.6.1 — Corrective State Coordination`.
- `CHANGELOG.md` : Dernière section documentée `[0.6.6.1] - 2026-11-14`.

**Constat :** Il existait un décalage entre la constante interne (`v0.6.6`), les manifestes (`0.6.6.1`) et la version cible `0.6.6.2`. L'alignement universel vers `0.6.6.2` est nécessaire.

### 1.3 Intégrité des API publiques et dépendances
- Les en-têtes publics majeurs (`VoiceKit.h`, `VoxCoreVoice.h`, `VoiceProfileManager.h`, `IntegratedVoicePipeline.h`, `ToolRegistry.h`, `VoiceUxController.h`, `VoiceUxLiveBridge.h`) sont tous présents à la racine de `src/`, garantissant la compatibilité d'inclusion directe `#include <VoiceKit.h>` sans chemins relatifs complexes.
- Les dépendances matérielles sont strictement épinglées : `m5stack/M5CoreS3@1.0.0` et `m5stack/M5Unified@0.2.23`.
- Le standard C++17 (`-std=gnu++17`) est requis pour les structures structurées, lambdas et `std::string_view`.

---

## 2. Problèmes Confirmés

### 2.1 Identifiants et clés API dans l'historique Git initial (SÉCURITÉ CRITIQUE)
- **Localisation :** Commit initial `aae5465d317e068dfc5277132c60402931747633` (23 oct. 2026).
- **Problème :** Le fichier `examples/MinimalTTS/include/secrets.h` ainsi que `examples/OrientationClockVoice/include/wifi_config.h` contenaient en clair des identifiants Wi-Fi et une clé API Google Gemini Live (`AQ.Ab8...`). Ces fichiers ont été supprimés dès le commit suivant `6fef6fe354c263f1f5b0651cf18da3b6d324076f`.
- **Impact pour diffusion publique :** Si le dépôt est rendu public tel quel, tout utilisateur ou robot scrutant l'historique Git aura accès à ces identifiants historiques.
- **Action requise :** 
  1. Révoquer immédiatement la clé API Gemini concernée sur Google AI Studio si ce n'est pas déjà fait.
  2. Procéder à une purge de l'historique Git (ex. via `git-filter-repo` ou réécriture propre de la racine) avant de rendre le dépôt public sur GitHub.

### 2.2 Fichiers et artefacts locaux résiduels
- `examples/IntegratedVoiceDemo/.gitignore` non versionné (statut Git `untracked`).
- Fichiers `.pio` volumineux résiduels dans le dossier `tutorials/` (`tutorials/01-HelloVoiceKit/.pio`, `tutorials/03-SpeechControl/.pio`).
- Fichiers de logs locaux à la racine (`hardware_lvgl_validation.log`, `hardware_validation_trace.log`, `test_out.txt`).

### 2.3 Tutoriel d'installation avec chemins et partitions historiques
- `tutorials/00-Installation/README.md` et `tutorials/01-HelloVoiceKit/platformio.ini` référençaient des partitions locales `no_ota.csv` (2 Mo) et la dépendance `symlink://../..` au lieu de fournir des directives pour un utilisateur installant VoiceKit comme dépendance externe dans un projet autonome.

### 2.4 Absence de CI (Intégration Continue)
- Aucun dossier `.github/workflows/` n'était configuré. Tout commit ou PR ultérieur sur GitHub ne disposait pas d'une validation automatisée des tests natifs et de la compilation des exemples.

---

## 3. Risques Identifiés

| Risque | Gravité | Probabilité | Impact | Atténuation |
|---|---|---|---|---|
| **Fuite de clé API dans historique Git** | P0 (Bloquant) | Élevée | Compromission de compte cloud si le dépôt devient public | Notification immédiate au propriétaire, révocation de clé, purge d'historique recommandée. |
| **Divergence de constantes de version** | P1 | Élevée | Confusion pour les utilisateurs et outils de packaging | Synchronisation systématique de tous les manifestes et constantes en `0.6.6.2`. |
| **Dépendance symlink introuvable hors monorepo** | P1 | Élevée | Échec de compilation pour un développeur tiers | Rédaction de `QUICK_START.md` avec `lib_deps` pointant vers Git/URL et explications des flags de compilation. |
| **Dépassement de partition Flash (2 Mo vs 16 Mo)** | P1 | Moyenne | Erreur de flashage sur les exemples intégrant LVGL 9 | Clarification du partitionnement : utilisation explicite de `partitions_16MB.csv` (8 Mo d'application). |
| **Régression audio / réseau en situation réelle** | P1 | Moyenne | Blocage conversationnel ou audio haché | Validation matérielle stricte sur M5Stack CoreS3 via port COM9. |

---

## 4. Corrections Indispensables pour v0.6.6.2

1. **Versionnement :**
   - Mise à niveau de `library.json` vers `"version": "0.6.6.2"`.
   - Mise à niveau de `library.properties` vers `version=0.6.6.2`.
   - Mise à niveau de `src/core/VoiceHubConfig.h` : `VOICEKIT_VERSION = "v0.6.6.2"`.
   - Mise à niveau de la bannière dans `src/VoxCoreVoice.cpp`.
   - Mise à jour du test d'assertion de version dans `tests/test_voicekit_all.cpp`.
2. **Hygiène du dépôt :**
   - Traitement des fichiers ignorés et nettoyage des dossiers `.pio` résiduels dans `tutorials/`.
   - Ajout d'un fichier `SECURITY.md` décrivant la politique de signalement des vulnérabilités et la gestion des secrets.
3. **Documentation de mise à disposition publique :**
   - Création de `docs/QUICK_START.md` (guide pas à pas pour développeur externe).
   - Création de `docs/KNOWN_LIMITATIONS.md` (matériel, réseau, quotas IA, modes expérimentaux).
   - Restructuration du `README.md` principal pour présenter un point d'entrée épuré et didactique.
   - Mise à jour de `tutorials/00-Installation/README.md` pour clarifier la compilation autonome.
4. **Intégration continue :**
   - Création du workflow GitHub Actions `.github/workflows/ci.yml` exécutant les tests natifs et validant les manifestes/exemples.
5. **Préparation de la Release :**
   - Rédaction du changelog v0.6.6.2 dans `CHANGELOG.md`.
   - Rédaction des notes de publication `RELEASE_NOTES.md` et de la `RELEASE_CHECKLIST.md`.

---

## 5. Éléments Explicitement Exclus de cette Version (v0.6.6.2)

Conformément à la règle de zéro ajout fonctionnel :
- **Aucun nouveau fournisseur d'IA** (Anthropic Claude, Deepgram, ElevenLabs, Mistral restent exclus).
- **Aucun nouveau moteur STT ou TTS** (moteurs actuels Gemini Live et OpenAI Realtime conservés).
- **Aucune modification du moteur de Wake Word** (WakeNet « Bonjour ESP » hors ligne conservé tel quel).
- **Aucune nouvelle abstraction architecturale** (les signatures C++ et classes existantes sont sanctuarisées).
- **Aucune nouvelle dépendance externe**.
- **Aucun protocole de configuration supplémentaire** (pas de portail captif Wi-Fi ni de BLE provisioning dans cette version).

---

## 6. Classification des Points d'Audit

### P0 — Bloquants pour la Diffusion Publique
1. **Identifiants réels dans le commit `aae5465` de l'historique Git** : Nécessite la révocation confirmée de la clé d'API et la purge / réécriture de l'historique avant passage en visibilité publique.
2. **Cohérence des manifestes et versions** : La bibliothèque doit déclarer `0.6.6.2` de manière uniforme et reproductible.

### P1 — À Corriger ou Documenter Strictement Avant Publication
1. **Guide de démarrage externe (`docs/QUICK_START.md`)** : Nécessaire pour permettre à un développeur tiers de compiler sans dépendre de l'arbre du dépôt (`symlink`).
2. **Documentation des limitations (`docs/KNOWN_LIMITATIONS.md`)** : Les contraintes (Wi-Fi 2.4 GHz, clés d'API requises, partitionnement 16 Mo pour LVGL, standard C++17) doivent être limpides.
3. **Mise en place de la CI GitHub Actions** : Automatiser la validation native pour garantir la pérennité du code sur GitHub.
4. **Validation matérielle sur M5Stack CoreS3 réel** : Exécuter et consigner les scénarios physiques indispensables sur le port COM9 disponible.

### P2 — Améliorations Futures Reportées (v0.7.0+)
1. Support automatisé de l'Arduino IDE CLI via un environnement CI dédié avec core ESP32 préconfiguré.
2. Intégration d'un portail web captif de configuration Wi-Fi / API.
3. Personnalisation avancée de modèles acoustiques WakeNet par l'utilisateur final.
