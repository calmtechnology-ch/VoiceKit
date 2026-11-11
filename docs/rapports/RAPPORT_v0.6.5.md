# Rapport de Livraison VoiceKit v0.6.5 — Integrated Voice Pipeline

---

## 1. Synthèse Exécutive

La version **v0.6.5** de **VoiceKit** constitue une avancée architecturale déterminante. Elle fait évoluer la bibliothèque d'une collection de briques logicielles expertes (moteur audio `VoxCoreVoice`, registre de commandes `ToolRegistry`, arbitrage local `VoiceUxController`, transport multi-fournisseurs `IVoiceProvider`) vers un **pipeline vocal autonome et intégré** (`IntegratedVoicePipeline`).

Avant cette version, l'intégration complète d'une interaction vocale obligeait l'application hôte (`main.cpp`) à orchestrer manuellement :
1. L'armement du bouton Push-to-Talk (PTT).
2. L'attente et le filtrage des transcriptions STT.
3. L'arbitrage entre intentions conversationnelles locales (STOP, REPEAT) et commandes applicatives.
4. L'analyse syntaxique de la phrase pour invoquer un outil dans le `ToolRegistry`.
5. La transmission conditionnelle vers une IA distante (Gemini Live ou OpenAI Realtime).
6. La restitution vocale TTS du résultat sans conflit avec le microphone.
7. La confirmation sur écran tactile des opérations sensibles.

Avec la v0.6.5, **cette logique d'orchestration est entièrement encapsulée** dans la classe centrale `IntegratedVoicePipeline`, configurable par `VoicePipelineConfig`, tout en garantissant :
- Le respect strict de la **Règle A** : Un seul moteur audio physique (`VoxCoreVoice`), sans double buffer PCM, ni double tâche FreeRTOS, ni conflit I2S.
- Le respect de la **Règle B** : Un seul tour actif à la fois (séquencement strict et numérotation monotone).
- Le respect de la **Règle C** : Aucune double exécution (corrélation de bout en bout via `turnId` et `speechId`).
- Le respect de la **Règle D** : Cloisonnement strict des responsabilités (le pipeline orchestre, les composants spécialisés exécutent).
- Le respect de la **Règle E** : Préservation intégrale de la rétrocompatibilité (2945 assertions de tests réussies, 0 échec).

---

## 2. Tableau d'Audit Comparatif Initial

Conformément à la Section 2 du cahier des charges, voici le bilan d'analyse des composants préexistants :

| Composant | Statut v0.6.4.3.2 | Responsabilité | Adaptation pour v0.6.5 | Lacunes comblées |
| :--- | :--- | :--- | :--- | :--- |
| `VoxCoreVoice` | **Réutilisable avec ajout** | Moteur audio central (I2S, DMA, WebSocket, STT, TTS) | Ajout de la méthode `askAi(question)` et de `getLastAssistantText()` | Permet de soumettre une question à l'IA sans préfixe directif de répétition littérale TTS (`promptPrefix`). |
| `LiveSessionController` | **Réutilisable sans modif** | Gestionnaire de session WebSocket et flux audio | Aucune modification nécessaire | Transport fiable et détection des interruptions déjà validés. |
| `TextGate` | **Réutilisable sans modif** | Filtrage des phrases vides et hésitations vocales | Aucune modification nécessaire | Réutilisé directement par le pipeline au stade initial de filtrage. |
| `VoiceIntentRouter` | **Réutilisable sans modif** | Détection d'intentions locales (STOP, REPEAT, etc.) | Aucune modification nécessaire | Détection immédiate des commandes conversationnelles prioritaires. |
| `ReplyMemory` | **Réutilisable sans modif** | Historique FIFO des réponses vocales | Aucune modification nécessaire | Alimenté par le pipeline uniquement après restitution complète. |
| `ToolRegistry` | **Réutilisable sans modif** | Catalogue et exécution sécurisée des commandes | Aucune modification nécessaire | Politiques `READ_ONLY`, `WRITE`, `SENSITIVE` et jetons de confirmation exploités nativement. |
| `VoiceUxLiveBridge` | **Conservé (Exemples)** | Pont UX pour applications légères sans ToolRegistry | Maintien pour rétrocompatibilité | Le nouveau pipeline `IntegratedVoicePipeline` va plus loin en intégrant les outils et l'IA. |
| `VoiceCommandDispatcher` | **NOUVEAU** | Liaison déterministe phrase $\to$ outil | Création `VoiceCommandDispatcher.h/.cpp` | Normalisation du français, stripping de politesse, extraction d'arguments entiers/flottants. |
| `IntegratedVoicePipeline` | **NOUVEAU** | Orchestrateur central de tour vocal séquentiel | Création `IntegratedVoicePipeline.h/.cpp` | Machine à états finis à 12 états, gestion de timeouts, corrélation `turnId`/`speechId`. |
| `VoicePipelineConfig` | **NOUVEAU** | Configuration déclarative du pipeline | Création `VoicePipelineConfig.h` | Activation/désactivation fine des sous-systèmes, timeouts, consignes orales. |

