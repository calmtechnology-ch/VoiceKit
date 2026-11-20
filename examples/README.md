# Catalogue Pédagogique des Exemples — VoiceKit v0.6.7.2

Ce répertoire contient les **12 exemples officiels** de VoiceKit (11 projets PlatformIO et 1 exemple Arduino IDE), organisés par ordre progressif de découverte pour **M5Stack CoreS3**.

> **Comment démarrer ?** Pour tous les exemples nécessitant Internet, copiez `include/secrets.example.h` en `include/secrets.h` dans le dossier de l'exemple, renseignez votre Wi-Fi 2.4 GHz et votre clé API, puis lancez `pio run -t upload`. Voir le **[Guide de démarrage rapide](../docs/QUICK_START.md)**.

---

## Quel point d'entrée choisir : `VoiceKit.h` ou `VoxCoreVoice.h` ?

Dans la bibliothèque VoiceKit, deux en-têtes publics coexistent sans redondance :

- **`<VoiceKit.h>` (recommandé pour tout nouveau projet)** : façade haut niveau et unifiée. Elle délègue au moteur interne `VoxCoreVoice` tout en offrant une API simplifiée pour le TTS (`say`), le STT (`listen`), l'écoute continue, la conversation, le Wake Word et les profils vocaux.
- **`<VoxCoreVoice.h>` (moteur direct / usage avancé)** : utilisé directement par plusieurs exemples historiques et par les intégrations bas niveau avec `VoiceInteractionController` ou `IntegratedVoicePipeline` (accessible aussi depuis `VoiceKit::getEngine()`).

Les deux en-têtes sont des API publiques stables et maintenues. Pour un nouveau projet applicatif simple, commencez par `<VoiceKit.h>`.

---

## Niveau 1 — Débutant

Idéal pour vérifier votre matériel et réaliser votre première interaction vocale étape par étape.

| Exemple | Fonction principale | Fournisseur | Interface | Matériel suppl. | Internet | Difficulté | État de validation | Documentation |
|---|---|---|---|---|---|---|---|---|
| [`MinimalTTS`](MinimalTTS/) | Faire parler le CoreS3 (synthèse vocale TTS 24 kHz) | Google Gemini Live | M5GFX + Série | Aucun | Oui | Débutant | Compilation PlatformIO validée (`v0.6.7.2`) | [README](MinimalTTS/README.md) |
| [`MinimalSTT`](MinimalSTT/) | Reconnaître la parole en Push-to-Talk (STT 16 kHz) | Google Gemini Live | M5GFX + Série | Aucun | Oui | Débutant | Compilation PlatformIO validée (`v0.6.7.2`) | [README](MinimalSTT/README.md) |
| [`MinimalConversation`](MinimalConversation/) | Première conversation vocale bidirectionnelle | Google Gemini Live | M5GFX + Série | Aucun | Oui | Débutant | Compilation PlatformIO validée (`v0.6.7.2`) | [README](MinimalConversation/README.md) |
| [`TwoClickConversationLVGL`](TwoClickConversationLVGL/) | Conversation tactile PTT à 2 clics (`PARLER` / `TERMINER`), validation VAD locale et bouton `STOP` | OpenAI Realtime *(défaut)* & Google Gemini Live *(configurable)* | LVGL 9 + M5GFX + Série | Aucun | Oui | Débutant | Compilation PlatformIO validée (`v0.6.7.2`) ; 8/8 MAT CoreS3 validés en v0.6.7.1 | [README](TwoClickConversationLVGL/README.md) |

---

## Niveau 2 — Intermédiaire

Pour utiliser OpenAI Realtime, personnaliser les voix/langues, diagnostiquer le système ou tester le mot de réveil hors ligne.

