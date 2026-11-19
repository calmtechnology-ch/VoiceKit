# Changelog — VoiceSensorStation

## [v0.1.0.1] — 2026-11-19

### Diagnostic et correctif (bascule OpenAI ↔ Gemini)
- **Suppression de la double déconnexion redondante** : `StationVoiceController` ne lance plus `requestDisconnectSession()` + attente `DISCONNECTING_SESSION` (180 ms) avant `applyVoiceProfile()` lorsque la session est `CONNECTED`. La fermeture de la session précédente et la connexion au nouveau fournisseur sont désormais déléguées directement et atomiquement à `VoxCoreVoice::applyVoiceProfile()` (VoiceKit v0.6.7.1). `DISCONNECTING_SESSION` n'est conservé que si VoiceKit est déjà en `CONNECTING` / `RECONNECTING`.
- **Correction de la condition de course dans `beginProfileApply()`** : l'état `APPLYING` est désormais armé *avant* l'appel à `m_voice.applyVoiceProfile()` afin qu'un callback synchrone (`hasSameAcousticConfig`) ou rapide sur `websocket_task` ne soit jamais manqué ni écrasé.
- **Élimination des courses inter-tâches FreeRTOS (`websocket_task` ↔ `loopTask`)** : les callbacks `onReady`, `onProfileApplied`, `onProfileError` et `onError` capturent désormais leurs événements dans des tampons protégés par `portMUX_TYPE`, puis sont traités de façon déterministe sur `loopTask` dans `StationVoiceController::update()`.
- **Vérification stricte des callbacks et rejet des événements tardifs** : `m_activeProfile` n'est jamais mis à jour de manière optimiste ; les callbacks tardifs de l'ancien fournisseur sont ignorés ; le cas `onReady()` sans `onProfileApplied()` est réconcilié uniquement si VoiceKit confirme le fournisseur cible (`!isProfileChangePending()`, `CONNECTED`, fournisseur actif concordant).
- **Récupération sur échec, timeout et STOP pendant bascule** : watchdog de sécurité aligné au-dessus des timeouts natifs VoiceKit (18 s vs 12 s Gemini / 15 s OpenAI) forçant `m_voice.stop()` si VoiceKit reste en transition ; annulation propre lors d'un `STOP` en cours de bascule ; restauration cohérente du profil précédent et reprise de `resumeAutoReconnect()`.
- **Correction bouton `PARLER` LVGL (`SensorStationUI`)** : suivi de la couleur de fond (`m_lastRenderedTalkBg`) afin que le bouton `PARLER` ne reste jamais bloqué en gris après une bascule réussie ; réinitialisation de la couleur du message de statut lors d'une nouvelle demande.
- **Instrumentation série `[SWITCH]`** : traces légères horodatées (`millis()`) sans jamais exposer aucune clé API.
- **Tests logiciels de non-régression** : ajout de `ProfileSwitchLogic.h` et de 14 assertions unitaires dans `tests/test_station_core.cpp`, ainsi que de la matrice matérielle `SW-01` à `SW-10` dans `tests/TEST_PLAN.md` (validation matérielle CoreS3 requise).

## [v0.1.0] — 2026-11-19

### Ajouté
- Application showcase complète **VoiceSensorStation v0.1.0** pour **M5Stack CoreS3** et **M5Stack ENV Pro U169 (BME688)**.
- Sous-système capteur modulaire (`SensorData`, `EnvSensor`, `SensorManager`) :
  - Bus I²C externe Port A CoreS3 (`GPIO 2` / `GPIO 1`, adresses `0x77` / `0x76`).
  - Acquisition périodique non bloquante toutes les 2 secondes (mode forcé asynchrone).
  - Validation physique des mesures, instantané cohérent horodaté, détection `STALE` (> 10 s), gestion `ERROR`/`DISCONNECTED` et reconnexion automatique contrôlée.
- Sous-système vocal (`StationVoiceController`, `SensorCommands`) sur **VoiceKit v0.6.7.1** sans modification de la bibliothèque :
  - Push-to-Talk à deux clics (`VoiceInteractionController`), validation locale VAD (`VoiceCaptureValidator`) et `STOP` prioritaire sans reconnexion forcée.
  - 7 commandes environnementales locales enregistrées dans `ToolRegistry` + `VoiceCommandDispatcher` avec réponses vocales françaises basées uniquement sur les données réelles.
  - Routage des questions analytiques vers **Google Gemini Live** ou **OpenAI Realtime** (`askAi`) avec injection structurée des mesures réelles.
- Interface tactile **LVGL 9** thème clair (`SensorStationUI`) :
  - Tableau de bord 320 × 240 avec polices françaises accentuées et mise à jour différentielle sans clignotement.
  - Écran Réglages toujours accessible hors ligne (fournisseur IA, langue, voix, volume, sauvegarde NVS, état capteur) et écran Diagnostics.
- Diagnostics série périodiques lisibles (capteur, vocal, mémoire, performances mesurées).
- Suite de tests unitaires natifs (`tests/test_station_core.cpp`) et plan de tests matériels (`tests/TEST_PLAN.md`, VSS-01 à VSS-16).
