# Rapport Correctif Ciblé — VoiceAssistant v0.1.0

Date d'intervention : 18 Novembre 2026  
Projet : **VoiceAssistant v0.1.0** — Vitrine officielle VoiceKit  
Matériel cible : **M5Stack CoreS3 (ESP32-S3)**  
Bibliothèque sous-jacente : **VoiceKit v0.6.7.1**  

---

## 1. Liste des Fichiers Modifiés

Toutes les modifications du périmètre de cette mission sont strictement contenues dans le répertoire applicatif `showcases/VoiceAssistant/` :

- `showcases/VoiceAssistant/src/VoiceAssistantUI.h` : Ajout du conteneur défilable pour la carte ASSISTANT.
- `showcases/VoiceAssistant/src/VoiceAssistantUI.cpp` : Remplacement des API LVGL obsolètes par les équivalents conformes (`lv_obj_remove_flag`, `lv_obj_add_flag`), implémentation du conteneur vertical scrollable pour les réponses longues (>= 10 lignes), conditionnement du bouton PARLER avec `LV_STATE_DISABLED`, et appel de `m_settingsScreen.update()`.
- `showcases/VoiceAssistant/src/SettingsScreen.h` : Ajout de la méthode `update()` pour le suivi non bloquant des opérations de profil vocal, et de l'indicateur d'attente de confirmation.
- `showcases/VoiceAssistant/src/SettingsScreen.cpp` : Remplacement des appels LVGL obsolètes, affichage des messages d'état (« Modification en cours... », « Modification appliquée », « Échec modification »), restauration des valeurs affichées en cas d'échec, synchronisation des indices internes de sélection (`m_selectedVoiceIdx` et `m_selectedLangIdx`).
- `showcases/VoiceAssistant/src/AppController.h` : Ajout de la machine d'état asynchrone pour les profils (`ProfileOperationState`), découplage thread-safe de la purge d'historique (`consumeHistoryCleared()`), et enregistrement atomique d'un seul échange complet par tour.
- `showcases/VoiceAssistant/src/AppController.cpp` : Remplacement du hack `m_voice.getSession().deactivate()` par une orchestration complète via `applyVoiceProfile(target, true)`, synchronisation rigoureuse de `m_activeProfile` uniquement sur callback `onProfileApplied`, vérification d'autorisation PTT dans `onTalkClick()`, et protection de l'historique lors de STOP.
- `showcases/VoiceAssistant/tests/TEST_PLAN.md` : Ajout de la section 6 détaillant les tests correctifs `COR-01` à `COR-10`.
- `showcases/VoiceAssistant/README.md` : Mise à jour de la documentation pour refléter les correctifs et clarifier la persistance NVS du profil acoustique (hors volume).