---

## 3. Architecture Logicielle Complète de la v0.6.5

L'architecture v0.6.5 repose sur un modèle d'orchestration non-intrusif qui connecte les composants existants sans en dupliquer les responsabilités :

```
                                  +------------------------------------+
                                  |         Application Hôte           |
                                  |   (ex: IntegratedVoiceDemo, LVGL)  |
                                  +-----------------+------------------+
                                                    |
                                                    v
                                  +------------------------------------+
                                  |      IntegratedVoicePipeline       |
                                  | (Orchestrateur séquentiel 12 états)|
                                  +---+--------------+--------------+--+
                                      |              |              |
                +---------------------+              |              +---------------------+
                v                                    v                                    v
     +--------------------+               +--------------------+               +--------------------+
     |    VoxCoreVoice    |               |  VoiceUxController |               |    ToolRegistry    |
     | (Audio/STT/TTS/IA) |               |  (TextGate, Router,|               |  (Outils & Sécurité|
     +----------+---------+               |   ReplyMemory)     |               |    SENSITIVE)      |
                |                         +--------------------+               +---------+----------+
                |                                                                        ^
                |                                                                        |
                |                         +--------------------+                         |
                +------------------------>|    VoiceCommand    |-------------------------+
                                          |     Dispatcher     |
                                          +--------------------+
```

### Principes Directeurs
1. **Source Unique d'Audio** : `VoxCoreVoice` conserve la possession exclusive des périphériques audio (micro ES7210 / haut-parleur AW88298 sur le CoreS3).
2. **Durée de Vie et Inversion de Contrôle** : Le pipeline reçoit des références aux instances existantes (`pipeline.begin(voice, tools, config)`). Il ne masque aucune allocation dynamique cachée.
3. **Boucle Coopérative Non Bloquante** : `pipeline.update()` est cadencé dans la boucle principale (`loop()`) ou dans une tâche dédiée sans jamais bloquer l'interface graphique LVGL 9.

---

## 4. Machine à États du Pipeline (12 États)

L'orchestrateur implémente une machine à états finis formelle comportant 12 états distincts garantissant qu'une seule opération est active à la fois.

### Diagramme d'États Mermaid

