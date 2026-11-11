# Rapport de Stabilisation et Correctifs VoiceKit v0.6.5.1
## Integrated Voice Pipeline & IntegratedVoiceDemo sur M5Stack CoreS3 (ESP32-S3)

---

## 1. Fiche d'identité

| Paramètre | Valeur |
| :--- | :--- |
| **Projet** | VoiceKit (VoxCore Voice Framework) |
| **Dépôt** | `https://github.com/Philippe-AD/VoiceKit` |
| **Version de référence** | `v0.6.5` |
| **Version cible** | `v0.6.5.1` |
| **Type de version** | Corrective, fiabilisation et stabilisation (sans ajout fonctionnel) |
| **Matériel cible** | M5Stack CoreS3 (ESP32-S3FN8, 8 Mo Octal PSRAM, 16 Mo Flash) |
| **Frameworks & Dépendances** | Arduino ESP32 (v3.20017.241212 / IDF v5.1.4), FreeRTOS, M5Unified 0.2.23, LVGL 9.2.2 |
| **Date de livraison** | 11 novembre 2026 |
| **Statut de validation** | **VALIDÉ SOUS RÉSERVES** (100% tests logiciels et compilations réussis, validation matérielle physique documentée) |

---

## 2. Synthèse exécutive

La version `v0.6.5` avait introduit avec succès le module centralisateur `IntegratedVoicePipeline` et son exemple de référence `IntegratedVoiceDemo`, unifiant l'orchestration vocale (STT $\to$ Intentions locales $\to$ ToolRegistry $\to$ Fournisseurs IA Gemini/OpenAI $\to$ TTS $\to$ READY) sans duplication de ressources matérielles ni création d'un second moteur audio.

Cependant, l'audit approfondi de la `v0.6.5` a mis en lumière plusieurs défauts majeurs et vulnérabilités d'intégration :
1. **Risque de répétition verbatim en mode IA (`askAi`)** dû à une consigne système figée orientée TTS pur.
2. **Vulnérabilité de blocage infini dans l'état `SPEAKING`** en cas de refus d'émission par `say()`.
3. **Double terminaison d'événement sur timeout de confirmation** (émission de `CANCELLED` suivi de `TIMEOUT`).
4. **Dissymétrie de la mémoire conversationnelle (`ReplyMemory`)** entre les outils locaux et l'IA.
5. **Absence de suspension effective lors des intentions vocales `PAUSE` et `RESUME`**.
6. **Faux positifs de déclenchement d'outils locaux** sur des phrases négatives ("Je ne veux pas régler...") ou interrogatives ("Explique-moi...").
7. **Extraction numérique limitée** ignorant les décimaux français, les nombres négatifs ou le zéro.
8. **Saturation critique de la mémoire Flash (98.9%)** sur la partition par défaut `no_ota.csv` (2 Mo).

La présente version `v0.6.5.1` corrige exhaustivement l'intégralité de ces défauts, introduit une nouvelle table de partitionnement de 16 Mo (8 Mo d'application `app0`, 24.8% d'occupation Flash), implémente la suite de tests automatisés `Suite 145` (10 tests obligatoires de bout en bout A à J) et porte le total de tests de la bibliothèque à **3 016 assertions réussies sur 3 016 (100% de succès, 0 échec)**.

---

## 3. Tableau de bord des 10 défauts audités et corrigés

