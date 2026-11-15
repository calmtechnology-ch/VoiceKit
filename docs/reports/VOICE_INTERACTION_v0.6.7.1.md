# RAPPORT DE VALIDATION TECHNIQUE — VoiceKit v0.6.7.1
## Stabilisation de l'Interaction Vocale : VoiceInteractionController & VoiceCaptureValidator
### PTT à Deux Clics, VAD Holistique & STOP Prioritaire sur M5Stack CoreS3

**Date :** 15 novembre 2026  
**Version cible :** `v0.6.7.1 — Voice Interaction Stabilization`  
**Version précédente :** `v0.6.7.0 Experimental`  
**Référence de diffusion publique préparée :** `v0.6.6.2`  
**Application de référence matérielle :** `VoiceKit-SmartUI v0.1.0.6`  
**Exemple de référence autonome :** `TwoClickConversationLVGL`  
**Statut de stabilisation :** **STABILISÉ (Prêt pour réutilisation applicative)**

---

## 1. Résumé des Modifications

La version **v0.6.7.1** de VoiceKit a pour objectif exclusif la **stabilisation et le durcissement** des composants d'interaction vocale introduits en v0.6.7.0 (`VoiceInteractionController` et `VoiceCaptureValidator`), en s'appuyant sur les résultats des tests sur matériel réel M5Stack CoreS3 et sur un audit statique et dynamique approfondi du code.

### 1.1 Périmètre d'intervention
- **Aucune nouvelle fonctionnalité ajoutée** : respect strict du cahier des charges de stabilisation.
- **Aucune refonte d'architecture** : conservation de l'arbre source, des classes existantes et de leurs interfaces.
- **Préservation des comportements validés sur CoreS3 réel** : maintien des seuils VAD, des constantes temporelles et de l'orchestration PTT à deux clics.
- **Non-régression des applications et exemples existants** : préservation de la compatibilité des 12 exemples officiels et de VoiceKit-SmartUI v0.1.0.6.

### 1.2 Synthèse des corrections et durcissements appliqués
1. **Immunisation contre les callbacks tardifs (`VoiceInteractionController`) :**
   - Protection des méthodes `notifyListeningStarted()`, `notifyListeningFinished()` et `notifySpeechStarted()` contre les événements résiduels arrivant après une annulation locale ou une commande STOP (`_lastTurnCancelled == true`).
   - Maintien du contrôleur dans l'état `READY` sans résurrection artificielle des états `LISTENING`, `PROCESSING` ou `SPEAKING`.
2. **Déterminisme des watchdogs et prévention des retours artificiels à READY :**
   - Conditionnement systématique du retour à `READY` à la disponibilité réelle du moteur (`_voice.isReady() && !_voice.isBusy()`).
   - Ajout d'une gestion de repli sécurisée (`cancelListening()`) dans le watchdog microphone (15 s) en cas d'échec de `stopListening()`.
   - Marquage explicite `_lastTurnCancelled = true` lors de l'expiration du timeout de réponse serveur (15 s) afin de garantir qu'une réponse tardive ne sera pas jouée à contretemps.
3. **Harmonisation et conformité documentaire de `TwoClickConversationLVGL` :**
   - Mise en conformité intégrale du `README.md` avec les 10 sections obligatoires des exemples VoiceKit.
   - Intégration de l'exemple dans le banc de validation officiel `tools/validation/validate_examples.py`.
4. **Alignement des métadonnées et manifestes de version :**
   - Mise à jour vers la version `0.6.7.1` dans `library.json`, `library.properties`, `tools/validation/validate_examples.py`, les exemples et les en-têtes documentaires.

---

## 2. Résultats Matériels Validés (Transmis par l'Utilisateur)

Les tests ci-dessous ont été exécutés et validés par l'utilisateur sur une carte **M5Stack CoreS3 physique** équipée de son écran tactile ILI9342C/FT6336U, de son codec ES7210 et de son haut-parleur AW88298 :

| Identifiant | Intitulé du Test Matériel | Scénario Éprouvé sur CoreS3 | Résultat Rapporté |
|:---:|---|---|:---:|
| **MAT-01** | Conversation normale | Clic PARLER, énonciation d'une phrase (2-3 s), clic TERMINER. Réponse vocale complète. | **PASS** |
| **MAT-02** | Silence rejeté | Clic PARLER, maintien du silence ambiant, clic TERMINER. Capture annulée localement, 0 appel IA. | **PASS** |
| **MAT-03** | Bruit bref rejeté | Clic PARLER, tapotement bref sur la table (< 120 ms), clic TERMINER. Rejet `BRIEF_NOISE`, micro coupé. | **PASS** |
| **MAT-04** | Double clic accidentel | Deux clics très rapprochés (< 120 ms). Filtrage anti-rebond matériel actif, écoute maintenue. | **PASS** |
| **MAT-05** | STOP pendant réponse | Appui sur bouton STOP pendant l'élocution de l'assistant. Coupure immédiate du haut-parleur. | **PASS** |
| **MAT-06** | STOP pendant écoute | Appui sur bouton STOP pendant la capture micro. Coupure immédiate, annulation locale. | **PASS** |
| **MAT-07** | Rafale de clics | Appuis tactiles rapides répétés sur les boutons. Aucune désynchronisation d'état ni plantage. | **PASS** |
| **MAT-08** | Timeout microphone | Clic PARLER, parole continue sans clic TERMINER. Clôture automatique à 15 s, évaluation VAD et émission. | **PASS** |

