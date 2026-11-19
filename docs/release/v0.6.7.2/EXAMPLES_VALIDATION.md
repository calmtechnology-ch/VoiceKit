# VoiceKit v0.6.7.2 — Validation des 12 Exemples Officiels (`EXAMPLES_VALIDATION.md`)

**Date :** 19 novembre 2026  
**Commande exécutée :** `python tools/validation/validate_examples.py`  
**Environnement :** PlatformIO Core 6.2.0.post1 (`espressif32@6.9.0`, `m5stack-cores3`)  
**Arduino CLI :** Non installé sur la machine hôte (`MinimalTTS_Arduino` marqué `NOT TESTED (CLI)`).

---

## 1. Résumé Global

- **Audit de structure & documentation (`README.md`, `platformio.ini`, `secrets.example.h`) :** **12 / 12 OK** (10/10 sections conformes pour chaque exemple).
- **Compilations PlatformIO réelles :** **11 / 11 PASS** (0 échec).
- **Compilation Arduino IDE (`MinimalTTS_Arduino`) :** **NOT TESTED (CLI)** (distingué des 11 projets PlatformIO).

---

## 2. Résultats Détaillés par Exemple

| Exemple | Niveau | Type | Structure & README | Compilation | Durée | Flash | RAM |
|---|---|---|---|---|:---:|---|---|
| `MinimalTTS` | Niveau 1 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 51.46 s | 79.5 % (1 667 285 / 2 097 152 o) | 25.4 % (83 204 / 327 680 o) |
| `MinimalSTT` | Niveau 1 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 47.86 s | 79.6 % (1 669 645 / 2 097 152 o) | 25.4 % (83 256 / 327 680 o) |
| `MinimalConversation` | Niveau 1 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 45.63 s | 94.4 % (1 979 541 / 2 097 152 o) | 25.4 % (83 272 / 327 680 o) |
| `TwoClickConversationLVGL` | Niveau 1 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 35.27 s | 96.9 % (2 032 045 / 2 097 152 o) | 40.0 % (131 156 / 327 680 o) |
| `OpenAIConversationLVGL` | Niveau 2 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 96.22 s | 96.2 % (2 018 217 / 2 097 152 o) | 40.0 % (131 228 / 327 680 o) |
| `VoiceSettingsDemo` | Niveau 2 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 94.66 s | 24.0 % (2 009 385 / 8 388 608 o) | 49.7 % (162 996 / 327 680 o) |
| `VoiceDiagnostics` | Niveau 2 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 46.76 s | 80.7 % (1 693 317 / 2 097 152 o) | 25.9 % (84 932 / 327 680 o) |
| `WakeWordFoundation` | Niveau 2 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 84.81 s | 80.7 % (1 693 077 / 2 097 152 o) | 25.7 % (84 320 / 327 680 o) |
| `IntegratedVoiceDemo` | Niveau 3 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 93.22 s | 25.2 % (2 111 677 / 8 388 608 o) | 40.2 % (131 768 / 327 680 o) |
| `WakeWordConversation` | Niveau 3 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 52.10 s | 80.3 % (1 684 341 / 2 097 152 o) | 25.7 % (84 320 / 327 680 o) |
| `LocalToolRegistryDemo` | Niveau 3 | PlatformIO | ✅ OK (10/10) | ✅ **PASS** | 44.90 s | 26.5 % (556 177 / 2 097 152 o) | 7.1 % (23 232 / 327 680 o) |
| `MinimalTTS_Arduino` | Arduino IDE | Arduino (`.ino`) | ✅ OK (10/10) | ⚠️ **NOT TESTED (CLI)** | — | — | — |

---

## 3. Observations Techniques

- Les exemples `TwoClickConversationLVGL` (96.9 %) et `OpenAIConversationLVGL` (96.2 %) utilisent une partition 2 Mo (`no_ota.csv`) proche de la limite ; toute extension UI/polices supplémentaire devra basculer sur `partitions_16MB.csv` (8 Mo), comme le font déjà `VoiceSettingsDemo` et `IntegratedVoiceDemo`.
- Le catalogue pédagogique complet et le guide de choix `<VoiceKit.h>` vs `<VoxCoreVoice.h>` sont documentés dans [`examples/README.md`](../../../examples/README.md).
