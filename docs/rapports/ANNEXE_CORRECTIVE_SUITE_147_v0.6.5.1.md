# Annexe Corrective Suite 147 — VoiceKit v0.6.5.1

**Date :** 13 novembre 2026  
**Version cible :** `v0.6.5.1` (maintenue sans incrément)  
**Commit de référence analysé :** `3f2e41f45c5161b71ac61e5a879856425f8ad671`  
**Statut final :** VALIDÉ (0 échec logiciel, 147 suites passées avec succès)

---

## 1. Contexte et Objectif

Le journal de validation `test_out.txt` du commit `3f2e41f45c5161b71ac61e5a879856425f8ad671` présentait 5 échecs dans la suite 147 (section `TEST P1-CONFIRM`) :

```text
[FAIL] Conf.C.1 WAITING_CONFIRMATION
[FAIL] Conf.C.2 Nouvelle ecoute acceptee
[FAIL] Conf.C.3 Etat LISTENING
[FAIL] Conf.C.4 Tour statut termine
[FAIL] Conf.D.1 WAITING_CONFIRMATION

BILAN FINAL DES TESTS : 3157 REUSSIS, 5 ECHECS
```

De plus, la présentation du bilan affichait à tort `[TEST SUITE 147] SUCCES INTEGRAL` malgré ces 5 échecs.

La mission corrective a consisté à :
1. Reproduire et analyser rigoureusement la cause racine de ces 5 échecs.
2. Corriger le code de production et garantir l'indépendance des tests.
3. Corriger la condition d'affichage du bilan de la suite 147.
4. Valider l'absence totale de régression sur les 146 suites précédentes.
5. Valider la compilation PlatformIO de tous les firmwares cibles.

---

## 2. Cause racine des 5 échecs & Diagnostics

### Diagnostics d'exécution (`[CONF-DIAG]`)
Des diagnostics structurés ont été injectés avant et après chaque scénario A, B, C, D et E :
- `PipelineState`, `VoiceState`, `VoiceReady`, `VoiceBusy`
- `IsListening`, `IsTranscribing`, `IsSpeaking`
- `ActiveTurnId`, `PendingToolId`, `HasPendingConfirmation`
- `MicReservation`, `LastError`

### Mécanisme de défaillance
1. **Défaut de production dans `IntegratedVoicePipeline::startListening()` :**
   Dans le scénario B, une simulation d'échec matériel de capture (`prepareMicCapture((void*)0xdeadbeef)`) fait échouer `_voice->startListening()`.
   `VoxCoreVoice` passe alors en `VoiceState::ERROR_STATE`.
   Or, `IntegratedVoicePipeline::startListening()` appelait aveuglément :
   ```cpp
   setState(PipelineState::READY);
   return false;
   ```
   Cette transition forçait le pipeline en `READY` alors que le moteur vocal sous-jacent était toujours en `ERROR_STATE`.

2. **Défaut d'isolation des tests (cascade B -> C -> D) :**
   Dans la suite 147 d'origine, les scénarios A, B, C, D et E partageaient la même instance de `VoxCoreVoice`, `IntegratedVoicePipeline` et `ToolRegistry`.
   L'échec de capture provoqué au scénario B laissait `_finalTranscriptionReceivedForTurn = true` et des drapeaux non réinitialisés.
   Au démarrage du scénario C, le premier appel `pipeline.startListening()` était perturbé, ce qui provoquait l'échec de `feedSimulatedTranscription("reinitialise", true)` (filtrage de double finale), empêchant la transition vers `WAITING_CONFIRMATION` (`Conf.C.1`).
   Le pipeline restant bloqué dans `TRANSCRIBING`, les assertions suivantes `Conf.C.2`, `Conf.C.3`, `Conf.C.4` et `Conf.D.1` ont échoué en cascade.

---

## 3. Corrections appliquées