> **Bilan Matériel Confirmé par l'Utilisateur : 8/8 PASS (100% de succès).**  
> *Note méthodologique : Ces résultats proviennent de la validation sur matériel réel par l'utilisateur et ne sont pas une extrapolation automatique ou une invention de logs série.*

---

## 3. Résultats des Tests Logiciels Réellement Exécutés

Les tests logiciels automatisés ont été exécutés directement dans l'environnement Windows PlatformIO / g++ via le script `tests/run_tests.bat`.

### 3.1 Bilan global des tests
- **Compilateur C++ :** MinGW g++ (`-std=gnu++17 -O2`)
- **Total des tests exécutés :** **3 807 assertions**
- **Succès :** **3 807**
- **Échecs :** **0**
- **Régression :** **Aucune**

```text
===========================================================
  BILAN FINAL DES TESTS : 3807 REUSSIS, 0 ECHECS
===========================================================
```

### 3.2 Évolution du nombre de tests (v0.6.7.0 $\rightarrow$ v0.6.7.1)
- **v0.6.7.0 Baseline :** 3 804 assertions réussies.
- **v0.6.7.1 Stabilisation :** 3 807 assertions réussies (+3 assertions).
- **Justification des 3 assertions supplémentaires :** Ajout des vérifications de non-régression dans le scénario 16 de la Suite 152 pour valider le rejet des callbacks tardifs (`notifyListeningStarted()`, `notifyListeningFinished()`, `notifySpeechStarted()`) après une interruption STOP.

### 3.3 Détail de la Suite 152 d'Interaction Vocale
| Scénario | Objectif du Test | Assertions | Résultat |
|:---:|---|:---:|:---:|
| **SC-01** | Initialisation du contrôleur (DISABLED / READY) | 10 | **PASS** |
| **SC-02** | Transition DISABLED $\rightarrow$ READY | 2 | **PASS** |
| **SC-03** | Premier clic (`START_CAPTURE`) | 4 | **PASS** |
| **SC-04** | Deuxième clic avec parole valide (`FINISH_CAPTURE`) | 6 | **PASS** |
| **SC-05** | Deuxième clic avec silence (Rejet VAD local) | 6 | **PASS** |
| **SC-06** | Rejet d'un bruit bref (`BRIEF_NOISE`) | 6 | **PASS** |
| **SC-07** | Rejet d'une capture trop courte (`TOO_SHORT`) | 3 | **PASS** |
| **SC-08** | Anti-rebond tactile (Double clic < 120 ms) | 3 | **PASS** |
| **SC-09** | Commande STOP prioritaire pendant l'écoute micro | 6 | **PASS** |
| **SC-10** | Commande STOP prioritaire pendant la réponse assistant | 4 | **PASS** |
| **SC-11** | Idempotence des commandes STOP répétées | 2 | **PASS** |
| **SC-12** | Résilience à l'échec de `startListening()` | 2 | **PASS** |
| **SC-13** | Résilience à l'échec de `stopListening()` | 2 | **PASS** |
| **SC-14** | Perte de connectivité Wi-Fi pendant l'action | 4 | **PASS** |
| **SC-15** | Rétablissement réseau et transition automatique vers READY | 2 | **PASS** |
| **SC-16** | Protection contre les événements et callbacks tardifs après STOP | 5 | **PASS** |
| **SC-17** | Watchdog d'écoute micro (timeout 15 s) | 3 | **PASS** |
| **SC-18** | Watchdog de réponse serveur (timeout 15 s) | 2 | **PASS** |
| **SC-19** | Intégrité et robustesse UTF-8 des messages de notification | 4 | **PASS** |
| **SC-20** | Absence de double exécution d'une intention utilisateur | 1 | **PASS** |
| **Total** | **Suite 152 complète** | **77** | **100% PASS** |

---

## 4. Résultats des Compilations Matérielles PlatformIO

