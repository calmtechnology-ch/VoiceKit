# Exemple Autonome : WakeWordFoundation (VoiceKit v0.6.2.1)

Cet exemple autonome valide la fondation matérielle et logicielle de détection locale de mot de réveil (**Wake Word Foundation**) sur **M5Stack CoreS3**, **sans écran LVGL** (rendu direct M5GFX) et **sans connexion réseau Wi-Fi / Gemini / OpenAI**.

## Objectifs validés par cet exemple

1. **Propriété unique du microphone CoreS3 ES7210** :
   - Acquisition continue non bloquante en **PCM 16 000 Hz mono 16 bits signé** via `LiveAudioStreamer` en mode capture locale (`startLocalCapture()`).
   - Aucune seconde initialisation concurrente du codec ES7210.
2. **Pont audio temps réel (`WakeWordAudioBridge`)** :
   - File circulaire SPSC bornée (8 trames de 512 échantillons = 256 ms).
   - Une seule copie mémoire (`memcpy` DMA $\to$ slot) dans la tâche audio, zéro copie au dépilage dans `voice.update()`, zéro impression `Serial` dans le callback audio critique.
3. **Machine à états complète et affichage français explicite (`WakeWordManager`)** :
   - Transitions clairement distinguées sur l'écran CoreS3 et le moniteur série :
     - `ARMED — En écoute...` (activation initiale)
     - `DETECTED — Mot reconnu` (avec encadré vert `MOT DE RÉVEIL DÉTECTÉ !`, `« Bonjour ESP »` et `Détection réussie — événement #N`)
     - `SUSPENDED — Suspension temporaire (Détection suspendue)`
     - `ARMED — Prêt pour un nouveau réveil (Écoute réactivée)`
   - Pied de page de compteurs : `Détections : X | Réarmements : Y | Erreurs : Z` (une détection n'est comptabilisée qu'une seule fois par événement réel).
   - Aucune sortie série répétitive dans `loop()`.
4. **Distinction honnête du moteur actif (`IWakeWordEngine`)** :
   - Affiche explicitement le nom du moteur actif ainsi que **`RECONNAISSANCE RÉELLE`** (pour `WAKENET` / `wn9l_fr_bonjouresp_tts3`) ou **`SIMULATION (TEST)`** (pour `TEST`).
   - Ne présente jamais un déclenchement du moteur `TEST` comme une détection acoustique.

## Affichage Écran M5Stack CoreS3 & Moniteur Série (115200 bauds)

```text
VOICEKIT — WAKE WORD                                            v0.6.2.1
Moteur actif : WAKENET (wn9l_fr_bonjouresp_tts3) — RECONNAISSANCE RÉELLE
────────────────────────────────────────────────────────────────────────

🎙 ARMED — En écoute...
✓ DETECTED — Mot reconnu

╭──────────────────────────────────────────────────────────────────────╮
│  ✓ MOT DE RÉVEIL DÉTECTÉ !                                           │
│                                                                      │
│  « Bonjour ESP »                                                     │
│                                                                      │
│  Détection réussie — événement #12                                   │
│  Moteur : WAKENET — RECONNAISSANCE RÉELLE                            │
╰──────────────────────────────────────────────────────────────────────╯

⏸ SUSPENDED — Suspension temporaire (Détection suspendue)
✓ ARMED — Prêt pour un nouveau réveil (Écoute réactivée)
────────────────────────────────────────────────────────────────────────
Détections : 12 | Réarmements : 12 | Erreurs : 0
```

## Commandes Série Interactives

| Commande | Action |
|---|---|
| `bonjour esp` ou `wake` | Déclenche une détection de test simulée (`SIMULATION (TEST)`, uniquement si moteur `TEST` et état `ARMED`) |
| `r` ou `resume` | Simule la fin de conversation et réarme le détecteur (`SUSPENDED` $\to$ `ARMED — Prêt pour un nouveau réveil (Écoute réactivée)`) |
| `s` ou `suspend` | Suspend manuellement le détecteur (`ARMED` $\to$ `SUSPENDED — Suspension temporaire (Détection suspendue)`) |
| `d` ou `disable` | Désactive le détecteur (`DISABLED`) |
| `e` ou `enable` | Réactive le détecteur (`DISABLED` $\to$ `INITIALIZING` $\to$ `ARMED`) |
| `c` ou `cycle` | Exécute 5 cycles d'activation / suspension / reprise / désactivation |
| `w` ou `wakenet` | Sélectionne le moteur `WAKENET` (`RECONNAISSANCE RÉELLE` si disponible, sinon `ERROR` explicite) |
| `t` ou `test` | Sélectionne le moteur `TEST` (`SIMULATION (TEST)`) et arme la capture locale (`ARMED`) |
| `m` ou `metrics` | Affiche les métriques mémoire (RAM interne, PSRAM), CPU, file audio et compteurs (`Détections`, `Réarmements`, `Erreurs`) |

## Compilation PlatformIO

```bash
pio run -d examples/WakeWordFoundation
```
