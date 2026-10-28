# Exemple Autonome : WakeWordFoundation (VoiceKit v0.6.2.1)

Cet exemple autonome valide la fondation matérielle et logicielle de détection locale de mot de réveil (**Wake Word Foundation**) sur **M5Stack CoreS3**, **sans écran LVGL** et **sans connexion réseau Wi-Fi / Gemini / OpenAI**.

## Objectifs validés par cet exemple

1. **Propriété unique du microphone CoreS3 ES7210** :
   - Acquisition continue non bloquante en **PCM 16 000 Hz mono 16 bits signé** via `LiveAudioStreamer` en mode capture locale (`startLocalCapture()`).
   - Aucune seconde initialisation concurrente du codec ES7210.
2. **Pont audio temps réel (`WakeWordAudioBridge`)** :
   - File circulaire SPSC bornée (8 trames de 512 échantillons = 256 ms).
   - Une seule copie mémoire (`memcpy` DMA $\to$ slot) dans la tâche audio, zéro copie au dépilage dans `voice.update()`, zéro impression `Serial` dans le callback audio critique.
3. **Machine à états complète et affichage français explicite (`WakeWordManager`)** :
   - Transitions clairement distinguées :
     - `ARMED — En écoute` (activation initiale)
     - `DETECTED — Mot reconnu` (avec encadré très visible `MOT DE RÉVEIL DÉTECTÉ` et texte `Bonjour ESP`)
     - `SUSPENDED — Détection suspendue`
     - `ARMED — Écoute réactivée` (après réarmement)
   - Compteurs de détections (comptabilisées une seule fois par événement réel), de réarmements et d'erreurs.
   - Aucune sortie série répétitive dans `loop()`.
4. **Distinction honnête du moteur actif (`IWakeWordEngine`)** :
   - Affiche explicitement le nom du moteur actif ainsi que **`RECONNAISSANCE RÉELLE`** (pour `WAKENET` / `wn9l_fr_bonjouresp_tts3`) ou **`SIMULATION (TEST)`** (pour `TEST`).
   - Ne présente jamais un déclenchement du moteur `TEST` comme une détection acoustique.

## Sortie Moniteur Série (115200 bauds)

```text
[WAKE] VoiceKit v0.6.2.1
[WAKE] CoreS3 ES7210 prêt (PCM 16000 Hz / 16-bit mono)
[WAKE] État : INITIALIZING — Initialisation en cours | Moteur : WAKENET [RECONNAISSANCE RÉELLE] | Détections=0 | Réarmements=0 | Erreurs=0
[WAKE] État : ARMED — En écoute | Moteur : WAKENET [RECONNAISSANCE RÉELLE] | Détections=0 | Réarmements=0 | Erreurs=0
[WAKE] Moteur actif : WAKENET (modèle : wn9l_fr_bonjouresp_tts3) — RECONNAISSANCE RÉELLE
[WAKE] État : DETECTED — Mot reconnu | Moteur : WAKENET [RECONNAISSANCE RÉELLE] | Détections=1 | Réarmements=0 | Erreurs=0

============================================================
||                 MOT DE RÉVEIL DÉTECTÉ                  ||
============================================================
  Texte reconnu          : Bonjour ESP
  Moteur actif           : WAKENET (wn9l_fr_bonjouresp_tts3)
  Mode de reconnaissance : RECONNAISSANCE RÉELLE
  Origine                : Détection acoustique réelle (microphone CoreS3 ES7210)
  Compteur de détections : #1 (réelles=1 | simulées=0)
  Compteurs cumulés      : Détections=1 | Réarmements=0 | Erreurs=0
============================================================
[WAKE] État : SUSPENDED — Détection suspendue | Moteur : WAKENET [RECONNAISSANCE RÉELLE] | Détections=1 | Réarmements=0 | Erreurs=0
[WAKE] État : ARMED — Écoute réactivée | Moteur : WAKENET [RECONNAISSANCE RÉELLE] | Détections=1 | Réarmements=1 | Erreurs=0
```

## Commandes Série Interactives

| Commande | Action |
|---|---|
| `bonjour esp` ou `wake` | Déclenche une détection de test simulée (`SIMULATION (TEST)`, uniquement si moteur `TEST` et état `ARMED`) |
| `r` ou `resume` | Simule la fin de conversation et réarme le détecteur (`SUSPENDED` $\to$ `ARMED — Écoute réactivée`) |
| `s` ou `suspend` | Suspend manuellement le détecteur (`ARMED` $\to$ `SUSPENDED — Détection suspendue`) |
| `d` ou `disable` | Désactive le détecteur (`DISABLED`) |
| `e` ou `enable` | Réactive le détecteur (`DISABLED` $\to$ `INITIALIZING` $\to$ `ARMED`) |
| `c` ou `cycle` | Exécute 5 cycles d'activation / suspension / reprise / désactivation |
| `w` ou `wakenet` | Sélectionne le moteur `WAKENET` (`RECONNAISSANCE RÉELLE` si disponible, sinon `ERROR` explicite) |
| `t` ou `test` | Sélectionne le moteur `TEST` (`SIMULATION (TEST)`) et arme la capture locale (`ARMED`) |
| `m` ou `metrics` | Affiche les métriques mémoire (RAM interne, PSRAM), CPU, file audio et compteurs (détections, réarmements, erreurs) |

## Compilation PlatformIO

```bash
pio run -d examples/WakeWordFoundation
```