Toutes les compilations ont été exécutées via PlatformIO (`platform: espressif32@6.9.0`, cible `board: m5stack-cores3`, standard `-std=gnu++17`).

### 4.1 Exemple de référence `TwoClickConversationLVGL`
- **Commande :** `pio run -d examples/TwoClickConversationLVGL`
- **Résultat :** **SUCCÈS (21.84 s)**
- **Occupation RAM :** **131 156 octets** (40.0 % des 320 Ko de RAM interne)
- **Occupation Flash :** **2 060 901 octets** (98.3 % des 2 Mo de partition `no_ota.csv`)
- **Avertissements :** 0 erreur, 0 avertissement bloquant.

### 4.2 Bilan de compilation des 11 exemples PlatformIO
Vérifié par `tools/validation/validate_examples.py` :

| Exemple | Type | Fichiers & Config | Sections README | Statut Compilation | Durée | Empreinte Flash | Empreinte RAM |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **TwoClickConversationLVGL** | PlatformIO | OK | 10/10 | **PASS** | 25.03 s | 2 061 129 o (98.3%) | 131 156 o (40.0%) |
| **MinimalTTS** | PlatformIO | OK | 10/10 | **PASS** | 28.36 s | 1 696 261 o (80.9%) | 83 204 o (25.4%) |
| **MinimalSTT** | PlatformIO | OK | 10/10 | **PASS** | 27.47 s | 1 698 685 o (81.0%) | 83 256 o (25.4%) |
| **MinimalConversation** | PlatformIO | OK | 10/10 | **PASS** | 15.67 s | 2 008 485 o (95.8%) | 83 272 o (25.4%) |
| **OpenAIConversationLVGL** | PlatformIO | OK | 10/10 | **PASS** | 31.91 s | 2 047 229 o (97.6%) | 131 228 o (40.0%) |
| **IntegratedVoiceDemo** | PlatformIO | OK | 10/10 | **PASS** | 23.56 s | 2 140 757 o (25.5%) | 131 768 o (40.2%) |
| **VoiceDiagnostics** | PlatformIO | OK | 10/10 | **PASS** | 15.58 s | 1 722 245 o (82.1%) | 84 932 o (25.9%) |
| **VoiceSettingsDemo** | PlatformIO | OK | 10/10 | **PASS** | 42.97 s | 2 038 373 o (24.3%) | 162 996 o (49.7%) |
| **LocalToolRegistryDemo** | PlatformIO | OK | 10/10 | **PASS** | 21.42 s | 555 793 o (26.5%) | 23 232 o (7.1%) |
| **WakeWordFoundation** | PlatformIO | OK | 10/10 | **PASS** | 28.10 s | 1 722 113 o (82.1%) | 84 320 o (25.7%) |
| **WakeWordConversation** | PlatformIO | OK | 10/10 | **PASS** | 29.40 s | 1 713 369 o (81.7%) | 84 320 o (25.7%) |
| **MinimalTTS_Arduino** | Arduino IDE | OK | 10/10 | *NOT TESTED (CLI)* | — | — | — |

> **Résultat de l'audit de conformité : 12/12 exemples validés en structure (100% OK), 11/11 exemples PlatformIO compilés avec succès.**

---

## 5. Audit des 10 Points et Anomalies Corrigées

