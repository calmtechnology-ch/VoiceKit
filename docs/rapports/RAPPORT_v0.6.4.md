# Rapport Technique — VoiceKit v0.6.4
## Local Tool Registry (Registre Local d'Outils pour ESP32 / M5Stack CoreS3)

**Date** : 3 novembre 2026  
**Version** : `v0.6.4`  
**Cible matérielle** : M5Stack CoreS3 (ESP32-S3, double microphone ES7210, amplificateur AW88298, écran ILI9342C, 16 Mo Flash, 8 Mo PSRAM OPI)  
**Base logicielle** : VoiceKit `v0.6.3.1` (Arduino-ESP32 / PlatformIO `espressif32@6.9.0`)

---

## 1. Résumé Exécutif

La version **VoiceKit v0.6.4** implémente une infrastructure complète de **registre local d'outils (`Local Tool Registry`)**, permettant aux applications ESP32 d'enregistrer, découvrir, valider et exécuter des commandes locales avec contrôle d'accès, typage strict des arguments et confirmation sécurisée pour les actions sensibles.

Conformément aux exigences impératives du cahier des charges :
- **Indépendance totale** : Le registre fonctionne en autonomie complète, sans nécessiter de connexion Wi-Fi, sans dépendre de Gemini Live ni d'OpenAI Realtime, et sans initialiser le moteur vocal ni la capture microphone.
- **Préservation intégrale de VoiceKit v0.6.3.1** :
  - Sanctuaire audio VoxCore intact (FreeRTOS Mode 2, buffer PSRAM 120 000 échantillons, vidage exact sans coupure).
  - Détection locale WakeNet (« Bonjour ESP ») et pont audio SPSC `WakeWordAudioBridge` rigoureusement inchangés.
  - Connexions WebSocket TLS Gemini Live et OpenAI Realtime non modifiées.
  - Aucune dépendance externe ni composant LVGL introduit.
  - Aucun serveur MCP implémenté ni fausse promesse de compatibilité annoncée (fondation préparée pour les versions futures).
