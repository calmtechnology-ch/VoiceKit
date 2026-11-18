# Applications Vitrines (Showcases) — VoiceKit

Ce répertoire regroupe les applications applicatives complètes construites sur la bibliothèque VoiceKit.

---

## Projets Vitrines

### 1. VoiceAssistant (`showcases/VoiceAssistant/`)
- **Description** : Assistant vocal conversationnel complet avec écran tactile LVGL 9 (320 × 240) pour M5Stack CoreS3. Dialogue naturel avec Google Gemini Live et OpenAI Realtime, Push-to-Talk à deux clics, validation locale de parole (VAD), bouton STOP prioritaire, sélection multilingue et profil vocal persistant en NVS.
- **Technologies** : M5Stack CoreS3, LVGL 9, VoiceKit v0.6.7.1 (`VoiceInteractionController`, `VoxCoreVoice`, `VoiceProfileManager`), WebSocket TLS, FreeRTOS.
- **Raison du classement en vitrine** : Application utilisateur complète et vitrine publique officielle de VoiceKit, intégrant l'interface graphique tactile, la gestion des préférences et le cycle complet d'interaction vocale sans modification de la bibliothèque.

### 2. OrientationClockVoice (`showcases/OrientationClockVoice/`)
- **Description** : Horloge connectée avec détection gyroscopique de l'orientation (paysage, portrait, paysage inversé) et annonce vocale de l'heure en français naturel.
- **Technologies** : M5Stack CoreS3, capteur IMU 6 axes, synchronisation NTP Wi-Fi, LVGL 9, VoiceKit TTS.
- **Raison du classement en vitrine** : Il s'agit d'une application utilisateur à part entière combinant plusieurs sous-systèmes matériels et graphiques, plutôt que d'un exemple d'apprentissage ciblé de l'API VoiceKit.