```mermaid
stateDiagram-v2
    [*] --> READY
    
    READY --> LISTENING : startListening()
    READY --> ERROR : Erreur asynchrone
    
    LISTENING --> TRANSCRIBING : stopListening()
    LISTENING --> READY : cancel() [STOP]
    
    TRANSCRIBING --> ROUTING : Transcription finale reçue
    TRANSCRIBING --> READY : Timeout STT / cancel()
    
    ROUTING --> READY : Filtre TextGate (Ignore)
    ROUTING --> READY : Intention STOP
    ROUTING --> LOCAL_PROCESSING : Intention UX (REPEAT, PREVIOUS, PAUSE)
    ROUTING --> TOOL_EXECUTING : Commande READ_ONLY ou WRITE reconnue
    ROUTING --> WAITING_CONFIRMATION : Commande SENSITIVE reconnue
    ROUTING --> AI_PROCESSING : Demande transmise à l'IA
    ROUTING --> SPEAKING : Refus explicite prononcé
    ROUTING --> READY : Refus silencieux
    
    LOCAL_PROCESSING --> SPEAKING : speakOutput(rappel)
    
    TOOL_EXECUTING --> SPEAKING : speakOutput(résultat)
    TOOL_EXECUTING --> READY : speakToolResult=false
    
    WAITING_CONFIRMATION --> TOOL_EXECUTING : confirmPendingAction() [Succès]
    WAITING_CONFIRMATION --> READY : rejectPendingAction() / Timeout / cancel()
    
    AI_PROCESSING --> SPEAKING : Réponse vocale IA reçue
    AI_PROCESSING --> READY : Timeout IA / cancel()
    AI_PROCESSING --> ERROR : Échec transport IA
    
    SPEAKING --> READY : onSpeechFinished(speechId)
    SPEAKING --> STOPPING : cancel() [STOP]
    
    STOPPING --> READY : voice.isBusy() == false
    
    ERROR --> READY : recoverFromError()
```

### Description des Transitions Clés
- **`ROUTING` vers `WAITING_CONFIRMATION`** : Lorsqu'un outil avec `ToolSecurityPolicy::SENSITIVE` est détecté, `ToolRegistry::execute()` émet un jeton unique (`CONFIRMATION_REQUIRED`). Le pipeline bascule en `WAITING_CONFIRMATION` et émet le callback `onConfirmationRequired()`. Si `speakConfirmationRequired` est activé, l'invite vocale est prononcée en arrière-plan sans quitter cet état d'attente.
- **Protection Anti-Interférence `STOPPING`** : Une demande d'arrêt d'urgence (`cancel()`) pendant la parole place le système en `STOPPING` jusqu'à ce que les tampons DMA de `LiveAudioPlayer` soient vidés, évitant tout bruit transitoire lors du tour suivant.

---

## 5. VoiceCommandDispatcher

Le module `VoiceCommandDispatcher` résout de façon déterministe la correspondance entre les phrases transcrites en langage naturel et les identifiants d'outils du `ToolRegistry`.

### Caractéristiques Principales
- **Normalisation Linguistique Française** :
  - Conversion en minuscules avec prise en charge des caractères UTF-8 accentués (`é`, `è`, `ê`, `à`, `ç`, `ô`, `ù`, etc.).
  - Suppression de la ponctuation (`?`, `!`, `,`, `.`, `;`, `:`).
  - Réduction des espaces multiples et rognage (`trim()`).
- **Élimination des Formules de Politesse** :
  - Détection et suppression automatique des préfixes tels que « peux-tu me donner », « s'il vous plaît », « dis-moi », « je voudrais savoir », « active ».
  - Suppression des suffixes tels que « s'il te plaît », « merci ».
- **Extraction Typée des Paramètres** :
  - Recherche et extraction automatique des valeurs numériques entières et flottantes dans la phrase (ex: « règle la luminosité à 75 » $\to$ `args.set("level", 75)`).
- **Sécurité et Filtrage** :
  - Aucune commande inconnue n'est exécutée arbitrairement.
  - Vérification de l'existence de l'outil dans le `ToolRegistry` avant d'émettre une décision de routage.

---

## 6. Routage des Demandes vers l'IA

### Analyse Technique du Problème Initial
Dans les versions antérieures, la méthode `VoxCoreVoice::say(text)` avait été conçue pour la synthèse vocale stricte d'un texte fourni :
```cpp
bool VoxCoreVoice::dispatchSpeech(const String& text) {
    String prompt = _config.promptPrefix + text; // "Prononce exactement et uniquement ce texte : "
    return _session.sendTextMessage(prompt, _activeSpeechId);
}
```
L'utilisation de `say()` pour poser une question à l'IA provoquait la récitation littérale de la question par le modèle Gemini, au lieu de produire une réponse conversationnelle !

### Solution Apportée en v0.6.5
1. **Introduction de `VoxCoreVoice::askAi(const String& question)`** :
   - Bypasse le préfixe de répétition littérale (`promptPrefix`).
   - Applique une consigne conversationnelle concise configurable (`aiPromptPrefix` : « Réponds brièvement et directement en français en une ou deux phrases : ») lorsque le mode conversationnel global est inactif.
   - Utilise l'infrastructure de streaming WebSocket existante sans créer de seconde connexion réseau.
