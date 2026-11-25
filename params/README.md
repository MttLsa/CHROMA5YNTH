# params/ – unica fonte dei parametri

Ogni file `*.yaml` contiene una lista `params:`. Campi:

| Campo | Obbl. | Descrizione |
|---|---|---|
| `id` | sì | ID stabile (snake_case, mai cambiarlo dopo un rilascio) |
| `name` | sì | Nome mostrato all'host e nella GUI |
| `type` | sì | `float` \| `int` \| `bool` \| `choice` |
| `min`, `max` | float/int | Range |
| `default` | sì | Valore di default (per `choice`: indice o etichetta) |
| `unit` | no | Unità (dB, Hz, ms, %, st, …) |
| `skew` | no | Punto centrale del range per lo skew (float) |
| `step` | no | Passo (float) |
| `choices` | choice | Lista di etichette |
| `section` | sì | Sezione GUI/manuale (es. Master, Layer, Macro) |
| `description` | sì | Testo per il manuale |
| `version` | no | Version hint VST3/AU (default 1; incrementare solo per parametri aggiunti in versioni successive) |
| `automatable` | no | Esposto all'host (default true) |

`tools/gen_params.py` genera ParamIDs.h, ParamLayout.cpp, GuiLayout.h (in build/) e `docs/manual/parameters.md`.
