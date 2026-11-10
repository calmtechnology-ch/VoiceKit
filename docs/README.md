# Documentation VoiceKit

Index de la documentation approfondie de la bibliothèque. Pour la prise en main, commencez par le [README principal](../README.md) et le [parcours d'apprentissage](../tutorials/README.md).

## Architecture

| Document | Sujet |
|---|---|
| [architecture_tools_registry_ai_mcp.md](architecture_tools_registry_ai_mcp.md) | Registre Local d'Outils, appels d'outils IA et MCP |

## Rapports techniques & de livraison

Classés de la version la plus récente à la plus ancienne. L'historique détaillé des modifications reste dans le [CHANGELOG](../CHANGELOG.md).

| Version | Rapport | Thème |
|---|---|---|
| v0.6.4.2 | [RAPPORT_v0.6.4.2.md](rapports/RAPPORT_v0.6.4.2.md) | Voice UX Live Bridge (pont STT → UX → TTS) |
| v0.6.4.1 | [RAPPORT_v0.6.4.1.md](rapports/RAPPORT_v0.6.4.1.md) | Voice UX Foundation (TextGate, IntentRouter, ReplyMemory) |
| v0.6.4 | [RAPPORT_v0.6.4.md](rapports/RAPPORT_v0.6.4.md) | Registre Local d'Outils (ToolRegistry) |
| v0.6.3.2.2 | [RAPPORT_v0.6.3.2.2.md](rapports/RAPPORT_v0.6.3.2.2.md) | Validation technique et corrective |
| v0.6.3.2.1 | [RAPPORT_CORRECTIVE_v0.6.3.2.1.md](rapports/RAPPORT_CORRECTIVE_v0.6.3.2.1.md) | Livraison corrective |
| v0.6.3.1 | [RAPPORT_v0.6.3.1.md](rapports/RAPPORT_v0.6.3.1.md) | Rapport technique (latence `voice.update()`, WakeNet configurable) |
| v0.6.3.1 | [RAPPORT_CORRECTIVE_v0.6.3.1.md](rapports/RAPPORT_CORRECTIVE_v0.6.3.1.md) | Validation et livraison corrective |
| v0.6.3 | [RAPPORT_v0.6.3.md](rapports/RAPPORT_v0.6.3.md) | Conversation mains libres « Bonjour ESP » |
| v0.6.2.1 | [RAPPORT_v0.6.2.1.md](rapports/RAPPORT_v0.6.2.1.md) | Durcissement Wake Word Foundation |
| v0.6.2 | [RAPPORT_v0.6.2.md](rapports/RAPPORT_v0.6.2.md) | Wake Word Foundation hors ligne |
| v0.6.1 | [RAPPORT_LIVRAISON_v0.6.1.md](rapports/RAPPORT_LIVRAISON_v0.6.1.md) | Correctifs de stabilité temps réel |
| v0.6.0 | [RAPPORT_LIVRAISON_v0.6.0.md](rapports/RAPPORT_LIVRAISON_v0.6.0.md) | Architecture multi-fournisseurs (Gemini Live & OpenAI Realtime) |

> Les rapports propres à un exemple restent dans le dossier de cet exemple (par ex. `examples/OpenAIConversationLVGL/`, `examples/OpenAIConversation/`, `tutorials/`).

## Outils de validation

Les scripts Python de validation matérielle sur port série sont regroupés dans [`tools/validation/`](../tools/validation/README.md).

