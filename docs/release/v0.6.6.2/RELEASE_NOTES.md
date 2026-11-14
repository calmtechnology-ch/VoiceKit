# VoiceKit v0.6.6.2 — Notes de Publication (Release Notes)
**Version :** v0.6.6.2 — Public Release Readiness  
**Date :** 14 novembre 2026  
**Type :** Version de stabilisation, de validation et de préparation à la diffusion  
**Statut :** Release Candidate / Prêt pour évaluation avant publication

---

## 1. Présentation de la Version

La version **v0.6.6.2** de VoiceKit constitue un jalon décisif de stabilisation et de préparation technique avant la première mise à disposition publique de la bibliothèque (prévue sous le numéro `v0.7.0`).

Conformément à la règle de non-régression et de gel fonctionnel :
- **Aucune nouvelle fonctionnalité applicative ou moteur d'IA n'a été ajouté.**
- L'effort s'est exclusivement porté sur l'audit du code, la validation de reproductibilité, l'intégration continue, la sécurité des secrets et la documentation didactique pour développeurs tiers.

---

## 2. Fonctionnalités Existantes Intégralement Conservées

L'ensemble des briques architecturales éprouvées de VoiceKit reste disponible sans aucune rupture de signature d'API :
- Façade unifiée `VoiceKit` et moteur audio `VoxCoreVoice`.
- Pipeline d'orchestration vocal `IntegratedVoicePipeline`.
- Routeur de commandes déterministe `VoiceCommandDispatcher`.
- Gestionnaire de profils vocaux universels `VoiceProfileManager` (20 voix, persistance NVS versionnée CRC32).
- Registre local d'outils sécurisé `ToolRegistry` (isolation `READ_ONLY`, `WRITE`, `SENSITIVE`).
- Prise en charge native de Google Gemini Live et OpenAI Realtime.
- Détection locale de mot de réveil « Bonjour ESP » hors ligne (`WakeWordFoundation`).
- Gestion Push-to-Talk (PTT) fiabilisée et commande STOP prioritaire non réentrante.

---

## 3. Plateforme Cible & Prérequis

- **Cible matérielle :** M5Stack CoreS3 (ESP32-S3FN8, 16 Mo Flash, 8 Mo PSRAM, écran IPS 320 × 240, double micro ES7210, HP AW88298).
- **Environnement de développement :** PlatformIO IDE (sous VS Code) avec Arduino Framework `espressif32@6.9.0`.
- **Standard C++ :** **C++17** (`-std=gnu++17`).
- **Dépendances matérielles :** `M5CoreS3@1.0.0`, `M5Unified@0.2.23`.
- **Réseau :** Wi-Fi 2.4 GHz avec accès Internet.

---

## 4. Synthèse des Résultats de Validation

### 4.1 Tests Logiciels Natifs (Hôte GCC)
- **3 716 assertions unitaires exécutées** : **3 716 SUCCÈS, 0 ÉCHEC (100 % PASS)**.
- Couverture complète des 22 scénarios de non-régression et des 10 invariants architecturaux (INV-01 à INV-10).

### 4.2 Compilation des Exemples Officiels (PlatformIO)
- **10 exemples PlatformIO compilés avec succès :** **10 / 10 PASS (100 %)**.
- Absence d'erreur de liaison pour le composant binaire WakeNet `libesp_sr_wakenet_s3.a`.
- Exemple autonome Arduino IDE annoté `NOT TESTED (CLI)` en l'absence de l'outil CLI configuré.

### 4.3 Validation d'Intégration Externe
- Projet PlatformIO indépendant vierge créé hors monorepo.
- Résolution automatique des dépendances et compilation complète validée en 52,69 s.

### 4.4 Validation Matérielle sur M5Stack CoreS3 Physique (Port COM9)
- **19 scénarios physiques validés en direct :** Bootloader, Wi-Fi DHCP, SNTP, audio DMA FreeRTOS, PTT, STT streaming, TTS 24 kHz, conversation Gemini Live complète, rejet de double appui PTT, STOP prioritaire sous 50 ms avec neutralisation des paquets audio tardifs, stabilité mémoire.
- **1 scénario non exécuté sur carte :** OpenAI Realtime (marqué `NOT TESTED`).

---

## 5. Modifications Techniques et Documentaires Apportées

1. **Synchronisation universelle des versions :** Manifestes (`library.json`, `library.properties`), code source (`VoiceHubConfig.h`, `VoxCoreVoice.cpp`) et exemples alignés sur `v0.6.6.2`.
2. **Création du Guide de Démarrage Rapide :** `docs/QUICK_START.md`.
3. **Création du Registre des Limitations :** `docs/KNOWN_LIMITATIONS.md`.
4. **Création de la Politique de Sécurité :** `SECURITY.md`.
5. **Restructuration du README :** 14 sections didactiques sans encombrement historique.
6. **Mise en place de la CI :** Workflow GitHub Actions `.github/workflows/ci.yml`.

---

## 6. Prochaines Étapes vers la Première Bêta Publique (v0.7.0)

Pour transformer cette version candidate en publication publique `v0.7.0`, les étapes restantes sont :
1. **Sécurité :** Révoquer la clé d'API Google historique présente dans le tout premier commit `aae5465`, et procéder à la purge / réécriture de l'historique Git public.
2. **Validation matérielle OpenAI Realtime :** Réaliser un essai vocal physique avec une clé OpenAI active sur le M5Stack CoreS3.
3. **Publication officielle :** Création du tag `v0.7.0` et publication de la GitHub Release après accord explicite du propriétaire du dépôt.