2. **Cycle de Mémorisation Garanti** :
   - Le texte de la réponse de l'assistant IA est extrait via `getLastAssistantText()`.
   - Il n'est inséré dans l'historique `ReplyMemory` **qu'après achèvement effectif et complet de sa restitution vocale** (callback `onSpeechFinishedWithId`), empêchant toute mémorisation d'une réponse interrompue ou avortée.

---

## 7. Gestion de la Confirmation et Écran Tactile

### Déroulement pour les Commandes `SENSITIVE`
Lorsqu'un utilisateur dicte une commande classée sensible (ex: « Réinitialiser l'écran » ou « Formater le stockage ») :
1. Le dispatcher identifie l'outil `system.reset_screen`.
2. Le pipeline constate la politique `ToolSecurityPolicy::SENSITIVE`.
3. Le `ToolRegistry` génère un jeton cryptographique à usage unique et passe en attente de confirmation.
4. L'événement `onConfirmationRequired(turnId, toolId, token, prompt)` est notifié à l'application.
5. L'interface graphique LVGL 9 affiche instantanément une boîte de dialogue modale au centre de l'écran avec deux boutons tactiles : **« Confirmer »** et **« Refuser »**.
6. **Action Utilisateur** :
   - Clic sur « Confirmer » $\to$ `pipeline.confirmPendingAction()`. Le jeton est consommé, l'outil s'exécute, le résultat est prononcé et la modale se referme.
   - Clic sur « Refuser » $\to$ `pipeline.rejectPendingAction()`. L'action est annulée, le statut devient `CANCELLED`, la modale disparaît.
   - Absence d'action $\to$ Au bout de `confirmationTimeoutMs` (30 s par défaut), le tour expire en `TIMEOUT`.

---

## 8. Protection Anti-Rejeu et Corrélation

Conformément aux règles de sécurité P0 validées en v0.6.4.3.2, la v0.6.5 verrouille les flux asynchrones :
1. **Numérotation Monotone des Tours (`turnId`)** :
   - Chaque activation d'écoute incrémente l'identifiant monotone du moteur et du pipeline.
   - Toute transcription reçue portant un `turnId` antérieur ou différent de l'actuel est immédiatement rejetée sans décision ni exécution.
2. **Numérotation Monotone de Restitution (`speechId`)** :
   - Chaque annonce vocale se voit attribuer un identifiant unique de parole.
   - Une notification matérielle de fin de lecture audio issue d'une ancienne annonce interrompue ne peut pas clore un nouveau tour actif.
3. **Invalidation par STOP** :
   - L'appel à `cancel()` arme `_cancelledTurnId`. Tout résidu différé arrivant du serveur WebSocket ou des tampons DMA est éliminé.
4. **Protection Anti-Rejeu du Jeton** :
   - Un jeton de confirmation consommé ne peut plus jamais être validé une seconde fois (`PERMISSION_DENIED`).

---

## 9. Exemple de Référence IntegratedVoiceDemo

Un projet complet et immédiatement exploitable a été développé pour le **M5Stack CoreS3** dans `examples/IntegratedVoiceDemo` :
- **Fichiers inclus** :
  - `platformio.ini` : Environnement PlatformIO configuré pour `m5stack-cores3` avec Arduino-ESP32 3.x, M5Unified 0.2.23, M5CoreS3 1.0.0 et LVGL 9.2.2.
  - `include/lv_conf.h` : Configuration optimisée de LVGL 9 avec gestion de la mémoire PSRAM.
  - `include/secrets.h` : Gestion sécurisée des identifiants Wi-Fi et clés d'API (Gemini Live / OpenAI Realtime).
  - `src/main.cpp` : Code source documenté implémentant l'interface tactile et vocale complète.
