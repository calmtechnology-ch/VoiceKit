# VoiceKit v0.6.6.2 — Checklist Pré-Publication GitHub Release

Ce document récapitule l'ensemble des contrôles techniques, qualitatifs et de sécurité requis avant toute publication publique officielle.

---

## 1. État des Contrôles

| Domaine | Élément contrôlé | Statut | Observations |
|---|---|:---:|---|
| **Manifestes** | Cohérence des versions `0.6.6.2` | ✅ **FAIT** | `library.json`, `library.properties`, `VoiceHubConfig.h`, `VoxCoreVoice.cpp` synchronisés |
| **Tests Logiciels** | 3 716 tests natifs unitaires et d'intégration | ✅ **FAIT** | 100 % de succès (0 échec, 0 ignoré) via `tests/run_tests.bat` |
| **Compilations** | 10 exemples PlatformIO officiels | ✅ **FAIT** | 10/10 compilés avec succès (`tools/validation/validate_examples.py`) |
| **Intégration Externe** | Installation dans projet tiers isolé | ✅ **FAIT** | Projet vierge compilé et binaire `firmware.bin` produit sans erreur |
| **Validation Matérielle** | Scénarios CoreS3 physiques indispensables | ✅ **FAIT** | 19/20 scénarios validés sur CoreS3 réel via port USB COM9 |
| **Documentation** | Refonte README didactique (14 sections) | ✅ **FAIT** | Structure normalisée pour les développeurs tiers |
| **Prise en main** | Guide de démarrage rapide | ✅ **FAIT** | `docs/QUICK_START.md` rédigé et vérifié |
| **Limitations** | Référentiel des limites et contraintes | ✅ **FAIT** | `docs/KNOWN_LIMITATIONS.md` publié |
| **Sécurité** | Politique de sécurité et protection des clés | ✅ **FAIT** | `SECURITY.md` rédigé ; alerte historique commit `aae5465` signalée |
| **Licences** | Vérification légale MIT et dépendances | ✅ **FAIT** | MIT pour VoiceKit et cJSON, Apache 2.0 pour ESP-SR / WakeNet |
| **Notes de release** | Rédaction des notes de publication | ✅ **FAIT** | `docs/release/v0.6.6.2/RELEASE_NOTES.md` prêt |
| **Changelog** | Mise à jour détaillée de `CHANGELOG.md` | ✅ **FAIT** | Section `[0.6.6.2]` rédigée selon Keep a Changelog |
| **CI** | Intégration continue GitHub Actions | ✅ **FAIT** | Workflow `.github/workflows/ci.yml` configuré |
| **Révocation Clé** | Révocation de la clé API historique (commit `aae5465`) | ⚠️ **ACTION REQUISE** | À confirmer par le propriétaire du compte Google AI Studio |
| **Purge Git** | Nettoyage de l'historique avant ouverture publique | ⚠️ **ACTION REQUISE** | Nettoyage via `git-filter-repo` recommandé avant `git push` public |
| **Tag Git** | Pose du tag `v0.6.6.2` | ⏳ **EN ATTENTE** | En attente de décision humaine (ne pas créer sans autorisation) |
| **Publication** | Bascule du dépôt en visibilité publique | ⏳ **EN ATTENTE** | Réservé à l'approbation explicite de l'utilisateur |

---

## 2. Décision Recommandée

- **Niveau d'état :** **CONDITIONAL GO — Release Candidate**
- **Justification :** L'intégralité des validations logicielles, compilatoires, documentaires et matérielles locales est validée avec un taux d'excellence technique exemplaire. La publication publique définitive doit attendre la purge de la clé API historique dans le commit Git `aae5465` et la décision formelle du propriétaire.
