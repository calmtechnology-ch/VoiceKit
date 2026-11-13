# VoiceSettingsDemo — VoiceKit v0.6.6 (Universal Voice Profiles)

Exemple officiel de configuration vocale universelle multi-fournisseurs (**Google Gemini Live** et **OpenAI Realtime**) avec interface tactile **LVGL 9** (`320 × 240`) et sauvegarde **NVS** sur **M5Stack CoreS3**.

---

## 1. Objectif

Permettre à l'utilisateur de configurer, tester, appliquer dynamiquement et sauvegarder un profil vocal universel complet sans redémarrer le CoreS3 :
- Choix du fournisseur vocal : **Gemini Live** ou **OpenAI Realtime**.
- Sélection de la langue parlée (`fr-FR`, `fr-CH`, `en-US`, `en-GB`, `de-DE`, `it-IT`, `es-ES`).
- Filtrage par genre vocal perçu (classification indicative du catalogue) et choix de la voix compatible.
- Personnalisation du style d'élocution, de la tonalité et de l'accent.
- Sauvegarde et restauration automatique en mémoire non volatile NVS (`Preferences`) sans jamais stocker de clé API.

---

## 2. Fonctionnalités

- **Écran 1 — Fournisseur et langue** : Bascule tactile Gemini Live / OpenAI Realtime, sélection de la langue BCP-47 et affichage en direct de l'état Wi-Fi, de la présence des clés API et de l'état de session.
- **Écran 2 — Choix de la voix** : Filtres *Toutes / Féminin / Masculin / Neutre*, liste tactile défilante des voix compatibles du fournisseur sélectionné et bouton **« Tester la voix sélectionnée »** déclenchant un véritable essai audio dans la langue choisie.
- **Écran 3 — Personnalisation** : Réglage tactile du **Style** (*Naturel*, *Professionnel*, *Expressif*, *Narratif*), de la **Tonalité** (*Neutre*, *Calme*, *Chaleureux*, *Enjoué*, *Sérieux*), de l'**Accent** et bouton **« Enregistrer »** en NVS.
- **Écran 4 — Validation** : Résumé comparatif du profil actif confirmé et du profil sélectionné, boutons **« Écouter »**, **« Appliquer »** (n'affichant *« Profil appliqué »* qu'après confirmation effective du fournisseur), **« Réinitialiser »** et bannière d'affichage des erreurs.
- **Polices UTF-8 françaises intégrées** : Affichage net de tous les accents français (`é`, `è`, `ê`, `à`, `ç`, `œ`, `« »`) via `font_french_12` et `font_french_14`.
- **Boucle LVGL 9 non bloquante** : Les transitions réseau et reconnexions TLS s'exécutent via la machine à états de `VoxCoreVoice` sans figer l'interface tactile.

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3, Flash 16 Mo, PSRAM 8 Mo).
- **Écran tactile** : LCD IPS 2.0" `320 × 240` capacitif intégré au CoreS3.
- **Audio** : Haut-parleur intégré CoreS3 (amplificateur AW88298).
- **Alimentation** : Câble USB-C 5V.

---

## 4. Configuration

1. Copier le modèle de configuration des secrets :
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
2. Renseigner `include/secrets.h` (renseigner l'une des deux clés API ou les deux pour basculer librement entre Gemini et OpenAI) :
   ```cpp
   #define WIFI_SSID       "Votre_SSID_WiFi"
   #define WIFI_PASSWORD   "Votre_Mot_De_Passe_WiFi"
   #define GEMINI_API_KEY  "Votre_Cle_API_Gemini"
   #define OPENAI_API_KEY  "Votre_Cle_API_OpenAI"
   ```

---

## 5. Compilation

Compiler le firmware avec PlatformIO :
```bash
pio run -d examples/VoiceSettingsDemo
```

---

## 6. Téléversement

1. Téléverser sur le M5Stack CoreS3 :
   ```bash
   pio run -d examples/VoiceSettingsDemo -t upload
   ```
2. Ouvrir le moniteur série (115200 bauds) :
   ```bash
   pio device monitor -d examples/VoiceSettingsDemo -b 115200
   ```

---

## 7. Mode d'utilisation

1. Naviguer entre les **4 écrans** via la barre d'onglets supérieure (`1. Fourn.`, `2. Voix`, `3. Style`, `4. Valid.`).
2. Sur l'**Écran 1**, choisir **Gemini Live** ou **OpenAI Realtime** ainsi que la langue souhaitée.
3. Sur l'**Écran 2**, filtrer par genre perçu si souhaité, toucher une voix dans la liste, puis appuyer sur **« Tester la voix sélectionnée »** pour entendre un échantillon vocal réel.
4. Sur l'**Écran 3**, ajuster le style, la tonalité et l'accent, puis appuyer sur **« Enregistrer dans la mémoire NVS »** pour conserver ce profil au prochain démarrage.
5. Sur l'**Écran 4**, appuyer sur **« Appliquer »** pour négocier et confirmer le profil auprès du fournisseur, **« Écouter »** pour réentendre le profil actif, ou **« Réinitialiser »** pour revenir au profil par défaut.

---

## 8. Tests à effectuer

| # | Scénario à vérifier sur CoreS3 | Résultat attendu |
|---|---|---|
| 1 | Gemini + `fr-FR` + `Aoede` -> *Tester* | Confirmation serveur puis restitution fluide de la phrase française accentuée. |
| 2 | Gemini + `de-DE` + `Charon` -> *Appliquer* | Affichage *« Attente confirmation fournisseur... »* puis *« Profil appliqué »* après `setupComplete`. |
| 3 | Bascule OpenAI + `en-US` + `marin` -> *Tester* | Arrêt propre de la session Gemini, ouverture OpenAI Realtime et lecture anglaise avec *marin*. |
| 4 | OpenAI + `it-IT` + `cedar` après essai audio | Reconnexion sécurisée automatique (respect du verrouillage de voix OpenAI) et essai en italien. |
| 5 | *Enregistrer* en NVS puis redémarrage CoreS3 | Restauration automatique du profil sauvegardé au démarrage sans perte ni stockage de clé API. |

---

## 9. Résultats attendus

- L'interface tactile LVGL 9 reste fluide (~30 FPS) pendant les connexions et reconnexions WebSocket TLS.
- L'état **« Profil appliqué »** n'apparaît jamais avant la confirmation effective du serveur (`setupComplete` / `session.updated`).
- En cas d'absence de clé API ou de coupure Wi-Fi pendant un changement, l'erreur est affichée clairement sur l'Écran 4 et l'ancien profil valide est restauré.

---

## 10. Limitations connues

- Le genre vocal (*Féminin*, *Masculin*, *Neutre*) est une classification indicative servant au filtrage du catalogue VoiceKit et n'est pas un paramètre acoustique transmis aux API.
- Le style, la tonalité et l'accent sont appliqués via des instructions textuelles système ; le rendu exact dépend de l'interprétation du modèle distant.
- Sur OpenAI Realtime, le changement de voix après émission d'audio dans une session requiert l'ouverture d'une nouvelle session WebSocket TLS (~400 à 900 ms).
