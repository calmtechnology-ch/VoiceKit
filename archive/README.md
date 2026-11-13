# Exemples Archivés — VoiceKit

Ce répertoire regroupe les anciens exemples conservés à titre de référence historique ou pour des besoins spécifiques, mais retirés de la liste des exemples officiels de VoiceKit v0.6.5.2.

---

## Inventaire et Raisons de l'Archivage

| Exemple | Emplacement | Raison de l'archivage | Remplaçant officiel v0.6.5.2 |
|---|---|---|---|
| **MinimalVoiceKit** | `archive/examples/MinimalVoiceKit/` | Exemple historique composite. Remplacé par des exemples modulaires spécialisés plus lisibles. | `examples/MinimalTTS` et `examples/MinimalConversation` |
| **ContinuousSTT** | `archive/examples/ContinuousSTT/` | Mode VAD continu autonome. Les fonctionnalités d'interaction continue sont désormais centralisées dans le pipeline intégré. | `examples/IntegratedVoiceDemo` |
| **OpenAIConversation** | `archive/examples/OpenAIConversation/` | Première implémentation M5GFX brute pour OpenAI. Remplacée par l'implémentation graphique moderne LVGL 9 avec typographie française complète. | `examples/OpenAIConversationLVGL` |

---

> [!NOTE]
> Le code source de ces exemples est intégralement préservé ainsi que leurs configurations PlatformIO. Leurs chemins relatifs pointent désormais vers `../../..` pour permettre leur compilation autonome en cas de besoin.

