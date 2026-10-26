# VoiceDiagnostics — VoiceKit v0.5.3.1

Exemple officiel de **diagnostic, télémétrie et visualisation** pour la bibliothèque **VoiceKit v0.5.3.1** sur **M5Stack CoreS3**.

Cette application fournit un tableau de bord tactile fluide et léger permettant d'inspecter l'état du moteur vocal, les flux audio (RMS micro, états STT/TTS), les métriques réseau (Wi-Fi, session Gemini Live), les **temps de démarrage et de connexion réels (Fast Connection v0.5.2)**, les **métriques de latence vocale instrumentées (7 jalons, 6 durées dérivées v0.5.3)**, l'utilisation mémoire (RAM, PSRAM) et le journal horodaté des événements.

---

## 1. Fonctionnalités et Interface Tactile (320 × 240)

L'interface est construite avec **M5Unified** (sans surcharge ni dépendance LVGL) et s'articule autour de 6 onglets tactiles :

### 📊 Onglet `SYS` (Système)
- **État VoiceKit** : État opérationnel en temps réel (`READY`, `SPEAKING`, `LISTENING`, etc.).
- **Mode vocal** : Mode fonctionnel actif (`IDLE`, `CONTINUOUS`, `CONVERSATION`).
- **RAM libre / min** : Mémoire heap interne disponible et minimum libre observé.
- **PSRAM libre** : Mémoire étendue disponible (8 Mo sur CoreS3).
- **Uptime** : Durée de fonctionnement depuis le démarrage (en secondes).
- **ESP32 CPU** : Fréquence du processeur (240 MHz).
- **Puce & Flash** : Modèle de SoC ESP32-S3 et taille de la mémoire flash.

### 🎙️ Onglet `AUDIO` (Audio & Acoustique)
- **Microphone RMS** : Niveau sonore d'acquisition micro en temps réel exposé par VoxCore.
- **Capture STT** : État de la dictée vocale (`INACTIF`, `EN COURS`, `TRANSCRIPTION`).
- **Restitution TTS** : État du haut-parleur (`INACTIF`, `EN COURS`, `OCCUPE`).
- **Compteurs audio** : Débuts et fins de synthèse TTS, débuts et fins d'écoute micro, transcriptions finales reçues.
- **Métriques conversation** : Latence du premier bloc audio et perte d'échantillons du dernier tour conversationnel (affiché `N/D` si aucun tour n'a eu lieu).

### ⏱️ Onglet `LAT` (Latence Vocale & Seuils VAD — v0.5.3 / v0.5.3.1)
- **Latence rep. perçue** : Latence perçue par l'utilisateur (`getResponseLatencyMs()`), mesurée depuis la fin de parole VAD (ou relâchement bouton en mode manuel) jusqu'au premier échantillon remis au DMA. Affiché en vert (`ms`) ou `N/D`.
- **Délai Gemini 1erPCM** : Temps d'aller-retour réseau et traitement Gemini (`getActivityEndToFirstAudioMs()`) entre l'envoi de `activityEnd` et la réception du 1er paquet PCM 24 kHz.
- **Délai local -> DMA** : Délai local de décodage et soumission DMA (`getFirstAudioToPlaybackMs()`).
- **Durée capture micro** : Durée de capture effective de la parole de l'utilisateur (`s_lastMicCaptureMs`).
- **Tours mes./incomplet** : Compteur de tours complets mesurés versus tours interrompus ou incomplets (sans altérer les statistiques globales).
- **Min / Moy / Max** : Statistiques glissantes de latence perçue sur la session (sur tampon fixe de 64 échantillons).
- **Médiane / Seuil VAD** : Médiane glissante de session et seuil VAD actuellement actif.
- **Contrôle interactif du seuil VAD** : Un appui tactile sur la dernière ligne permet de basculer à chaud entre les 3 seuils recommandés : **800 ms** (commandes courtes), **1 000 ms** (usage équilibré) et **1 200 ms** (parole naturelle, valeur par défaut).

### 🌐 Onglet `NET` (Réseau & Gemini Live)
- **Connexion Wi-Fi** : Statut de liaison (`CONNECTE` ou `DECONNECTE`).
- **Adresse IP** : Adresse IP locale attribuée par le routeur.
- **Signal RSSI** : Puissance du signal Wi-Fi en dBm.
- **Session Gemini** : État de la liaison WebSocket sécurisée TLS (`PRETE`, `CONNEXION...`, `NON PRETE`).
- **Reconnexions** : Nombre de reconnexions réussies observées.
- **Erreurs observées** : Compteur d'erreurs de transport ou protocole.
- **Dernière erreur** : Intitulé abrégé du dernier incident survenu.