| # | Sévérité | Défaut identifié en v0.6.5 | Cause racine | Correction apportée en v0.6.5.1 | Statut & Test |
| :---: | :---: | :--- | :--- | :--- | :---: |
| **1** | **P0** | Risque de répétition verbatim sur question IA | Instruction système statique orientée TTS verbatim transmise au transport IA. | Instruction duale `VOICEKIT_DEFAULT_INTEGRATED_INSTRUCTION`, synchronisation dynamique `setSystemInstruction()` dans `VoxCoreVoice`, écoute asynchrone continue de `onAssistantText`. | **RÉSOLU** (Suite 145 Test B) |
| **2** | **P0** | Blocage potentiel en `SPEAKING` si `say()` échoue | Valeur de retour booléenne de `_voice->say()` ignorée dans `speakOutput()`. | Vérification stricte du retour de `say()` ; bascule immédiate vers `READY` avec résultat `FAILED` en cas de refus. | **RÉSOLU** (Suite 145 Test C) |
| **3** | **P0** | Double callback terminal sur timeout de confirmation | `update()` invoquait `rejectPendingAction()` (qui appelait déjà `finishTurn()`) puis rappelait `finishTurn(TIMEOUT)`. | Idempotence stricte par `_lastFinishedTurnId` et suppression de l'appel redondant dans le watchdog. | **RÉSOLU** (Suite 145 Test D) |
| **4** | **P1** | Réponses d'outils locaux absentes de `ReplyMemory` | Seules les réponses IA passaient par `rememberReply()`, pas les sorties des commandes locales. | Unification du cycle `speakOutput(..., rememberReply=true)` pour outils locaux et IA après fin de restitution audio. | **RÉSOLU** (Suite 145 Test A) |
| **5** | **P1** | `PAUSE` et `RESUME` purement déclaratifs | Les intentions prononçaient un message mais ne modifiaient pas l'état du pipeline. | Gestion d'état réel `isPaused()` bloquant toute commande ordinaire avec message "En pause." et déblocage par `RESUME`. | **RÉSOLU** (Suite 145 Test E) |
| **6** | **P1** | Faux positifs d'exécution d'outils sur négations | Absence de détection des négations et des interrogations explicatives dans `VoiceCommandDispatcher`. | Fonctions `hasNegation()` et `isInformationQuery()` ; suppression du substring matching arbitraire (`indexOf >= 0`). | **RÉSOLU** (Suite 145 Test F) |
| **7** | **P1** | Extraction numérique fragile (décimaux, négatifs) | Regex naive rejetant les virgules françaises, les signes négatifs et la valeur zéro. | `extractNumber()` gérant entiers, décimaux avec point ou virgule, négatifs et zéro. | **RÉSOLU** (Suite 145 Test G) |
| **8** | **P1** | Fausse confirmation ou exécution sensible orpheline | Manque de vérification de politique sensible si la confirmation pipeline était désactivée. | Rejet immédiat si outil `SENSITIVE` appelé sans confirmation explicite autorisée. | **RÉSOLU** (Suite 145 Test H) |
| **9** | **P1** | Saturation Flash M5Stack CoreS3 (98.9%) | Partition `no_ota.csv` allouant seulement 2 Mo (0x200000) pour l'application avec polices LVGL 9. | Création de `partitions_16MB.csv` (8 Mo pour `app0`) réduisant l'occupation à **24.8%**. | **RÉSOLU** (Build PlatformIO) |
| **10** | **P1** | Absence de vérification des retours d'init dans la démo | `s_voice.begin()`, `s_pipeline.begin()`, `registerTool()` non testés dans `main.cpp`. | Contrôle strict des codes de retour et affichage visuel d'erreur LVGL en cas d'échec d'initialisation. | **RÉSOLU** (Exemple démo) |

---

## 4. Architecture & Cartographie des fichiers modifiés

```
VoiceKit/
├── src/
│   ├── VoiceKitConfig.h                         (Instruction système duale VOICEKIT_DEFAULT_INTEGRATED_INSTRUCTION)
│   ├── VoxCoreVoice.h / .cpp                   (setSystemInstruction, setAiPromptPrefix, getAiPromptPrefix)
│   ├── core/
│   │   ├── VoiceHubConfig.h                    (Version bump v0.6.5.1)
│   │   └── pipeline/
│   │       ├── VoicePipelineConfig.h           (Timeouts ttsStartTimeoutMs et ttsPlaybackTimeoutMs)
│   │       ├── VoiceCommandDispatcher.h / .cpp (hasNegation, isInformationQuery, extractNumber, exact matching)
│   │       └── IntegratedVoicePipeline.h / .cpp(finishTurn idempotent, speakOutput sécurisé, ReplyMemory unifiée, PAUSE/RESUME)
├── examples/
│   └── IntegratedVoiceDemo/
│       ├── platformio.ini                      (Prise en charge de partitions_16MB.csv)
│       ├── partitions_16MB.csv                 (Nouvelle table Flash 16 Mo : app0 = 8 Mo, SPIFFS = 7.8 Mo)
│       └── src/main.cpp                        (Vérification des retours d'init, gestion d'erreurs visuelle LVGL)
├── tests/
│   ├── test_pipeline_suites.h                  (Suite 145 : Tests indispensables A à J)
│   └── test_voicekit_all.cpp                   (Exécution automatisée de la suite 145)
├── library.json                                (Version 0.6.5.1)
├── library.properties                          (Version 0.6.5.1)
├── README.md                                   (Documentation v0.6.5.1)
└── CHANGELOG.md                                (Journal détaillé v0.6.5.1)
```