- **Outils de Démonstration Enregistrés** :
  1. `system.get_status` (`READ_ONLY`) : Rapport d'état et niveau de batterie.
  2. `system.get_free_memory` (`READ_ONLY`) : Mesure de la RAM interne et de la PSRAM disponible.
  3. `system.set_brightness` (`WRITE`) : Réglage du rétroéclairage de l'écran avec argument numérique `level`.
  4. `system.reset_screen` (`SENSITIVE`) : Réinitialisation visuelle avec confirmation obligatoire à l'écran.

---

## 10. Bilan Exhaustif des Tests Automatisés

La suite complète de tests a été exécutée sous environnement natif avec le compilateur GNU C++17 via `tests/run_tests.bat`.

### Synthèse des Suites de Tests (Suites 1 à 144)

| Suite | Périmètre | Assertions | Résultat |
| :--- | :--- | :---: | :---: |
| **Suites 1 à 79** | Moteur audio de base, TTS, STT, ContinuousSTT, VAD, profils vocaux, Wi-Fi | 1 540 | **SUCCÈS (0 régression)** |
| **Suites 80 à 112** | Multi-fournisseurs (Gemini Live & OpenAI Realtime), audio PCM 24 kHz, métriques | 680 | **SUCCÈS (0 régression)** |
| **Suites 113 à 129** | Local Tool Registry v0.6.4 (paramètres typés, limites, autorisations, jetons) | 260 | **SUCCÈS (0 régression)** |
| **Suites 130 à 134** | Voice UX Foundation v0.6.4.1 (TextGate, VoiceIntentRouter, ReplyMemory) | 185 | **SUCCÈS (0 régression)** |
| **Suites 135 à 143** | VoiceUxLiveBridge & Corrélation événementielle corrective (v0.6.4.2 à v0.6.4.3.2) | 148 | **SUCCÈS (0 régression)** |
| **Suite 144** | **VoiceKit v0.6.5 — Integrated Voice Pipeline (Tests A à J)** | **132** | **100% SUCCÈS** |
| **TOTAL** | **Toutes suites confondues** | **2 945** | **2 945 RÉUSSIS (0 ÉCHEC)** |

### Détail des 10 Tests Obligatoires de la Suite 144

1. **TEST A — Tour local complet** : Cycle Push-to-Talk $\to$ transcription STT avec politesse $\to$ validation TextGate $\to$ correspondance `system.get_status` $\to$ exécution ToolRegistry $\to$ restitution vocale TTS $\to$ retour à l'état `READY`. **Validé.**
2. **TEST B — Tour IA complet** : Question sans commande locale $\to$ routage vers `AI_PROVIDER` $\to$ transmission sans consigne littérale $\to$ retour à l'état `READY`. **Validé.**
3. **TEST C — Commande inconnue** : Phrase non reconnue avec IA désactivée $\to$ rejet explicite `REJECTED` $\to$ aucun outil arbitraire exécuté. **Validé.**
4. **TEST D — STOP dans tous les états** : Interruption `cancel()` validée successivement dans `LISTENING`, `TRANSCRIBING`, `WAITING_CONFIRMATION` $\to$ retour déterministe à `READY`. **Validé.**
5. **TEST E — Transcription tardive** : Transcription différée d'un tour antérieur annulé $\to$ rejet sans exécution pour le tour en cours. **Validé.**
6. **TEST F — Ancienne fin TTS** : Notification de fin de restitution avec `speechId` obsolète $\to$ tour en cours inaltéré. **Validé.**
7. **TEST G — Commande sensible** : Déclenchement de `system.factory_reset` $\to$ état `WAITING_CONFIRMATION` $\to$ rejet avec faux jeton $\to$ succès avec vrai jeton $\to$ protection anti-rejeu. **Validé.**
8. **TEST H — Erreur et récupération** : Notification d'erreur réseau $\to$ transition `ERROR` $\to$ réarmement via `recoverFromError()`. **Validé.**
9. **TEST I — ReplyMemory non polluée** : Exécution de commandes `REPEAT` et `STOP` $\to$ historique local reste totalement vierge de toute pollution interne. **Validé.**
10. **TEST J — Normalisation VoiceCommandDispatcher** : Validation de la suppression des accents, de la ponctuation, de la casse, du stripping des formules de politesse et de l'extraction des nombres. **Validé.**

