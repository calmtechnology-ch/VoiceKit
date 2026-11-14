# Politique de Sécurité & Confidentialité — VoiceKit

Le projet VoiceKit accorde une priorité absolue à la protection de la vie privée des utilisateurs finaux et à la sécurité des identifiants d'API utilisés sur microcontrôleur.

---

## 1. Gestion des Secrets & Clés d'API

### 1.1 Modèle de configuration par `secrets.example.h`
- Aucun fichier de secrets réels ne doit être commité dans le dépôt Git.
- Tous les exemples utilisent un fichier modèle `secrets.example.h` contenant uniquement des valeurs factices (`VOTRE_WIFI_SSID`, `AIzaSy...`, `sk-proj...`).
- Les fichiers `secrets.h` créés localement par les développeurs sont systématiquement exclus par les règles du fichier `.gitignore`.

### 1.2 Masquage dans les journaux série (Télémétrie Sécurisée)
- VoiceKit et l'ensemble de ses exemples officiels appliquent une politique stricte d'assainissement des journaux : **aucune clé d'API, mot de passe Wi-Fi ou token d'autorisation n'est imprimé sur le port série**.
- Les messages d'initialisation affichent exclusivement `Clé API : [CONFIGURÉE]` ou `[NON CONFIGURÉE]`.

### 1.3 Exclusion absolue des clés API dans la persistance NVS (Invariant INV-10)
- Le gestionnaire de profils vocaux (`VoiceProfileManager` et `VoxCoreVoice`) sauvegarde les préférences de langue, de voix et de style dans la mémoire NVS de l'ESP32.
- **Les clés d'API ne sont JAMAIS stockées en mémoire NVS.** En cas d'analyse physique de la puce Flash, aucun secret cloud ne peut être extrait de la NVS.

---

## 2. Confidentialité des Flux Audio et Services Cloud

- **Nature des données transmises :**
  - En mode STT ou Conversation, les données audio captées par le microphone du M5Stack CoreS3 (PCM 16 kHz) sont diffusées en streaming vers le fournisseur cloud sélectionné (Google Gemini Live ou OpenAI Realtime).
  - Aucune donnée audio n'est enregistrée localement sur la carte SD ou la mémoire Flash.
- **Chiffrement du transport :**
  - Toutes les communications avec les fournisseurs cloud utilisent un chiffrement fort TLS 1.2 / 1.3 avec vérification des certificats d'autorité racine officiels (`GoogleRoots.h` et `OpenAIRoots.h`).
- **Fournisseurs tiers et facturation :**
  - L'utilisation des API cloud peut engendrer des coûts selon les grilles tarifaires de Google AI Studio ou d'OpenAI. L'utilisateur est seul responsable de la surveillance de ses quotas et coûts d'accès.

---

## 3. Sécurité des Commandes Matérielles Locales (ToolRegistry)

- VoiceKit intègre un registre d'outils matériels sécurisé (`ToolRegistry`) et un répartiteur déterministe (`VoiceCommandDispatcher`).
- Chaque commande locale dispose d'une politique de sécurité explicite :
  - `READ_ONLY` : consultation passive sans impact matériel (ex. lecture de température, statut).
  - `WRITE` : modification non critique (ex. luminosité d'écran, volume).
  - `SENSITIVE` : action à impact matériel irréversible ou critique (ex. réinitialisation, commutation de relais).
- **Règle impérative (Invariant INV-08) :** Toute commande marquée `SENSITIVE` exige une confirmation explicite (appui tactile ou commande série 'c') avant exécution physique.

---

## 4. Précautions Avant Partage de Firmware Compilé

- **Attention :** Lors de la compilation d'un firmware avec `secrets.h`, les clés d'API et identifiants Wi-Fi sont incorporés dans les sections de données du binaire (`firmware.bin` ou `firmware.elf`).
- **Recommandation :** Ne distribuez jamais un binaire `firmware.bin` compilé avec vos propres identifiants personnels. Si vous partagez un binaire pré-compilé, veillez à ce que l'utilisateur puisse configurer ses identifiants séparément ou utilisez un firmware de démonstration hors ligne (`LocalToolRegistryDemo` ou `WakeWordFoundation`).

---

## 5. Signalement d'une Vulnérabilité de Sécurité

Si vous découvrez une faille de sécurité ou une vulnérabilité potentielle dans VoiceKit :

1. **Ne créez pas d'issue publique sur GitHub.**
2. Envoyez un rapport détaillé par courriel au mainteneur du projet :
   - **Contact :** `philippe@voxcore.org` ou via le profil GitHub du mainteneur [Philippe-AD](https://github.com/Philippe-AD).
3. Incluez dans votre message :
   - La description précise de la vulnérabilité.
   - Les étapes pour reproduire le problème.
   - L'impact potentiel sur les utilisateurs ou le matériel.
4. Une réponse vous sera adressée sous 48 heures pour accuser réception et coordonner la publication d'un correctif.
