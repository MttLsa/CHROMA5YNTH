# CHROMA5YNTH – Regole di lavoro (leggere sempre, per primo)

## Risparmio token (obbligatorio)
- Leggi SOLO la sezione di SPEC.md indicata dal prompt di fase + PROGRESS.md. Mai l'intera SPEC.
- Non rileggere file già letti nella sessione. Usa grep/ricerche mirate invece di aprire file interi.
- Modifiche mirate (edit), mai riscrivere file interi esistenti.
- Output in chat: niente spiegazioni, niente riepiloghi del codice. Solo: file toccati, esito build/test, problemi aperti.
- Non chiedere conferme per scelte già decise in SPEC.md. Le voci marcate [DEFAULT] sono decise.
- A fine fase: build verde + test verdi → commit → aggiorna PROGRESS.md (max 10 righe per fase) → STOP.

## Identità plugin (obbligatoria)
- Nome prodotto: CHROMA5YNTH. Produttore (come appare nelle DAW): "TMFU audiovibe" (scritto esattamente così).
- JUCE: COMPANY_NAME "TMFU audiovibe", PLUGIN_MANUFACTURER_CODE "Tmfu", PLUGIN_CODE "Chr5", BUNDLE_ID "com.tmfuaudiovibe.chroma5ynth".
- AU: manufacturer "TMFU audiovibe", nome "TMFU audiovibe: CHROMA5YNTH". VST3 vendor "TMFU audiovibe". Installer, manuale e About usano lo stesso produttore.

## Stack
- JUCE 8, C++20, CMake (JUCE via FetchContent), Catch2 per test, pluginval + auval per validazione.
- Formati: VST3, AU, Standalone (solo per test). macOS Universal (x86_64 + arm64), min macOS 10.13. Windows x64 VST3 (deciso).
- Build ufficiali e installer SOLO via GitHub Actions (il Mac dello sviluppatore è un MacBook Pro 2012 Intel con connessione hotspot: evitare download pesanti in locale).

## Architettura del codice
- Parametri definiti SOLO in `params/*.yaml` (id, nome, range, default, unità, sezione, descrizione).
  `tools/gen_params.py` genera: ParamIDs C++, layout GUI generico, tabelle del manuale. Mai hard-code di parametri.
- Moduli condivisi in `src/shared/` (ENV, LFO, ModMatrix, Voice, Filter library, Drift, FX). Gli engine li riusano, non li duplicano.
- Ogni engine in `src/engines/<nome>/`, interfaccia comune `IEngine` (prepare, noteOn/Off, MPE per-nota, render block).

## DSP
- Audio thread: zero allocazioni, zero lock, zero I/O. Denormals disattivati.
- Smoothing su ogni parametro continuo. Oscillatori band-limited (PolyBLEP/minBLEP o mipmap). Filtri ZDF/TPT con non-linearità.
- Layer spenti, FX spenti e voci silenziose = zero CPU.
- SIMD dove utile (additive, unison, wavetable).

## Legale
- Nessun marchio di terzi nella UI, nei preset o nel manuale (Moog, Roland, Juno, TB-303, Dimension D, RAT, DS-1, Prophet, ecc.). Usa nomi generici ("Ladder 24", "Diode 303-style" NO → "Diode LP"). Nel codice e nei commenti puoi citare il riferimento tecnico.
- Wavetable e preset di fabbrica generati da noi, nessun materiale di terzi.
