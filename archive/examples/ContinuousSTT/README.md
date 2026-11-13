# Exemple ContinuousSTT — VoiceKit v0.5.3.1

Exemple autonome de reconnaissance vocale **STT continue mains libres (Continuous Speech-to-Text)** sur **M5Stack CoreS3**, utilisant la bibliothèque **VoiceKit v0.5.3.1** connectée à l'API bidirectionnelle **Google Gemini Live**.

L'application reproduit le fonctionnement d'une **dictée vocale moderne** :
1. Écoute permanente et non intrusive dès que le moteur est prêt.
2. Détection automatique de la voix de l'utilisateur par un **VAD local léger** (Voice Activity Detector) sur les blocs PCM 16 kHz.
3. Affichage en temps réel des transcriptions partielles (intérim).
4. Détection automatique de fin de prise de parole après un silence configurable (1 200 ms).
5. Réception et affichage de la transcription finale authentique de Gemini Live, conservée à l'écran.
6. Reprise automatique et immédiate de l'écoute pour le segment suivant (mains libres).

---

## 1. Architecture et Détection de Parole (VAD)

L'exemple intègre le composant `VoiceActivityDetector` (`src/core/VoiceActivityDetector.h`) directement couplé au flux de capture audio PCM 16 kHz de VoiceKit (`LiveAudioStreamer`) sans créer de seconde capture concurrente du microphone :

- **Traitement bloc par bloc** : Analyse synchrone sur blocs PCM 16 kHz (512 échantillons mono = 32 ms par bloc).
- **Suivi adaptatif du bruit ambiant** : Plancher de bruit (`noiseFloor`) ajusté en continu en phase de silence par lissage exponentiel (EMA).
- **Hystérésis déterministe** :
  - **Seuil d'amorce de parole** : `(noiseFloor * 2.0) + 250 RMS` (nécessite 200 ms consécutifs au-dessus du seuil pour valider l'entrée en parole et rejeter les clics/bruits transitoires).
  - **Seuil de maintien** : `(noiseFloor * 1.3) + 120 RMS` (tolère les pauses légères et les syllabes douces sans coupure prématurée).
- **Détection de fin d'intervention** : Décompte du silence post-parole réinitialisé si la voix reprend ; clôture du segment à 1 200 ms de silence consécutif via `voice.stopListening()`.
- **Garde-fou temporel** : Clôture forcée à 30 secondes de parole ininterrompue.
- **Chemin non bloquant** : L'analyse VAD s'exécute en microsecondes lors de la complétion du bloc, et la commande `stopListening()` est déclenchée de façon thread-safe dans la boucle principale `loop()`.

---

## 2. États de l'Application et Interface M5Unified (320 × 240)

L'interface graphique est construite nativement avec **M5Unified** (sans LVGL requis), assurant une réactivité maximale et une consommation mémoire maîtrisée :

| État | Badge Écran | Couleur | Comportement & Rôle |
| :--- | :--- | :--- | :--- |
| **ATTENTE** | `ATTENTE` / `PAUSE` | Gris neutre | Connexion Wi-Fi/Gemini Live en cours, ou dictée suspendue par bouton tactile |
| **ÉCOUTE** | `ECOUTE` | Bleu cyan | Microphone actif en écoute passive du bruit ambiant ; attente de prise de parole |
| **PAROLE** | `PAROLE` | Vert vif | Voix active détectée (> 200 ms) ; streaming audio et affichage des textes en direct |
| **SILENCE** | `SILENCE` | Jaune / Ambre | Voix suspendue ; jauge de silence active (0 à 1 200 ms) avant validation |
| **TRANSCRIPTION** | `TRANSCRIPTION` | Magenta | `stopListening()` émis ; attente de la transcription finale authentique de Gemini |
| **TERMINÉ** | `TERMINE` | Vert émeraude | Transcription finale authentique reçue, validée et conservée ; reprise automatique |
| **ERREUR** | `ERREUR` | Rouge | Signalement d'anomalie réseau, session ou matériel audio |

### Éléments Graphiques Affichés :
- **Bandeau d'en-tête** : Titre, badge d'état coloré, compteur de segments transcrits et plancher de bruit ambiant (`RMS`).
- **VU-mètre micro instantané** : Barregraphe horizontal dynamique avec repère jaune indiquant le seuil de parole actuel.
- **Jauge de silence** : Barregraphe jaune/rouge indiquant en direct la progression du silence vers les 1 200 ms de validation.
- **Zone de transcription** :
  - **Cadre Texte Définitif** : Affiche le texte final authentique, mis en évidence et conservé à l'écran.
  - **Cadre En Direct** : Affiche les révisions partielles au fil de l'élocution.
- **Bouton tactile PAUSE / REPRENDRE** : Situé en bas de l'écran (Y=194, H=40), permet de suspendre et reprendre la reconnaissance d'un simple toucher.

---

## 3. Gestion Intégrale de l'UTF-8 Français

Conformément aux exigences de VoiceKit :
- L'intégralité des chaînes UTF-8 françaises est préservée (accents `é`, `è`, `ê`, `ë`, `à`, `â`, `ç`, `î`, `ï`, `ô`, `ù`, `û`).
- L'affichage exploite la police vectorielle/bitmap compatible `fonts::efontJA_12` de M5Unified / LovyanGFX, supportant le jeu de caractères ISO-10646 / Latin-1 étendu.
- Aucun caractère accentué n'est supprimé ou simplifié.
- Les textes partiels ne sont jamais concaténés aveuglément ni transformés artificiellement en transcription finale.
- En cas d'absence de transcription finale dans le délai imparti (timeout 6 s), la mention explicite `[Transcription non confirmée (délai dépassé)]` est affichée.

---

## 4. Configuration et Déploiement

### 4.1. Secrets Wi-Fi et Clé API Gemini Live
Dans `examples/ContinuousSTT/include/secrets.h` :
```cpp
#pragma once
#define WIFI_SSID     "VOTRE_BOX_WIFI"
#define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE"
#define GEMINI_KEY    "AIzaSy..." // Clé API Google Gemini Live
```

### 4.2. Compilation PlatformIO
```bash
pio run -d examples/ContinuousSTT
```

### 4.3. Téléversement sur CoreS3
```bash
pio run -d examples/ContinuousSTT -t upload
pio device monitor -b 115200
```
