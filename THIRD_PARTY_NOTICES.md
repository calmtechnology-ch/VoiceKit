# Notices de Licences & Composants Tiers — VoiceKit

La bibliothèque **VoiceKit** est distribuée sous **[licence MIT](LICENSE)** (Copyright (c) 2026 Philippe AD / VoxCore Project).

Cependant, le dépôt inclut certains composants tiers régis par leurs propres licences. **La licence MIT de VoiceKit ne s'applique pas à ces composants tiers.**

---

## 1. Espressif ESP-SR / WakeNet 9 (Mot de réveil « Bonjour ESP »)

- **Fichiers concernés dans ce dépôt :**
  - `src/esp_sr/lib/esp32s3/libesp_sr_wakenet_s3.a` (bibliothèque statique précompilée ESP32-S3)
  - `src/esp_sr/srmodels_bonjouresp.c` (modèle acoustique `wn9l_fr_bonjouresp_tts3`)
  - `src/esp_sr/include/*.h` (en-têtes d'interface ESP-SR)
- **Auteur / Propriétaire :** Espressif Systems (Shanghai) Co., Ltd.
- **Source officielle :** https://github.com/espressif/esp-sr
- **Licence :** Conditions de licence **Espressif ESP-SR** (incluant la licence binaire/modèles propriétaire d'Espressif pour les bibliothèques précompilées WakeNet et les modèles associés).
- **Conditions principales à respecter :**
  - Les bibliothèques précompilées et modèles WakeNet fournis par Espressif sont destinés à être utilisés **exclusivement sur les puces matérielles Espressif** (ici ESP32-S3 / M5Stack CoreS3).
  - Toute redistribution ou utilisation commerciale doit respecter les termes complets de la licence officielle publiée dans le dépôt `espressif/esp-sr` (`LICENSE`).

---

## 2. cJSON (Suite de tests natifs uniquement)

- **Fichiers concernés :** `tests/native_mock/cJSON.c`, `tests/native_mock/cJSON.h`
- **Auteur :** Dave Gamble et contributeurs cJSON
- **Source officielle :** https://github.com/DaveGamble/cJSON
- **Licence :** MIT License

---

## 3. Dépendances externes (non embarquées, téléchargées par PlatformIO)

Les bibliothèques suivantes ne sont pas redistribuées dans ce dépôt mais déclarées comme dépendances dans `library.json` / `platformio.ini` :
- **M5CoreS3**, **M5Unified**, **M5GFX** (M5Stack) — Licence MIT.
- **LVGL 9** (utilisée dans certains exemples graphiques et showcases) — Licence MIT.
- **Arduino-ESP32 / ESP-IDF** (Espressif Systems) — Licence Apache 2.0 / LGPL 2.1 selon composants.
