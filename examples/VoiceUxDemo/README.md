# VoiceKit v0.6.4.1 — VoiceUxDemo

Exemple d'évaluation et de démonstration de la fondation **Voice UX locale** (`VoiceUxController`) pour M5Stack CoreS3.

---

## 1. Caractéristiques de l'exemple

- **100% Hors-Ligne** : Aucun Wi-Fi requis, aucune clé API Gemini ou OpenAI requise.
- **Zéro capture audio requise** : Démonstration logicielle pure via injection de texte par port série ou tactile.
- **Filtrage déterministe** : Démonstration du filtre `TextGate` (rejet des hésitations isolées comme « euh », « hmm », préservation des commandes courtes).
- **Routage d'intentions locales** : Détection déterministe de `REPEAT`, `PREVIOUS`, `STOP`, `PAUSE`, `RESUME`, `SLOWER`, `REPHRASE`.
- **Protection anti faux positifs** : Rejet strict des questions complexes (« Pourquoi la voiture s'arrête-t-elle ? » -> `NONE`).
- **Mémoire conversationnelle** : Suivi des 5 dernières réponses en RAM (`ReplyMemory`), rappel instantané sur `REPEAT` ou `PREVIOUS`.
- **Interface M5GFX** : Affichage temps réel de l'état, de la décision, de l'intention reconnue, de la mémoire et des métriques.

---

## 2. Compilation et Téléversement

Avec PlatformIO CLI :

```bash
pio run -d examples/VoiceUxDemo -t upload
pio device monitor -d examples/VoiceUxDemo -b 115200
```

---

## 3. Commandes du Moniteur Série

Ouvrez un terminal série à **115 200 bauds**. Les commandes suivantes sont disponibles :

### Tester une transcription simulée (`T <texte>`)

```text
T Répète
T Mets en pause
T Continue
T Quelle heure est-il ?
T Euh...
T Pourquoi la voiture s'arrête-t-elle ?
T Peux-tu répéter cette expérience scientifique ?
T Réponse précédente
T Parle moins vite s'il te plaît
T Reformule
```

### Mémoriser une réponse de l'assistant (`M <texte>`)

```text
M La température est de 22 degrés.
M L'humidité relative est de 48 %.
```

### Consulter l'historique mémorisé (`H`)

Affiche la liste ordonnée des réponses en RAM (du plus récent au plus ancien).

### Effacer la mémoire (`C`)

Vide intégralement les réponses mémorisées.

### Activer / Désactiver le module (`D`)

Bascule l'état du module entre `ACTIF` et `DÉSACTIVÉ` (mode pass-through neutre).

---

## 4. Interaction Tactile sur l'Écran

- Un appui n'importe où sur l'écran tactile du CoreS3 bascule l'activation du module (`ACTIF` / `DÉSACTIVÉ`).

