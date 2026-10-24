# Rapport de Livraison — VoiceKit v0.4.1

**Date** : 24 octobre 2026  
**Version** : `v0.4.1`  
**Dépôt** : [https://github.com/Philippe-AD/VoiceKit](https://github.com/Philippe-AD/VoiceKit)  
**Objet** : Configuration vocale avancée et chaîne de traitement français UTF-8 native.

---

## 1. Synthèse Exécutive

La version **v0.4.1** de VoiceKit fait évoluer la bibliothèque vers une prise en charge complète et personnalisable des profils vocaux de l'API Gemini Live, tout en garantissant une chaîne de transport UTF-8 irréprochable pour la langue française.

Toutes les exigences du cahier des charges ont été scrupuleusement respectées :
- **Règle absolue observée** : Aucune altération du moteur audio VoxCore stabilisé (`LiveAudioPlayer`, `LiveAudioStreamer`), des tampons circulaires en PSRAM, des quadruple-buffers DMA, des tâches FreeRTOS (Core 1 priorité 4) ni des mécanismes de vidage matériel exact de fin de phrase.
- **Configuration vocale étendue** : Support complet des paramètres `voice`, `language` (BCP-47, défaut `fr-FR`), `style`, `tone` et `accent` dans [VoiceKitConfig.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoiceKitConfig.h) et la façade [VoxCoreVoice.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoxCoreVoice.h).
- **Intégration Gemini Live sécurisée** : Détection intelligente de la compatibilité des modèles pour `speechConfig.languageCode`, injection des directives de style/ton/accent dans les consignes vocales sans altérer les instructions métier (TTS, STT, conversation), et restriction stricte des changements de profil à chaud à l'état de repos (`!isBusy()`).
- **Chaîne UTF-8 native de bout en bout** : Création du module centralisé [Utf8Utils.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/Utf8Utils.h) (validation RFC 3629, échappement JSON RFC 8259, zéro translittération ou suppression d'accents).
- **Validation obligatoire** : Validation de tous les caractères demandés (`é è ê ë à â ù û î ï ô ö ç œ æ É À Ç ’ « »`) et de la phrase obligatoire :  
  *« À Noël, Élise prépare un café crème. L'été à Genève est agréable. François préfère un cœur léger. »*
- **Nouvel exemple autonome** : Création de [examples/VoiceSettingsDemo/](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/VoiceSettingsDemo/) pour M5Stack CoreS3 (5 scénarios interactifs, écran tactile et port série).
- **Non-régression totale** : 39 suites de tests automatisés (396 tests réussis, 0 échec) et compilation réussie des **5 projets PlatformIO**.

---

## 2. Inventaire des Fichiers Modifiés et Créés

### Nouveaux Fichiers

| Fichier | Rôle |
| :--- | :--- |
| [src/core/Utf8Utils.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/Utf8Utils.h) | Fonctions inline pour la validation UTF-8 RFC 3629, la sanitisation sécurisée sans altération, l'échappement JSON conforme RFC 8259 et le comptage des points de code Unicode. |
| [src/Utf8Utils.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/Utf8Utils.h) | En-tête de redirection standard Arduino 1.5 pour `Utf8Utils.h`. |
| [include/Utf8Utils.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/include/Utf8Utils.h) | En-tête de redirection racine/include pour la rétrocompatibilité des projets consommateurs. |
| [examples/VoiceSettingsDemo/platformio.ini](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/VoiceSettingsDemo/platformio.ini) | Fichier de configuration PlatformIO pour le CoreS3 avec partitionnement `no_ota.csv` et lien `symlink://../..`. |
| [examples/VoiceSettingsDemo/src/main.cpp](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/VoiceSettingsDemo/src/main.cpp) | Démonstrateur autonome tactile M5Stack CoreS3 testant 5 profils vocaux successifs et les textes français accentués. |
| [examples/VoiceSettingsDemo/include/secrets.example.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/VoiceSettingsDemo/include/secrets.example.h) | Modèle de configuration des identifiants Wi-Fi et clé API Gemini. |
| [examples/VoiceSettingsDemo/include/secrets.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/VoiceSettingsDemo/include/secrets.h) | Fichier d'en-tête de secrets local pour la compilation. |
| [examples/VoiceSettingsDemo/README.md](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/VoiceSettingsDemo/README.md) | Guide d'utilisation du démonstrateur tactile. |

### Fichiers Modifiés

| Fichier | Modifications apportées |
| :--- | :--- |
| [src/VoiceKitConfig.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoiceKitConfig.h) | Ajout des énumérations `VoiceStyle` et `VoiceTone`, des wrappers transparents `VoiceStyleWrapper` / `VoiceToneWrapper` (acceptant indifféremment enum ou string), des champs `language`, `style`, `tone`, `accent`, et des générateurs `buildVoiceDirectives()` et `buildEffectiveInstruction()`. |
| [src/core/VoiceHubConfig.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/VoiceHubConfig.h) | Incrémentation de la constante globale `VOICEKIT_VERSION` vers `"v0.4.1"`. |
| [src/core/GeminiLiveClient.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/GeminiLiveClient.h) | Ajout des accesseurs de langue et de directives vocales, de la méthode `isModelSpeechLanguageCodeSupported()`. |
| [src/core/GeminiLiveClient.cpp](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/GeminiLiveClient.cpp) | Intégration d'`Utf8Utils::escapeJson` pour la centralisation de l'échappement JSON, gestion conditionnelle de `speechConfig.languageCode` (réservé aux modèles compatibles, exclu pour les modèles natifs bidi), concaténation des directives vocales sans modifier l'instruction système de base. |
| [src/core/DisplayTextNormalizer.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/DisplayTextNormalizer.h) | Documentation explicite de la séparation stricte : normaliseur réservé aux périphériques d'affichage 7-bit ASCII, exclu de la chaîne de transport audio. |
| [src/core/LiveSessionController.cpp](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/LiveSessionController.cpp) | Retrait de la normalisation destructive sur le log série de réception de l'assistant : préservation intégrale des caractères UTF-8 reçus. |
| [src/VoxCoreVoice.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoxCoreVoice.h) | Déclaration des méthodes publiques de profil vocal : `setVoiceProfile`, `setVoice`, `setLanguage`, `setStyle`, `setTone`, `setAccent`, et des accesseurs `getVoice`, `getLanguage`, `getStyle`, `getTone`, `getAccent`. |
| [src/VoxCoreVoice.cpp](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/VoxCoreVoice.cpp) | Implémentation des changements de profils vocaux avec vérification stricte `!isBusy()`, réinitialisation sécurisée de la session WebSocket (`deactivate()` / `activate()`), mise à jour du bandeau de démarrage vers `v0.4.1`. |
| [library.json](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/library.json) | Mise à jour de version vers `0.4.1` et description. |
| [library.properties](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/library.properties) | Mise à jour de version vers `0.4.1` et description. |
| [README.md](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/README.md) | Documentation complète de l'API de configuration vocale, du support UTF-8 et du démonstrateur `VoiceSettingsDemo`. |
| [CHANGELOG.md](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/CHANGELOG.md) | Historique exhaustif des ajouts, améliorations et corrections de la v0.4.1. |
| [tests/test_voicekit_all.cpp](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/tests/test_voicekit_all.cpp) | Ajout des suites 36, 37, 38 et 39 validant UTF-8, VoiceKitConfig, la trame de setup Gemini et la sécurité de reconnexion. |

---

## 3. Détails des Évolutions Techniques

### 3.1. Chaîne UTF-8 Native & Caractères Français

L'en-tête [Utf8Utils.h](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/src/core/Utf8Utils.h) fournit une suite d'outils ultra-légers (sans allocation mémoire superflue) :
1. **Validation RFC 3629 (`isValidUtf8`)** :  
   Rejette les octets hors plage (`0xC0`, `0xC1`, `0xF5..0xFF`), les séquences de continuation orphelines et les encodages surlongues.
2. **Échappement JSON RFC 8259 (`escapeJson`)** :  
   Échappe les caractères de contrôle (`\b`, `\t`, `\n`, `\f`, `\r`, `\"`, `\\`) et préserve les octets Unicode (> `0x7F`) sous forme d'octets bruts UTF-8 valides dans le JSON (conformément à la RFC 8259 §7).
3. **Préservation sans translittération** :  
   Aucun caractère accentué français (`é è ê ë à â ù û î ï ô ö ç œ æ É À Ç ’ « »`) n'est supprimé, converti en équivalent ASCII ou altéré lors de la transmission à Gemini ou de la réception de transcriptions.

### 3.2. Configuration Vocale & Intégration Gemini Live

- **Structure unifiée** : `VoiceKitConfig` intègre :
  - `voice` : nom de voix Gemini officiel (ex. `"Aoede"`, `"Puck"`, `"Charon"`).
  - `language` : code BCP-47 (défaut `"fr-FR"`).
  - `style` : `natural`, `professional`, `expressive`, `narrative`.
  - `tone` : `neutral`, `calm`, `warm`, `cheerful`, `serious`.
  - `accent` : chaîne libre optionnelle pour orienter l'accentuation régionale.
- **Directives vocales vs Instructions métier** :  
  Les consignes vocales générées par `buildVoiceDirectives()` (ex. `[Directives vocales] S'exprimer en langue fr-FR. Style: natural. Tonalite: neutral.`) sont injectées dans la trame de setup WebSocket (`sendSetupMessage()`) à la suite de l'instruction système sans altérer l'instruction de base (`_systemInstruction`), garantissant la stricte préservation des comportements TTS, STT et conversationnels.
- **Compatibilité des modèles Gemini** :  
  L'API Gemini Multimodal Live (`generativelanguage.googleapis.com`) rejette le champ `speechConfig.languageCode` pour les modèles natifs bidirectionnels (`gemini-2.0-flash-exp`, `gemini-2.5`, `live`, etc.) avec une erreur `INVALID_ARGUMENT`. La méthode `isModelSpeechLanguageCodeSupported()` détecte ces modèles et omet automatiquement le champ pour piloter la langue exclusivement via l'instruction système, tout en restant compatible avec d'éventuels modèles futurs acceptant ce paramètre.
- **Sécurité absolue de session** :  
  L'API `setVoiceProfile` et ses variantes vérifient `!isBusy()`. Si une énonciation vocale, une acquisition microphone ou un tour conversationnel est en cours, la fonction retourne immédiatement `false` sans interrompre le flux en cours. Si le moteur est au repos, la session est fermée proprement puis réactivée avec les nouveaux paramètres.

---

## 4. Résultats des Validations

### 4.1. Tests Automatisés Natifs (MinGW C++14/C++17)

Exécution du script [tests/run_tests.bat](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/tests/run_tests.bat) :
- **Nombre total de suites** : 39 suites
- **Nombre total de vérifications** : 396 assertions
- **Résultat** : **396 RÉUSSIS, 0 ÉCHEC**

```text
===========================================================
  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.4.1    
  Validation TTS + STT + Conversation + Profils Vocaux    
  Cible logique C++14 / C++17                             
===========================================================
...
[TEST SUITE 36] Chaine UTF-8 integrale : accents francais, ligatures et ponctuation
[TEST SUITE 37] Parametres VoiceKitConfig : voix, langue, style, ton et accent
[TEST SUITE 38] Trame de setup Gemini Live : speechConfig, voix et directives
[TEST SUITE 39] Changement dynamique de profil vocal et securite d'interruption
===========================================================
  BILAN FINAL DES TESTS : 396 REUSSIS, 0 ECHECS
===========================================================
```

### 4.2. Compilations PlatformIO (Cible Matérielle M5Stack CoreS3)

Les cinq exemples ont été compilés avec succès pour la carte `m5stack-cores3` (ESP32-S3) :

| Projet Exemple | Rôle | Empreinte Flash (app0) | Empreinte RAM (DRAM) | Statut PlatformIO |
| :--- | :--- | :--- | :--- | :---: |
| **`examples/MinimalTTS`** | Synthèse vocale TTS basique | 55.9% (1 832 949 B / 3.28 MB) | 20.7% (67 788 B / 327 KB) | **SUCCESS** |
| **`examples/MinimalSTT`** | Reconnaissance STT Push-to-Talk | 56.2% (1 842 549 B / 3.28 MB) | 20.7% (67 868 B / 327 KB) | **SUCCESS** |
| **`examples/OrientationClockVoice`** | Horloge LVGL 9 + RTC + IMU + TTS | 76.0% (2 491 509 B / 3.28 MB) | 48.7% (159 476 B / 327 KB) | **SUCCESS** |
| **`examples/MinimalConversation`** | Conversation bidirectionnelle (Mode B) | 56.2% (1 843 685 B / 3.28 MB) | 20.7% (67 868 B / 327 KB) | **SUCCESS** |
| **`examples/VoiceSettingsDemo`** | Démonstrateur vocal tactile v0.4.1 | 56.1% (1 840 501 B / 3.28 MB) | 20.7% (67 860 B / 327 KB) | **SUCCESS** |

---

## 5. Distinction Obligatoire : Validation Logicielle vs Essais Physiques

> [!IMPORTANT]
> **Avertissement de conformité acoustique** :  
> L'ensemble des compilations logicielles, des formats de trames WebSocket, des règles d'échappement JSON, des encodages UTF-8 et des transitions d'état ont été rigoureusement validés par tests unitaires et compilation croisée ESP-IDF/PlatformIO.  
> Cependant, **aucun essai acoustique physique sur matériel M5Stack CoreS3 réel n'a été réalisé dans cet environnement de développement logiciel**.  
> Conformément au cahier des charges : **les réglages de langue, de ton ou de style ne sont en aucun cas déclarés validés acoustiquement** sans essai réel préalable sur le haut-parleur AW88298 et le microphone ES7210 d'un boîtier M5Stack CoreS3 physique connecté aux serveurs Gemini Live.

---

## 6. Limitations Connues de l'API Gemini Live & Analyse des Risques

1. **Contrôle acoustique natif de l'API Gemini Live** :  
   - L'API Gemini Live applique le choix de la voix (`voiceName`) au niveau de son synthétiseur vocal neuronal.
   - Les paramètres de style (`style`), de tonalité (`tone`) et d'accent (`accent`) sont injectés sous forme de consignes textuelles d'orientation au sein de l'instruction système. L'interprétation exacte de ces nuances stylistiques dépend de la sensibilité du modèle LLM distant et ne fait pas l'objet d'un paramètre acoustique binaire garanti par Google.
2. **Acceptation de `speechConfig.languageCode`** :  
   - Les spécifications du protocole BidiGenerateContent de Gemini mentionnent `speechConfig.languageCode`. Cependant, à la date de publication, plusieurs endpoints de serveurs Gemini Live retournent une erreur `400 Bad Request` ou `INVALID_ARGUMENT` si ce champ est présent pour les modèles audio bidirectionnels natifs. VoiceKit v0.4.1 intègre un filtrage automatique évitant toute rupture de connexion.
3. **Affichage LVGL vs Chaîne de transport** :  
   - La chaîne audio VoiceKit transmet intégralement les chaînes UTF-8 contenant tous les glyphes accentués et ligatures.
   - Si une application hôte utilise un moteur d'affichage (ex. LVGL ou `M5.Display`) configuré avec une police ASCII 7-bit par défaut, les glyphes étendus peuvent s'afficher sous forme de rectangles ou points d'interrogation sur l'écran LCD. Cette limitation d'affichage relève de la configuration des polices de l'hôte et n'affecte en rien la prononciation ou le transport réseau.

---

## 7. Conclusion

VoiceKit `v0.4.1` apporte une solution robuste, standardisée et entièrement compatible aux besoins d'internationalisation et de personnalisation vocale sur microcontrôleur ESP32-S3, sans la moindre régression sur les versions précédentes.
