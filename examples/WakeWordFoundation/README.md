# Exemple Autonome : WakeWordFoundation (VoiceKit v0.6.2)

Cet exemple autonome valide la fondation matérielle et logicielle de détection locale de mot de réveil (**Wake Word Foundation**) sur **M5Stack CoreS3**, **sans écran LVGL** et **sans connexion réseau Wi-Fi / Gemini / OpenAI**.

## Objectifs validés par cet exemple

1. **Propriété unique du microphone CoreS3 ES7210** :
   - Acquisition continue non bloquante en **PCM 16 000 Hz mono 16 bits signé** via `LiveAudioStreamer` en mode capture locale (`startLocalCapture()`).
   - Aucune seconde initialisation concurrente du codec ES7210.
2. **Pont audio temps réel (`WakeWordAudioBridge`)** :
   - File circulaire SPSC bornée (8 trames de 512 échantillons = 256 ms).
   - Une seule copie mémoire (`memcpy` DMA $\to$ slot) dans la tâche audio, zéro copie au dépilage dans `voice.update()`.
3. **Machine à états complète (`WakeWordManager`)** :
   - `DISABLED` $\to$ `INITIALIZING` $\to$ `ARMED` $\to$ `DETECTED` $\to$ `SUSPENDED` $\to$ `ARMED`.
4. **Moteur officiel Espressif ESP-SR WakeNet (`IWakeWordEngine` / `WakeNetEngine`)** :
   - Active par défaut sur ESP32-S3 le modèle acoustique français officiel Espressif **`wn9l_fr_bonjouresp_tts3`** (« **Bonjour ESP** ») avec accélération SIMD Xtensa TIE728 et extraction MFCC/Filterbank matérielle.
   - Le modèle binaire (`srmodels_bonjouresp.c`, ~292 Ko) est directement embarqué en Flash (`.rodata`), sans nécessiter de téléversement séparé d'une partition `srmodels.bin` (tout en prenant automatiquement en charge une partition Flash `model` si elle est présente).
   - Un VU-mètre RMS temps réel s'affiche sur l'écran du CoreS3 pour visualiser le niveau microphone pendant la prononciation de **« Bonjour ESP »**, avec reprise automatique (`SUSPENDED` $\to$ `ARMED`) après 2,5 secondes.

## Sortie Moniteur Série (115200 bauds)

```text
[WAKE] VoiceKit v0.6.2
[WAKE] CoreS3 ES7210 ready
[WAKE] PCM 16000 Hz / 16-bit
[WAKE] WAKENET status: UNAVAILABLE in Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7 (wn10_fr_bonjouresp requires ESP-IDF >= 5.0)
[WAKE] Engine: TEST (SIMULATED - validation capture PCM 16 kHz, pont audio et machine a etats)
[WAKE] State: INITIALIZING
[WAKE] State: ARMED
[WAKE] Detection: Bonjour ESP
[WAKE] Notice: Detection SIMULEE (moteur TEST - aucune reconnaissance vocale acoustique)
[WAKE] State: SUSPENDED
[WAKE] State: ARMED
```

## Commandes Série Interactives

| Commande | Action |
|---|---|
| `bonjour esp` ou `wake` | Déclenche une détection de test simulée (uniquement si moteur `TEST` et état `ARMED`) |
| `r` ou `resume` | Simule la fin de conversation et réarme le détecteur (`SUSPENDED` $\to$ `ARMED`) |
| `s` ou `suspend` | Suspend manuellement le détecteur (`ARMED` $\to$ `SUSPENDED`) |
| `w` ou `wakenet` | Sélectionne le moteur `WAKENET` (démontre le passage explicite en état `ERROR` sous IDF 4.4) |
| `t` ou `test` | Sélectionne le moteur `TEST` et arme la capture locale (`ARMED`) |
| `m` ou `metrics` | Affiche les compteurs mémoire (RAM interne, PSRAM), temps CPU max et statistiques de file |

## Compilation PlatformIO

```bash
pio run -d examples/WakeWordFoundation
```

