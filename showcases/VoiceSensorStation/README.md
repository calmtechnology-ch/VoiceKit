# VoiceSensorStation v0.1.0

**Station environnementale vocale autonome** pour **M5Stack CoreS3** et capteur **M5Stack ENV Pro U169 (Bosch BME688)**, construite sur la bibliothèque **VoiceKit v0.6.7.1**.

---

## 1. Présentation

**VoiceSensorStation v0.1.0** mesure et affiche en temps réel quatre paramètres environnementaux (température, humidité relative, pression atmosphérique et résistance de gaz brute), tout en permettant :
- d'interroger les mesures à la voix en français naturel ;
- de poser des questions analytiques et explicatives à **Google Gemini Live** ou **OpenAI Realtime** avec injection des mesures réelles ;
- de continuer à mesurer, afficher et diagnostiquer le capteur même **sans connexion Internet ni service IA**.

La bibliothèque `VoiceKit/src/` n'est pas modifiée : l'application utilise exclusivement ses API publiques (`VoxCoreVoice`, `VoiceInteractionController`, `VoiceCaptureValidator`, `ToolRegistry`, `VoiceCommandDispatcher`, `VoiceProfileManager`).

---

## 2. Matériel nécessaire

- **M5Stack CoreS3** (ESP32-S3, 16 Mo Flash, 8 Mo PSRAM, écran tactile 320 × 240, microphones ES7210, haut-parleur AW88298, Wi-Fi).
- **M5Stack Unit ENV Pro (SKU : U169)** équipé du capteur **Bosch BME688**.
- Câble Grove (4 broches) pour connexion sur le **Port A** du CoreS3.
- Câble USB-C pour programmation et diagnostics série.

---

## 3. Câblage du ENV Pro U169

Connecter le module **ENV Pro U169** sur le **Port A (I²C externe, connecteur rouge)** du M5Stack CoreS3 :

| Signal Grove | Broche CoreS3 (Port A) | Bus |
|---|---|---|
| **GND** (noir) | GND | Masse |
| **5V** (rouge) | 5V | Alimentation |
| **SDA** (blanc) | **GPIO 2** | I²C externe (`Wire`, 100 kHz) |
| **SCL** (jaune) | **GPIO 1** | I²C externe (`Wire`, 100 kHz) |

- Adresse I²C sondée automatiquement : **`0x77`** (adresse par défaut du ENV Pro U169), avec repli automatique sur **`0x76`**.
- Le bus I²C interne du CoreS3 (`Wire1`, GPIO 12 / GPIO 11, utilisé par AXP2101, audio ES7210/AW88298 et tactile) n'est jamais réinitialisé ni perturbé.

---

## 4. Fonctionnement des mesures

- **Acquisition non bloquante** toutes les **2 secondes** via le mode forcé asynchrone (`beginReading()` / `endReading()`), sans `delay()` long dans la boucle principale.
- **Validation physique** de chaque échantillon (bornes BME688 et rejet des valeurs `NaN`/`Inf`).
- **Instantané cohérent** (`SensorData`) horodaté de manière monotone (`timestampMs`) : aucune donnée partiellement mise à jour n'est exposée.
- **États explicites** (`SensorState`) : `INITIALIZING`, `READY`, `READING`, `STALE` (> 10 s sans mesure fraîche), `ERROR`, `DISCONNECTED`.
- **Reconnexion automatique contrôlée** toutes les 3 secondes si le capteur est débranché puis rebranché.

> **Précision scientifique sur le gaz** : la résistance de gaz brute du BME688 (exprimée en `kOhm`) est une grandeur électrique sensible à la présence de composés organiques volatils. Elle n'est **pas** présentée comme une concentration de CO₂, de COV en ppm ni comme un indice IAQ certifié. Bosch BSEC n'est pas intégré dans la v0.1.0.

---

## 5. Description de l'interface (LVGL 9)

Interface **thème clair** optimisée pour l'écran 320 × 240 avec polices françaises accentuées (`font_french_12`, `font_french_14`, `font_french_bold_16`) et mise à jour différentielle sans clignotement :

