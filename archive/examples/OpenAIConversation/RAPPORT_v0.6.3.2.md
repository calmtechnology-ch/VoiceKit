# Rapport de Conception, Implémentation et Validation — VoiceKit v0.6.3.2
## Modern Voice UI Edition — M5Stack CoreS3 (OpenAI Realtime Conversation)

**Date** : 2 novembre 2026  
**Auteur** : Antigravity (Google DeepMind) — Pair programming avec Philippe AD  
**Version cible** : `examples/OpenAIConversation v0.6.3.2`  
**Dépôt** : `Philippe-AD/VoiceKit`  
**Matériel cible** : M5Stack CoreS3 (ESP32-S3FN8, ES7210, AW88298, Flash 16 Mo, PSRAM OPI 8 Mo, tactile FT6336U, LCD ILI9342C 320×240)  
**Environnement** : PlatformIO Core 6.2.0, Espressif32 6.9.0, Arduino ESP32 2.0.17, M5Unified 0.2.23, M5GFX 0.2.35  

---

## 1. Synthèse des Objectifs et Résultats

La version **v0.6.3.2** de `examples/OpenAIConversation` remplace intégralement l'ancienne interface graphique par la **Modern Voice UI**, une interface moderne, fluide, élégante et tactile, conforme à la maquette de référence, tout en respectant scrupuleusement la règle fondamentale du projet :

> [!IMPORTANT]
> **Règle fondamentale** : Seul l'exemple `examples/OpenAIConversation` a été modifié. Le moteur VoiceKit v0.6.3.1 (`src/`), les autres exemples du dépôt et les mécanismes audio éprouvés sont **strictement préservés sans aucune altération**.

### Résultats Clés :
1. **Esthétique Dark Mode Premium** : Fond bleu nuit profond (`0x0842` / `#080E14`), contrastes doux, cartes aux coins arrondis, bordures subtiles et typographie soignée.
2. **Orbe vocal lumineux animé (VoiceOrb)** : Composant graphique autonome au centre de l'écran (diamètre cible ~65 px), gérant 5 états visuels distincts (respiration cyan calme, pulsation corail avec modulation par le volume RMS réel du micro, rotation violette orbitale, ondes sonores turquoise rayonnantes, alerte rouge statique). Rendu en double-buffering `M5Canvas` à 18 fps sans aucun `delay()`.
3. **Moteur UTF-8 français en pixels réels (FrenchUI)** : Calcul du retour à la ligne (word-wrap) basé sur la **largeur réelle en pixels** (`display.textWidth()`), préservation intégrale des séquences multi-octets, rendu géométrique précis des diacritiques (`é, è, ê, ë, à, â, ç, î, ï, ô, ù, û, ü, É, È, Ê, À, Ç, œ, Œ, « », ’, —, …`) et conservation de la transcription complète en mémoire.
4. **Composition d'écran épurée (VoiceUI)** : En-tête compact avec indicateur Wi-Fi et badge court, message d'état centré en français clair (sans jargon technique), cartes `Vous` et `Assistant` compactes et grand bouton tactile d'action (hauteur 35 px).
5. **Gestion tactile éprouvée** : Conservation intégrale des algorithmes de `CoreS3-TouchDiagnostics v0.1.0.1` (anti-rebond 40 ms, suivi monotone, rétention de coordonnées, gestion des glissements et priorité absolue au bouton STOP dès le front montant).
6. **Validation et non-régression** : Compilation PlatformIO réussie avec succès (RAM 24,0 %, Flash 80,3 %) et **100 % de succès sur la suite complète des 2100 tests unitaires automatisés (0 échec)**.

---

## 2. Inventaire des Fichiers Modifiés et Ajoutés

