# Checklist Finale avant Publication Publique — VoiceKit v0.6.7.2 (`RELEASE_CHECKLIST.md`)

**Branche de préparation :** `release/v0.6.7.2-preparation`  
**Cible :** Première publication publique sur https://github.com/calmtechnology-ch/VoiceKit

---

## 1. Sécurité & Historique Git (BLOQUANT — P0)

- [x] Audit des fichiers suivis par Git effectué (`SECURITY_AUDIT.md`).
- [x] Clé OpenAI historique exposée dans `docs/rapports/RAPPORT_CORRECTIVE_v0.6.3.1.md` masquée sur la branche.
- [x] 16 fichiers `secrets.example.h` vérifiés (uniquement des placeholders).
- [x] Règles `.gitignore` racine et sous-projets vérifiées (`**/secrets.h`, `**/wifi_config.h`, `.env`, `*.bin`, `*.elf`).
- [x] Aucun firmware `.bin` / `.elf` contenant des identifiants personnels suivi par Git.
- [ ] **ACTION HUMAINE P0 :** Révoquer la clé API Google Gemini exposée dans `aae5465`.
- [ ] **ACTION HUMAINE P0 :** Révoquer la clé API OpenAI exposée dans `6a0a32c` et `RAPPORT_CORRECTIVE_v0.6.3.1.md`.
- [ ] **ACTION HUMAINE P0 :** Changer le mot de passe Wi-Fi exposé dans `aae5465` (si le réseau est toujours actif).
- [ ] **ACTION HUMAINE P0 :** Exécuter la purge `git-filter-repo` décrite dans `SECURITY_AUDIT.md` (sur une sauvegarde miroir), vérifier `git log --all` puis pousser l'historique nettoyé avec accord explicite.
- [ ] **ACTION HUMAINE P0 :** Vérifier que le dépôt secondaire `ancien` (`Philippe-AD/VoiceKit`) reste privé ou est archivé/purgé.

---

## 2. Licences & Redistribution de Composants Tiers (P1)

- [x] Licence MIT du projet VoiceKit vérifiée (`LICENSE`, `library.json`, `README.md`).
- [x] `cJSON` (utilisé dans `tests/native_mock/`) : licence MIT compatible.
- [ ] **ACTION HUMAINE P1 — Vérification ESP-SR / WakeNet :**
  - Le dépôt embarque le binaire précompilé `src/esp_sr/lib/esp32s3/libesp_sr_wakenet_s3.a`, les en-têtes `src/esp_sr/include/*.h` et le modèle `src/esp_sr/srmodels_bonjouresp.c` (`wn9l_fr_bonjouresp_tts3`) issus d'Espressif ESP-SR.
  - **Attention :** La licence MIT de VoiceKit ne couvre pas ces composants tiers. Les binaires et modèles WakeNet d'Espressif sont distribués sous les conditions de la licence **Espressif ESP-SR** (usage autorisé sur matériel Espressif uniquement, avec mentions de copyright/licence requises).
  - **Action recommandée avant publication publique :** Ajouter un fichier `THIRD_PARTY_NOTICES.md` (ou `src/esp_sr/LICENSE`) reproduisant les termes exacts et l'attribution officielle de la version d'Espressif ESP-SR dont proviennent `libesp_sr_wakenet_s3.a` et `wn9l_fr_bonjouresp_tts3`.

---

## 3. Prise en Main & Documentation

- [x] `README.md` réécrit pour un nouvel utilisateur (12 sections, parcours *Découvrir → Installer → Faire parler → Converser → Développer*).
- [x] `docs/QUICK_START.md` mis à jour (`calmtechnology-ch/VoiceKit`, `v0.6.7.2`, Parcours A + Parcours B autonome sans `symlink://`, dépannage complet, signature `onError(const String&)` corrigée).
- [x] `examples/README.md` créé (12 exemples classés en 3 niveaux + Arduino IDE + guide `VoiceKit.h` vs `VoxCoreVoice.h`).
- [x] `showcases/README.md` mis à jour (`VoiceAssistant`, `VoiceSensorStation`, `VoiceControl` ; référence obsolète `OrientationClockVoice` retirée).
- [x] `docs/README.md` corrigé (liens cassés vers `tutorials/` supprimés et remplacés par les guides actifs).
- [x] `docs/KNOWN_LIMITATIONS.md` harmonisé vers `v0.6.7.2`.
- [x] `CHANGELOG.md` mis à jour avec la section `[0.6.7.2]`.
- [x] Manifestes et constantes alignés sur `0.6.7.2` (`library.json`, `library.properties`, `src/core/VoiceHubConfig.h`, `src/VoxCoreVoice.cpp`).

---

## 4. Validations Techniques & Automatisées

- [x] Workflow CI GitHub Actions créé (`.github/workflows/ci.yml`).
- [x] `tests/run_tests.bat` et `tests/test_voicekit_all.cpp` corrigés pour le chemin archivé `archive/examples/OrientationClockVoice/`.
- [x] Tests natifs C++17 exécutés et consignés honnêtement (`TEST_RESULTS.md`).
- [x] 11 exemples PlatformIO compilés via `tools/validation/validate_examples.py` (`EXAMPLES_VALIDATION.md`).
- [x] Projet consommateur externe autonome compilé sans `symlink://` (`INSTALLATION_VALIDATION.md`).
- [ ] **ACTION HUMAINE P1 :** Exécuter la campagne de validation matérielle CoreS3 `v0.6.7.2` (MAT-01..MAT-08 + 8 scénarios bi-fournisseurs) selon le plan manuel dans `TEST_RESULTS.md`.
- [ ] **ACTION HUMAINE P2 :** Corriger les 7 assertions obsolètes de la suite de tests natifs (`FrenchTimeFormatter` archivé et comptage `pollDeactivate` dans `test_voicekit_all.cpp`) afin que le job CI `native-tests` passe à 100 %.

---

## 5. Publication GitHub (Approbation Humaine Requise)

- [ ] Fusionner `release/v0.6.7.2-preparation` dans `main` après revue.
- [ ] Mettre à jour `docs/QUICK_START.md` / `README.md` si nécessaire pour basculer la recommandation sur `#v0.6.7.2` au moment du tag.
- [ ] Créer et pousser le tag annoté `v0.6.7.2` (après purge historique Git).
- [ ] Basculer la visibilité du dépôt en **Public** (uniquement après validation complète de la section 1).
- [ ] Publier la GitHub Release officielle à partir de `docs/release/v0.6.7.2/RELEASE_NOTES.md`.