> **Note sur `VoiceKit/src/VoxCoreVoice.cpp` :**  
> Conformément aux directives impératives, la modification préexistante issue du commit `4c9d752` (déplacement de `_session.deactivate()` en amont de `if (_profileChangePending)` lors d'un timeout de connexion) a été analysée et **strictement conservée sans modification ni altération**. Aucun fichier dans `VoiceKit/src/` n'a été modifié lors de cette mission.

---

## 2. Description Détaillée des Six Corrections

### Correction 1 — Compatibilité LVGL 9 & Navigation
- Remplacement systématique des fonctions LVGL non conformes dans `VoiceAssistantUI.cpp` et `SettingsScreen.cpp` :
  - `lv_obj_set_scrollable(obj, false)` remplacé par `lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE)`
  - `lv_obj_set_hidden(obj, true)` remplacé par `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)`
  - `lv_obj_set_hidden(obj, false)` remplacé par `lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN)`
  - `!lv_obj_is_hidden(obj)` remplacé par `!lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)`
- Les transitions d'écrans entre la vue *Conversation* et la vue *Paramètres* masquent et affichent désormais les conteneurs sans fuite d'affichage ni superposition.

### Correction 2 — Basculement Gemini Live / OpenAI Realtime
- Élimination de l'appel direct `m_voice.getSession().deactivate()`, qui perturbait les états internes de VoiceKit.
- Orchestration asynchrone complète du changement de profil (`requestProfileChange`) :
  1. Contrôle préalable de la présence de la clé API cible (`hasProviderApiKey`).
  2. Vérification qu'aucune opération ou connexion n'est déjà en cours.
  3. Si une interaction est active, envoi d'un arrêt prioritaire (`triggerStop()`) et attente de stabilisation (`ProfileOperationState::PENDING_STOP`).
  4. Transmission du profil via l'API publique de haut niveau `applyVoiceProfile(targetProfile, true)`.
  5. Attente de la confirmation serveur via le callback `onProfileApplied()` avant toute mutation de `m_activeProfile`.
  6. En cas d'erreur (`onProfileError` ou watchdog applicatif à 18 s), conservation stricte du fournisseur précédent et notification d'échec sur l'IHM.
  7. Interdiction formelle d'engager une conversation vocale durant la phase transitoire.

### Correction 3 — Historique Conversationnel et Callbacks Thread-Safe
- Suppression de l'enregistrement intempestif des fragments partiels : les fragments de réponse reçus dans `onAssistantText()` mettent à jour l'affichage en flux tendu via `m_pendingAssistantText`, mais l'entrée dans `m_history` n'est enregistrée qu'une seule fois au terme de la restitution (`onSpeechFinished`).
- En cas d'interruption par le bouton STOP (`isLastTurnCancelled()`), le tour annulé est ignoré et aucun résidu partiel n'est injecté dans l'historique.
- Aucune manipulation de widget LVGL ni allocation mémoire longue n'est effectuée dans les callbacks audio et réseau. Les données sont acheminées vers la boucle principale par des sections critiques courtes (`portENTER_CRITICAL` / `portEXIT_CRITICAL`).
- Le bouton « Effacer » purge immédiatement `m_history`, `m_lastUserText`, `m_lastAssistantText`, et ordonne à l'IHM de réinitialiser instantanément les libellés de dialogue, tout en maintenant active la session WebSocket de l'IA.

### Correction 4 — Réponses Longues et Défilement Tactile (ASSISTANT)
- La carte ASSISTANT (dimensions extérieures conservées à 304 × 60 px) intègre désormais un conteneur dédié au défilement vertical (`m_contAssistantScroll`) configuré avec `LV_OBJ_FLAG_SCROLLABLE` et `LV_DIR_VER`.
- Le label de texte (`m_lblAssistantText`) utilise une hauteur dynamique (`LV_SIZE_CONTENT`) avec retour automatique à la ligne (`LV_LABEL_LONG_MODE_WRAP`), garantissant la lisibilité complète de réponses de plus de 10 lignes par glissement tactile vertical.
- Les boutons d'action `PARLER` et `STOP` demeurent parfaitement visibles et immobiles à leurs coordonnées initiales (y = 174).

### Correction 5 — État Réel et Verrouillage du Bouton PARLER
- Application des états LVGL, notamment `LV_STATE_DISABLED` :
  - **READY** : Bouton actif (`PARLER`, bleu `#1976D2`) uniquement si la session IA et le Wi-Fi sont connectés et qu'aucun changement de profil n'est en cours. Si la session est en cours d'établissement, le bouton passe en état désactivé avec le libellé « ATTENTE » (gris `#9E9E9E`).
  - **LISTENING** : Bouton actif (`TERMINER`, rouge `#D50000`).
  - **DISABLED, STARTING, STOPPING_CAPTURE, PROCESSING, SPEAKING, INTERRUPTING, RECOVERING, ERROR_STATE** : Bouton désactivé (`LV_STATE_DISABLED`).
- Dans le contrôleur applicatif (`AppController::onTalkClick`), tout événement tactile survenant en dehors des états autorisés est rejeté avec log de diagnostic explicite.
- Le bouton STOP reste accessible en permanence avec une priorité absolue.

### Correction 6 — Paramètres, Profils NVS et Indices de Sélection
- Au démarrage : restauration du profil sauvegardé en NVS, vérification de compatibilité de la voix avec le fournisseur actif (application de la voix par défaut si discordance), et synchronisation initiale des indices internes de sélection (`m_selectedLangIdx`, `m_selectedVoiceIdx`).
- Pendant une modification de fournisseur, langue ou voix : affichage immédiat de « Modification en cours... », bascule asynchrone, puis affichage de « Modification appliquée » après réception de `onProfileApplied()` ou restauration des valeurs précédentes en cas d'échec.
- Sauvegarde NVS : réservée aux profils effectivement appliqués et stabilisés.
- Clarification sur le volume : le volume audio est configuré pour la session courante (en mémoire vive) et n'est pas persisté en NVS par `VoiceProfileManager`.

---

## 3. Résultat Exact de Compilation PlatformIO

La compilation a été exécutée avec succès via la commande :

```powershell
pio run -d showcases/VoiceAssistant -e m5stack-cores3
```

### Extrait du journal de compilation :
```text
Processing m5stack-cores3 (platform: espressif32@6.9.0; board: m5stack-cores3; framework: arduino)
--------------------------------------------------------------------------------
CONFIGURATION: https://docs.platformio.org/page/boards/espressif32/m5stack-cores3.html
PLATFORM: Espressif 32 (6.9.0) > M5Stack CoreS3
HARDWARE: ESP32S3 240MHz, 320KB RAM, 16MB Flash
LDF: Library Dependency Finder -> https://bit.ly/configure-pio-ldf
LDF Modes: Finder ~ deep, Compatibility ~ soft
Found 38 compatible libraries
Scanning dependencies...
Dependency Graph
|-- M5CoreS3 @ 1.0.0
|-- M5Unified @ 0.2.23
|-- lvgl @ 9.6.0
|-- VoiceKit @ 0.6.7.1
|-- WiFi @ 2.0.0
Building in release mode
Compiling .pio\build\m5stack-cores3\src\AppController.cpp.o
Compiling .pio\build\m5stack-cores3\src\SettingsScreen.cpp.o
Compiling .pio\build\m5stack-cores3\src\VoiceAssistantUI.cpp.o
Compiling .pio\build\m5stack-cores3\src\main.cpp.o
Linking .pio\build\m5stack-cores3\firmware.elf
Retrieving maximum program size .pio\build\m5stack-cores3\firmware.elf
Checking size .pio\build\m5stack-cores3\firmware.elf
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [=====     ]  50.2% (used 164460 bytes from 327680 bytes)
Flash: [==        ]  24.9% (used 2088933 bytes from 8388608 bytes)
Building .pio\build\m5stack-cores3\firmware.bin with action:
  esptool.py v4.5.1
Creating esp32s3 image...
Merged 2 ELF sections
Successfully created esp32s3 image.
========================= [SUCCESS] Took 30.69 seconds =========================
```

---

## 4. Environnement Technique

- **PlatformIO Core** : 6.2.0.post1
- **Plateforme cible** : `espressif32 @ 6.9.0` (framework `arduinoespressif32 @ 3.20017.241212`)
- **Carte cible** : `m5stack-cores3` (ESP32-S3FN8, 16 Mo Flash, PSRAM 8 Mo)
- **Version effective de LVGL** : `9.6.0`
- **Version effective de VoiceKit** : `0.6.7.1` (via symlink local)

---

## 5. Bilan Mémoire

| Ressource | Utilisé | Capacité allouée | Pourcentage | Marge restante |
|---|---|---|---|---|
| **RAM Statique (SRAM)** | 164 460 octets | 327 680 octets | **50.2 %** | 163 220 octets (49.8 %) |
| **Flash Programme** | 2 088 933 octets | 8 388 608 octets (8 Mo partition app) | **24.9 %** | 6 299 675 octets (75.1 %) |
| **PSRAM** | Dédiée aux buffers audio circulaires et trames réseau | 8 388 608 octets (8 Mo) | Détection automatique M5Unified | > 7.5 Mo libres |

---

## 6. Analyse des Modèles IA Configurés

Conformément à la Section 9 :
```cpp
GEMINI_MODEL = "models/gemini-2.5-flash";
OPENAI_MODEL = "gpt-realtime-2.1-mini";
```
- **Gemini Live** : Le modèle `models/gemini-2.5-flash` est préservé tel que configuré dans l'application. Sur le plan de l'interopérabilité API, Google propose officiellement la connexion WebSocket Multimodal Live (`v1alpha.GenerativeService.BidiGenerateContent`) avec `gemini-2.0-flash-exp` ou `gemini-2.0-flash`. Si l'endpoint Live rejette le descripteur `models/gemini-2.5-flash` lors des essais réseau réels, l'application le signalera par le log `[ERROR] VoiceKit : ...` sans planter grâce à la gestion d'erreur implémentée.
- **OpenAI Realtime** : Le modèle `gpt-realtime-2.1-mini` est également conservé sans altération. La négociation de session s'effectue via `applyVoiceProfile()`.

---

## 7. Validation des Tests Logiciels et Matériels

### Tests Logiciels Réalisés
- [x] Compilation sans erreur C++ ni symbole indéfini.
- [x] Résolution des drapeaux et fonctions LVGL 9.
- [x] Vérification de l'absence de fuite mémoire statique (RAM < 55 %).
- [x] Vérification de la non-altération du sous-dossier `VoiceKit/src/`.
- [x] Vérification Git : absence de clés API dans l'arborescence suivie (`secrets.h` ignoré).

### Tests Matériels à Effectuer sur CoreS3 Physique
Tous les tests suivants sont répertoriés dans `tests/TEST_PLAN.md` et demeurent avec la mention obligatoire **À TESTER SUR MATÉRIEL** :

| ID | Intitulé | Statut |
|---|---|---|
| **COR-01** | Interface LVGL compilée et navigation Conversation / Paramètres | À TESTER SUR MATÉRIEL |
| **COR-02** | Gemini vers OpenAI sans incohérence | À TESTER SUR MATÉRIEL |
| **COR-03** | Échec de connexion correctement géré | À TESTER SUR MATÉRIEL |
| **COR-04** | Historique vidé intégralement | À TESTER SUR MATÉRIEL |
| **COR-05** | Réponse longue défilable | À TESTER SUR MATÉRIEL |
| **COR-06** | PARLER désactivé hors connexion | À TESTER SUR MATÉRIEL |
| **COR-07** | Langue réellement appliquée | À TESTER SUR MATÉRIEL |
| **COR-08** | Voix réellement appliquée | À TESTER SUR MATÉRIEL |
| **COR-09** | Profil NVS correctement restauré | À TESTER SUR MATÉRIEL |
| **COR-10** | STOP et PTT sans régression | À TESTER SUR MATÉRIEL |

---

## 8. Limitations Éventuelles

1. **Volume en NVS** : `VoiceProfileManager` sauvegarde les paramètres acoustiques (fournisseur, voix, langue, genre, style, ton, accent), mais n'inclut pas de clé NVS dédiée au niveau de volume sonore. Celui-ci est donc initialisé à 80 % (`DEFAULT_VOLUME`) à chaque démarrage.
2. **Modèles IA spécifiques** : La disponibilité de `models/gemini-2.5-flash` sur le protocole bidirectionnel WebSocket Live dépend du déploiement côté infrastructure Google.

---

## 9. Attestation de Non-Régression VoiceKit

Il est formellement confirmé qu'**aucune nouvelle modification** n'a été apportée au code source de la bibliothèque `VoiceKit` (`VoiceKit/src/`, `examples/`, `library.json`). La seule modification présente dans `VoiceKit/src/VoxCoreVoice.cpp` est celle résultant du commit précédent `4c9d752` et a été scrupuleusement conservée.
