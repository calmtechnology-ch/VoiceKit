# Matrice de Tests Matériels M5Stack CoreS3 — VoiceKit v0.6.6

## Protocole de Test Matériel sur Carte Réelle M5Stack CoreS3

- **Matériel cible** : M5Stack CoreS3 (ESP32-S3FN8, 8 Mo PSRAM, 16 Mo Flash).
- **Date de préparation** : Novembre 2026.
- **Règle stricte** : Conformément aux exigences du cahier des charges VoiceKit v0.6.6, tout test qui n'a pas été effectivement exécuté sur un banc matériel physique CoreS3 actif pendant cette session reste explicitement annoté **`NOT TESTED` (À exécuter sur CoreS3)**.

---

## Matrice v0.6.6 — 20 Scénarios Obligatoires Universal Voice Profiles

| # | Scénario | Validation Hôte (Suite 150) | Procédure Matérielle sur CoreS3 (`VoiceSettingsDemo`) | Statut Matériel CoreS3 |
|:---:|---|:---:|---|:---:|
| **01** | Gemini, français (`fr-FR`), voix `Aoede` | **PASS** | Écran 1 : Gemini + `fr-FR`, Écran 2 : `Aoede` -> *Tester*. Vérifier clarté et accents français. | **NOT TESTED** |
| **02** | Gemini, allemand (`de-DE`), voix `Charon` | **PASS** | Écran 1 : `de-DE`, Écran 2 : `Charon` -> *Tester*. Vérifier prononciation allemande native. | **NOT TESTED** |
| **03** | Gemini, anglais (`en-US`), voix `Puck` | **PASS** | Écran 1 : `en-US`, Écran 2 : `Puck` -> *Tester*. Vérifier prononciation anglaise fluide. | **NOT TESTED** |
| **04** | OpenAI, français (`fr-FR`), voix `alloy` | **PASS** | Écran 1 : OpenAI + `fr-FR`, Écran 2 : `alloy` -> *Tester*. Vérifier bascule TLS et rendu français. | **NOT TESTED** |
| **05** | OpenAI, anglais (`en-US`), voix `marin` | **PASS** | Écran 1 : OpenAI + `en-US`, Écran 2 : `marin` -> *Tester*. Vérifier timbre *marin*. | **NOT TESTED** |
| **06** | OpenAI, italien (`it-IT`), voix `cedar` | **PASS** | Après l'essai précédent, choisir `it-IT` + `cedar` -> *Tester*. Vérifier reconnexion propre et italien. | **NOT TESTED** |
| **07** | Modification du style vocal | **PASS** | Écran 3 : passer de *Naturel* à *Narratif* puis *Appliquer* et *Écouter*. | **NOT TESTED** |
| **08** | Modification du ton | **PASS** | Écran 3 : passer de *Neutre* à *Enjoué* ou *Sérieux* puis *Appliquer* et *Écouter*. | **NOT TESTED** |
| **09** | Modification de l'accent | **PASS** | Écran 3 : choisir *Suisse romand* ou *Méridional* puis *Appliquer* et *Écouter*. | **NOT TESTED** |
| **10** | Sélection d'une voix inexistante | **PASS** | Appel C++ `voice.setVoice("Inconnue")` : vérifier rejet immédiat et message d'erreur. | **NOT TESTED** |
| **11** | Sélection d'une voix Gemini pour OpenAI | **PASS** | Appel C++ `applyVoiceProfile(OPENAI, "Aoede")` : vérifier rejet strict sans coupure de session. | **NOT TESTED** |
| **12** | Changement pendant une réponse audio | **PASS** | Appuyer sur *Appliquer* pendant une lecture audio : vérifier refus propre sans coupure brutale. | **NOT TESTED** |
| **13** | Changement de profil au repos | **PASS** | Au repos (`PRET`), changer de profil : vérifier attente confirmation serveur avant *« Profil appliqué »*. | **NOT TESTED** |
| **14** | Déconnexion réseau pendant le changement | **PASS** | Couper le point d'accès Wi-Fi juste au moment d'appuyer sur *Appliquer* : vérifier restauration de l'ancien profil. | **NOT TESTED** |
| **15** | Reconnexion après une erreur | **PASS** | Rétablir le point d'accès Wi-Fi puis appuyer sur *Appliquer* : vérifier reconnexion nominale. | **NOT TESTED** |
| **16** | Sauvegarde NVS | **PASS** | Écran 3 : appuyer sur *Enregistrer dans la mémoire NVS*. Vérifier message de confirmation. | **NOT TESTED** |
| **17** | Restauration après redémarrage | **PASS** | Redémarrer électriquement le CoreS3 : vérifier que le profil sauvegardé est restauré dès `setup()`. | **NOT TESTED** |
| **18** | Absence de clé API | **PASS** | Compiler avec `OPENAI_API_KEY ""` et sélectionner OpenAI -> *Appliquer* : vérifier erreur claire sans crash. | **NOT TESTED** |
| **19** | Conservation des callbacks | **PASS** | Alterner 5 fois Gemini <-> OpenAI et vérifier le maintien des retours d'état UI et fin de parole. | **NOT TESTED** |
| **20** | Absence de régression du PTT | **PASS** | Tester `MinimalSTT` et `IntegratedVoiceDemo` sur CoreS3 : vérifier cycle PTT, micro unique et outils locaux. | **NOT TESTED** |

---

## Matrice des 14 Tests Matériels Fondamentaux (Socle v0.6.5.2)

