# Charte graphique VoiceDiagnostics

Référence réutilisable pour reproduire l'identité visuelle de VoiceDiagnostics dans d'autres applications. Cette charte décrit l'interface de `src/main.cpp` : un tableau de bord technique sombre, compact et fonctionnel.

## 1. Principes visuels

- Privilégier la lisibilité des mesures et des états.
- Utiliser un fond presque noir et des panneaux anthracite.
- Réserver les couleurs vives à la sélection, à l'activité et aux états opérationnels.
- Employer des aplats, des coins légèrement arrondis et des alignements réguliers.
- Éviter les ombres, les dégradés et les éléments décoratifs.
- Présenter les diagnostics en lignes de libellés et de valeurs.

## 2. Palette et constantes

Les valeurs RGB565 constituent la référence embarquée. Les équivalents RGB888 sont obtenus par mise à l'échelle des canaux vers 0–255, avec arrondi ; le rendu réel dépend de l'écran.

| Constante | Usage | RGB565 | RGB888 / CSS |
|---|---|---|---|
| `COLOR_BG` | Fond général presque noir | `0x0841` | `#080808` |
| `COLOR_PANEL` | Panneau et onglets inactifs | `0x18E3` | `#191C19` |
| `COLOR_FG` | Texte principal | `0xFFFF` | `#FFFFFF` |
| `COLOR_ACCENT` | Sélection, activité, écoute | `0x2E9F` | `#29D2FF` |
| `COLOR_MUTED` | Libellés et texte secondaire | `0xAD55` | `#ADAAAD` |
| `COLOR_WARN` | Attente, erreurs, indisponibilité | `0xFD20` | `#FFA600` |
| `COLOR_GREEN` | Connexion prête, début de restitution audio | `0x07E0` | `#00FF00` |
| `COLOR_BTN_TTS` | Fond du bouton TEST TTS | `0x1AB2` | `#195592` |
| `COLOR_BTN_STOP` | Fond du bouton STOP AUDIO | `0xA2C3` | `#A55D19` |

Les noms historiques « cyan foncé » et « rouge » présents dans les commentaires du code ne correspondent pas exactement aux valeurs des boutons : conserver les valeurs numériques pour reproduire fidèlement l'interface.

### Constantes C++ pour écran RGB565

```cpp
#include <stdint.h>

namespace VoiceDiagnosticsTheme {
static constexpr uint16_t COLOR_BG       = 0x0841;
static constexpr uint16_t COLOR_PANEL    = 0x18E3;
static constexpr uint16_t COLOR_FG       = 0xFFFF;
static constexpr uint16_t COLOR_ACCENT   = 0x2E9F;
static constexpr uint16_t COLOR_MUTED    = 0xAD55;
static constexpr uint16_t COLOR_WARN     = 0xFD20;
static constexpr uint16_t COLOR_GREEN    = 0x07E0;
static constexpr uint16_t COLOR_BTN_TTS  = 0x1AB2;
static constexpr uint16_t COLOR_BTN_STOP = 0xA2C3;
}
```

### Variables CSS pour une adaptation web

```css
:root {
  --vd-bg: #080808;
  --vd-panel: #191c19;
  --vd-fg: #ffffff;
  --vd-accent: #29d2ff;
  --vd-muted: #adaaad;
  --vd-warn: #ffa600;
  --vd-green: #00ff00;
  --vd-btn-tts: #195592;
  --vd-btn-stop: #a55d19;
  --vd-radius-tab: 4px;
  --vd-radius-panel: 6px;
  --vd-radius-button: 6px;
}
```

## 3. Géométrie de référence

Écran paysage de **320 × 240 px**. Les coordonnées partent du coin supérieur gauche. Les coordonnées des textes désignent le curseur de dessin M5Unified.

