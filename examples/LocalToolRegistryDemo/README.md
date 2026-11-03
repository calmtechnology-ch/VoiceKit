# LocalToolRegistryDemo — VoiceKit v0.6.4

Démonstration du **registre local d'outils (Local Tool Registry)** sur M5Stack CoreS3.

Cet exemple fonctionne **intégralement hors ligne**, sans connexion Wi-Fi et sans clé API Gemini / OpenAI.

## Objectif

Illustrer l'infrastructure logicielle permettant aux applications embarquées ESP32 d'enregistrer, découvrir, valider et exécuter des commandes locales avec politique de sécurité et gestion d'événements.

Cette brique constitue le socle pour les futurs appels d'outils IA et une éventuelle passerelle MCP.

## Commandes Enregistrées

| Commande | Catégorie | Paramètres | Description & Action Matérielle |
|---|---|---|---|
| `system.get_status` | `READ_ONLY` | Aucun | Retourne en JSON l'état matériel du CoreS3 (batterie, mémoire heap libre, uptime, volume, luminosité). |
| `audio.set_volume` | `WRITE` | `volume` (INT, 0-255, requis) | Modifie le volume matériel du haut-parleur CoreS3 via `M5.Speaker.setVolume()`. |
| `display.set_brightness` | `WRITE` | `brightness` (INT, 0-255, requis) | Modifie la luminosité matérielle de l'écran LCD via `M5.Display.setBrightness()`. |
| `system.factory_reset` | `SENSITIVE` | Aucun | Commande sensible requérant une confirmation explicite par jeton avec protection anti-rejeu et expiration. |

## Utilisation

1. Ouvrir le projet dans PlatformIO :
   ```bash
   pio run -d examples/LocalToolRegistryDemo -t upload
   ```
2. Ouvrir le moniteur série à 115200 bauds :
   ```bash
   pio device monitor -d examples/LocalToolRegistryDemo -b 115200
   ```
3. Utiliser le menu interactif :
   - `1` : Découvrir les commandes déclarées (noms, types, bornes, descriptions).
   - `2` : Exécuter `system.get_status`.
   - `3` : Exécuter `audio.set_volume` (volume réglé à 180 avec bip de confirmation).
   - `4` : Exécuter `display.set_brightness` (luminosité réglée à 200).
   - `5` : Tester le rejet de validation sur paramètre manquant.
   - `6` : Tester le rejet de validation sur valeur hors limites (brightness=350 > max 255).
   - `7` : Déclencher la commande sensible `system.factory_reset` (obtention d'un jeton `CONF-xxx`).
   - `8` : Confirmer l'exécution de la commande sensible via son jeton.
   - `9` : Tenter de réutiliser le même jeton (refus immédiat avec protection anti-rejeu).