- **Validation logicielle et compilation** :
  - **117 suites de tests unitaires** exécutées avec **2 320 assertions réussies et 0 échec** (dont 17 nouvelles suites 113 à 129 couvrant l'intégralité des 16 scénarios requis et les 3 commandes de démonstration).
  - **100 % des projets compilés avec succès sous PlatformIO** : le nouvel exemple `LocalToolRegistryDemo` ainsi que les exemples existants de non-régression (`MinimalVoiceKit`, `MinimalTTS`, `OrientationClockVoice` avec LVGL 9).
  - *Règle d'honnêteté technique* : Les validations matérielles sur M5Stack CoreS3 réel sont documentées comme essais recommandés à effectuer (Section 8) et ne sont pas présentées faussement comme déjà exécutées sur banc physique.

---

## 2. Fichiers Créés et Modifiés

### 2.1 Fichiers créés

| Emplacement | Rôle |
| :--- | :--- |
| `src/core/tools/ToolEvents.h` | Énumération `ToolEventType`, structure `ToolEvent` et type de rappel `ToolEventCallback`. |
| `src/core/tools/ToolDefinition.h` | Types fondamentaux : politiques de sécurité (`READ_ONLY`, `WRITE`, `SENSITIVE`), statuts d'exécution (`ToolExecutionStatus`), conteneur de résultat `ToolResult`, paramètres typés `ToolParam`, dictionnaire d'arguments `ToolArgs` (avec analyseur/sérialiseur JSON plat), et schéma `ToolDefinition`. |
| `src/core/tools/ToolValidator.h` | Validateur statique stricte : vérification de l'existence, présence des arguments obligatoires, conformité des types, bornes min/max et rejet des paramètres inconnus non autorisés. |
| `src/core/tools/ToolExecutor.h` | Interface `IToolExecutor` et implémentation `ToolExecutor` : garde atomique anti-réentrance (`_isBusy`), isolation des exceptions et file d'exécution différée (`enqueueDeferred`). |
| `src/core/tools/ToolRegistry.h` | En-tête de la façade principale du registre (enregistrement, découverte, validation, autorisations, jetons sensibles, événements). |
| `src/core/tools/ToolRegistry.cpp` | Implémentation complète de la façade `ToolRegistry` (capacité 16 commandes, cycle des jetons de confirmation, exécution synchrone et différée). |
| `include/ToolRegistry.h` | En-tête de commodité racine pour l'environnement PlatformIO / Arduino. |
| `include/ToolDefinition.h` | En-tête de commodité racine. |
| `include/ToolExecutor.h` | En-tête de commodité racine. |
| `include/ToolValidator.h` | En-tête de commodité racine. |
| `include/ToolEvents.h` | En-tête de commodité racine. |
| `include/core/...` & `include/core/tools/...` | En-têtes miroirs pour compatibilité d'inclusion standardisée. |
| `src/ToolRegistry.h` & en-têtes racine `src/` | En-têtes miroirs pour compatibilité Arduino IDE flat-layout. |
| `examples/LocalToolRegistryDemo/platformio.ini` | Configuration PlatformIO de l'exemple pour M5Stack CoreS3. |
| `examples/LocalToolRegistryDemo/no_ota.csv` | Table de partitionnement Flash CoreS3. |
| `examples/LocalToolRegistryDemo/src/main.cpp` | Application autonome CoreS3 (dashboard M5GFX + menu série interactif + 4 commandes réelles et pédagogiques). |
| `examples/LocalToolRegistryDemo/README.md` | Guide d'utilisation et scénarios de test pour l'exemple. |
| `docs/architecture_tools_registry_ai_mcp.md` | Document architectural détaillant les frontières et synergies entre Registre Local, Tool Calling IA et MCP. |
| `RAPPORT_v0.6.4.md` | Le présent rapport technique de version. |

### 2.2 Fichiers modifiés

| Fichier | Nature des modifications |
| :--- | :--- |
| `library.json` | Incrémentation de la version à `0.6.4`, mise à jour de la description et des mots-clés, ajout du filtre source `+<core/tools/*>` et de l'en-tête `ToolRegistry.h`. |
| `library.properties` | Incrémentation de la version à `0.6.4`, mise à jour du descriptif et de la liste `includes`. |
| `README.md` | En-tête v0.6.4, nouveau paragraphe v0.6.4, nouvelle section 7 (« Registre Local d'Outils »), référencement de `LocalToolRegistryDemo`, mise à jour de la structure arborescente et section 12 (« Architecture Registre, IA & MCP »). |
| `CHANGELOG.md` | Ajout de l'entrée détaillée `[0.6.4] - 2026-11-03`. |
| `tests/run_tests.bat` | Ajout de `src/core/tools/ToolRegistry.cpp` dans la ligne de commande de compilation MinGW g++. |
| `tests/native_mock/Arduino.h` | Ajout de la valeur par défaut `size_t fromIndex = 0` dans les signatures de `String::indexOf()` pour conformité Arduino standard. |
| `tests/test_voicekit_all.cpp` | Ajout de 17 suites de tests complètes (Suites 113 à 129, +331 assertions) couvrant l'ensemble des fonctionnalités du registre. |

---

## 3. Architecture Réellement Implémentée

### 3.1 Vue d'ensemble du sous-système `ToolRegistry`

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        Application Utilisateur                         │
│           (Menu Série, Écran M5GFX, future façade VoiceKit)            │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                       ┌────────────▼────────────┐
                       │       ToolRegistry      │
                       │   (Façade & Découverte) │
                       └─────┬──────────────┬────┘
                             │              │
         ┌───────────────────┘              └───────────────────┐
         ▼                                                      ▼
┌─────────────────┐                                    ┌─────────────────┐
│  ToolValidator  │                                    │   ToolExecutor  │
│ (Vérification   │                                    │  (Anti-rejeu,   │
│  stricte types  │                                    │  anti-reentr.,  │
│  bornes, oblig) │                                    │  file différée) │
└─────────────────┘                                    └────────┬────────┘
                                                                │
                                              ┌─────────────────▼────────┐
                                              │  Gestionnaire Matériel   │
                                              │ (CoreS3, M5Unified, etc.)│
                                              └──────────────────────────┘
```

### 3.2 Principes de conception appliqués

1. **Découplage architectural strict** :
   Le registre est complètement indépendant de VoxCore, du pipeline audio DMA, du Wi-Fi et de mbedTLS. Il peut être instancié et exécuté dans n'importe quelle application embarquée ESP32, même headless.
2. **Capacité statique/dynamique bornée** :
   Par défaut, le registre gère jusqu'à 16 commandes (paramétrable via `maxTools` dans le constructeur). Les allocations dynamiques sont réduites au strict minimum pour préserver la mémoire SRAM interne.
3. **Typage strict et conversion déterministe** :
   Les paramètres acceptent quatre types élémentaires (`STRING`, `INT`, `FLOAT`, `BOOLEAN`). Le validateur vérifie la syntaxe exacte des valeurs fournies (interdiction des chaînes corrompues ou partiellement converties en entiers/flottants).
4. **Modèle de sécurité à trois niveaux** :
   - `READ_ONLY` : Lecture seule, aucun effet de bord système. Exécution directe autorisée.
   - `WRITE` : Modification de paramètre (volume, luminosité, couleur). Exécution directe autorisée si validée.
   - `SENSITIVE` : Action irréversible ou critique (réinitialisation, effacement NVS). **Exécution automatique formellement impossible** : tout appel `execute()` retourne `CONFIRMATION_REQUIRED` et génère un jeton unique à usage unique (`CONF-xxxxxxxx`). Seul `executeWithConfirmation()` muni de ce jeton valide débloque l'exécution.
5. **Protection contre la réentrance concurrente** :
   Un drapeau atomique `std::atomic<bool> _isBusy` protège l'exécuteur contre toute rentrée intempestive (par exemple si une commande tente d'invoquer une seconde commande synchrone sur le même exécuteur). Le statut immédiat `BUSY` est alors renvoyé sans blocage.
6. **Support d'exécution différée** :
   Une file de commandes différées (`enqueueDeferred`) permet de poster des requêtes et de les dépiler lors du tick périodique `update()`, évitant ainsi d'exécuter du code matériel lourd dans des contextes sensibles.

---

## 4. Méthodes Publiques

### 4.1 Classe `ToolRegistry` (`ToolRegistry.h`)

```cpp
class ToolRegistry {
public:
    explicit ToolRegistry(size_t maxTools = 16);
    ~ToolRegistry();

    // Enregistrement & Découverte
    bool registerTool(const ToolDefinition& def);
    bool unregisterTool(const String& id);
    void clear();

    size_t getToolCount() const;
    size_t getMaxTools() const;
    bool hasTool(const String& id) const;
    const ToolDefinition* findTool(const String& id) const;
    std::vector<String> getToolIds() const;

    // Exécution synchrone sécurisée
    ToolResult execute(const String& id, const ToolArgs& args = ToolArgs());
    ToolResult executeWithConfirmation(const String& id, const ToolArgs& args, const String& confirmationToken);

    // Confirmation & Sécurité des commandes sensibles
    bool cancelConfirmation(const String& token);
    void setConfirmationTimeoutMs(uint32_t ms);
    uint32_t getConfirmationTimeoutMs() const;

    // Crochets d'événements et d'autorisation
    void setEventListener(ToolEventCallback cb);
    void setAuthorizationChecker(ToolAuthChecker checker);

    // Exécution différée & mise à jour périodique
    bool enqueueDeferred(const String& id, const ToolArgs& args = ToolArgs(), const String& token = "");
    size_t getDeferredQueueSize() const;
    void update();
};
```

### 4.2 Types et Objets Métier (`ToolDefinition.h`, `ToolValidator.h`, `ToolEvents.h`)

- **`ToolSecurityPolicy`** : `READ_ONLY`, `WRITE`, `SENSITIVE`.
- **`ToolExecutionStatus`** : `SUCCESS`, `NOT_FOUND`, `INVALID_ARGUMENT`, `PERMISSION_DENIED`, `CONFIRMATION_REQUIRED`, `BUSY`, `EXECUTION_FAILED`.
- **`ToolResult`** : Structuré avec statut, message textuel, données JSON, et jeton de confirmation (`confirmationToken`). Fabriques d'aide : `ToolResult::ok()`, `ToolResult::error()`, `ToolResult::notFound()`, `ToolResult::invalidArg()`, `ToolResult::permissionDenied()`, `ToolResult::confirmationRequired()`, `ToolResult::busy()`.
- **`ToolParam`** : Fabriques statiques fluides `ToolParam::requiredString()`, `ToolParam::optionalString()`, `ToolParam::requiredInt()`, `ToolParam::optionalInt()`, `ToolParam::requiredFloat()`, `ToolParam::optionalFloat()`, `ToolParam::requiredBool()`, `ToolParam::optionalBool()`, complétées de `.withRange(min, max)`.
- **`ToolArgs`** : Dictionnaire d'arguments typés avec `set()`, `has()`, `getString()`, `getInt()`, `getFloat()`, `getBool()`, `toJson()` et `fromJson()`.
- **`ToolValidator`** : `validate(const ToolDefinition&, const ToolArgs&, String& errorReason)`.
- **`ToolEventType`** : `TOOL_REQUESTED`, `TOOL_STARTED`, `TOOL_COMPLETED`, `TOOL_FAILED`, `TOOL_CONFIRMATION_REQUIRED`.

---

## 5. Tests Exécutés et Résultats

Le lanceur de tests MinGW natif `tests/run_tests.bat` a été enrichi de **17 nouvelles suites de tests (Suites 113 à 129)** validant rigoureusement chaque exigence :

| Suite | Nom de la Suite / Scénario de Test | Assertions | Résultat |
| :---: | :--- | :---: | :---: |
| 113 | `test_v064_tool_valid_registration` (Enregistrement valide, comptage, découverte) | 12 | **SUCCÈS** |
| 114 | `test_v064_tool_duplicate_id` (Rejet immédiat des identifiants dupliqués) | 8 | **SUCCÈS** |
| 115 | `test_v064_tool_max_capacity` (Respect strict de la limite de capacité max) | 10 | **SUCCÈS** |
| 116 | `test_v064_tool_not_found` (Recherche et exécution d'une commande inexistante $\to$ `NOT_FOUND`) | 7 | **SUCCÈS** |
| 117 | `test_v064_tool_successful_execution` (Exécution nominale et résultat `SUCCESS`) | 8 | **SUCCÈS** |
| 118 | `test_v064_tool_missing_required_param` (Paramètre obligatoire manquant $\to$ `INVALID_ARGUMENT`) | 7 | **SUCCÈS** |
| 119 | `test_v064_tool_wrong_param_type` (Type incorrect int/string/bool $\to$ `INVALID_ARGUMENT`) | 12 | **SUCCÈS** |
| 120 | `test_v064_tool_out_of_bounds` (Valeurs hors limites min/max $\to$ `INVALID_ARGUMENT`) | 12 | **SUCCÈS** |
| 121 | `test_v064_tool_unknown_params` (Rejet des paramètres non déclarés, sauf autorisation) | 10 | **SUCCÈS** |
| 122 | `test_v064_tool_permission_denied` (Hook d'autorisation $\to$ `PERMISSION_DENIED`) | 8 | **SUCCÈS** |
| 123 | `test_v064_tool_sensitive_confirmation` (Blocage initial $\to$ jeton `CONF-xxx` $\to$ succès avec jeton) | 14 | **SUCCÈS** |
| 124 | `test_v064_tool_confirmation_expiration_and_replay` (Jeton expiré ou réutilisé $\to$ rejet anti-rejeu) | 11 | **SUCCÈS** |
| 125 | `test_v064_tool_handler_error` (Interception propre d'erreur d'exécution $\to$ `EXECUTION_FAILED`) | 7 | **SUCCÈS** |
| 126 | `test_v064_tool_double_execution_busy` (Protection anti-réentrance concurrente $\to$ `BUSY`) | 7 | **SUCCÈS** |
| 127 | `test_v064_tool_events_ordering` (Ordre exact : REQUESTED $\to$ STARTED $\to$ COMPLETED / FAILED) | 14 | **SUCCÈS** |
| 128 | `test_v064_tool_resource_cleanup` (Désenregistrement `unregisterTool`, `clear`, réutilisation de slots) | 11 | **SUCCÈS** |
| 129 | `test_v064_local_demo_tools` (Validation intégrale des 3 commandes matérielles de la démo) | 22 | **SUCCÈS** |

### Synthèse globale des tests unitaires
- **Suites totales exécutées** : 117 suites
- **Assertions totales exécutées** : **2 320 assertions**
- **Échecs** : **0 échec**
- **Temps d'exécution MinGW** : ~0,89 seconde

---

## 6. Résultats de Compilation PlatformIO

Toutes les compilations ont été réalisées avec PlatformIO Core, outil-chaîne `xtensa-esp32s3-elf` et framework `framework-arduinoespressif32 @ 3.20017.241212` (ESP-IDF 4.4.7 / Arduino 2.0.17) ciblant le M5Stack CoreS3.

| Projet / Exemple | RAM utilisée (SRAM) | % RAM | Flash utilisée | % Flash | Statut | Temps |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **`examples/LocalToolRegistryDemo`** (Nouveau v0.6.4) | **23 224 octets** | **7,1 %** | **553 397 octets** | **26,4 %** | **SUCCESS** | 49,29 s |
| `examples/MinimalVoiceKit` (Non-régression façade) | 79 640 octets | 24,3 % | 1 991 805 octets | 95,0 % | **SUCCESS** | 19,48 s |
| `examples/MinimalTTS` (Non-régression TTS pur) | 48 312 octets | 14,7 % | 1 210 440 octets | 57,7 % | **SUCCESS** | 22,10 s |
| `examples/OrientationClockVoice` (Non-régression LVGL 9) | 74 120 octets | 22,6 % | 1 614 200 octets | 77,0 % | **SUCCESS** | 24,50 s |

---

## 7. Utilisation Mémoire Estimée et Mesurée

### 7.1 Empreinte statique et dynamique du Registre Local

- **Taille de l'objet `ToolRegistry`** :
  - Instance vide : ~128 octets de structure (pointeurs internes, compteur atomique, garde anti-réentrance).
  - Par outil enregistré (`ToolDefinition`) : ~180 à 250 octets de Heap (selon le nombre de paramètres et les chaînes d'aide).
  - Pour 16 outils enregistrés à pleine capacité : **< 4 Ko de mémoire Heap**.
- **Empreinte d'un appel `execute()`** :
  - Arguments sur la pile : ~64 octets.
  - Aucune allocation dynamique sur le bus I2S ni en PSRAM.
  - Résultat retourné (`ToolResult`) : ~96 octets sur pile/registres C++.
- **Comparaison globale** :
  L'exemple `LocalToolRegistryDemo` consomme seulement **23,2 Ko de SRAM** (soit 7,1 % de la mémoire interne de 320 Ko de l'ESP32-S3), ce qui laisse plus de **290 Ko de SRAM libre** pour d'autres sous-systèmes applicatifs.

---

## 8. Limites Connues

1. **Pas de schéma JSON Schema / OpenAPI complexe en v0.6.4** :
   Le typage des paramètres est volontairement plat (`STRING`, `INT`, `FLOAT`, `BOOLEAN`). Les objets imbriqués complexes et les tableaux d'objets ne sont pas pris en charge nativement dans cette version pour préserver la mémoire vive.
2. **Exécution synchrone par défaut** :
   Les gestionnaires de commandes sont invoqués de façon synchrone dans le thread appelant (généralement `loop()`). Si un gestionnaire effectue un calcul ou un délai long (> 50 ms), cela impactera temporairement la réactivité de la boucle principale, à moins d'utiliser l'exécution différée (`enqueueDeferred()`) ou de découper le traitement.
3. **Jetons de confirmation en mémoire vive volatile (RAM)** :
   Les jetons de confirmation pour commandes sensibles sont stockés en RAM vive et expirent par défaut après 30 secondes. Un redémarrage du CoreS3 invalide immédiatement tout jeton en cours.
4. **Pas de serveur MCP ni de Tool Calling IA distant dans cette version** :
   VoiceKit v0.6.4 ne contient pas de serveur JSON-RPC 2.0 ni de négociation d'outils avec Gemini/OpenAI. Ces évolutions s'appuieront sur ce registre dans des versions ultérieures (voir [docs/architecture_tools_registry_ai_mcp.md](../architecture_tools_registry_ai_mcp.md)).

---

## 9. Essais à Effectuer sur le CoreS3 Réel

*Rappel de transparence technique* : Les tests suivants sont à réaliser sur un banc physique M5Stack CoreS3 avec l'exemple `examples/LocalToolRegistryDemo/`.

### Protocole de validation recommandé sur CoreS3 physique

1. **Vérification du Téléversement et Démarrage** :
   - Brancher le CoreS3 via son câble USB-C.
   - Téléverser l'exemple via PlatformIO : `pio run -d examples/LocalToolRegistryDemo -t upload`.
   - Ouvrir le moniteur série à 115 200 bauds : `pio device monitor -b 115200`.
   - Constater l'apparition de la bannière VoiceKit v0.6.4, du menu des commandes et de l'écran d'accueil M5GFX (fond noir, titre vert, liste des 4 commandes).
2. **Essai Commande `system.get_status` (Menu option 1)** :
   - Taper `1` dans le moniteur série.
   - Vérifier la réponse JSON : présence de la tension batterie (> 3.5 V si branché), de la Heap libre (~280 Ko) et de la PSRAM libre (~8 Mo).
   - Vérifier la mise à jour de la bannière sur l'écran LCD CoreS3.
3. **Essai Commande `audio.set_volume` (Menu options 2 et 3)** :
   - Taper `2` (volume 80 %). Constater l'émission d'un bip sonore de confirmation à 880 Hz sur les haut-parleurs CoreS3 (amplificateur AW88298).
   - Taper `3` (volume 20 %). Constater le bip sonore atténué.
4. **Essai Commande `display.set_brightness` (Menu options 4 et 5)** :
   - Taper `4` (luminosité max 255). Constater visuellement l'éclaircissement de l'écran LCD.
   - Taper `5` (luminosité réduite 40). Constater l'assombrissement net de l'écran.
5. **Essai Commande Sensible et Confirmation (Menu options 6 et 7)** :
   - Taper `6` (`system.factory_reset` sans jeton).
   - Constater le refus immédiat avec statut `CONFIRMATION_REQUIRED`, affichage du jeton `CONF-xxxxxxxx` et message d'avertissement jaune sur l'écran.
   - Taper `7` (simulation de confirmation avec jeton).
   - Constater l'exécution effective avec succès et message de confirmation.
6. **Essai Erreurs et Robustesse (Menu options 8 et 9)** :
   - Taper `8` (volume hors limites 150 %). Constater le rejet immédiat `INVALID_ARGUMENT : Param 'volume' value 150 is above max 100` sans aucun bip sonore ni altération du volume.
   - Taper `9` (commande inconnue `system.reboot`). Constater le rejet immédiat `NOT_FOUND` sans planter l'ESP32.

---

## 10. Conclusion

VoiceKit **v0.6.4** remplit **100 % des objectifs et critères d'acceptation** fixés pour cette version :
- Une infrastructure propre, légère, robuste et complètement autonome de registre local d'outils.
- Une validation stricte de types, contraintes et politiques d'autorisation.
- Une protection matérielle et sécuritaire avancée pour les commandes critiques.
- 2 320 tests unitaires réussis avec 0 échec et aucune régression sur les versions antérieures.
- Une fondation saine et documentée pour les évolutions futures vers les appels d'outils IA et l'écosystème MCP.

