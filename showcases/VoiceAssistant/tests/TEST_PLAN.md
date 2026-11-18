# Protocole de Tests et Validation Matérielle — VoiceAssistant v0.1.0

Ce document définit le protocole formel de test et de validation de l'application **VoiceAssistant v0.1.0** sur matériel réel M5Stack CoreS3 (ESP32-S3), s'appuyant sur la bibliothèque **VoiceKit v0.6.7.1**.

---

## 1. Environnement de Test

- **Matériel cible** : M5Stack CoreS3 (ESP32-S3FN8, 16 Mo Flash, PSRAM 8 Mo).
- **Audio matériel** : Codec microphone ES7210 (I2S DMA 16 kHz), amplificateur haut-parleur AW88298 (I2S DMA 24 kHz).
- **Écran** : LCD ILI9342C 320 × 240, tactile capacitif FT6336U.
- **Réseau** : Wi-Fi 2.4 GHz 802.11 b/g/n.
- **Moteurs IA** : Google Gemini Live (`models/gemini-2.0-flash-exp`) & OpenAI Realtime (`gpt-realtime-2.1-mini`).
- **Surveillance série** : 115200 bauds, terminal UTF-8.

---

## 2. Préfixes de Logs Série

Les observations série respectent une nomenclature sobre et normalisée :

| Préfixe | Description |
|---|---|
| `[APP]` | Cycle de vie de l'application, initialisation, transitions globales |
| `[VOICE]` | Moteur vocal VoxCore, sessions WebSocket, audio player et streamer |
| `[PTT]` | Machine à états à deux clics (`VoiceInteractionController`), intentions différées |
| `[VAD]` | Validation locale d'activité vocale (`VoiceCaptureValidator`), métriques RMS |
| `[UI]` | Événements tactiles et rafraîchissement d'affichage LVGL 9 |
| `[NET]` | État Wi-Fi, reconnexions réseau et DNS |
| `[ERROR]` | Alertes, timeouts ou anomalies récupérées |

---

## 3. Matrice des Tests Fonctionnels (VA-01 à VA-20)

