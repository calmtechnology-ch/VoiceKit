# LocalToolRegistryDemo — VoiceKit v0.6.5.2

Exemple officiel de démonstration hors ligne du **registre local d'outils sécurisé (`ToolRegistry`)** sur **M5Stack CoreS3**.

---

## 1. Objectif

Démontrer et valider l'infrastructure d'enregistrement, de validation de typage, de politiques de sécurité (`READ_ONLY`, `WRITE`, `SENSITIVE`) et d'exécution déterministe d'outils matériels locaux, sans recourir au Wi-Fi ni à une IA.
Cet exemple montre comment `ToolRegistry` protège l'ESP32 contre les arguments invalides, les doubles exécutions et le rejeu de commandes sensibles.

---

## 2. Fonctionnalités

- **100 % Hors ligne** : Aucune dépendance réseau ni clé API nécessaire.
- **Typage fort et validation des arguments** : Vérification des types (`INT`, `FLOAT`, `STRING`, `BOOL`), des plages de valeurs (bornes min/max) et des paramètres obligatoires.
- **Politiques de sécurité strictes** :
  - `READ_ONLY` : Consultation d'état sans effet de bord (`system.get_status`).
  - `WRITE` : Modification matérielle autorisée avec validation stricte (`audio.set_volume`, `display.set_brightness`).
  - `SENSITIVE` : Action critique requérant l'émission d'un jeton à usage unique et une confirmation explicite (`system.factory_reset`).
- **Protection anti-rejeu et expiration** : Un jeton de confirmation ne peut être utilisé qu'une seule fois.
- **Sécurité des tests** : Aucune véritable réinitialisation d'usine n'est exécutée (action simulée et traçable).
- **Interface double** : Affichage graphique sur écran CoreS3 (320 × 240) et menu interactif série 115 200 baud.

---

## 3. Matériel requis

- **Carte** : M5Stack CoreS3 (ESP32-S3).
- **Affichage** : Écran LCD et contrôleur de luminosité interne.
- **Audio** : Haut-parleur et amplificateur AW88298 pour les bips de validation.
- **Alimentation** : USB-C standard.

---

## 4. Configuration

Aucune configuration d'identifiants n'est nécessaire. L'exemple est immédiatement prêt à l'emploi.

---

## 5. Compilation

Compiler avec PlatformIO :
```bash
pio run -d examples/LocalToolRegistryDemo
```

---

## 6. Téléversement

1. Téléverser sur la carte :
   ```bash
   pio run -d examples/LocalToolRegistryDemo -t upload
   ```
2. Ouvrir le moniteur série :
   ```bash
   pio device monitor -d examples/LocalToolRegistryDemo -b 115200
   ```

---

## 7. Mode d'utilisation

L'écran LCD affiche l'état matériel en direct (batterie, heap, volume, luminosité, dernier événement).
Le moniteur série propose un menu interactif numéroté :
- Tapez `1` : Découverte et inspection du schéma de tous les outils déclarés.
- Tapez `2` : Lecture de l'état système via `system.get_status`.
- Tapez `3` : Modification du volume à 180/255 (avec bip sonore de confirmation).
- Tapez `4` : Modification de la luminosité de l'écran à 200/255.
- Tapez `5` : Test de rejet sur paramètre obligatoire manquant.
- Tapez `6` : Test de rejet sur argument hors bornes (`brightness=350`).
- Tapez `7` : Déclenchement de la commande sensible (génération du jeton `CONF-xxx`).
- Tapez `8` : Confirmation effective de la commande sensible via son jeton.
- Tapez `9` : Tentative de réutilisation du même jeton (rejet immédiat).

---

## 8. Tests à effectuer

| # | Test | Procédure | Résultat attendu |
|---|---|---|---|
| 1 | Découverte | Touche `1` | Liste JSON/texte des outils avec types, bornes et politiques. |
| 2 | Lecture état | Touche `2` | JSON valide renvoyé contenant la batterie, la RAM libre et l'uptime. |
| 3 | Écriture valide | Touches `3` et `4` | Changement physique effectif du son et de la luminosité écran. |
| 4 | Rejet paramètre | Touches `5` et `6` | Erreur explicite retournée, aucun effet de bord matériel. |
| 5 | Action sensible | Touche `7` puis `8` | Jeton généré, exécution validée uniquement après confirmation. |
| 6 | Anti-rejeu | Touche `9` après le test #5 | Rejet catégorique avec notification de jeton déjà consommé. |

---

## 9. Résultats attendus

- Traitement déterministe synchrone (< 1 ms par évaluation).
- Aucun crash en cas d'arguments corrompus ou hors plage.
- Traçabilité complète des événements sur la console série et sur l'écran.

---

## 10. Limitations connues

- Les actions matérielles sont adaptées à la carte M5Stack CoreS3.
- `ToolRegistry` gère la validation et l'exécution locale ; le routage vocal en amont est confié à `VoiceCommandDispatcher` ou à l'orchestrateur `IntegratedVoicePipeline`.