---

## 11. Analyse des Performances et Empreinte Mémoire

Les mesures de compilation embarquée PlatformIO pour cible M5Stack CoreS3 (ESP32-S3, 16 Mo Flash, 8 Mo PSRAM) indiquent :

- **Occupation Mémoire Flash** : 2 044 369 octets (97.5% de la partition standard app de 2 Mo sans OTA). Pour un déploiement industriel intégrant LVGL 9 et les bibliothèques réseau complètes, il est recommandé d'utiliser une table de partitions 16 Mo (ex: `large_spiffs_16MB.csv` ou partition app de 4 Mo).
- **Occupation Mémoire RAM Interne** : 127 044 octets sur 327 680 octets disponibles (**38.8%** de charge RAM). Une marge de plus de 200 Ko de RAM interne reste disponible pour les descripteurs DMA I2S et SPI.
- **Latence de Traitement Locale** : L'analyse d'une transcription par `TextGate` et `VoiceCommandDispatcher` prend moins de 4 microsecondes sur processeur cadencé à 240 MHz, garantissant une réactivité instantanée avant tout déclenchement réseau ou actionneur.

---

## 12. Délimitations Matérielles et Banc Réel

- **Validé sur Banc Logiciel et Simulateur Intégré** :
  - 100% de la logique d'état, de la corrélation temporelle, du parseur d'arguments, de l'arbitrage UX et des politiques de sécurité.
  - La compilation et le link binaire PlatformIO pour architecture Xtensa ESP32-S3.
- **Opérations Nécessitant le Matériel Physique M5Stack CoreS3** :
  - L'étalonnage acoustique du double microphone I2S ES7210 en environnement bruité.
  - La réponse en fréquence de l'amplificateur audio AW88298 et de son haut-parleur intégré.
  - La sensibilité du contrôleur tactile capacitif FT6336 sous diverses conditions d'alimentation USB.

---

## 13. Guide de Migration de la v0.6.4.3.2 vers la v0.6.5

La transition vers la v0.6.5 simplifie radicalement le code utilisateur :

### Avant (v0.6.4.3.2)
```cpp
// Nécessitait la gestion manuelle de dizaines de callbacks dans main.cpp :
s_voice.onTranscriptionWithTurnId([](const String& text, bool isFinal, uint32_t turnId) {
    if (isFinal) {
        if (text == "statut") {
            ToolResult r = s_tools.execute("system.get_status");
            s_voice.say(r.output);
        } else {
            // Logique manuelle de rejet ou transmission IA complexe
        }
    }
});
```

### Après (v0.6.5)
```cpp
#include <IntegratedVoicePipeline.h>

IntegratedVoicePipeline pipeline;

void setup() {
    // 1. Initialiser moteur et outils
    voice.begin(vcfg);
    tools.registerTool(...);

    // 2. Initialiser et mapper le pipeline
    pipeline.begin(voice, tools);
    pipeline.getDispatcher().registerMapping("system.get_status", {"statut", "etat"});

    // 3. Écouter les événements de haut niveau
    pipeline.onToolResult([](uint32_t turn, const String& toolId, const ToolResult& res) {
        // UI mise à jour automatiquement
    });
}

void loop() {
    pipeline.update(); // Gère tout le cycle de vie de manière non bloquante
}
```

---

## 14. Conclusion et Perspectives

La version **v0.6.5 — Integrated Voice Pipeline** atteint avec une rigueur absolue l'ensemble des objectifs fixés dans le cahier des charges :
- Aucune régression sur le socle éprouvé de VoiceKit (2945/2945 assertions réussies).
- Simplification drastique de l'intégration dans des applications complexes.
- Intégration harmonieuse avec LVGL 9 sur l'écran tactile du M5Stack CoreS3.
- Cloisonnement exemplaire entre les rôles audio, conversationnels et sécuritaires.

Cette étape prépare idéalement le terrain pour la future version **v0.7.0**, qui pourra introduire l'appel dynamique de fonctions par l'IA (*Function Calling* bidirectionnel avec Gemini Live et OpenAI Realtime).

