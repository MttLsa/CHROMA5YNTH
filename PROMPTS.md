# CHROMA5YNTH – Prompt per Claude Code

## Come usarli (risparmio token)
1. Usa Claude Code (terminale o app), non la chat: lavora direttamente sui file e non ricarica la specifica a ogni messaggio.
2. Metti CLAUDE.md, SPEC.md e un PROGRESS.md vuoto nella cartella del progetto prima della Fase 0.
3. Una fase = una sessione. Alla fine scrivi `/clear` e incolla il prompt della fase successiva.
4. Se una sessione si allunga: `/compact`.
5. Modello: Sonnet per le fasi normali, Opus solo per le fasi marcate ★ (DSP difficile).
6. Per correggere un bug incolla SOLO l'errore e il nome del file, non tutta la conversazione.

---

### FASE 0 – Scaffolding
Leggi CLAUDE.md e SPEC.md §8. Crea: CMake + JUCE 8 (FetchContent), target VST3/AU/Standalone "CHROMA5YNTH", `params/` con schema YAML, `tools/gen_params.py` (genera ParamIDs, GUI generica, tabelle manuale), Catch2, workflow GitHub Actions (macOS Universal + Windows, pluginval, auval, artifact installer .pkg/.exe vuoti funzionanti, release su tag `v*`). Il plugin deve caricarsi in un host e passare pluginval. Commit, PROGRESS.md, stop.

### FASE 1 – Core ★
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §1 e §2 (escluse FILTER LIBRARY e LAYER FX). Implementa: sistema 12 Layer con IEngine, pool voci 12/24/36, SCALE, MPE, ENV, LFO, Mod Matrix, Voice, Performance, smoothing, Master Level + limiter −1 dBTP, 8 macro globali, automation slot. Engine di test: sinusoide. Test: SCALE (assegnazioni, slot vuoto), voice stealing, limiter mai oltre −1 dBTP. Commit, PROGRESS.md, stop.

### FASE 2 – Filter Library + Drift ★
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §2 (FILTER LIBRARY, DRIFT). Implementa tutti i modelli ZDF con non-linearità e auto-oscillazione stabile 44.1–192 kHz. Test: risposta in frequenza, stabilità a resonance max, niente NaN/denormal. Commit, PROGRESS.md, stop.

### FASE 3 – Engine ANALOG
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §3A. Oscillatori band-limited, SuperSaw, sync/FM/ring, sub, noise, mixer e routing. Test aliasing (sotto −80 dB a 4×). Commit, PROGRESS.md, stop.

### FASE 4 – FX ★
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §2 (LAYER FX) e §4 (MASTER FX). Implementa le FX una volta sola in src/shared/fx e riusale tra Layer e Master. Commit, PROGRESS.md, stop.

### FASE 5 – Engine FM6
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §3C. Commit, PROGRESS.md, stop.

### FASE 6 – Engine WAVETABLE + editor
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §3D. Prima il motore e le tabelle di fabbrica, poi l'editor. Commit, PROGRESS.md, stop.

### FASE 7 – Engine ADDITIVE ★
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §3B. Obiettivo CPU: 64 parziali × 24 voci sotto il 15% di un core a 48 kHz. Commit, PROGRESS.md, stop.

### FASE 8 – Engine PHASE DISTORTION
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §3E. Commit, PROGRESS.md, stop.

### FASE 9 – Preset, Global, MIDI
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §5 e §6 (senza preset di fabbrica). Browser, Multi/Layer preset, Undo/Redo, A/B, Randomize, MIDI learn, microtuning. Commit, PROGRESS.md, stop.

### FASE 10 – GUI definitiva
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §7. Sostituisci la GUI generica. Ridimensionabile, scala 75–200%, a 60 fps senza pesare sull'audio. Commit, PROGRESS.md, stop.

### FASE 11 – Preset di fabbrica + normalizzazione LUFS
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §6. Crea tools/presetnorm, poi 64 preset (almeno 8 per engine + 24 multi che usano SCALE), normalizzati. Commit, PROGRESS.md, stop.

### FASE 12 – Manuale, installer, release
Leggi CLAUDE.md, PROGRESS.md, SPEC.md §8. Manuale completo in italiano (+ inglese) dalle tabelle generate, GitHub Pages, PDF. Tag v1.0.0 → release con link agli installer. Scrivimi solo i link finali. Stop.

### FASE 13 – QA finale
Leggi CLAUDE.md e PROGRESS.md. pluginval livello 10, auval, test CPU con 12 layer attivi e 36 voci, test automazione e salvataggio stato in host. Correggi e ripeti finché tutto è verde. Elenca in 10 righe cosa devo testare a orecchio.
