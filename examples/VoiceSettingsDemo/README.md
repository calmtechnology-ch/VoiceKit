# VoiceSettingsDemo — VoiceKit v0.6.5.2

Exemple officiel de configuration vocale dynamique, support multilingue et validation de chaîne UTF-8 intégrale sur **M5Stack CoreS3**.

---

## 1. Objectif

Démontrer la richesse et la flexibilité de paramétrage de la synthèse vocale avec Google Gemini Live :
- Changement dynamique de profil vocal (voix, langue BCP-47, style, tonalité, accent).
- Restitution phonétique fidèle de phrases complexes en français (accents, cédilles, ligatures `œ`/`æ`), allemand et anglais.
- Reconnexion de session sécurisée sans coupure abrupte ni interférence mutuelle.

---

## 2. Fonctionnalités

- **Palette de voix Gemini** : Alternance entre les voix *Aoede*, *Charon*, *Fenrir*, *Kore* et *Puck*.
- **Support multilingue** : Français (`fr-FR`), allemand (`de-DE`) et anglais (`en-US`).
- **Styles et tonalités** :
  - Styles : `NATURAL`, `PROFESSIONAL`, `EXPRESSIVE`, `NARRATIVE`.
  - Tons : `WARM`, `SERIOUS`, `CHEERFUL`, `CALM`.
- **Validation UTF-8 stricte** : Prise en charge des guillemets chevrons « », apostrophes courbes, ligatures et caractères accentués multiples.
- **Alternance fluide** : Reconfiguration de la session WebSocket effectuée au repos pour préserver l'état audio.
- **Interface tactile et série** : Bouton *PROFIL SUIVANT* et commandes série interactives (`N`, `R`, `S`).

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3).
- **Mémoire** : 8 Mo PSRAM.
- **Audio** : Amplificateur AW88298 et haut-parleur CoreS3.
- **Alimentation** : USB-C 5V / 1A.

---

## 4. Configuration

1. Copier le fichier d'exemple des secrets :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Éditer `include/secrets.h` :
   ```cpp
   #define WIFI_SSID     "Votre_SSID_WiFi"
   #define WIFI_PASSWORD "Votre_Mot_De_Passe"
   #define GEMINI_KEY    "Votre_Cle_API_Google_Gemini"
   ```

---

## 5. Compilation

Compiler avec PlatformIO :
```bash
pio run -d examples/VoiceSettingsDemo
```

---

## 6. Téléversement

1. Téléverser sur la carte :
   ```bash
   pio run -d examples/VoiceSettingsDemo -t upload
   ```
2. Ouvrir la console série :
   ```bash
   pio device monitor -d examples/VoiceSettingsDemo -b 115200
   ```

---

## 7. Mode d'utilisation

1. À la mise sous tension, le premier profil (Français UTF-8, voix *Aoede*) est chargé et énoncé.
2. **Changer de profil** : Appuyer sur le bouton tactile **PROFIL SUIVANT** ou envoyer la lettre `N` dans la console série.
3. Le profil suivant est appliqué, la session se reconfigure et la nouvelle phrase est automatiquement prononcée.
4. **Répéter** : Envoyer `R` sur la liaison série pour répéter le profil actif.
5. **Stopper** : Envoyer `S` pour couper la parole en cours.

---

## 8. Tests à effectuer

| # | Profil testé | Voix / Langue | Texte & Caractéristiques |
|---|---|---|---|
| 1 | Français standard | Aoede / fr-FR | *« À Noël, Élise prépare un café crème. L'été à Genève est agréable. François préfère un cœur léger. »* |
| 2 | Français professionnel | Charon / fr-FR | *« Le compte rendu de la séance plénière confirme une hausse des investissements de vingt-quatre pour cent. »* |
| 3 | Français expressif | Fenrir / fr-FR | *« Formidable ! Quelle magnifique journée ensoleillée pour célébrer cette splendide réussite ! »* |
| 4 | Allemand calme | Kore / de-DE | *« Guten Tag! Das Wetter in Berlin ist heute wunderbar und die Vögel singen fröhlich im Garten. »* |
| 5 | Anglais narratif | Puck / en-US | *« Once upon a time, high in the misty mountains, an ancient clock began to strike twelve. »* |

---

## 9. Résultats attendus

- Intonation distincte audible entre chaque voix et style.
- Prononciation irréprochable des mots français contenant des caractères non-ASCII (`Élise`, `Genève`, `cœur`).
- Les paramètres affichés sur l'écran correspondent exactement à la configuration transmise au moteur.

---

## 10. Limitations connues

- Le changement de profil vocal requiert une renégociation de setup sur la session WebSocket TLS (durée d'environ 300 à 600 ms).
- L'accent et le ton sont des directives données au modèle Gemini et peuvent varier subtilement selon la phrase.
