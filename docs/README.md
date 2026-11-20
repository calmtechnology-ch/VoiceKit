# Documentation VoiceKit (v0.6.7.2)

Index de la documentation de la bibliothèque. Pour démarrer rapidement, commencez par :

- **[README principal](../README.md)** — Présentation générale et parcours de découverte.
- **[Guide de démarrage rapide (QUICK_START.md)](QUICK_START.md)** — Installation pas à pas (exemples et projet personnel) et dépannage.
- **[Catalogue pédagogique des 12 exemples](../examples/README.md)** — Choix d'un exemple par niveau (Débutant, Intermédiaire, Avancé, Arduino IDE).
- **[Applications showcases](../showcases/README.md)** — `VoiceAssistant`, `VoiceSensorStation` et `VoiceControl`.
- **[Limitations connues](KNOWN_LIMITATIONS.md)** — Périmètre matériel, réseau, cloud et fonctionnalités expérimentales.
- **[Politique de sécurité](../SECURITY.md)** — Gestion des clés API, masquage des journaux et exclusion NVS.

---

## Guides d'architecture et composants

| Document | Sujet |
|---|---|
| [VOICE_INTERACTION.md](VOICE_INTERACTION.md) | Interaction vocale PTT à deux clics (`VoiceInteractionController`) et validation VAD (`VoiceCaptureValidator`) |
| [architecture_tools_registry_ai_mcp.md](architecture_tools_registry_ai_mcp.md) | Registre Local d'Outils (`ToolRegistry`), appels d'outils IA et MCP |

---

## Rapports de préparation de release

| Version | Dossier | Contenu |
|---|---|---|
| **v0.6.7.2** | [`docs/release/v0.6.7.2/`](release/v0.6.7.2/RELEASE_NOTES.md) | Audit sécurité, validation installation/exemples/showcases, résultats tests, Release Notes et Checklist |
| v0.6.6.2 | [`docs/release/v0.6.6.2/`](release/v0.6.6.2/FINAL_RELEASE_READINESS_REPORT.md) | Rapport historique de préparation à la diffusion |

---

## Rapports techniques & de livraison historiques

Classés de la version la plus récente à la plus ancienne. L'historique détaillé des modifications est maintenu dans le [CHANGELOG](../CHANGELOG.md).

| Version | Rapport | Thème |
|---|---|---|
| v0.6.7.0 | [VOICE_INTERACTION_v0.6.7.0.md](reports/VOICE_INTERACTION_v0.6.7.0.md) | Interaction vocale 2 clics & VAD holistique |
| v0.6.4.3 | [RAPPORT_CORRECTIVE_v0.6.4.3.md](rapports/RAPPORT_CORRECTIVE_v0.6.4.3.md) | Correctifs ToolRegistry, VoiceUxLiveBridge, PTT & Stabilisation |
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

---

## Outils de validation

Les scripts Python de validation des exemples et du matériel sont regroupés dans [`tools/validation/`](../tools/validation/README.md).