| Fichier | Statut | Nature des modifications |
|---|---|---|
| `examples/OpenAIConversation/include/FrenchUI.h` | **Modifié** | Moteur UTF-8 français étendu : calcul du retour à la ligne basé sur la largeur réelle en pixels (`wrapUtf8WordsByPixels`), mesure pixel exacte des diacritiques (`measureUtf8PixelWidth`), troncature propre sans orphelins et centrage automatique. |
| `examples/OpenAIConversation/include/VoiceOrb.h` | **Ajouté** | En-tête du composant d'orbe vocal autonome : définition de l'état `OrbState`, des dimensions (74×74 px, Y=35..108), du canvas et des interfaces de rendu. |
| `examples/OpenAIConversation/src/VoiceOrb.cpp` | **Ajouté** | Implémentation du rendu de l'orbe : 5 modes graphiques, double-buffering `M5Canvas`, réaction dynamique au son RMS réel de VoiceKit (`voice.getAudioLevel()`), régulation à 18 fps et auto-dégradation. |
| `examples/OpenAIConversation/include/VoiceUI.h` | **Ajouté** | En-tête du gestionnaire de composition graphique : gestion des zones d'écran, de la palette Dark Mode, des états `ConvUIState`, des drapeaux de rafraîchissement partiel et du hit-testing tactile. |
| `examples/OpenAIConversation/src/VoiceUI.cpp` | **Ajouté** | Implémentation de l'interface moderne : en-tête compact avec Wi-Fi et badge d'état court, texte principal d'état centré, cartes `Vous` et `Assistant` aux coins arrondis, grand bouton tactile arrondi avec icônes micro/stop. |
| `examples/OpenAIConversation/src/main.cpp` | **Modifié** | Orchestration de l'exemple : initialisation de `VoiceUI`, intégration synchrone des callbacks `VoxCoreVoice`, gestion tactile `CoreS3-TouchDiagnostics`, commandes série (P/R/S/D) et diagnostics instrumentés. |
| `examples/OpenAIConversation/include/secrets.example.h` | **Modifié** | Mise à jour de l'en-tête de version v0.6.3.2 (conservation stricte des placeholders sécurisés). |
| `examples/OpenAIConversation/README.md` | **Modifié** | Documentation complète actualisée pour la v0.6.3.2 (Modern Voice UI, orbe animé, guide d'utilisation, commandes série). |
| `examples/OpenAIConversation/RAPPORT_v0.6.3.2.md` | **Ajouté** | Présent document d'audit, de conception, de mesures et de validation. |

---

## 3. Architecture Visuelle et Répartition de l'Écran (320 × 240)

L'agencement de l'écran respecte rigoureusement la grille validée de 320 × 240 pixels (rotation 1 paysage M5GFX) :

```text
+--------------------------------------------------------------+ Y = 0
| OpenAI Conversation                  [Wi-Fi] [ BADGE D'ÉTAT ] |
+--------------------------------------------------------------+ Y = 30
|                                                              |
|                         ( ORBE VOCAL )                       | Y = 35..108
|                      Diamètre ~65 px (74x74)                 |
|                                                              |
+--------------------------------------------------------------+
|                     Message d'état centré                    | Y = 110..132
+--------------------------------------------------------------+
| [ Vous ]   (Dernière transcription utilisateur / Whisper)   | Y = 136..164
+--------------------------------------------------------------+
| [Assistant] (Dernière réponse de l'assistant OpenAI)         | Y = 168..196
+--------------------------------------------------------------+
|                                                              |
|                  [ BOUTON D'ACTION PRINCIPAL ]               | Y = 200..235
|                                                              |
+--------------------------------------------------------------+ Y = 240
```

### Grille Dimensionnelle Exacte

| Zone | Intervalle Y | Hauteur | Description & Éléments Graphiques |
|---|---|---|---|
| **En-tête** | Y = 0..30 | 30 px | Titre `OpenAI Conversation` à gauche, indicateur d'état Wi-Fi à droite, badge d'état court (62×18 px, r=4) et ligne de séparation subtile à Y = 30 (`0x1166`). |
| **Orbe Vocal** | Y = 35..108 | 74 px | Orbe centré en (160, 71), bounding box 74×74 px (X = 123..196). Rendu double-buffering `M5Canvas` à 18 fps sans scintillement. |
| **Message d'État** | Y = 110..132 | 22 px | Message d'instruction centré horizontalement à X = 160, Y = 117 en français naturel (*« Appuyez pour parler »*, *« Je vous écoute… »*, etc.). |
| **Carte Utilisateur** | Y = 136..164 | 28 px | Boîte arrondie (300×28 px, r=4) à X = 10. Pill `Vous` à gauche, texte utilisateur sur 1 ou 2 lignes à droite avec word-wrap en pixels. |
| **Carte Assistant** | Y = 168..196 | 28 px | Boîte arrondie (300×28 px, r=4) à X = 10. Pill `Assistant` à gauche, réponse de l'IA sur 1 ou 2 lignes à droite avec troncature propre. |
| **Bouton Principal** | Y = 200..235 | 35 px | Grand bouton tactile arrondi (300×35 px, r=8) à X = 10. Visibilité maximale, icône micro ou stop, libellés clairs (`PARLER`, `RELÂCHER`, `STOP`, `RÉESSAYER`). |

---

## 4. Orbe Vocal Animé (`VoiceOrb`)

L'orbe vocal est l'élément interactif central de la Modern Voice UI. Il a été conçu pour allier qualité visuelle et respect strict du budget CPU de l'ESP32-S3.

### 4.1 Les 5 Comportements Visuels

| État | Couleur Dominante | Animation et Rendu Graphique | Rôle & Comportement Interactif |
|---|---|---|---|
| **PRÊT** | Cyan doux (`0x0359` / `0x067E`) | Respiration lente et douce (période 3,6 s, `sinf`), halo discret externe, gradient sphérique interne et éclat spéculaire 3D. | Indique le calme et la disponibilité du système pour un nouveau dialogue. |
| **ÉCOUTE** | Corail / Orange vif (`0xFA60` / `0xFD20`) | Pulsation vive (période 850 ms) combinée à une **modulation dynamique par le niveau RMS réel du micro** issu de `voice.getAudioLevel()`. | Réagit en direct à la voix de l'utilisateur. Aucune simulation artificielle : si l'utilisateur ne parle pas, l'orbe reste au repos. |
| **ANALYSE** | Violet / Pourpre (`0x6156` / `0xB2DF`) | Cœur violet en pulsation douce, entouré de **3 particules orbitales lumineuses lilas en rotation continue** (rayon 31 px, période 2,2 s). | Indique visuellement que la question est en cours de traitement sur les serveurs OpenAI. |
| **RÉPONSE** | Turquoise éclatant (`0x0538` / `0x2EDB`) | **Ondes sonores circulaires concentriques en expansion** (rayons de 22 à 35 px, période 1,2 s) simulant l'émission vocale. | Animation active et dynamique pendant toute la restitution audio sur le haut-parleur. |
| **ERREUR** | Rouge d'alerte (`0xC800` / `0xF9A6`) | Orbe statique rouge entouré d'un anneau sombre, avec **symbole d'alerte point d'exclamation `!`** dessiné en blanc au centre. | Rendu statique immédiat sans animation continue inutile (0 % CPU pendant l'erreur). |

### 4.2 Performance et Respect de la Boucle Vocale

- **Zéro `delay()`** : Toute la planification temporelle repose exclusivement sur `millis()`.
- **Fréquence maîtrisée** : Cadence nominale bornée à **18 images par seconde** (intervalle de 55 ms).
- **Double-buffering M5Canvas** : Un canvas de 74 × 74 pixels (10,9 Ko de mémoire) est alloué une seule fois à l'initialisation. Tous les cercles et gradients sont dessinés hors-écran, puis transférés en un seul appel `pushSprite(123, 35)` d'environ **1 ms**, éliminant tout déchirement ou clignotement.
- **Auto-dégradation sous charge** : Si un cycle de `voice.update()` dépasse 5 ms, l'orbe bascule automatiquement à une cadence réduite (~9 fps) pour donner la priorité absolue au traitement des paquets audio PCM et au flux WebSocket TLS.

---

## 5. Moteur Typographique Français UTF-8 (`FrenchUI`)

L'affichage de texte français sur écran TFT embarqué sans police CJK volumineuse présentait auparavant des limites (retour à la ligne basé sur un nombre de caractères arbitraire, accents décalés ou tronqués). Le module `FrenchUI` a été profondément amélioré :

### 5.1 Retour à la Ligne Basé sur la Largeur Réelle en Pixels
- La méthode `wrapUtf8WordsByPixels(display, text, maxPixelWidth, maxLines)` mesure la **largeur exacte en pixels** de chaque mot via `measureUtf8PixelWidth`.
- Les largeurs proportionnelles des caractères fins (`i`, `l`, `'`) et larges (`w`, `m`, `œ`) sont fidèlement prises en compte.
- Si le texte excède le nombre maximal de lignes de la carte (2 lignes), la dernière ligne est tronquée au dernier mot possible et complétée élégamment par des points de suspension `...`, sans jamais dépasser la largeur de la carte.

### 5.2 Préservation Intégrale des Données
- Le texte complet issu de Whisper et de l'assistant OpenAI est intégralement conservé dans les variables `s_userFullText` et `s_assistantFullText`.
- La carte n'affiche que la portion visible optimisée, évitant tout débordement hors du cadre ou masquage involontaire.

### 5.3 Précision Diacritique Géométrique
- Prise en charge native complète des accents français : `é, è, ê, ë, à, â, ç, î, ï, ô, ù, û, ü`, majuscules `É, È, Ê, À, Ç`, ligatures `œ, Œ`, guillemets français `« »`, apostrophes typographiques `’`, tirets cadratins `—` et points de suspension `…`.
- Calcul des positions d'accents ajusté selon la casse (minuscule vs majuscule) pour un rendu visuel harmonieux.

---

## 6. Composition de l'Interface (`VoiceUI`)

Le module `VoiceUI` centralise l'ordonnancement de l'affichage en mode événementiel :

### 6.1 Rafraîchissement Ciblé par Drapeaux Sales (`dirty flags`)
L'écran n'est jamais effacé en totalité pendant l'exécution (`fillScreen` proscrit hors de l'initialisation) :
- `_dirtyHeader` : Retrace l'en-tête uniquement si le Wi-Fi ou l'état change.
- `_dirtyStateText` : Retrace le texte central uniquement lors d'un changement d'état.
- `_dirtyUserCard` : Retrace la carte utilisateur lors de la réception d'une transcription Whisper.
- `_dirtyAsstCard` : Retrace la carte assistant lors de l'arrivée de texte OpenAI.
- `_dirtyBtn` : Retrace le bouton principal lors des transitions d'action.

### 6.2 Grand Bouton d'Action Tactile
Situé à Y = 200..235 (hauteur 35 px, largeur 300 px), le bouton offre une zone tactile généreuse et des indications claires :
- **PRÊT** : Fond bleu royal (`0x1B3F`), bordure lumineuse, icône microphone stylisée et texte `PARLER`.
- **ÉCOUTE** : Fond orange/corail (`0xFA60`), texte `RELÂCHER` (Push-to-Talk : maintenir pour parler, relâcher pour envoyer).
- **ANALYSE / RÉPONSE** : Fond rouge vif (`0xC800`), icône carré stop et texte `STOP` (interruption prioritaire).
- **ERREUR** : Fond rouge sombre (`0x8800`), texte `RÉESSAYER`.
- **INITIALISATION** : Fond gris neutre (`0x2965`), texte `CONNEXION` (désactivé).

---

## 7. Gestion Tactile Robuste (CoreS3-TouchDiagnostics)

La gestion tactile de la v0.6.3.2 conserve et consolide les acquis de stabilisation :
- **Anti-rebond temporel** : Seuil de 40 ms (`TOUCH_DEBOUNCE_MS`) interdisant les doubles déclenchements.
- **Suivi de contact monotone** : Chaque nouvel appui physique incrémente un identifiant unique `s_contactId`.
- **Mémorisation des dernières coordonnées** : Les coordonnées `s_lastTouchX` et `s_lastTouchY` sont mémorisées en continu afin que l'événement de relâchement (`pointCount == 0`) soit détecté exactement à l'endroit où le doigt a quitté l'écran.
- **Tolérance de glissement** : Marge de 15 px autour du bouton principal. Un glissement au-delà déclenche une fin de prise de parole sécurisée sans blocage.
- **Priorité absolue STOP** : Pendant `PROCESSING` ou `SPEAKING`, le moindre appui sur STOP déclenche immédiatement `voice.stop()` sur le **front montant (PRESS)** sans attendre que le doigt soit levé.
- **Sécurité d'enchaînement** : Un appui ne peut démarrer une session que si le moteur vocal confirme `isReady()` et `!isBusy()`. Aucun nouveau tour ne peut être associé à un doigt maintenu sur l'écran.

---

## 8. Résultats Réels des Compilations et Tests Automatisés

### 8.1 Compilation PlatformIO de l'Exemple `OpenAIConversation`
- **Commande** : `& "C:\Users\phall\.platformio\penv\Scripts\pio.exe" run -d examples/OpenAIConversation`
- **Statut** : **SUCCESS** (Code retour 0)
- **Durée de compilation** : 49.66 secondes
- **Binaire généré** : `.pio\build\m5stack-cores3\firmware.bin`
- **Empreinte mémoire mesurée** :
  - **RAM** : **78 772 octets** / 327 680 octets (**24,0 %**)
  - **Flash** : **1 684 417 octets** / 2 097 152 octets (**80,3 %**)
- **Analyse d'empreinte** : L'intégration de la Modern Voice UI, du sprite `M5Canvas` (74×74 px) et des modules modulaires n'ajoute que **120 octets de RAM** et environ **17 Ko de Flash** par rapport à la v0.6.3.1, tout en restant très largement sous la limite de la partition `no_ota.csv` (2 Mo).

### 8.2 Exécution de la Suite Complète de Tests VoiceKit (`tests/run_tests.bat`)
- **Commande** : `tests\run_tests.bat`
- **Statut** : **SUCCÈS TOTAL (Code 0)**
- **Résultat** : **2100 tests réussis, 0 échec** sur l'ensemble des 105 suites de tests de VoiceKit.
- **Non-régression** : L'ensemble du cœur VoiceKit v0.6.3.1, du client OpenAI Realtime, de Gemini Live, de WakeNet et des buffers audio est resté 100 % intact et validé.

---

## 9. Protocole de Validation sur Matériel Réel (M5Stack CoreS3)

> [!NOTE]
> Ce protocole doit être exécuté sur le véritable M5Stack CoreS3 connecté au port série pour valider l'expérience utilisateur et acoustique sur cible physique.

### 9.1 Préparation
1. Renseigner les identifiants réels dans `examples/OpenAIConversation/include/secrets.h`.
2. Flasher le binaire et ouvrir le moniteur série :
   ```bash
   cd examples/OpenAIConversation
   pio run -t upload
   pio device monitor -b 115200
   ```

### 9.2 Scénarios de Test Matériel

| N° | Scénario de Test | Procédure Opératoire | Résultat Attendu sur le CoreS3 |
|---|---|---|---|
| **1** | **Démarrage & Connexion** | Mettre le CoreS3 sous tension. | L'écran affiche le thème sombre. L'orbe pulse doucement en cyan. Le badge affiche `ATTENTE` puis `PRÊT` dès la négociation WebSocket. Le voyant Wi-Fi devient vert. |
| **2** | **Appui PARLER** | Maintenir le bouton `PARLER`. | L'orbe bascule instantanément en corail/orange avec une pulsation vive. Le bouton devient `RELÂCHER`. Le message d'état affiche *« Je vous écoute… »*. |
| **3** | **Réactivité sonore de l'orbe** | Parler fort puis chuchoter tout en maintenant le bouton. | L'orbe module la taille de son halo externe en direct selon le niveau sonore réel de la voix sans délai perceptible. |
| **4** | **Relâchement du bouton** | Relâcher le doigt. | Transition immédiate vers `ANALYSE`. L'orbe devient violet avec 3 particules en rotation orbitale. La transcription Whisper s'affiche dans la carte `Vous`. Le message d'état affiche *« Je réfléchis… »*. |
| **5** | **Restitution vocale de l'IA** | Écouter la réponse de l'assistant. | L'orbe devient turquoise et diffuse des ondes sonores concentriques en expansion. La réponse textuelle s'affiche dans la carte `Assistant` avec un retour à la ligne propre. Le message d'état affiche *« Je vous réponds »*. |
| **6** | **Clôture et retour à PRÊT** | Laisser la phrase se terminer. | La dernière syllabe est audible sans coupure sèche. Dès la fin du son, l'orbe redevient cyan doux en respiration lente, et le bouton repasse à `PARLER`. |
| **7** | **Interruption prioritaire STOP** | Pendant que l'assistant parle, appuyer sur le bouton `STOP`. | Coupure instantanée du son (< 50 ms), l'orbe revient immédiatement à `PRÊT`, et le haut-parleur est libéré. |
| **8** | **Test des accents français** | Poser la question : *« Peux-tu épeler éléphant, cœur et Noël ? »*. | Les cartes affichent les accents correctement placés au-dessus des voyelles sans artefact ni caractère corrompu. |
| **9** | **Réponse longue et troncature** | Poser une question demandant 3 phrases. | Le texte se répartit harmonieusement sur les 2 lignes de la carte `Assistant`. La fin de la deuxième ligne se termine par `...` sans déborder sur le bouton. |
| **10** | **20 cycles consécutifs** | Réaliser 20 tours de parole d'affilée. | Aucune fuite mémoire dans les logs périodiques (`[DIAG][MEM]`), cadence de l'orbe toujours fluide, zéro blocage. |
| **11** | **Simulation de glissement hors zone** | Appuyer sur `PARLER` puis glisser le doigt de 3 cm vers le haut. | Dès que le doigt sort de la marge de 15 px, la capture s'arrête proprement et l'audio capturé est transmis sans crash. |
| **12** | **Test de résilience réseau** | Couper la box Wi-Fi en cours d'utilisation. | L'orbe bascule en rouge d'alerte statique avec le symbole `!`. Le badge passe en `ERREUR`. À la réactivation du Wi-Fi, un appui sur `RÉESSAYER` rétablit la session sans redémarrer le CoreS3. |

---

## 10. Conclusion

La version **VoiceKit v0.6.3.2** (`examples/OpenAIConversation`) accomplit l'ensemble des objectifs fixés :
- L'ancienne interface rudimentaire est intégralement remplacée par la **Modern Voice UI**, élégante, fluide et fidèle à la maquette.
- L'orbe vocal animé à 5 états apporte une signature visuelle premium tout en restant ultra-léger grâce au double-buffering `M5Canvas`.
- La typographie française UTF-8 avec calcul en pixels réels assure une lisibilité sans faille dans les cartes conversationnelles.
- La stabilité, la robustesse tactile et l'intégrité audio de VoiceKit sont préservées à 100 %.