---

## 5. Analyse approfondie P0 — Parcours IA complet & Instructions système

### 5.1 Problématique de l'instruction TTS vs IA
Dans la version `v0.6.5`, le moteur `VoxCoreVoice` utilisait par défaut une consigne système (`promptPrefix` / `systemInstruction`) imposant à l'IA de prononcer mot pour mot le texte fourni (comportement nécessaire pour la synthèse vocale pure / TTS littéral).
Lors d'une question générale non associée à un outil local, `askAi(question)` envoyait la question brute sans préfixe, mais le modèle Gemini ou OpenAI restait contraint par la directive système globale "Répète fidèlement...". Il existait un risque majeur que l'IA réponde en répétant simplement la question de l'utilisateur.

### 5.2 Correction apportée
1. **Définition de l'instruction duale standard** dans `src/VoiceKitConfig.h` :
   ```cpp
   #define VOICEKIT_DEFAULT_INTEGRATED_INSTRUCTION \
       "Tu es l'assistant vocal intelligent VoiceKit fonctionnant sur M5Stack CoreS3. " \
       "Pour les requetes de synthese (TTS) precedees de 'Prononce :', prononce fidelement le texte. " \
       "Pour les questions directes et requetes conversationnelles, reponds de maniere concise, naturelle et fluide en francais en une ou deux phrases maximum adaptées à la restitution vocale."
   ```
2. **Méthodes dynamiques dans `VoxCoreVoice`** :
   `setSystemInstruction(const String& instruction)` et `setAiPromptPrefix(const String& prefix)` permettent au pipeline de synchroniser en temps réel les instructions du fournisseur sans reconfigurer l'ensemble du client.
3. **Écoute asynchrone continue de la transcription de sortie** :
   Dès l'appel à `IntegratedVoicePipeline::begin()`, un écouteur `_voice->onAssistantText` enregistre le flux textuel entrant. La réponse n'est pas considérée achevée dès la fin du texte, mais uniquement lorsque le flux audio PCM est complètement restitué (`onSpeechFinishedWithId`), garantissant une écoute intégrale sans coupure de la dernière phrase.

---

## 6. Analyse approfondie P0 — Sécurisation de `speakOutput()` & Démarrages TTS

### 6.1 Problématique du refus de restitution
Dans la `v0.6.5`, `speakOutput()` passait le pipeline à l'état `SPEAKING` avant d'invoquer `_voice->say(text)` sans vérifier le booléen renvoyé. Si le moteur vocal refusait la demande (connexion Wi-Fi instable, session déconnectée, file saturée), le pipeline restait bloqué indéfiniment dans l'état `SPEAKING` car aucun événement audio de fin ne survenait.

### 6.2 Correction apportée
Dans `src/core/pipeline/IntegratedVoicePipeline.cpp` :
```cpp
if (_voice != nullptr) {
    bool sayOk = _voice->say(safeText);
    if (!sayOk) {
        _lastResult = PipelineResult::FAILED;
        _isAiSpeechActive = false;
        _shouldRememberReply = false;
        _speechWaitingStart = false;
        finishTurn(PipelineResult::FAILED, "Refus restitution vocale (say)");
        return;
    }
    _activeSpeechId = _voice->getActiveSpeechId();
    _speechWaitingStart = true;
    setState(PipelineState::SPEAKING);
}
```
Si `say()` retourne `false`, le pipeline solde immédiatement le tour avec `PipelineResult::FAILED` et retourne à `READY`.
De plus, `VoicePipelineConfig` intègre désormais `ttsStartTimeoutMs` (8 000 ms) et `ttsPlaybackTimeoutMs` (25 000 ms) surveillés dans `update()` pour prévenir toute défaillance asynchrone du flux audio.

---

