# Changelog — VoiceSensorStation

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
