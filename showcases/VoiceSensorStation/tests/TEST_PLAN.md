# Plan de Tests — VoiceSensorStation v0.1.0.1

Ce document distingue strictement :
1. **Les tests unitaires natifs** (exécutables sans matériel CoreS3 ni capteur physique).
2. **Les tests fonctionnels matériels (VSS-01 à VSS-16)** nécessitant un **M5Stack CoreS3** et un capteur **M5Stack ENV Pro U169 (BME688)**.

> **Règle de rigueur** : aucun test natif ou simulé ne doit être présenté comme une validation matérielle.

---

## 1. Tests unitaires automatisables (sans matériel)

Fichier : `tests/test_station_core.cpp`

Couvre la logique métier réelle compilée en C++17 natif :
- Validation des mesures BME688 (`validateSensorSample`, bornes physiques, rejet `NaN`/`Inf`, validation `gasResistanceOhms > 0`).
- Calcul monotone de l'âge des données (`computeSampleAgeMs`) et détection de péremption (`isSampleFresh`).
- Machine à états `SensorManager` (`INITIALIZING`, `READING`, `READY`, `STALE`, `ERROR`, `DISCONNECTED` et reconnexion contrôlée).
- Classification déterministe des 7 commandes environnementales locales vs questions analytiques/interprétatives routées vers l'IA.
- Formatage exact des réponses vocales françaises (virgule décimale, capteur indisponible, mesure périmée).
- Génération du contexte structuré pour OpenAI / Gemini (aucune donnée fictive en cas d'indisponibilité).

---

## 2. Tests fonctionnels matériels (M5Stack CoreS3 + ENV Pro U169)

| ID | Test | Procédure matérielle | Résultat attendu | Statut |
|---|---|---|---|---|
| **VSS-01** | Démarrage normal | Brancher ENV Pro U169 sur Port A (SDA=GPIO2, SCL=GPIO1), alimenter le CoreS3. | Interface LVGL 9 thème clair affichée, BME688 détecté (`0x77`), état `Capteur OK`. | NOT RUN |
| **VSS-02** | Acquisition périodique | Observer l'écran et le moniteur série (115200 bauds) pendant 30 s. | Température, humidité, pression et gaz actualisés toutes les 2 s sans clignotement ni blocage. | NOT RUN |
| **VSS-03** | Absence de capteur | Démarrer sans ENV Pro U169 branché. | Affichage `Capteur indisponible`, valeurs `--`, aucune erreur fatale, interface tactile fluide. | NOT RUN |
| **VSS-04** | Reconnexion capteur | Rebrancher ENV Pro U169 sur Port A après VSS-03 et attendre ≤ 5 s. | Détection automatique, retour à `READY`, mesures réelles affichées. | NOT RUN |
| **VSS-05** | Question température | 1er clic `PARLER` -> « Quelle est la température ? » -> 2e clic `TERMINER`. | Commande exécutée localement (`sensor.get_temperature`), réponse vocale avec la mesure réelle affichée. | NOT RUN |
| **VSS-06** | Question humidité | PTT 2 clics : « Quel est le taux d'humidité ? » | Commande exécutée localement (`sensor.get_humidity`), réponse vocale exacte. | NOT RUN |
| **VSS-07** | Toutes les mesures | PTT 2 clics : « Donne-moi toutes les mesures. » | Réponse cohérente énonçant température, humidité, pression et résistance de gaz réelles. | NOT RUN |
| **VSS-08** | Capture silencieuse | Cliquer `PARLER`, rester silencieux 2 s, cliquer `TERMINER`. | Rejet local par `VoiceCaptureValidator` (VAD), aucune requête envoyée, aucune réponse. | NOT RUN |
| **VSS-09** | Double clic PTT | Effectuer deux clics rapides (< 120 ms) sur `PARLER`. | Anti-rebond actif : une seule capture démarrée, aucun blocage d'état. | NOT RUN |
| **VSS-10** | STOP pendant réponse | Poser une question longue à l'IA, puis cliquer `STOP` pendant la parole. | Interruption audio immédiate, retour à `READY` sans reconnexion forcée ni réactivation tardive. | NOT RUN |
| **VSS-11** | Wi-Fi coupé | Couper le point d'accès Wi-Fi en fonctionnement normal. | Indicateur `Hors ligne`, mesures BME688 et affichage continuent toutes les 2 s. | NOT RUN |
| **VSS-12** | Reconnexion Wi-Fi | Rétablir le point d'accès Wi-Fi. | Reconnexion automatique et retour de l'assistant vocal à `READY`. | NOT RUN |
| **VSS-13** | Changement IA | Ouvrir `RÉGLAGES`, basculer `Gemini` <-> `OpenAI` (avec les deux clés configurées). | Fermeture propre session précédente, connexion contrôlée au nouveau fournisseur, pas de blocage `CONNECTING`. | NOT RUN |
| **VSS-14** | Conversation prolongée | Enchaîner 5 questions analytiques (ex. confort, humidité, BME688) sur plusieurs minutes. | Interface tactile réactive, mesures rafraîchies pendant la parole, heap/PSRAM stables. | NOT RUN |
| **VSS-15** | Paramètres hors ligne | Sans Wi-Fi, cliquer sur `RÉGLAGES`, ajuster le volume, ouvrir `Diagnostics`. | Écran réglages et diagnostics immédiatement accessibles et fonctionnels. | NOT RUN |
| **VSS-16** | Mesure périmée | Débrancher le capteur après une lecture valide, attendre > 10 s, demander la température. | Aucune valeur périmée annoncée comme actuelle (« Capteur indisponible » ou « Je n'ai pas de mesure récente... »). | NOT RUN |

---

## 3. Scénarios matériels de bascule fournisseur IA (v0.1.0.1 — SW-01 à SW-10)

| ID | Test | Attendu | Statut |
|---|---|---|---|
| **SW-01** | Démarrage direct OpenAI | READY | NOT RUN |
| **SW-02** | OpenAI → Gemini | Gemini READY | NOT RUN |
| **SW-03** | Question température avec Gemini | Réponse correcte | NOT RUN |
| **SW-04** | Gemini → OpenAI | OpenAI READY | NOT RUN |
| **SW-05** | Trois bascules consécutives | Aucun blocage | NOT RUN |
| **SW-06** | STOP après bascule | Interruption correcte | NOT RUN |
| **SW-07** | Clé Gemini absente | Erreur contrôlée | NOT RUN |
| **SW-08** | Connexion Gemini impossible | Récupération contrôlée | NOT RUN |
| **SW-09** | Capteur pendant bascule | Acquisition continue | NOT RUN |
| **SW-10** | PTT après bascule | Conversation normale | NOT RUN |