| # | Point d'Audit | Constat Initial (v0.6.7.0) | Correction Appliquée (v0.6.7.1) | Statut |
|:---:|---|---|---|:---:|
| **1** | Cohérence des transitions | Cycle nominal conforme mais risque de retour prématuré à `READY` si `_voice.isBusy()`. | Vérification stricte `!_voice.isBusy()` avant toute transition vers `READY`. | **CORRIGÉ & VALIDÉ** |
| **2** | Absence de double ouverture micro | Protégé par les verrous booléens et l'intention `START_CAPTURE`. | Conservé intact (confirmé MAT-04 & SC-03). | **CONFORME** |
| **3** | Absence de double validation | `_validator.stop()` et `_isListeningActive = false` exécutés en tête de fonction. | Conservé intact (confirmé SC-04 & SC-20). | **CONFORME** |
| **4** | Annulation effective silence/bruits | Appel local exclusif à `_voice.cancelListening()`, 0 trame IA. | Conservé intact (confirmé MAT-02, MAT-03, SC-05, SC-06). | **CONFORME** |
| **5** | Priorité de STOP universelle | STOP traité en tête de `processIntents()` et priorité absolue sur les clics. | Conservé intact (confirmé MAT-05, MAT-06, SC-09, SC-10, SC-11). | **CONFORME** |
| **6** | Protection callbacks tardifs | Vulnérabilité : `notifyListeningStarted()`, `notifyListeningFinished()` ou `notifySpeechStarted()` pouvaient forcer un changement d'état après STOP. | Ajout de filtres systématiques vérifiant `!_lastTurnCancelled` et la cohérence de l'état source. | **CORRIGÉ & VALIDÉ** |
| **7** | Reconnexion après coupure | Passage à `DISABLED` puis retour à `READY` lors de la réactivation. | Conservé intact (confirmé SC-14 & SC-15). | **CONFORME** |
| **8** | Déterminisme des watchdogs | Watchdog micro 15 s ne gérait pas un éventuel refus de `stopListening()`. | Ajout de repli immédiat sur `cancelListening()` en cas d'échec d'arrêt. | **CORRIGÉ & VALIDÉ** |
| **9** | Non-blocage des tâches FreeRTOS | Aucun appel bloquant (`delay`, boucle d'attente ou mutex lourd) dans le contrôleur ni le validateur ($O(1)$). | Aucun blocage détecté, CPU stable. | **CONFORME** |
| **10** | Non-régression des API existantes | Toutes les signatures publiques et méthodes de compatibilité sont préservées. | 100% de compatibilité descendante. | **CONFORME** |

---

## 6. Compatibilité et Non-Régression des Applications

### 6.1 VoiceKit-SmartUI v0.1.0.6
- L'application historique de référence **n'a subi aucune modification**.
- Elle demeure la référence matérielle intouchée.
- La migration de SmartUI vers `VoiceInteractionController` et `VoiceCaptureValidator` s'effectuera dans un jalon ultérieur dédié.

### 6.2 Exemples historiques de VoiceKit
- Tous les exemples existants continuent d'utiliser directement `VoxCoreVoice` ou `VoiceKit` sans nécessiter les nouveaux composants.
- L'introduction de `VoiceInteractionController` et `VoiceCaptureValidator` est strictement modulaire et optionnelle.

### 6.3 Exemple de référence `TwoClickConversationLVGL`
- L'architecture de `TwoClickConversationLVGL` a été auditée : elle se limite rigoureusement à l'initialisation matérielle, l'IHM LVGL 9 et le relais d'événements vers la bibliothèque.
- **Aucune duplication de la logique VAD ou PTT dans le code applicatif**.

---

## 7. Limitations Restantes

1. **Ajustement pour microphones non-ES7210 :**  
   Les paramètres par défaut du validateur VAD (plancher 380 RMS, seuil $1.8 \times \text{noiseFloor} + 150$) sont calibrés pour le codec Everest ES7210 du M5Stack CoreS3. Sur une autre cible matérielle (ex: micro analogique ou SPM1423), une adaptation via `VoiceInteractionConfig` est nécessaire.
2. **Environnement sonore continu très élevé :**  
   En cas d'exposition soudaine à un bruit continu supérieur à 85 dB RMS, le filtre EMA requiert environ 1 à 2 secondes d'ambiance pour réajuster le plancher de bruit.
3. **Partitionnement Flash :**  
   Les exemples intégrant à la fois les polices typographiques complètes et LVGL 9 nécessitent une partition d'au moins 2 Mo pour le firmware (`no_ota.csv` ou `partitions_16MB.csv`).
4. **Diffusion publique :**  
   Le dépôt demeure privé et aucun tag Git ou version publique n'est publié tant que les clés révoquées dans l'ancien historique Git n'ont pas été formellement purgées.

---

## 8. Décision de Stabilisation

| Critère d'Acceptation | Exigence | Constat v0.6.7.1 | Décision |
|---|---|---|:---:|
| **Tests logiciels natifs** | 100% de réussite sur les tests existants | **3 807/3 807 PASS (0 échec)** | **VALIDÉ** |
| **Validation matérielle** | 8/8 scénarios fonctionnels CoreS3 | **8/8 PASS (Confirmé utilisateur)** | **VALIDÉ** |
| **Compilations PlatformIO** | 100% de succès sur les exemples ciblés | **11/11 PlatformIO PASS** | **VALIDÉ** |
| **Protection callbacks tardifs** | Aucune désynchronisation après STOP | **Vérifié et testé unitairement** | **VALIDÉ** |
| **Respect de l'architecture** | Aucune régression sur SmartUI ou exemples | **Strictement additif et non-invasif** | **VALIDÉ** |
| **Documentation et cohérence** | Guides, rapports et manifestes alignés | **Alignement 0.6.7.1 complet** | **VALIDÉ** |

### **DÉCISION FINALE : GO POUR STABILISATION (v0.6.7.1)**

La version **v0.6.7.1** atteint son objectif d'interaction vocale stabilisée. Les composants `VoiceInteractionController` et `VoiceCaptureValidator` sont déclarés stables, robustes et prêts pour une réutilisation universelle dans les applications et démonstrateurs tiers sur M5Stack CoreS3.