### ⚡ Onglet `CONN` (Fast Connection — Temps Réels v0.5.2)
- **WiFi -> IP** : Durée exacte de connexion Wi-Fi jusqu'à obtention de l'IP (avec indicateur `(FAST)` si connexion directe par BSSID/canal).
- **SNTP -> Horloge** : Durée de synchronisation NTP jusqu'à heure valide (ou `0 ms (IMMEDIAT)` si horloge déjà valide en redémarrage chaud/RTC).
- **WS connecté** : Temps d'établissement du transport WebSocket TLS vers l'API Gemini Live.
- **Setup complete** : Temps d'échange de la configuration initiale Gemini jusqu'à réception de `setupComplete`.
- **TOTAL -> READY** : Durée globale du démarrage jusqu'à l'état `VoiceKit::isReady()`.
- **Essais WiFi/Gem** : Compteur de tentatives et échecs éventuels.
- **FastConnect** : État du mécanisme rapide (`ACTIF ch<N>`, `REPLI SCAN`, `INITIAL`) et BSSID mémorisé en NVS.

### 📝 Onglet `EVT` (Journal des Événements)
- **Historique circulaire** : Affichage chronologique des 6 derniers événements vocaux horodatés (transitions d'état, début/fin TTS, début/fin micro, réceptions de transcription STT, erreurs).
- **Zéro allocation dynamique** : Tampon circulaire statique en mémoire fixe.

### 🔘 Boutons de Contrôle Permanents
- **`TEST TTS`** : Déclenche la prononciation vocale d'une phrase de test en français (*« Bonjour. Ceci est un test de diagnostic vocal VoiceKit sur Core S trois. »*).
- **`STOP AUDIO`** : Interrompt immédiatement toute restitution ou capture en cours via `voice.stop()`.

---

## 2. Sortie Série Structurée et CSV

L'application émet des relevés structurés sur le port série (115200 baud).

### Journalisation de latence `[LATENCY_CSV]` (v0.5.3)
À chaque fin de tour vocal ou tour conversationnel, une ligne CSV complète est émise :
```text
[LATENCY_CSV] <timestamp_ms>,<turn_id>,<speech_end_to_mic_stop_ms>,<mic_stop_to_activity_end_ms>,<activity_end_to_first_audio_ms>,<first_audio_to_playback_ms>,<response_latency_ms>,<total_turn_duration_ms>,<mic_capture_ms>,<rx_blocks>,<dropped_samples>
```
L'en-tête CSV est émis au démarrage :
```text
[LATENCY_CSV_HEADER] timestamp_ms,turn_id,speech_end_to_mic_stop_ms,mic_stop_to_activity_end_ms,activity_end_to_first_audio_ms,first_audio_to_playback_ms,response_latency_ms,total_turn_duration_ms,mic_capture_ms,rx_blocks,dropped_samples
```
> **Règle stricte des données manquantes** : Tout jalon ou calcul dérivé non disponible ou incomplet renvoie explicitement `-1`, évitant tout faux zéro ou fausse latence nulle.

### Journalisation de connexion `[CONN]` et `[CONN_CSV]` (v0.5.2)
À chaque transition vers `READY`, une ligne récapitulative des 5 phases temporelles est produite :
```text
[CONN] t_wifi=824ms t_sntp=0ms t_ws=945ms t_setup=412ms t_ready=2181ms fast=1 sntp_skip=1 ch=6 bssid=12:34:56:78:9a:bc
[CONN_CSV] 824,0,945,412,2181,1,1,1,1,0,6,12:34:56:78:9a:bc
```
L'en-tête CSV est émis au démarrage :
```text
[CONN_CSV_HEADER] t_wifi_ms,t_sntp_ms,t_ws_ms,t_setup_ms,t_ready_ms,fast_connect,sntp_skipped,wifi_attempts,gemini_attempts,failures,channel,bssid
```

### Format standard `[DIAG]` (périodique 1 Hz)
```text
[DIAG] Voice=READY [DIAG] Mode=IDLE [DIAG] WiFi=CONNECTED [DIAG] RAM_FREE=152320 [DIAG] PSRAM_FREE=7845120 [DIAG] MIC_RMS=215 [DIAG] TTS_BUSY=0
```

---

## 3. Installation et Compilation

### Prérequis
- M5Stack CoreS3 connecté en USB-C.
- PlatformIO Core ou extension VS Code.

### Configuration des identifiants
Copier le gabarit de configuration et renseigner vos identifiants :
```bash
cp include/secrets.example.h include/secrets.h
```

Éditer `include/secrets.h` avec votre SSID Wi-Fi, mot de passe et clé API Gemini Live.
> **Sécurité** : Les clés API et mots de passe ne sont jamais affichés à l'écran ni transmis dans les trames de diagnostic série.

### Compilation
```bash
pio run -d examples/VoiceDiagnostics
```

### Téléversement et Moniteur
```bash
pio run -d examples/VoiceDiagnostics -t upload
pio device monitor -b 115200
```

---

## 4. Architecture et Respect des Contraintes

1. **Façade unique** : Utilise exclusivement l'instance `VoiceKit voice;`. Aucun deuxième moteur n'est instancié.
2. **Pas de tâche concurrente** : Aucune tâche FreeRTOS supplémentaire n'est créée.
3. **Fréquence d'affichage** : Rafraîchissement cadencé à 5 Hz (200 ms) avec effacement localisé du panneau central pour supprimer tout scintillement.
4. **Authenticité des mesures** : Toutes les données proviennent directement de l'API matérielle ESP32 ou de l'instrumentation VoiceKit. Les métriques absentes sont indiquées `N/D` (ou `-1` en CSV).