| Exemple | Fonction principale | Fournisseur | Interface | Matériel suppl. | Internet | Difficulté | État de validation | Documentation |
|---|---|---|---|---|---|---|---|---|
| [`OpenAIConversationLVGL`](OpenAIConversationLVGL/) | Conversation temps réel avec OpenAI Realtime et interface tactile LVGL 9 | OpenAI Realtime | LVGL 9 + Série | Aucun | Oui | Intermédiaire | Compilation PlatformIO validée (`v0.6.7.2`) | [README](OpenAIConversationLVGL/README.md) |
| [`VoiceSettingsDemo`](VoiceSettingsDemo/) | Sélection multilingue, profils vocaux (20 voix) et persistance NVS | Google Gemini Live & OpenAI Realtime | LVGL 9 + M5GFX + Série | Aucun | Oui | Intermédiaire | Compilation PlatformIO validée (`v0.6.7.2`) | [README](VoiceSettingsDemo/README.md) |
| [`VoiceDiagnostics`](VoiceDiagnostics/) | Tableau de bord de télémétrie, latences et diagnostics système | Google Gemini Live | M5GFX + Série | Aucun | Oui | Intermédiaire | Compilation PlatformIO validée (`v0.6.7.2`) | [README](VoiceDiagnostics/README.md) |
| [`WakeWordFoundation`](WakeWordFoundation/) | Détection locale hors ligne du mot de réveil « Bonjour ESP » (WakeNet 9) *(expérimental)* | Hors ligne (ESP-SR WakeNet) | Série | Aucun | Non | Intermédiaire | Compilation PlatformIO validée (`v0.6.7.2`) | [README](WakeWordFoundation/README.md) |

---

## Niveau 3 — Avancé

Architectures complètes combinant pipeline vocal, commandes locales sécurisées ou réveil mains libres.

| Exemple | Fonction principale | Fournisseur | Interface | Matériel suppl. | Internet | Difficulté | État de validation | Documentation |
|---|---|---|---|---|---|---|---|---|
| [`IntegratedVoiceDemo`](IntegratedVoiceDemo/) | Orchestrateur complet (`IntegratedVoicePipeline`) : écoute, commandes locales, confirmation sensible, IA et TTS | Google Gemini Live & OpenAI Realtime | LVGL 9 + M5GFX + Série | Aucun | Oui | Avancé | Compilation PlatformIO validée (`v0.6.7.2`) ; 19/20 scénarios CoreS3 validés en v0.6.6.2 | [README](IntegratedVoiceDemo/README.md) |
| [`WakeWordConversation`](WakeWordConversation/) | Cycle mains libres complet : mot de réveil local → conversation cloud → retour en veille *(expérimental)* | Google Gemini Live & OpenAI Realtime | M5GFX + Série | Aucun | Oui | Avancé | Compilation PlatformIO validée (`v0.6.7.2`) | [README](WakeWordConversation/README.md) |
| [`LocalToolRegistryDemo`](LocalToolRegistryDemo/) | Registre local d'outils matériels sécurisés (`ToolRegistry` : `READ_ONLY`, `WRITE`, `SENSITIVE`) hors ligne | Hors ligne (aucun cloud) | M5GFX + Série | Aucun | Non | Avancé | Compilation PlatformIO validée (`v0.6.7.2`) | [README](LocalToolRegistryDemo/README.md) |

---

## Rubrique Arduino IDE

| Exemple | Fonction principale | Fournisseur | Interface | Matériel suppl. | Internet | Difficulté | État de validation | Documentation |
|---|---|---|---|---|---|---|---|---|
| [`MinimalTTS_Arduino`](MinimalTTS_Arduino/) | Synthèse vocale TTS autonome au format sketch Arduino IDE (`.ino`) | Google Gemini Live | M5GFX + Série | Aucun | Oui | Débutant (Arduino IDE) | Structure et documentation validées ; compilation Arduino CLI non exécutée automatiquement (`NOT TESTED`) | [README](MinimalTTS_Arduino/README.md) |

> **Conseil :** Pour tout projet dépassant un test TTS simple (notamment avec LVGL 9 ou WakeNet), **PlatformIO** reste l'environnement officiellement recommandé.