| ID | Intitulé | Scénario opérationnel | Résultat attendu | Statut |
|---|---|---|---|---|
| **VA-01** | Démarrage initial | Mise sous tension du CoreS3 | Écran éclairé, thème clair affiché, logo VoiceAssistant présent, aucun blocage | Matériel requis |
| **VA-02** | Connexion Wi-Fi | Démarrage avec Wi-Fi disponible | Statut "WiFi OK" dans l'en-tête, logs `[NET] Wi-Fi connecté` | Matériel requis |
| **VA-03** | Connexion Fournisseur | Établissement de la session live | Statut "Assistant prêt" (vert), bouton "PARLER" actif, log `[VOICE] Session conversationnelle prête` | Matériel requis |
| **VA-04** | Premier clic PARLER | Appui tactile sur [ PARLER ] | Bouton devient rouge "TERMINER", micro ouvert, log `[PTT] Début écoute micro ES7210` | Matériel requis |
| **VA-05** | Deuxième clic TERMINER | Second appui après parole | Micro fermé, validation VAD locale engagée, log `[PTT] Fin écoute micro ES7210` | Matériel requis |
| **VA-06** | Conversation nominale | Question posée en français | Question transcrite dans la carte "VOUS", réponse affichée dans "ASSISTANT", audio clair sur AW88298 | Matériel requis |
| **VA-07** | Rejet du silence | Clic PARLER puis clic TERMINER sans parler | VAD rejette (`SILENCE`), log `[VAD][REJECT]`, zéro trame vers l'IA, aucune réponse audio parasite | Matériel requis |
| **VA-08** | Rejet de bruit bref | Clic PARLER, tapotement sur le boîtier (<120 ms), clic TERMINER | VAD rejette (`BRIEF_NOISE`), capture annulée, retour immédiat à READY | Matériel requis |
| **VA-09** | Double clic accidentel | Deux appuis rapides consécutifs (< 350 ms) | VAD rejette (`TOO_SHORT`), anti-rebond 120 ms protège l'état, aucun freeze | Matériel requis |
| **VA-10** | STOP pendant écoute | Clic sur STOP rouge pendant que le micro est ouvert | Micro coupé instantanément, capture annulée, retour à READY, log `[PTT] Bouton STOP prioritaire actionné` | Matériel requis |
| **VA-11** | STOP pendant restitution | Clic sur STOP rouge pendant que l'assistant parle | Audio AW88298 coupé instantanément, session préservée au repos, aucun callback tardif | Matériel requis |
| **VA-12** | Rafale de clics tactiles | Appuis rapides et répétés sur les boutons | Traitement déterministe via intentions différées, zéro corruption mémoire, zéro blocage | Matériel requis |
| **VA-13** | Timeout microphone | Laisser le micro ouvert sans second clic (> 15 s) | Watchdog micro déclenché, fermeture automatique du micro, log `[ERROR] Timeout écoute micro`, retour à READY | Matériel requis |
| **VA-14** | Déconnexion Wi-Fi | Coupure du point d'accès Wi-Fi en cours de fonctionnement | Statut "WiFi NO" (rouge), bouton PARLER désactivé ("ATTENTE"), log `[NET] Perte de connexion Wi-Fi` | Matériel requis |
| **VA-15** | Reconnexion Wi-Fi | Rétablissement du point d'accès Wi-Fi | Reconnexion automatique, rétablissement de la session IA, retour à "Assistant prêt" | Matériel requis |
| **VA-16** | Changement de langue | Sélection d'une nouvelle langue (ex: en-US) dans Paramètres | Langue transmise au fournisseur, confirmation serveur, réponse de l'assistant dans la nouvelle langue | Matériel requis |
| **VA-17** | Changement de voix | Sélection d'une voix différente dans Paramètres | Nouvelle voix négociée, confirmation reçue, timbre vocal modifié à l'essai suivant | Matériel requis |
| **VA-18** | Changement de fournisseur | Bascule Gemini Live <-> OpenAI Realtime dans Paramètres | Session précédente fermée proprement, nouvelle session établie, pilule en-tête mise à jour | Matériel requis |
| **VA-19** | Réponse longue | Question nécessitant une explication détaillée | Restitution audio fluide sans coupure de fin de phrase, buffer DMA alimenté en continu | Matériel requis |
| **VA-20** | Navigation Paramètres | Aller-retour répété Conversation <-> Paramètres | Navigation fluide, conservation de l'état vocal et des textes en mémoire, zéro fuite mémoire | Matériel requis |

---

## 4. Lien avec les Tests Matériels Historiques MAT-01 à MAT-08

VoiceAssistant v0.1.0 préserve l'intégrité des 8 propriétés vérifiées par les tests MAT-01 à MAT-08 de VoiceKit v0.6.7.1 :

- **MAT-01 (Anti-rebond)** : Respecté par VA-09 et `VoiceInteractionConfig::debounceMs = 120`.
- **MAT-02 (VAD Silence)** : Respecté par VA-07 (`speechThresholdFloorRms = 380`).
- **MAT-03 (Bruit bref)** : Respecté par VA-08 (`minConsecutiveSpeechMs = 120`).
- **MAT-04 (STOP écoute)** : Respecté par VA-10 (`triggerStop()` -> `cancelListening()`).
- **MAT-05 (STOP parole)** : Respecté par VA-11 (`triggerStop()` -> `voice.stop()`).
- **MAT-06 (Protection callbacks tardifs)** : Respecté par VA-10/11 (`_lastTurnCancelled = true`).
- **MAT-07 (Timeout micro)** : Respecté par VA-13 (`micTimeoutMs = 15000`).
- **MAT-08 (Idempotence & rafale)** : Respecté par VA-12.

---

## 5. Critères d'Acceptation de la Vitrine

1. **Compilation 100 % réussie** sans avertissement critique sous PlatformIO (`espressif32@6.9.0`).
2. **Aucune altération** du cœur de VoiceKit (`src/`, `examples/`, `library.json`).
3. **Aucune duplication de code** du moteur vocal.
4. **Fluidité tactile** sous LVGL 9 à 30 FPS.
5. **Absence de secrets** : les clés d'API ne figurent que dans `secrets.h` ignoré par git.
