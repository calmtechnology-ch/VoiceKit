# OpenAIConversationLVGL v0.1.0.1 — Modern Voice UI avec LVGL 9 sur M5Stack CoreS3

Exemple autonome de référence pour **VoiceKit** combinant le modèle conversationnel haute fidélité **OpenAI Realtime (`gpt-realtime-2.1-mini`)** et une interface tactile dark mode moderne propulsée nativement par **LVGL 9** sur microcontrôleur **ESP32-S3 (M5Stack CoreS3)**.

> [!NOTE]
> La version **v0.1.0.1** apporte les correctifs critiques de déblocage de l'état `PROCESSING`, l'instrumentation des événements OpenAI Realtime, la machine d'états PTT durcie et l'élimination des allocations mémoire dans les sections critiques FreeRTOS.

---

## 1. Caractéristiques Principales

- **Interface Dark Mode Premium LVGL 9** : Remplacement intégral du rendu graphique M5GFX immédiat par un graphe de scène objet LVGL 9 (orbe lumineux animé, en-tête d'état, cartes de transcription et bouton tactile Push-to-Talk).
- **Typographie Française Native** : Polices Segoe UI personnalisées (`font_french_12`, `font_french_14`, `font_french_bold_16`) intégrant tous les caractères accentués (`é è ê ë à â ç î ï ô ù û ü É È Ê À Ç`), les ligatures (`œ Œ`) et la ponctuation française (`« » ’ … — €`).
- **Gestionnaire Push-to-Talk (PTT) Découplé & Sécurisé** : Machine à états finis explicite (`DISABLED`, `READY`, `STARTING`, `LISTENING`, `STOPPING_CAPTURE`, `PROCESSING`, `SPEAKING`, `INTERRUPTING`, `RECOVERING`, `ERROR_STATE`) avec autorisations strictes de l'état `READY` (`isReady() && !isBusy()`), watchdogs de récupération non bloquants et absorption des appuis concurrents/rapides.
- **Support Robuste OpenAI Realtime** : Journalisation non-sensible des événements (`response.created`, `response.output_item.added`, `response.audio.delta`), gestion complète de `response.done` avec extraction des statuts (`completed`, `cancelled`, `failed`) et détails d'erreur, et reprise automatique non-bloquante lors de réponses sans flux audio.
- **Synchronisation Sûre Inter-Threads sans Heap Allocation** : Échange d'événements et de transcriptions entre les callbacks VoiceKit et le thread d'affichage LVGL via buffers statiques bornés (512 octets) et sections critiques FreeRTOS légères (zéro allocation dynamique `String`), avec préservation de l'intégrité UTF-8 multi-octets.
- **Orbe Vocal Animé Réactif** : Composant `VoiceOrb` concentrique en cercles LVGL avec 5 états visuels (respiration cyan en repos, pulsation corail + niveau RMS en écoute, rotation violette en analyse, ondes turquoise en réponse, rouge alerte en erreur).
- **Ordonnancement Coopératif Non Bloquant** : Exécution conjointe de `voice.update()`, `ptt.update()` et `lv_timer_handler()` dans la boucle principale sur le Core 1 sans créer de tâche FreeRTOS dédiée à l'affichage.
- **Empreinte Mémoire Maîtrisée** : Tampon partiel RGB565 de 20 lignes (320 × 20 = 12,8 Ko) et pool dynamique LVGL de 32 Ko, préservant plus de 100 Ko de DRAM interne pour le handshake TLS/WebSocket mbedTLS.

---

## 2. Environnement Matériel et Logiciel

| Composant | Spécification |
|---|---|
| **Matériel** | M5Stack CoreS3 (ESP32-S3 dual-core LX7 @ 240 MHz) |
| **Mémoire** | 512 Ko SRAM interne + 8 Mo PSRAM Octal SPI |
| **Écran** | 320 × 240 LCD IPS (pilote ILI9342C via M5GFX) |
| **Tactile** | Écran capacitif FT6336U multi-point |
| **Audio Entrée** | Codec I2S ES7210 (double microphone numérique) |
| **Audio Sortie** | Amplificateur I2S AW88298 (haut-parleur interne) |
| **Framework** | Arduino ESP32 (`platform-espressif32 @ 6.9.0` / framework v2.0.17) |
| **Bibliothèque GUI** | LVGL v9.6.0 (`lvgl/lvgl@^9.2.2`) |
| **Pilotes Système** | `m5stack/M5Unified@0.2.23` + `m5stack/M5CoreS3@1.0.0` |
| **Moteur Vocal** | VoxCore VoiceKit v0.6.3.1 (fournisseur OpenAI Realtime) |
| **Modèle IA** | `gpt-realtime-2.1-mini` via WebSocket TLS sécurisé |

---

## 3. Structure du Projet

```text
examples/OpenAIConversationLVGL/
├── include/
│   ├── AppConfig.h          # Constantes géométriques, couleurs, prompt et timeouts (v0.1.0.1)
│   ├── Diagnostics.h        # Instrumentation et métriques de performance
│   ├── FrenchFonts.h        # Déclarations des polices LVGL Segoe UI françaises
│   ├── lv_conf.h            # Configuration dédiée LVGL 9 (RGB565, heap, widgets)
│   ├── PTTController.h      # Machine à états finis PTT et watchdogs anti-blocage
│   ├── secrets.example.h    # Modèle de configuration des clés API et Wi-Fi
│   ├── VoiceOrb.h           # Définition du composant d'animation centrale
│   └── VoiceUI.h            # Gestionnaire des widgets LVGL et pilotes M5GFX/Touch
├── src/
│   ├── Diagnostics.cpp      # Calcul des moyennes, pics et journaux périodiques
│   ├── font_french_12.c     # Police Segoe UI 12 pt avec glyphes étendus
│   ├── font_french_14.c     # Police Segoe UI 14 pt avec glyphes étendus
│   ├── font_french_bold_16.c# Police Segoe UI Bold 16 pt pour titres et boutons
│   ├── main.cpp             # Boucle non-bloquante, callbacks sûrs et synchronisation UI
│   ├── PTTController.cpp    # Traitement des intentions tactiles/série et reprises
│   ├── VoiceOrb.cpp         # Logique d'animation et variations d'échelle/couleur
│   └── VoiceUI.cpp          # Construction de l'interface et callbacks LVGL (flush SPI)
├── platformio.ini           # Configuration PlatformIO pour M5Stack CoreS3
├── README.md                # Ce document (actualisé v0.1.0.1)
└── RAPPORT_v0.1.0.1.md      # Rapport complet d'ingénierie et de validation v0.1.0.1
```

---

## 4. Configuration et Compilation

### 4.1. Configuration des Identifiants (`secrets.h`)

Créez le fichier `include/secrets.h` à partir du modèle :

```bash
cp include/secrets.example.h include/secrets.h
```

Renseignez votre clé API OpenAI Realtime et vos paramètres Wi-Fi :

```cpp
#pragma once

#define OPENAI_API_KEY  "sk-proj-votre-cle-api-securisee"
#define WIFI_SSID       "Votre_Reseau_WiFi"
#define WIFI_PASSWORD   "Votre_Mot_De_Passe"
```

> [!CAUTION]
> Ne commitez jamais votre fichier `secrets.h`. Il est exclu par défaut par le `.gitignore`.

### 4.2. Compilation et Téléversement

Avec PlatformIO CLI :

```powershell
# Compilation du firmware
pio run -d examples/OpenAIConversationLVGL

# Téléversement sur le CoreS3 (remplacer COM4 par votre port)
pio run -d examples/OpenAIConversationLVGL -t upload --upload-port COM4

# Ouverture du moniteur série
pio device monitor -p COM4 -b 115200
```

---

## 5. Fonctionnement de l'Interface

### 5.1. Disposition de l'Écran (320 × 240)

- **Y: 0–30 (En-tête)** : Titre de l'application, icône de connexion Wi-Fi (avec signal RSSI en dBm) et badge d'état moteur.
- **Y: 35–108 (Orbe Vocal)** : Composant graphique animé réagissant aux transitions d'états et au niveau RMS réel en écoute.
- **Y: 110–132 (Message d'état)** : Instruction utilisateur claire (`Appuyez pour parler`, `Écoute en cours...`, etc.).
- **Y: 136–164 (Carte Vous)** : Transcription Whisper de l'utilisateur avec étiquette verte et défilement automatique.
- **Y: 168–196 (Carte Assistant)** : Transcription de la réponse OpenAI en bleu cyan avec saut de ligne fluide.
- **Y: 200–235 (Bouton Principal)** : Large zone d'interaction tactile Push-to-Talk (PARLER / STOP).

### 5.2. Logique Push-to-Talk

| État Système | Action Utilisateur | Effet |
|---|---|---|
| **PRÊT** | Appui maintenu sur le bouton | Passage à `STARTING`, démarrage capture micro ES7210 |
| **ÉCOUTE** | Relâchement du bouton | Fin de capture, transmission `activityEnd` à OpenAI |
| **ANALYSE / PARLE** | Appui sur le bouton | Interruption prioritaire STOP immédiate, coupure DAC |
| **ERREUR** | Appui sur le bouton | Récupération contrôlée non-bloquante vers `READY` |

### 5.3. Commandes de Test Série

Le firmware répond aux commandes transmises sur le port série USB (115200 baud) :
- `P` ou `T` : Simulation d'appui sur le bouton PARLER.
- `R` ou `U` : Simulation de relâchement du bouton.
- `S` : Interruption prioritaire STOP immédiate.
- `D` : Affichage d'un rapport complet de diagnostics mémoire et latence.
- `E` : Injection d'une erreur simulée pour tester la récupération.

---

## 6. Architecture Technique et Arbitrages

### 6.1. Rendu LVGL 9 et Réalité du Transfert SPI

L'affichage utilise un tampon partiel de 20 lignes :
$$320 \times 20 \times 2\text{ octets (RGB565)} = 12\,800\text{ octets}$$
Les transferts vers l'écran sont délégués à `M5.Display.pushImage()`.
> [!NOTE]
> Dans M5GFX sur CoreS3, `pushImage()` effectue un transfert SPI **synchrone** (bloquant pour la durée du transfert, typiquement 2 à 20 ms selon la surface de la zone modifiée). La notification `lv_display_flush_ready(disp)` est appelée immédiatement après la fin du transfert synchrone. L'implémentation n'effectue aucun double buffering DMA asynchrone non-bloquant.

### 6.2. Cadence Maîtrisée et Priorité Audio

VoiceKit conserve une priorité absolue sur les cycles CPU :
- `voice.update()` s'exécute à chaque tour de boucle avec un temps d'exécution typique de **35 à 60 µs**.
- `lv_timer_handler()` est appelé périodiquement toutes les 15 à 30 ms (temps moyen : **80 à 120 µs**, max 42,6 ms).
- En cas de charge serveur exceptionnelle, un mécanisme d'auto-dégradation réduit temporairement le rafraîchissement graphique de 20 fps à 5 fps si `voice.update()` dépasse 5 ms.

### 6.3. Sécurité Mémoire et Concurrence

- Aucune allocation dynamique `String` ou `malloc` dans les sections critiques FreeRTOS.
- Utilisation de tampons statiques protégés par `portMUX_TYPE` pour transférer les transcriptions et messages vers `loop()`.
- Toutes les mutations de widgets LVGL s'exécutent strictement dans le thread UI de la boucle principale `loop()`.

---

## 7. Rapport de Validation et Non-Régression

Consulter le document dédié [RAPPORT_v0.1.0.1.md](file:///c:/Users/phall/Documents/Antigravity-5/VoiceKit/examples/OpenAIConversationLVGL/RAPPORT_v0.1.0.1.md) pour les détails complets de validation, incluant :
- 112 suites de tests logiciels réussies (2 210 assertions, 0 échec).
- Compilation réussie sous PlatformIO CoreS3 (Flash: 94.8%, RAM: 38.6%).
- Validation de non-régression de l'exemple M5GFX `OpenAIConversation`.
- Procédure de tests matériels réels pour M5Stack CoreS3.
