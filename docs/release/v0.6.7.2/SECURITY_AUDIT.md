# VoiceKit v0.6.7.2 — Audit de Sécurité Préalable (`SECURITY_AUDIT.md`)

**Date d'audit :** 19 novembre 2026  
**Branche auditée :** `release/v0.6.7.2-preparation` (issue de `main` @ `37999c1`)  
**Dépôt :** https://github.com/calmtechnology-ch/VoiceKit  
**Statut de sécurité pour publication publique :** **BLOQUÉ (P0)** tant que les actions humaines de révocation et de purge d'historique Git ne sont pas confirmées.

> **Règle de confidentialité :** Aucune valeur de secret (clé API, mot de passe Wi-Fi ou jeton) n'est reproduite dans ce document ni dans les journaux d'audit.

---

## 1. Synthèse des Résultats

| Domaine contrôlé | Résultat | Détails |
|---|---|---|
| **Fichiers `secrets.example.h` (16 fichiers)** | ✅ **CONFORME** | Uniquement des valeurs factices (placeholders) dans la version courante (`HEAD`). |
| **Règles `.gitignore` (racine + 15 sous-dossiers)** | ✅ **CONFORME** | Exclut `**/secrets.h`, `include/Secrets.h`, `**/wifi_config.h`, `*.secret.h`, `.env`, `*.bin`, `*.elf`, `.pio/`. |
| **Binaires firmware (`.bin`, `.elf`) dans Git** | ✅ **CONFORME** | Aucun binaire firmware compilé avec des identifiants personnels n'est suivi par Git. |
| **Fichiers suivis sur `main` (`HEAD`)** | ⚠️ **CORRIGÉ SUR LA BRANCHE** | Une clé API OpenAI historique figurait en clair dans `docs/rapports/RAPPORT_CORRECTIVE_v0.6.3.1.md` (lignes 42 et 89). Elle a été masquée (`sk-proj-[REDACTED_LEGACY_KEY]`) sur `release/v0.6.7.2-preparation`. |
| **Historique Git (`git log --all`)** | ❌ **BLOQUANT (P0)** | Des secrets réels subsistent dans plusieurs commits historiques (`aae5465`, `6a0a32c`, `f3ee433`, `3d8610e` et renommages de `RAPPORT_CORRECTIVE_v0.6.3.1.md`). |
| **Tests natifs (`tests/`)** | ✅ **CONFORME** | Les chaînes de `tests/test_v066_universal_voice_profiles_suites.h` (lignes 476–477) sont des valeurs factices de test dédiées à l'invariant INV-10 (non-persistance NVS). |
| **Composants binaires tiers suivis** | ⚠️ **À VÉRIFIER (P1)** | `src/esp_sr/lib/esp32s3/libesp_sr_wakenet_s3.a` (Espressif ESP-SR) est un binaire tiers sans données personnelles, soumis aux conditions de licence Espressif ESP-SR (voir `RELEASE_CHECKLIST.md`). |

---

## 2. Inventaire des Secrets Exposés dans l'Historique Git

L'analyse automatisée de l'ensemble des références Git (`git log --all`) a identifié **deux incidents historiques distincts** :

### Incident 1 — Commit initial `aae5465` (23 octobre 2026)
- **Commits concernés :** ajouté dans `aae5465` (`first commit`), supprimé dès `6fef6fe` (23 octobre 2026).
- **Fichiers concernés :**
  - `examples/MinimalTTS/include/secrets.h`
  - `examples/OrientationClockVoice/include/wifi_config.h`
- **Types de secrets exposés :**
  - `WIFI_SSID` (13 caractères, non factice)
  - `WIFI_PASSWORD` (18 caractères, non factice)
  - `GEMINI_KEY` (clé API Google, 53 caractères, non factice)

### Incident 2 — Clé OpenAI dans `secrets.example.h` puis recopiée dans un rapport correctif (26 oct. – 1er nov. 2026)
- **Commits concernés :**
  - `6a0a32c` (26 octobre 2026) : `examples/OpenAIConversation/include/secrets.example.h` contenait une véritable clé OpenAI (`sk-proj-...`).
  - `f3ee433` et `3d8610e` (1er novembre 2026) : la clé a été retirée de `secrets.example.h`, mais **recopiée en clair** dans le rapport `RAPPORT_CORRECTIVE_v0.6.3.1.md` (ultérieurement déplacé vers `docs/rapports/RAPPORT_CORRECTIVE_v0.6.3.1.md`).
- **Types de secrets exposés :**
  - Clé API OpenAI (`sk-proj-...`).
- **Correction appliquée sur `release/v0.6.7.2-preparation` :** Les 3 occurrences dans `docs/rapports/RAPPORT_CORRECTIVE_v0.6.3.1.md` ont été remplacées par `sk-proj-[REDACTED_LEGACY_KEY]`. Toutefois, l'historique Git antérieur conserve encore la valeur d'origine.

---

## 3. Actions Humaines Obligatoires (Propriétaire du Dépôt)

Ces opérations ne peuvent pas être exécutées automatiquement par un agent et **doivent être réalisées par le propriétaire des comptes** avant toute ouverture publique :