- **Écran principal** :
  - Barre supérieure : titre `VoiceSensorStation`, statut `WiFi OK` / `Hors ligne`, horloge `HH:MM` (si NTP synchronisé).
  - Grille 2 × 2 : `TEMPÉRATURE (°C)`, `HUMIDITÉ (%)`, `PRESSION (hPa)`, `GAZ (BME688) (kOhm)`.
  - Barre d'état : état capteur (`Capteur OK`, `Mesure anc.`, `Capteur indisponible`) et état vocal (`Assistant READY`, `Écoute...`, `Traitement...`, `Parole...`, `Hors ligne`).
  - Actions tactiles : `[ PARLER ]` (devient `[ TERMINER ]` pendant l'écoute), `[ STOP ]` (arrêt prioritaire), `[ RÉGLAGES ]` (toujours accessible, même hors ligne).
- **Écran Réglages** :
  - Choix du fournisseur IA (`Gemini` / `OpenAI`), langue (`fr-FR` par défaut), voix compatible, réglage local du volume (0–100 %), sauvegarde du profil en NVS (sans clés API), résumé capteur et accès aux **Diagnostics**.
- **Écran Diagnostics** :
  - État capteur, adresse I²C, âge des mesures, compteurs d'erreurs, fournisseur IA, état PTT, Wi-Fi, mémoire Heap/PSRAM et uptime.

---

## 6. Utilisation des commandes vocales (PTT à 2 clics)

1. **1er clic sur `PARLER`** : ouverture du microphone (`Écoute...`), le bouton passe en rouge `TERMINER`.
2. **Formulez votre demande** puis effectuez le **2e clic sur `TERMINER`** :
   - Validation locale de présence de parole (`VoiceCaptureValidator` / VAD). Les silences et bruits brefs sont rejetés localement sans solliciter le serveur.
3. **Bouton `STOP`** : interrompt immédiatement la capture ou la synthèse vocale et ramène le système à un état cohérent sans reconnexion forcée.

### Exemples de commandes environnementales locales

- « Quelle est la température ? »
- « Quel est le taux d'humidité ? »
- « Quelle est la pression atmosphérique ? »
- « Quelle est la résistance de gaz ? »
- « Donne-moi toutes les mesures. »
- « Est-ce que le capteur fonctionne ? »
- « De quand datent les dernières mesures ? »

### Exemples de questions analytiques (OpenAI / Gemini)

- « Comment interpréter l'humidité actuelle ? »
- « Est-ce que l'air est trop sec ? »
- « Pourquoi la pression atmosphérique change-t-elle ? »
- « Explique-moi le fonctionnement du BME688. »
- « Est-ce que les conditions de la pièce sont confortables ? »

---

## 7. Différence entre commandes locales et traitement IA

| Aspect | Commandes environnementales locales | Questions analytiques / conversation |
|---|---|---|
| **Reconnaissance vocale (STT)** | Via VoiceKit (Gemini Live / OpenAI Realtime) | Via VoiceKit (Gemini Live / OpenAI Realtime) |
| **Décision et exécution** | 100 % locale (`ToolRegistry` + `VoiceCommandDispatcher` sur `SensorManager`) | Envoi au fournisseur IA (`askAi`) avec contexte capteur réel injecté |
| **Fiabilité des chiffres** | Valeurs exactes du BME688 formatées localement, zéro hallucination | Prompt système strict interdisant d'inventer des valeurs |
| **Restitution vocale (TTS)** | Synthèse mot à mot (`say()`) | Réponse audio naturelle en français (1 à 2 phrases) |

> **Transparence hors ligne** : aucun moteur STT hors ligne n'est embarqué dans la v0.1.0. Sans Internet, l'acquisition BME688, l'affichage temps réel, les réglages locaux et les diagnostics restent pleinement opérationnels, tandis que l'interaction vocale en ligne est mise en attente.

---

## 8. Installation PlatformIO

Depuis la racine du dépôt VoiceKit :

```bash
cd showcases/VoiceSensorStation
pio run -e m5stack-cores3
```

Pour téléverser sur un M5Stack CoreS3 connecté en USB-C :

```bash
pio run -e m5stack-cores3 -t upload
pio device monitor -b 115200
```

---

## 9. Configuration Wi-Fi et clés API

1. Copier `include/secrets.example.h` vers `include/secrets.h`.
2. Renseigner vos identifiants Wi-Fi et au moins une clé API (`GEMINI_API_KEY` et/ou `OPENAI_API_KEY`).
3. `include/secrets.h` est ignoré par `.gitignore` et ne doit **jamais** être commité dans Git.

---

## 10. Diagnostics et dépannage

Un rapport lisible est émis toutes les 5 secondes sur le port série (`115200` bauds) : état du capteur, valeurs réelles, âge de l'échantillon, compteurs d'erreurs I²C, état vocal/PTT, mémoire libre (Heap/PSRAM), uptime et durées mesurées (`Sensor read`, `LVGL update`, `Voice update`).

- **Capteur indisponible** : vérifier le branchement sur le **Port A (rouge)** du CoreS3 et la présence d'un ENV Pro U169 (adresse `0x77` ou `0x76`).
- **Hors ligne** : vérifier `WIFI_SSID` / `WIFI_PASSWORD` dans `include/secrets.h`.
- **Erreur IA** : vérifier la clé API du fournisseur actif dans `include/secrets.h`.

---

## 11. Limites connues de la v0.1.0

- Pas de reconnaissance vocale (STT) ni de synthèse vocale (TTS) 100 % hors ligne.
- Pas d'intégration Bosch BSEC ni de calcul d'indice IAQ / CO₂ équivalent.
- Validation matérielle sur CoreS3 réel + ENV Pro U169 à exécuter selon `tests/TEST_PLAN.md` (tests VSS-01 à VSS-16).

---

## 12. Procédure de tests matériels

Consulter le plan détaillé dans [tests/TEST_PLAN.md](tests/TEST_PLAN.md) :
- Exécuter les tests unitaires natifs (`tests/test_station_core.cpp`) pour vérifier la logique capteur, fraîcheur et commandes locales.
- Téléverser sur le CoreS3 avec le ENV Pro U169 branché sur le Port A et dérouler les 16 scénarios matériels **VSS-01 à VSS-16**.