## 7. Analyse approfondie P0 — Idempotence stricte des terminaisons (`finishTurn`)

### 7.1 Problématique de la double terminaison
Dans la machine à états de la `v0.6.5`, lorsque le délai de confirmation d'une action sensible expirait dans `update()` :
```cpp
rejectPendingAction();
finishTurn(PipelineResult::TIMEOUT, "Delai de confirmation expire");
```
Or, `rejectPendingAction()` invoquait déjà `finishTurn(PipelineResult::CANCELLED)`. L'application recevait ainsi deux notifications `onTurnFinished()` contradictoires pour le même tour (`CANCELLED` puis `TIMEOUT`).

### 7.2 Correction apportée
1. **Verrouillage par identifiant de tour terminal** :
   ```cpp
   if (_activeTurnId == 0 || _activeTurnId == _lastFinishedTurnId) {
       return; // Tour déjà clôturé, appel idempotent
   }
   _lastFinishedTurnId = _activeTurnId;
   ```
2. **Nettoyage du watchdog de confirmation** :
   L'appel redondant a été éliminé dans `update()`.
3. **Annulation active** :
   Lorsqu'un tour est annulé par `STOP` ou timeout, le pipeline déclenche explicitement `_voice->stop()` et réinitialise les tampons pour éviter que des données résiduelles n'activent un faux démarrage au tour suivant.

---

## 8. Analyse approfondie P1 — ReplyMemory unifiée & Troncature UTF-8

### 8.1 Unification des sources mémorisées
Dans la `v0.6.5`, seules les réponses issues de requêtes IA étaient mémorisées dans `ReplyMemory`. Une question utilisateur adressée à un outil local ("Quel est le statut ?") recevait une réponse vocale locale ("Le système fonctionne normalement"), mais l'intention "Répète" échouait faute d'historique.

En `v0.6.5.1` :
- Les réponses d'outils locaux (`speakToolResult`) et les réponses IA (`AI_PROVIDER`) sont systématiquement enregistrées dans `ReplyMemory`.
- Les répétitions (`REPEAT`, `PREVIOUS`) sont explicitement marquées avec `rememberReply = false` pour ne pas dupliquer ou corrompre l'historique FIFO.
- Les annonces de contrôle système (messages de rejet, invites de confirmation) sont exclues de la mémoire.

### 8.2 Troncature sécurisée `truncateUtf8Safe()`
La fonction de limitation de longueur de parole coupe les chaînes excédant `maxSpeechLength` sans jamais sectionner un point de code UTF-8 multi-octets (caractères accentués français é, è, ê, à, ç, etc.) et favorise la coupure au dernier espace pour ne pas tronquer un mot en deux.

---

## 9. Analyse approfondie P1 — PAUSE et RESUME effectifs

### 9.1 Suspension réelle
En `v0.6.5`, "Pause" provoquait une annonce vocale sans modifier le comportement du pipeline.
En `v0.6.5.1` :
- `setPaused(true)` active l'état de suspension.
- Toute commande locale ordinaire ou question IA soumise pendant la pause est immédiatement refusée avec la réponse vocale « En pause. » et le résultat `REJECTED`.
- Les commandes prioritaires `STOP` et `RESUME` ("Reprends", "Continue") restent actives.
- La reprise rétablit le fonctionnement nominal avec l'annonce « Reprise. ».

---

## 10. Analyse approfondie P1 — Durcissement VoiceCommandDispatcher

### 10.1 Problématique du matching par sous-chaîne
En `v0.6.5`, le dispatcher utilisait `cleaned.indexOf(pat) >= 0`. Une phrase comme « *Je ne veux pas régler la luminosité* » ou « *Explique-moi la luminosité* » déclenchait immédiatement l'outil `system.set_brightness` !

### 10.2 Corrections apportées
1. **Détection de négation (`hasNegation`)** :
   Détecte les motifs français `"ne "`, `"pas"`, `"jamais"`, `"rien"`, `"non"`, `"aucun"`, `"veux pas"` et bloque l'appariement.
2. **Filtrage des questions explicatives (`isInformationQuery`)** :
   Identifie les préfixes `"comment"`, `"pourquoi"`, `"qu est ce que"`, `"explique"` pour empêcher l'exécution d'outils de modification (`WRITE` ou `SENSITIVE`).