1. **Révoquer la clé API Google Gemini** exposée dans `aae5465` via [Google AI Studio / Google Cloud Console](https://aistudio.google.com/) et générer une nouvelle clé si nécessaire.
2. **Révoquer la clé API OpenAI** exposée dans `6a0a32c` et `RAPPORT_CORRECTIVE_v0.6.3.1.md` via [OpenAI Platform](https://platform.openai.com/api-keys) et générer une nouvelle clé si nécessaire.
3. **Changer le mot de passe Wi-Fi** (et éventuellement le SSID) exposé dans `aae5465` si ce réseau est toujours actif.
4. **Purger l'historique Git** (procédure en section 4) sur toutes les branches et tags publiés avant de passer le dépôt en visibilité publique.
5. **Vérifier le dépôt distant secondaire `ancien`** (`https://github.com/Philippe-AD/VoiceKit.git`) : s'assurer qu'il reste privé ou qu'il est également purgé/archivé.

---

## 4. Procédure Sûre de Nettoyage de l'Historique Git (`git-filter-repo`)

> **Avertissement :** Ne jamais exécuter cette procédure automatiquement sur le dépôt distant sans sauvegarde préalable et validation explicite du mainteneur.

### 4.1 Prérequis et sauvegarde complète
1. Installer `git-filter-repo` :
   ```bash
   pip install git-filter-repo
   ```
2. Créer une sauvegarde complète miroir hors du répertoire de travail :
   ```bash
   git clone --mirror https://github.com/calmtechnology-ch/VoiceKit.git VoiceKit-backup.git
   ```
3. Travailler sur un clone miroir neuf dédié au nettoyage :
   ```bash
   git clone --mirror https://github.com/calmtechnology-ch/VoiceKit.git VoiceKit-clean.git
   cd VoiceKit-clean.git
   ```

### 4.2 Nettoyage des fichiers et chaînes sensibles
1. Supprimer intégralement de tout l'historique les fichiers de secrets du commit `aae5465` :
   ```bash
   git filter-repo \
     --path examples/MinimalTTS/include/secrets.h \
     --path examples/OrientationClockVoice/include/wifi_config.h \
     --invert-paths
   ```
2. Créer un fichier temporaire local `replacements.txt` (hors dépôt, détruit après usage) contenant la clé OpenAI exacte (et, par précaution, la clé Gemini et le mot de passe Wi-Fi) au format :
   ```text
   <VALEUR_EXACTE_CLE_OPENAI>==>sk-proj-[REDACTED_LEGACY_KEY]
   <VALEUR_EXACTE_CLE_GEMINI>==>AIzaSy[REDACTED_LEGACY_KEY]
   <VALEUR_EXACTE_MDP_WIFI>==>[REDACTED_LEGACY_WIFI_PASS]
   ```
3. Appliquer le remplacement sur l'ensemble de l'historique (y compris `6a0a32c`, `f3ee433`, `3d8610e` et `RAPPORT_CORRECTIVE_v0.6.3.1.md`) :
   ```bash
   git filter-repo --replace-text replacements.txt --force
   ```
4. Supprimer immédiatement `replacements.txt` du disque local.

### 4.3 Impact sur l'historique et les clones existants
- **Réécriture des hashes SHA-1 :** Tous les commits descendants des commits modifiés (c'est-à-dire la quasi-totalité de l'historique depuis `aae5465`) changeront d'identifiant SHA-1.
- **Branches et tags :** Toutes les branches locales/distantes (`main`, branches de sauvegarde/correctifs) et tous les tags pointant vers l'ancien historique devront être réécrits ou supprimés afin d'éviter la résurrection d'objets orphelins.
- **Clones existants :** Chaque collaborateur devra réaliser un **nouveau clone propre** (`git clone`) au lieu d'un `git pull` afin de ne pas réinjecter les anciens commits.
- **Cache GitHub :** Après le push nettoyé, contacter le support GitHub si nécessaire pour purger les vues/caches d'anciens commits (ou recréer un dépôt public neuf à partir de l'historique nettoyé).

### 4.4 Procédure de vérification après nettoyage
Exécuter ces contrôles dans le clone nettoyé avant tout `git push --force --mirror` (qui requiert l'accord explicite du propriétaire) :

```bash
# 1. Vérifier que les fichiers secrets n'existent plus dans aucun commit
git log --all --stat -- "*secrets.h" "*wifi_config.h"

# 2. Vérifier l'absence de motifs de clés API réelles dans tout l'historique
git log --all -G "AIza[0-9A-Za-z_-]{30,}" --oneline
git log --all -G "sk-proj-[0-9A-Za-z_-]{20,}" --oneline

# 3. Vérifier l'intégrité du dépôt nettoyé
git fsck --full --strict
```

Seuls les faux identifiants explicites des tests unitaires (`tests/test_v066_universal_voice_profiles_suites.h`) et les placeholders (`secrets.example.h`) doivent subsister.

---

## 5. Règle de Blocage de Publication

**La publication publique du dépôt VoiceKit reste strictement bloquée (P0)** tant que :
1. Les clés API Gemini et OpenAI historiques ne sont pas révoquées (et le mot de passe Wi-Fi changé si toujours utilisé).
2. L'historique Git distant n'a pas été purgé et vérifié selon la section 4.4.