| Élément | Position `(x, y)` | Dimensions | Rayon |
|---|---|---|---|
| Titre et version | `(10, 6)` | Selon le texte | — |
| Statut global | `(240, 6)` | Selon le texte | — |
| Onglet numéro `i`, de 0 à 3 | `(6 + 78 × i, 23)` | `74 × 25 px` | `4 px` |
| Texte d'un onglet | `(x de l'onglet + 12, 32)` | Selon le texte | — |
| Panneau central | `(6, 52)` | `308 × 138 px` | `6 px` |
| Bouton TEST TTS | `(6, 194)` | `150 × 40 px` | `6 px` |
| Bouton STOP AUDIO | `(164, 194)` | `150 × 40 px` | `6 px` |
| Texte TEST TTS | `(44, 209)` | Selon le texte | — |
| Texte STOP AUDIO | `(194, 209)` | Selon le texte | — |

### Grille des diagnostics

- Libellés à `x = 14`, valeurs à `x = 152`.
- Première ligne à `y = 58`, puis un pas vertical de `18 px`.
- Sept lignes de mesures : `58, 76, 94, 112, 130, 148, 166`.
- Journal : six événements au maximum, à `x = 14`, avec le même pas vertical.
- Marges extérieures principales de `6 px`, espacement entre onglets de `4 px` et entre boutons de `8 px`.

## 4. Typographie et contenu

- Police embarquée : `fonts::Font0`, avec `setTextSize(1)`.
- Même police et même taille pour le titre, les onglets, les diagnostics et les boutons.
- Hiérarchie obtenue par la position, l'alignement et la couleur.
- Texte principal et valeurs ordinaires en blanc ; libellés en gris atténué.
- Commandes et états courts, généralement en majuscules.
- Libellés français sans accents dans l'interface actuelle ; états techniques anglais conservés.
- Unités explicites à côté des valeurs : `KB`, `MHz`, `ms`, `s`, `dBm`.
- Données indisponibles indiquées par `N/D`.
- Événements précédés d'un temps relatif : `[12s] TTS: Debut restitution`.

Pour un autre environnement, choisir une police bitmap ou monospace compacte et ajuster sa taille à la lisibilité du support. Cette substitution est une adaptation, pas la police originale.

## 5. Composants et états

### Navigation

Quatre onglets persistants : `SYS`, `AUDIO`, `NET`, `EVENTS`. L'onglet actif utilise `COLOR_ACCENT`, les autres `COLOR_PANEL`. Le texte reste blanc dans l'implémentation de référence.

### Statut global

| Statut affiché | Couleur |
|---|---|
| `ONLINE` | `COLOR_GREEN` |
| `BUSY`, `WIFI`, `GEMINI` | `COLOR_ACCENT` |
| `WAIT`, `ERROR` | `COLOR_WARN` |

Associer toujours la couleur à un texte explicite. Dans les diagnostics, les états réseau prêts sont verts, les problèmes orange, l'écoute active cyan et la restitution active verte.

### Actions

- `TEST TTS` : fond `COLOR_BTN_TTS`, texte blanc.
- `STOP AUDIO` : fond `COLOR_BTN_STOP`, texte blanc.
- Les deux actions restent visibles sur chaque onglet.
- L'interface actuelle ne définit pas de variante visuelle pressée ou désactivée. Une action indisponible est signalée dans le journal.

### Journal

Utiliser le blanc pour les événements ordinaires, le gris pour l'initialisation ou l'attente sans événement, le cyan pour l'acquisition micro, le vert pour la disponibilité et le début TTS, et l'orange pour les erreurs ou l'arrêt audio.

## 6. Rafraîchissement et interaction

- Rafraîchissement périodique toutes les `200 ms`, soit environ `5 Hz`.
- Redessin complet à l'initialisation et au changement d'onglet.
- En fonctionnement normal, redessiner le panneau central et le statut global.
- Réagir aux appuis tactiles sur les onglets et les actions.
- Ne pas ajouter d'animation décorative ; donner la priorité à la stabilité de lecture.

## 7. Réutilisation sur d'autres écrans

Les points suivants sont des recommandations d'adaptation :

- Conserver l'ordre : en-tête, navigation, diagnostics, actions.
- Garder la palette sémantique et les alignements libellé/valeur.
- Sur un écran plus grand, augmenter les marges, la taille du texte et les zones tactiles sans étirer simplement un rendu bitmap.
- Sur un écran plus étroit, adapter la grille et gérer les textes longs par retour à la ligne, troncature ou défilement.
- Vérifier le contraste et la lisibilité sur le support cible, en particulier pour le texte blanc sur l'onglet cyan. Une variante à texte sombre peut être prévue pour améliorer le contraste.
- Centraliser les couleurs et les dimensions dans un thème partagé plutôt que de disperser des valeurs littérales dans le code.

## 8. Critères de conformité visuelle

- Fond sombre, panneau anthracite et texte principal blanc.
- Libellés secondaires gris, valeurs alignées et unités visibles.
- Cyan pour la sélection et l'activité, vert pour la disponibilité, orange pour les alertes.
- Coins légèrement arrondis, aplats sans ombre ni dégradé.
- Navigation et commandes persistantes.
- Présentation dense mais régulière, adaptée à un outil de diagnostic.

Source de référence : `examples/VoiceDiagnostics/src/main.cpp`.