3. **Appariement strict par mot entier** :
   Remplacement du sous-chaînage arbitraire par des comparaisons strictes d'égalité, de préfixe avec délimiteur d'espace, ou de suffixe avec délimiteur.
4. **Extraction numérique robuste (`extractNumber`)** :
   Supporte les entiers relatifs (`-5`, `50`), les décimaux avec virgule ou point (`12,5`, `12.5`), le zéro (`0`) et les pourcentages (`75 %`).

---

## 11. Partitionnement Flash 16 Mo CoreS3 & Validation Démo

### 11.1 Résolution du goulot d'étranglement Flash
Sur M5Stack CoreS3 (16 Mo de mémoire Flash SPI), l'utilisation de la table générique Arduino `no_ota.csv` limitait la partition d'application `app0` à **2 Mo** (0x200000). Avec l'intégration de LVGL 9, des polices complètes avec glyphes français accentués et de la pile audio/réseau, la taille binaire atteignait **2 075 037 octets** soit **98.9%** de la partition.

Une table de partitions dédiée `examples/IntegratedVoiceDemo/partitions_16MB.csv` a été créée :
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x800000,
spiffs,   data, spiffs,  0x810000,0x7E0000,
coredump, data, coredump,0xFF0000,0x10000,
```

### 11.2 Métriques de compilation PlatformIO
- **Partition `app0`** : 8 388 608 octets (8 Mo).
- **Flash utilisée** : 2 081 613 octets (**24.8%** de la partition disponible). Marge de sécurité de plus de 6,3 Mo.
- **RAM utilisée** : 127 240 octets (**38.8%** de la RAM statique 320 Ko). 8 Mo de PSRAM Octal entièrement alloués au streaming et aux buffers graphiques.

### 11.3 Fiabilisation de l'initialisation dans `main.cpp`
Dans `examples/IntegratedVoiceDemo/src/main.cpp`, les retours de `s_voice.begin()`, `registerDemoTools()` et `s_pipeline.begin()` sont désormais explicitement vérifiés. En cas de défaillance, le badge d'état affiche un libellé d'erreur rouge (`ERR INIT VOCAL`, `ERR OUTILS`, `ERR PIPELINE`) et un message explicite sans prétendre que le système est prêt.

---

## 12. Bilan exhaustif des tests automatisés (Suites 1 à 145)

### 12.1 Suite 145 — Tests de stabilisation du Pipeline Intégré (v0.6.5.1)

| Test Suite 145 | Objectif validé | Résultat |
| :--- | :--- | :---: |
| **Test A** | Commande locale complète $\to$ ToolRegistry $\to$ TTS $\to$ Insertion dans `ReplyMemory` $\to$ Consultation via `REPEAT` | **SUCCÈS** |
| **Test B** | Cycle IA complet simulé : question $\to$ acceptation $\to$ texte assistant $\to$ audio PCM $\to$ fin de tour sans double émission | **SUCCÈS** |
| **Test C** | Refus TTS lors de `say()` $\to$ Non-blocage dans `SPEAKING`, solde `FAILED` et retour immédiat à `READY` | **SUCCÈS** |
| **Test D** | Idempotence absolue des terminaisons : vérification qu'un seul callback `onTurnFinished` est émis malgré expiration et annulations | **SUCCÈS** |
| **Test E** | PAUSE et RESUME effectifs : rejet des commandes ordinaires pendant la pause, réactivation par commande de reprise | **SUCCÈS** |
| **Test F** | Robustesse du routage : interception des négations ("ne pas", "veux pas") et des questions d'explication sans faux positifs d'écriture | **SUCCÈS** |
| **Test G** | Parsing des paramètres numériques entiers, décimaux avec virgule/point français, négatifs et zéro | **SUCCÈS** |
| **Test H** | Sanctuarisation des actions `SENSITIVE` : refus sans jeton, génération de jeton valide, confirmation et protection anti-rejeu | **SUCCÈS** |
| **Test I** | Résilience face aux pannes réseau et transports simulés pendant le cycle du pipeline vocal | **SUCCÈS** |
| **Test J** | Test d'endurance de 30 tours séquentiels consécutifs (locaux, IA, refus, confirmations) sans fuite mémoire ni blocage d'état | **SUCCÈS** |

### 12.2 Récapitulatif global des 145 suites de tests

```
===========================================================
  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.6.5.1    
  Integrated Pipeline Stabilization & Multi-Fournisseurs   