### 3.1 Code de production (`src/core/pipeline/IntegratedVoicePipeline.cpp`)
Dans `IntegratedVoicePipeline::startListening()`, la transition après échec de `_voice->startListening()` respecte désormais scrupuleusement l'état réel du moteur :
- Si `_voice->getState() == VoiceState::ERROR_STATE`, le pipeline bascule en `PipelineState::ERROR` et ne force plus artificiellement `READY`.
- Si le moteur n'est pas en erreur, le pipeline revient à `READY`.
- La récupération d'erreur s'opère par le canal formel `recoverFromError()` une fois le moteur rétabli.

### 3.2 Fixture de test & Indépendance (`tests/test_pipeline_suites.h`)
- Création de `ConfFixture` pour garantir l'indépendance stricte et reproductible des scénarios A, B, C, D et E.
- **Scénario A :** Vérification du refus d'écoute quand le moteur est occupé, sans altération du jeton sensible en attente.
- **Scénario B :** Vérification de l'échec matériel micro HAL, non-restauration définitive du jeton, cohérence d'état (`pipeline == ERROR`, `voice == ERROR_STATE`), invalidation du jeton, puis validation explicite du cycle de récupération :
  `Échec capture → ERROR → recovery (recoverFromError) → READY → nouvelle capture réussie`.
- **Scénario C :** Nouvelle capture démarrée après confirmation en attente : annulation irréversible de l'action sensible, démarrage de la nouvelle capture, exécution nominale de la commande de lecture (`system.status`).
- **Scénario D :** Annulation par STOP : invalidation du jeton et refus de confirmation post-STOP.
- **Scénario E :** Terminaison unique et idempotence stricte des notifications `onTurnFinished()`.

### 3.3 Présentation du bilan de la suite 147
Conditionnement de l'annonce `[TEST SUITE 147] SUCCES INTEGRAL` au fait qu'aucune assertion n'ait échoué durant la suite (`s_testsFailed == failuresBefore147`), affichant `[TEST SUITE 147] ECHEC` sinon.

### 3.4 Sécurisation des transports de mock (`tests/test_pipeline_suites.h`)
- Ordre de déclaration de `mockTransportHw` avant `voiceHwFail` pour éviter les accès après libération.
- Remplacement de `strstr` par recherche sécurisée sur taille effective dans `ActivityEndFailingTransport`.
- Réinitialisation systématique des pointeurs de transport (`setTransport(nullptr)`).

---

## 4. Résultats de Validation

### Tests Natifs (C++17)
- **Script exécuté :** `tests/run_tests.bat`
- **Résultat :** **3169 REUSSIS, 0 ECHEC** (147 suites sur 147 validées intégralement).
- **Consignation :** Journal complet enregistré dans `test_out.txt`.

### Compilations PlatformIO (ESP32-S3 / M5Stack CoreS3)
Toutes les cibles requises ont été compilées avec succès :
1. `examples/IntegratedVoiceDemo` (Flash 16 Mo, partitions_16MB.csv) : **SUCCESS** (RAM: 38.8%, Flash: 24.9%)
2. `examples/MinimalSTT` : **SUCCESS** (RAM: 24.1%, Flash: 79.7%)
3. `examples/MinimalTTS` : **SUCCESS** (RAM: 24.0%, Flash: 79.6%)
4. `examples/MinimalConversation` : **SUCCESS** (RAM: 24.1%, Flash: 94.5%)
5. `examples/OpenAIConversation` : **SUCCESS** (RAM: 24.1%, Flash: 80.9%)
6. `examples/VoiceUxLiveDemo` : **SUCCESS** (RAM: 24.3%, Flash: 97.3%)
7. `tests/external-consumer` : **SUCCESS** (RAM: 24.1%, Flash: 80.8%)

---

## 5. Réserves Matérielles Restantes

1. **Stabilité I2S/DMA sur CoreS3 réel :** Le comportement d'arbitrage matériel entre AW88298 (HP) et ES7210 (Micro) a été rigoureusement validé sur mocks logiques et HAL ; la validation finale nécessite le flashage sur cible physique CoreS3.
2. **Qualité acoustique :** Ajustement des gains micro ES7210 et du seuil VAD en environnement réel bruité.

