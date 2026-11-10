# Outils de validation matérielle

Scripts Python pilotant un M5Stack CoreS3 via le port série (115 200 bauds) pour rejouer des protocoles de validation sur matériel réel.

| Script | Cible | Journal produit |
|---|---|---|
| `validate_v06322.py` | `examples/OpenAIConversation` (protocole v0.6.3.2.2) | `hardware_validation_trace.log` |
| `validate_lvgl_v010.py` | `examples/OpenAIConversationLVGL` (v0.1.0.x) | `hardware_lvgl_validation.log` |
| `test_voice_turn.py` | Test rapide d'un tour vocal PTT (`P` / `R`) | — (sortie console) |

## Utilisation

```powershell
pip install pyserial
python tools/validation/validate_v06322.py
```

- Le port série est défini par la constante `PORT` en tête de chaque script (`COM4` par défaut) : adaptez-la à votre poste.
- Les journaux `*.log` sont écrits dans le répertoire courant et sont ignorés par Git.