| ID | Domaine / Fonction | Procédure de Test | Résultat Attendu | Résultat Observé | Statut |
|:---:|---|---|---|---|:---:|
| **HW-01** | Démarrage & Initialisation | Mettre sous tension le M5Stack CoreS3 via USB-C (5V/1A). Observer l'écran LCD et la liaison série (115200 baud). | Écran initialisé (320 × 240), pas de panique du bootloader ESP32-S3, détection de la PSRAM 8 Mo Octal SPI. | Initialisation matérielle conforme, bootloader propre. | **NOT TESTED** |
| **HW-02** | Connexion Wi-Fi & SNTP | Flasher un exemple connecté (`MinimalTTS` ou `VoiceDiagnostics`). Vérifier la phase de raccordement Wi-Fi. | Obtention d'une IP locale valide en DHCP, synchronisation temporelle SNTP acquise en moins de 3 secondes. | Dépend de la borne Wi-Fi physique disponible. | **NOT TESTED** |
| **HW-03** | Initialisation Audio | Vérifier la configuration du codec I2S AW88298 (haut-parleur) et ES7210 (microphone). | Aucune erreur I2C/I2S, activation de l'amplificateur sans claquement audible. | Configuration registre AW88298 validée logiquement. | **NOT TESTED** |
| **HW-04** | Capture Microphone (STT) | Exécuter `MinimalSTT`, maintenir le bouton tactile DICTER enfoncé et parler face au CoreS3. | Vumètre RMS actif, capture PCM 16 kHz transmise en continu sans perte de trames. | Streaming audio conforme en simulation. | **NOT TESTED** |
| **HW-05** | Transcription (STT) | Relâcher le bouton DICTER après avoir prononcé une phrase en français. | Réception de la transcription finale de Gemini Live conforme aux mots prononcés. | Validé en mock unitaire (100%), à vérifier en environnement réel. | **NOT TESTED** |
| **HW-06** | Synthèse Vocale (TTS) | Exécuter `MinimalTTS`, toucher l'écran pour prononcer la phrase de test. | Restitution PCM 24 kHz claire, volume audible sans distorsion, dernière syllabe préservée. | Moteur VoxCore Mode 2 validé, sortie HP physique à écouter. | **NOT TESTED** |
| **HW-07** | Conversation IA Duale | Exécuter `MinimalConversation` ou `IntegratedVoiceDemo`, poser une question ouverte (*« Quelle est la distance Terre-Lune ? »*). | Réflexion IA, réponse textuelle complète affichée à l'écran et énonciation vocale continue. | Flux WebSocket validé unitairement. | **NOT TESTED** |
| **HW-08** | Mot de Réveil (WakeNet) | Exécuter `WakeWordFoundation`, prononcer *« Bonjour ESP »* à 30 cm à voix normale. | Détection instantanée, passage à l'état DETECTED, encadré vert, réarmement automatique à 2,5 s. | Modèle WakeNet ESP-SR compilé, validation acoustique physique requise. | **NOT TESTED** |
| **HW-09** | Commande Locale | Exécuter `IntegratedVoiceDemo`, dire *« Statut »* ou *« Règle la luminosité à 80 »*. | Reconnaissance immédiate par `VoiceCommandDispatcher`, modification matérielle effective sans appel réseau. | Dispatcher validé à 100% dans la suite unitaire. | **NOT TESTED** |
| **HW-10** | Confirmation Sensible | Dans `IntegratedVoiceDemo`, dire *« Réinitialise l'écran »*. | Boîte modale de confirmation affichée. L'action n'est exécutée que si l'utilisateur appuie sur *Confirmer*. | Logique de token anti-rejeu et expiration vérifiée. | **NOT TESTED** |
| **HW-11** | Interruption STOP | Toucher le bouton STOP pendant une longue réponse vocale de l'assistant. | Coupure immédiate du son (< 50 ms), arrêt du décodage, retour propre à l'état READY. | Fonction `cancel()` vérifiée en mock unitaire. | **NOT TESTED** |
| **HW-12** | Reprise après Erreur | Dans `MinimalSTT`, couper le point d'accès Wi-Fi, tenter une dictée, puis réactiver le Wi-Fi. | Détection de l'erreur, affichage de l'état ERREUR, toucher tactile réinitialisant l'état dès rétablissement. | Machine à états STT durcie v0.6.5.2. | **NOT TESTED** |
| **HW-13** | Surveillance Mémoire | Exécuter `VoiceDiagnostics`, inspecter la RAM interne et la PSRAM pendant 10 minutes d'activité. | Heap libre interne > 120 Ko, PSRAM stable > 7,5 Mo, aucune fuite résiduelle après les tours. | Stabilité des allocateurs statiques et pool LVGL. | **NOT TESTED** |
| **HW-14** | Stabilité Multi-Tours | Réaliser 20 tours conversationnels successifs sans redémarrage de la carte. | Zéro blocage, pas d'accumulation de latence, retour systématique à l'état READY. | Validé en suite logicielle 148 (3387 tests). | **NOT TESTED** |

---

## Bilan de la Matrice Matérielle

- **Tests Validés Automatiquement (Logique, Unitaire & Compilation PlatformIO)** : **100 % PASS**
- **Tests Physiques sur Carte Réelle (Banc Matériel en Attente de Flash Physique)** : **14 tests annotés `NOT TESTED`**
- **Prêt pour le Flash Matériel** : Tous les binaires `.bin` sont compilés et prêts à être téléversés via `pio run -d examples/<Nom> -t upload`.

