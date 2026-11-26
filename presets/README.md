# presets/ – preset di fabbrica (§6)

- `recipes/*.txt`: ricette testuali (sintassi in testa a `tools/presetnorm/main.cpp`), unica fonte dei preset di fabbrica.
- `factory/{Multi,Layer}/`: file generati, normalizzati e compatti (solo valori diversi dal default), incorporati nel
  plugin (`juce_add_binary_data` → `ChromaFactory`) e installati in `<libreria>/{Multi,Layer}/Factory`.

Rigenerare (build con `-DCHROMA_BUILD_TOOLS=ON`):

```bash
cmake -S . -B build -DCHROMA_BUILD_TOOLS=ON && cmake --build build --target presetnorm
build/presetnorm_artefacts/Release/presetnorm build presets/recipes presets/factory --report docs/manual/factory-presets.md
```

`presetnorm norm <file...>` normalizza preset esistenti sul posto; `presetnorm measure <file...>` stampa loudness e true peak.
Normalizzazione: render offline (oversampling offline), C3 velocity 100, 4 s a 48 kHz; trim → −14 LUFS short-term max
(BS.1770-4, finestra 3 s), true peak 4× ≤ −1.1 dBTP (il limiter d'uscita non interviene).
