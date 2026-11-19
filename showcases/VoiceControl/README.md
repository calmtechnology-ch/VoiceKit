# VoiceControl v0.1.0

Application showcase **VoiceKit v0.6.7.1** pour **M5Stack CoreS3** : centre de commandes vocales dédié exclusivement à **l'heure suisse et à la date**, avec interface **LVGL 9** et support **OpenAI Realtime** / **Google Gemini Live**.

---

## 1. Fonctionnalités

- **Horloge suisse temps réel** (`Europe/Zurich`) avec passage automatique heure d'hiver (**CET = UTC+1**) / heure d'été (**CEST = UTC+2**) via la règle POSIX `CET-1CEST,M3.5.0,M10.5.0/3`.
- **Synchronisation NTP non bloquante** après connexion Wi-Fi (`pool.ntp.org` / `time.google.com`). L'interface reste immédiatement utilisable au démarrage et n'invente jamais l'heure tant que l'horloge n'est pas synchronisée.
- **Configuration centralisée unique** dans [`src/config/VoiceControlConfig.h`](src/config/VoiceControlConfig.h) (aucun menu de paramètres sur l'écran CoreS3).
- **Support bilingue FR / EN** piloté par un unique paramètre `VC_LANGUAGE` (`"fr"` ou `"en"`), qui configure automatiquement l'interface LVGL, la reconnaissance vocale (STT) et la synthèse vocale (TTS).
- **Double fournisseur IA au démarrage** : `VC_PROVIDER_OPENAI` ou `VC_PROVIDER_GEMINI` (sans bascule dynamique).
- **Capture vocale PTT à deux clics** (`PARLER` / `TALK`) avec détection locale de silence (VAD VoiceKit) et **interruption immédiate `STOP`** sans forcer de reconnexion du fournisseur IA.
- **Moteur de commandes horaires déterministe** : toutes les valeurs horaires et calculs (`+1h`, `-1h`) sont calculés à partir de l'horloge locale réelle (timestamps UTC convertis en heure suisse), jamais devinés par l'IA. Toute commande hors périmètre est rejetée poliment.

---

## 2. Organisation des fichiers

```text
showcases/VoiceControl/
├── platformio.ini
├── partitions_16MB.csv
├── README.md
├── include/
│   ├── FrenchFonts.h
│   ├── lv_conf.h
│   └── secrets.example.h
├── src/
│   ├── main.cpp
│   ├── font_french_12.c
│   ├── font_french_14.c
│   ├── font_french_bold_16.c
│   ├── config/
│   │   └── VoiceControlConfig.h
│   ├── commands/
│   │   ├── TimeCommands.h
│   │   └── TimeCommands.cpp
│   ├── voice/
│   │   ├── VoiceController.h
│   │   └── VoiceController.cpp
│   └── ui/
│       ├── VoiceControlUI.h
│       ├── VoiceControlUI.cpp
│       ├── Localization.h
│       └── Localization.cpp
└── tests/
    └── test_voicecontrol_core.cpp
```

---

## 3. Configuration

### 3.1 Secrets (Wi-Fi et clés API)

Copiez `include/secrets.example.h` vers `include/secrets.h` (exclu de Git) et renseignez vos identifiants :

```cpp
#define WIFI_SSID      "VOTRE_WIFI_SSID"
#define WIFI_PASSWORD  "VOTRE_WIFI_PASSWORD"
#define OPENAI_API_KEY "sk-proj-VOTRE_CLE_OPENAI"
#define GEMINI_API_KEY "VOTRE_CLE_GEMINI"
```

### 3.2 Paramètres applicatifs (`src/config/VoiceControlConfig.h`)

Tous les réglages sont centralisés dans `src/config/VoiceControlConfig.h` :

| Paramètre | Valeur par défaut | Description |
|---|---|---|
| `VC_LANGUAGE` | `"fr"` | Langue système unique (`"fr"` ou `"en"`), appliquée à l'UI, au STT et au TTS |
| `VC_AI_PROVIDER` | `VC_PROVIDER_OPENAI` | Fournisseur IA au démarrage (`VC_PROVIDER_OPENAI` ou `VC_PROVIDER_GEMINI`) |
| `VC_OPENAI_MODEL` | `"gpt-realtime-2.1-mini"` | Modèle OpenAI Realtime |
| `VC_GEMINI_MODEL` | `"models/gemini-2.5-flash-native-audio-latest"` | Modèle Google Gemini Live |
| `VC_TIMEZONE_NAME` | `"Europe/Zurich"` | Fuseau horaire de référence |
| `VC_TIMEZONE_POSIX` | `"CET-1CEST,M3.5.0,M10.5.0/3"` | Règle POSIX CET/CEST automatique |
| `VC_NTP_SERVER_1` | `"pool.ntp.org"` | Serveur NTP principal |
| `VC_DIAG_ENABLED` | `1` | Activation des traces série `[VC][...]` |

---

## 4. Commandes vocales autorisées

| Commande française | Équivalent anglais (`VC_LANGUAGE = "en"`) | Comportement |
|---|---|---|
| « Quelle heure est-il ? » | "What time is it?" | Annonce l'heure suisse (24h) |
| « Donne-moi l'heure » | "Tell me the time" | Annonce l'heure suisse (24h) |
| « Quelle heure est-il en Suisse ? » | "What time is it in Switzerland?" | Annonce l'heure suisse (24h) |
| « Quelle est la date ? » | "What is the date?" | Annonce la date du jour |
| « Quel jour sommes-nous ? » | "What day is it?" | Annonce le jour de la semaine et la date |
| « Nous sommes en quelle année ? » | "What year is it?" | Annonce l'année courante |
| « Quelle heure sera-t-il dans une heure ? » | "What time will it be in one hour?" | Calcule l'heure suisse à `timestamp + 3600` |
| « Quelle heure était-il il y a une heure ? » | "What time was it one hour ago?" | Calcule l'heure suisse à `timestamp - 3600` |

Toute autre demande reçoit :
- FR : *« Je peux uniquement répondre aux questions concernant l'heure et la date. »*
- EN : *"I can only answer questions about the time and date."*

---

## 5. Compilation et téléversement (PlatformIO)

Depuis le répertoire `showcases/VoiceControl/` :

```bash
# Compiler (configuration active dans VoiceControlConfig.h)
pio run

# Téléverser sur M5Stack CoreS3
pio run -t upload

# Ouvrir le moniteur série (115200 bauds)
pio device monitor
```

### Scripts BATCH Windows (variantes FR/EN × Gemini/OpenAI)

Des scripts `.bat` prêts à l'emploi sont disponibles directement dans `showcases/VoiceControl/` (compilation + téléversement direct sur CoreS3) :

| Script | Variante | Environnement PlatformIO |
|---|---|---|
| `build_fr_gemini.bat` | Français + Google Gemini Live | `fr-gemini` |
| `build_fr_openai.bat` | Français + OpenAI Realtime | `fr-openai` |
| `build_en_gemini.bat` | Anglais + Google Gemini Live | `en-gemini` |
| `build_en_openai.bat` | Anglais + OpenAI Realtime | `en-openai` |
| `build_menu.bat` | Menu interactif de sélection | Tous |
| `build_all.bat` | Compile les 4 variantes sans téléverser | Tous |

Options facultatives en ligne de commande :
- Spécifier un port série : `build_fr_gemini.bat COM4`
- Compiler sans téléverser : `build_fr_gemini.bat --build-only`

### Exemples de traces série attendues

```text
[VC][BOOT] VoiceControl v0.1.0
[VC][CONFIG] Language: fr
[VC][CONFIG] Provider: OPENAI
[VC][CONFIG] Model: gpt-realtime-2.1-mini
[VC][TIME] Timezone: Europe/Zurich
[VC][NTP] Synchronization OK
[VC][TIME] Local: 19.11.2026 10:15:00
[VC][VOICE] READY
[VC][PTT] START
[VC][PTT] VALIDATED
[VC][STT] Quelle heure est-il ?
[VC][CMD] GET_TIME
[VC][TTS] Il est dix heures et quinze minutes.
[VC][VOICE] READY
```

---

## 6. Tests unitaires natifs (sans matériel)

Les calculs horaires suisses, les transitions CET/CEST (29 mars / 25 octobre), la classification FR/EN, le rejet hors périmètre et le cas NTP indisponible peuvent être testés localement sur PC :

```bash
g++ -std=gnu++17 -Wall -Wextra -Isrc tests/test_voicecontrol_core.cpp src/commands/TimeCommands.cpp src/ui/Localization.cpp -o tests/test_voicecontrol_core
./tests/test_voicecontrol_core
```

---

## 7. Matrice de validation (VC-01 à VC-15)

| Test | Description | Statut | Nature |
|---|---|---|---|
| VC-01 | Démarrage et synchronisation NTP | À valider sur CoreS3 | Matériel |
| VC-02 | Heure affichée correctement en Suisse | Validé (logique) / À vérifier à l'écran | Automatisé + Matériel |
| VC-03 | Commande « Quelle heure est-il ? » | Validé (logique) / À tester en voix réelle | Automatisé + Matériel |
| VC-04 | Commande « Quelle est la date ? » | Validé (logique) / À tester en voix réelle | Automatisé + Matériel |
| VC-05 | Commande « Quel jour sommes-nous ? » | Validé (logique) / À tester en voix réelle | Automatisé + Matériel |
| VC-06 | Calcul dans une heure | Validé (logique) / À tester en voix réelle | Automatisé + Matériel |
| VC-07 | Calcul une heure auparavant | Validé (logique) / À tester en voix réelle | Automatisé + Matériel |
| VC-08 | Rejet d'une commande inconnue | Validé (logique) / À tester en voix réelle | Automatisé + Matériel |
| VC-09 | Silence : aucune réponse | À valider sur CoreS3 (VAD micro ES7210) | Matériel |
| VC-10 | STOP pendant la réponse vocale | À valider sur CoreS3 | Matériel |
| VC-11 | Fonctionnement avec OpenAI | Compilation validée / Essai vocal à effectuer | Compilation + Matériel |
| VC-12 | Fonctionnement avec Gemini | Compilation validée / Essai vocal à effectuer | Compilation + Matériel |
| VC-13 | Interface et voix en anglais | Compilation + logique validées / Essai vocal à effectuer | Automatisé + Compilation + Matériel |
| VC-14 | Gestion heure d'été / heure d'hiver (CET/CEST) | Validé (transitions simulées mars & octobre) | Automatisé |
| VC-15 | NTP indisponible au démarrage | Validé (logique) / À tester hors ligne sur CoreS3 | Automatisé + Matériel |