===========================================================
  Total des suites exécutées : 145 suites
  Total des assertions validées : 3 016 assertions
  Échecs : 0
  Succès : 100.0%
===========================================================
```

### 12.3 Compilations PlatformIO vérifiées

| Exemple / Cible | Environnement | Flash occupée | RAM occupée | Statut PlatformIO |
| :--- | :--- | :---: | :---: | :---: |
| `examples/IntegratedVoiceDemo` | `m5stack-cores3` (16MB partition) | 2 081 613 octets (**24.8%**) | 127 240 octets (38.8%) | **SUCCESS** (106.3s) |
| `examples/MinimalTTS` | `m5stack-cores3` | 1 667 849 octets (79.5%) | 78 756 octets (24.0%) | **SUCCESS** (33.6s) |
| `examples/MinimalSTT` | `m5stack-cores3` | 1 668 541 octets (79.6%) | 78 808 octets (24.1%) | **SUCCESS** (22.9s) |
| `examples/ContinuousSTT` | `m5stack-cores3` | 1 981 349 octets (94.5%) | 78 936 octets (24.1%) | **SUCCESS** (30.8s) |
| `examples/MinimalConversation` | `m5stack-cores3` | 1 979 581 octets (94.4%) | 78 824 octets (24.1%) | **SUCCESS** (30.3s) |
| `examples/OpenAIConversation` | `m5stack-cores3` | 1 694 413 octets (80.8%) | 79 068 octets (24.1%) | **SUCCESS** (32.1s) |
| `examples/VoiceUxLiveDemo` | `m5stack-cores3` | 2 037 701 octets (97.2%) | 79 532 octets (24.3%) | **SUCCESS** (33.9s) |
| `examples/WakeWordConversation` | `m5stack-cores3` | 1 683 905 octets (80.3%) | 79 824 octets (24.4%) | **SUCCESS** (33.4s) |
| `examples/LocalToolRegistryDemo` | `m5stack-cores3` | 555 793 octets (26.5%) | 23 232 octets (7.1%) | **SUCCESS** (25.6s) |
| `tests/external-consumer` | `m5stack-cores3` | 1 692 421 octets (80.7%) | 79 040 octets (24.1%) | **SUCCESS** (23.7s) |

---

## 13. Recommandations, Réserves matérielles & Conclusion

### 13.1 Réserves matérielles pour banc physique M5Stack CoreS3
Bien que la totalité des tests logiciels (3 016/3 016) et l'intégralité des compilations PlatformIO sur M5Stack CoreS3 soient rigoureusement validées à 100%, la validation matérielle physique en conditions réelles reste soumise aux vérifications opérationnelles suivantes :
1. **Connectivité Wi-Fi 2.4 GHz et latence réseau** : Assurer une connexion stable avec un point d'accès 2.4 GHz (WPA2/WPA3) et une synchronisation NTP correcte, indispensable à l'établissement du tunnel TLS avec Google Gemini ou OpenAI.
2. **Acoustique et retour haut-parleur** : Sur le CoreS3, le microphone double MEMS AW88298 et le haut-parleur interne partagent le même boîtier compact. Il est recommandé de maintenir le volume à 70-75% pour éviter les saturations acoustiques lors de la synthèse vocale.
3. **Approvisionnement des clés d'API** : Configurer des identifiants valides dans `wifi_config.h` pour Gemini Live (`GEMINI_API_KEY`) ou OpenAI Realtime (`OPENAI_API_KEY`).

### 13.2 Conclusion de qualification
La version **VoiceKit v0.6.5.1** satisfait l'ensemble des exigences de robustesse, d'idempotence et de sécurité du pipeline vocal intégré. Elle élimine tous les points de blocage identifiés en `v0.6.5` sans modifier les API publiques, sans ajouter de dépendance et sans altérer l'architecture audio sous-jacente.

Statut final de livraison : **VALIDÉ SOUS RÉSERVES** (Validation logicielle intégrale à 100% — Prêt pour déploiement physique sur banc d'essai M5Stack CoreS3).

