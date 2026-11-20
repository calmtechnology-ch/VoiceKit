# VoiceKit v0.6.7.2 — Validation de l'Installation Externe (`INSTALLATION_VALIDATION.md`)

**Date :** 19 novembre 2026  
**Version testée :** VoiceKit v0.6.7.2 (`release/v0.6.7.2-preparation`)  
**Outils :** PlatformIO Core 6.2.0.post1, `espressif32@6.9.0`, `framework-arduinoespressif32@3.20017.241212+sha.dcc1105b`, `xtensa-esp32s3-elf-g++ 8.4.0`

---

## 1. Objectif

Vérifier qu'un développeur extérieur suivant le **Parcours B** de [`docs/QUICK_START.md`](../../QUICK_START.md) peut créer un projet PlatformIO autonome hors du dépôt VoiceKit, **sans `symlink://`** vers une copie locale de développement, en utilisant exclusivement l'API publique `<VoiceKit.h>`, et compiler un firmware valide (`firmware.bin`) pour M5Stack CoreS3.

---

## 2. Protocole Exécuté

1. Création d'un projet PlatformIO vierge dans un dossier temporaire isolé (`%TEMP%\voicekit-quickstart-v0672`).
2. Copie de `no_ota.csv` à la racine du projet et installation autonome de la bibliothèque VoiceKit (`library.json`, `library.properties`, `extra_script.py`, `src/`) dans `lib/VoiceKit/` (aucun lien symbolique ni chemin relatif vers le dépôt source).
3. Création de `include/secrets.h` avec identifiants factices.
4. Copie exacte du `platformio.ini` et du `src/main.cpp` documentés dans `docs/QUICK_START.md` (incluant `VoiceKit`, `VoiceKitConfig`, `voice.onReady(...)`, `voice.onError([](const String& err) ...)`, `voice.begin(vcfg)`, `voice.update()`).
5. Exécution de `pio run -d "%TEMP%\voicekit-quickstart-v0672"`.

> **Correctif détecté et appliqué durant la préparation :** L'ancien `docs/QUICK_START.md` (v0.6.6.2) utilisait `[](const char* err)` pour `voice.onError(...)`, signature incompatible avec `std::function<void(const String&)>`. La signature a été corrigée en `[](const String& err)` avant validation.

---

## 3. Résultat Réel de Compilation

- **Statut :** ✅ **PASS (`SUCCESS`)**
- **Durée totale (avec téléchargement des dépendances M5CoreS3 / M5Unified / M5GFX) :** `91.95 s`
- **Graphe de dépendances résolu :**
  - `M5CoreS3 @ 1.0.0`
  - `M5Unified @ 0.2.23` (`M5GFX @ 0.3.0`)
  - `VoiceKit @ 0.6.7.2` (avec `extra_script.py` et liaison `libesp_sr_wakenet_s3.a`)
- **Empreinte mémoire (`firmware.elf` / `firmware.bin`) :**
  - **RAM :** `25.8 %` (`84 552 / 327 680` octets)
  - **Flash :** `81.2 %` (`1 703 317 / 2 097 152` octets, partition `no_ota.csv`)

---

## 4. Note sur la Référence Git

Le tag Git `v0.6.7.2` n'est pas encore créé (opération réservée à la publication finale après approbation humaine). `docs/QUICK_START.md` distingue donc explicitement :
- la référence de développement utilisable dès maintenant (`https://github.com/calmtechnology-ch/VoiceKit.git#main`), et
- la référence commentée destinée à la future release officielle (`https://github.com/calmtechnology-ch/VoiceKit.git#v0.6.7.2`).
