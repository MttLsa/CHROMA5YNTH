# PROGRESS (max 10 righe per fase)

## FASE 0 – Scaffolding ✅
- Identità: "TMFU audiovibe", codici Tmfu/Chr5, bundle com.tmfuaudiovibe.chroma5ynth (CMake, installer, About, manuale).
- CMake + JUCE 8.0.9 / Catch2 3.7.1 via FetchContent; target VST3/AU(mac)/Standalone; sorgenti condivisi plugin+test via `chroma_configure_target`.
- `params/*.yaml` (schema in params/README.md) → `tools/gen_params.py` → build/generated/{ParamIDs.h, ParamLayout.cpp, GuiLayout.h} + docs/manual/parameters.md.
- Param iniziali: master_volume, master_polyphony. Editor generico da GuiLayout + About.
- Installer: installer/macos/build_pkg.sh (.pkg VST3+AU), installer/windows/chroma5ynth.iss (Inno, VST3).
- CI .github/workflows/build.yml: macOS Universal + Win x64, ctest, pluginval (strict 10), auval, artifact installer, release su tag chroma5ynth-v*, firma/notarizzazione opzionale via secrets.
- CI verde (run 45685177069): macOS (macos-15-intel, Universal) build+test+auval+pluginval+.pkg; Windows build+test+pluginval+.exe.

## FASE 0.5 – Stile grafico ✅
- Riferimento: TMFU-audiovibe/DUALBOX `Source/gui/LookAndFeel` (piatto, scuro, accento blu/lime, Inter).
- `assets/fonts/` Inter Regular/SemiBold + OFL.txt → `juce_add_binary_data(chroma_assets)` (namespace `ChromaAssets`). Nessuna immagine necessaria.
- `src/gui/StyleLookAndFeel.{h,cpp}`: `chroma::style` (colori, font, misure), StyleLookAndFeel, FineSlider, Knob, Segmented, Panel, drawHeader.
- `STYLE.md` (regole), CLAUDE.md: riga "GUI: usa sempre STYLE.md e StyleLookAndFeel"; font in assets/fonts.
- Editor generico: header wordmark + Panel per sezione, Knob (float/int), Segmented (choice/bool). Build + ctest verdi (Linux).

## FASE 1 – Core ✅
- params: layer.yaml (12×13 comuni host), macro.yaml (8), automation.yaml (256 slot), master.yaml (+Scale 12 maschere, MPE), layer_engine.yaml (`scope: layer`, interni: Voice, Performance, 4 ENV, 4 LFO, Mod Matrix 16). gen_params: `blocks`+`repeat`, array, `choices_from`, `gui: false` → LayerParams.h.
- src/shared: Envelope, Lfo, ModMatrix, Voice, MidiState(MPE), Scale, Smoothing, ParamRange, TripleBuffer, Limiter (true-peak 4×, look-ahead, −1 dBTP, latenza riportata).
- src/core: Synth (pool 12/24/36, stealing più vecchia→più silenziosa con crossfade 2 ms, Max Voices/Layer, mono/legato/glide, sustain/sostenuto, SCALE, MPE, mixer Layer, macro→qualunque param, automation slot), LayerParamStore; copy/paste/init/save/load layer.
- src/engines: IEngine + SineEngine (test, unison). Tutti gli engine mappati su Sine fino alle fasi §3.
- Editor generico: viewport scrollabile, ComboBox per choice > 4. Stato: child CORE (layer interni, macro, automation).
- Test: SCALE (mappa, slot vuoto), stealing (oldest, quietest), Max Voices, sustain, legato, MPE, automation/macro state, limiter ≤ −1 dBTP (meter 16×). 18/18 verdi (Linux).
- Aperti: GUI SCALE (tastiera) e assegnazione macro/automation via click destro → fase GUI; Drift con filtri.

## FASE 2 – Filter Library + Drift ✅
- params/layer_engine.yaml: sezione Filter (On, Model 23 modelli, Cutoff, Resonance, Drive, Key Track, Env Amount→Mod Env 1, Velocity, LFO Amount→LFO 1) + Drift (On, Amount, Drift Only). Filter default Off.
- src/shared/Filter.{h,cpp}: core ZDF/TPT N-poli risolto implicitamente (I−gA)⁻¹ + saturatore nel loop; modelli Ladder/OTA/Cascade/Classic LP24, Diode LP (ladder accoppiato, k e ω calibrati), Sallen-Key LP/HP12, Bridge LP12, SVF LP/BP/HP/Notch (stato BP saturato). Digitali: LP12–48/HP12–24 Butterworth TPT, BP, Notch, Comb ±, Formant A-E-I-O-U. Cutoff interpolato esponenziale, coeff. ogni 4 campioni, dither −126 dB per l'auto-oscillazione, guardia NaN/denormal.
- src/shared/Drift.h: random walk lenti per oscillatore (±12 ct) + tolleranza cutoff/res per voce. EngineVoiceParams.driftSemis; SineEngine lo applica.
- Voice: filtro per voce dopo l'engine (anche nel crossfade di stealing), spento/Drift Only = zero CPU.
- tests/test_filter.cpp: risposta LP/HP/BP/Notch/Comb/Formant, accuratezza cutoff 44.1–192 kHz, auto-oscillazione intonata (±10%) e limitata, hot input + sweep, no denormal, Drift, integrazione Voice. 26/26 verdi (Linux).

## FASE 3 – Engine ANALOG ✅
- params/layer_engine_analog.yaml (scope layer): 3 OSC (On, Wave ×8, Octave, Semitone, Fine, Level, Pan, Phase, Phase Reset, PW, SuperSaw Detune/Spread/Voices, Stereo Width, Wave Mod), Mod (Hard Sync 2→1, FM 3→1/2→1, Ring 1×2), Mixer (Volume/Pan/Mute/Solo/Route per OSC, Sub/Noise Level+Route, Osc Master), Sub, Noise.
- src/shared/Blep.h (BLEP/BLAMP 4 punti da B-spline cubica), Oversampler.h (decimatore halfband Kaiser 95/23 tap, 2/4/8×).
- src/engines/analog/AnalogEngine: oscillatori in oversampling interno (default 2×, `setOversampling` 1/2/4/8 → da collegare al Global Settings §5), stepper generico con breakpoint (salti di valore e pendenza), sync band-limited, FM lineare through-zero, ring, SuperSaw 2–7, unison/detune/spread/drift dal Voice; Sub (−1/−2 ott., segue OSC 1), Noise White/Pink + Color + Width a sample rate base; sorgenti inattive = zero CPU.
- IEngine: render → bool (bus Bypass post-filtro), EngineVoiceParams.base/offsets + `engineParam()` (valori modulati per voce). Voice somma il bypass dopo il filtro. Synth: engine Analog preallocati per Layer, cambio engine = kill voci del Layer; altri engine ancora Sine.
- tests/test_analog.cpp: aliasing a 4× < −80 dB (misurato −106…−167 dB, 8 forme × 440–7040 Hz + Sub), pitch/wave mod, sync, FM, ring, sub, noise, mute/solo, routing, unison/drift, integrazione Synth. 31/31 verdi (Linux).
- Aperti: SIMD su unison/SuperSaw (3 OSC, SuperSaw 7×8 unison a 4× ≈ 8% core per voce); verifica legale del nome d'onda "SuperSaw" (da SPEC); GUI dedicata in fase GUI.

## FASE 4 – FX ✅
- params/layer_fx.yaml (scope layer, 70 param: 8 FX con On/Off; float = destinazioni Mod Matrix, aggiunte in coda alla lista) + params/master_fx.yaml (host, 48 param: Drive/Chorus/Delay/Reverb/Filter).
- src/shared/fx (header-only, un solo DSP per Layer e Master): FxUtil (DelayLine Hermite, SVF/one-pole TPT, biquad RBJ, Bypass 5 ms), Distortion (9 tipi superset, non lineari a 2× halfband, dry allineato alla latenza, compensazione di volume, Tone tilt), Chorus (Custom, Chorus I/II BBD, Dimension 1–4 in controfase), Phaser 2–12 stadi, Flanger, Delay (Digital/Tape/BBD/Ping-Pong/Dual L/R/Multitap, sync, HP/LP in loop, width, modulation, ducking), Reverb (FDN 16 Hadamard + plate Dattorro; Room/Plate/Hall/Large Hall), Eq3, Compressor (soft knee, linkato), MasterFilter (HP/BP/LP 12 dB SVF + res, BP centro/larghezza compensato).
- FxChain: LayerFxChain (ordine fisso §2) e MasterFxChain (§4); FX spenta = non processata, chain che dorme dopo 2.5 s di silenzio in uscita (zero CPU), switch On/Off senza click.
- Synth: Engine → Layer FX (per Layer, parametri = base + offset Mod Matrix della voce più recente) → mixer → Somma → Master FX → Master Level/Limiter; Delay sync al tempo host.
- tests/test_fx.cpp: allineamento dry/wet distorsione, THD, modi chorus, notch phaser, tempi/feedback/ping-pong/multitap delay, RT60 reverb (±35%, 4 tipi) e stabilità a 20 s, EQ/compressore/filtro master, passthrough/sleep/switch delle chain, integrazione Synth. 40/40 verdi (Linux).
- CPU (Release, Linux): chain Layer completa (8 FX) ≈ 5.5% core @48 kHz; 12 Layer tutto acceso + Master ≈ 69% (caso peggiore). Bench: `chroma_tests "[.bench]"`.
- Aperti: 4 LFO Master (§4) non implementati; verifica legale del nome modo "Dimension" (da SPEC); SIMD su FDN/upsampler; GUI FX in fase GUI.

## FASE 5 – Engine FM6 ✅
- params/layer_engine_fm.yaml (scope layer): 6 Op (On, Ratio/Fixed, Ratio, Coarse, Fine, Fixed Hz, Detune, Wave ×8, Phase, Phase Reset, Sync, Feedback, Output Level, Velocity/AM Sens, Pan, key scaling Breakpoint/Depth/Curve ±Lin/±Exp, Rate Scaling) + ENV per Op (AHDSR, curve, Loop, Time Scale), Custom 6×6 + Out, Algorithm A/B + Morph, Mod Index, Pitch Env, FM LFO 1/2 depth (Pitch/Amp/Op su LFO 1/2 del Layer: Delay e Key sync = Mode Retrigger).
- src/engines/fm/FmEngine: PM a 6 operatori, tabella sinusoidale 4096 interpolata, 32 algoritmi classici (loop di feedback 4/6), Custom (diagonale = feedback), morph lineare di connessioni/feedback/carrier; legame ritardato di 1 campione per collegamenti "all'indietro", feedback mediato su 2 campioni; ramp per campione di ampiezze, pesi, gain, frequenze; Sync alla fondamentale; unison/detune/spread/drift; oversampling interno 2× (1/2/4/8) + decimatore halfband. Op spenti/muti/inascoltati = zero CPU. Filtro post-FM e Filter ENV = Filter Library del Voice (Mod Env 1).
- Legge livello: −6 dB per 1/12 di corsa (scala 0–99 → ×0.75 dB). IEngine: EngineVoiceParams.lfo (uscite LFO 1–4 della voce).
- src/engines/fm/FmSysex: import banchi .syx 32 voci (4104 byte con checksum o 4096 raw) → valori lp (algoritmo, feedback sull'Op giusto, ratio/fixed, key scaling, EG approssimati ad ADSR, Pitch EG, LFO 1, transpose, Amp Env neutro), nome patch.
- Synth: engine FM6 preallocato per Layer (indice 2). tests/test_fm.cpp: tabella algoritmi, sine pulita, sidebande = Bessel (±0.5 dB), Mod Index, feedback stabile/periodico, Ratio/Coarse/Fine/Fixed, key scaling, velocity, ENV/release, Custom = classico, Morph, Pitch Env, LFO pitch/amp/op, Sync, forme, pan/unison, aliasing 4× < −60 dB, import .syx, integrazione Synth. 59/59 verdi (Linux).
- CPU (Release, Linux): voce 6 Op con feedback a 2× ≈ 1.7% core @48 kHz. Bench: `chroma_tests "[.bench]"`.
- Windows annullato (richiesta utente): rimossi job CI windows e installer Inno; CLAUDE.md aggiornato (solo macOS).
- Aperti: GUI FM6 + selezione file .syx (fase GUI); Op Env a 4 rate/level non nativi (approssimazione ADSR); SIMD su unison.

## FASE 6 – Engine WAVETABLE + editor ✅
- params/layer_engine_wt.yaml (scope layer): 3 OSC (On, Table 46 fabbrica + User 1–3, Position, Scan, Volume, Octave/Semi/Fine, Detune Hz, Phase, Random Phase, Reset, Unison 1–16 + Detune/Spread/Width, Pan, Warp ×11 + Amount + Source OSC/Sub) + Advanced (Freeze, Blur, Tilt, Formant, Stretch, PD, Bit Depth, SR Reduction, Granular + Grain Size), Sub, Noise, Filter 2 + Routing Serial/Parallel, Mix, Pan, Stereo.
- src/engines/wavetable: Wavetable (mipmap 10 ottave band-limited via FFT, guard per Hermite, spettri per frame, TableSlot lock-free per tabelle utente), WavetableFactory (46 tabelle procedurali × 32 frame, nomi generici), WavetableEngine (lettura Hermite 4 punti + lineare tra frame, oversampling 2× default, warp, path spettrale = tabella per voce risintetizzata ogni 64 campioni con crossfade, granulare a 2 teste, sorgenti spente = zero CPU), WavetableDoc (modello editor).
- Voice: Filter 2 + routing Serial/Parallel/Mix/Pan/Stereo per engine con `dualFilter()` (F2: Env = Mod Env 2, LFO = LFO 2). Unison per-OSC sostituisce l'Unison del Layer per questo engine.
- Editor (src/gui/WavetableEditor, pulsante "WT Editor"): Import/Export WAV (2048/frame, ≤256), Add/Remove/Duplicate, Resample/Morph con interpolazione None/Linear/Spectral, Smooth, Normalize, Draw mode, editor armonico FFT ampiezza/fase, vista 3D, Apply → slot User del Layer (salvato nello stato e in copy/paste del Layer).
- tests/test_wavetable.cpp: banco/nomi, mipmap band-limited, pitch/morph, aliasing (< −65 dB a 2×, < −80 dB a 4×), warp, spettrali, unison/advanced, sub/noise/zero CPU, editor model, user table + stato, Filter 2 routing. 70/70 verdi (Linux).
- CPU (Release, Linux): voce 3 OSC × 4 unison (1 spettrale) a 2× ≈ 4.6% core @48 kHz. Memoria banco di fabbrica ≈ 38 MB (statico, condiviso).
- Aperti: SIMD su unison; Bend/Sync/Asym aliasing solo contenuto dall'oversampling (no BLEP); GUI definitiva in fase GUI.

## FASE 7 – Engine ADDITIVE ✅
- params/layer_engine_add.yaml (scope layer, 63 param): Partials 1–256, Spectrum A/B (13 forme di fabbrica + Custom), Morph, Distribution, Tilt, Odd/Even, Balance, Brightness/Slope, Density, Inharmonicity, Stretch, Shift, Phase Reset; Partial ENV (Amount, ADSR, Curve, Scaling, Distribution); Spectral ENV (ADSR, Amount, Curve, Tilt, Position); FM (Amount, Ratio, Frequency, Feedback, Depth, Env); AM (Amount, Freq, Depth = controfase pari/dispari, Wave, Env); Stereo (Width, Spread, Partial Stereo Position, Stereo Modulation, Mid/Side).
- src/engines/additive/AdditiveSpectrum: dati per parziale (Ratio, Freq fissa, Amp, Phase, Detune, Fine, Pan, Polarity, Mute, Solo), forme procedurali, CustomSpectrum A/B lock-free per Layer (LayerParamStore, stato + copy/paste) per l'editor a barre (GUI in fase GUI).
- AdditiveEngine: banco oscillatori scelto al posto dell'IFFT (FM/glide/pan per parziale esatti, nessuna latenza): seno polinomiale senza diramazioni x(¼−x²)P(x²), vettorizzato lungo i campioni; glide, FM e rampe d'ampiezza come somme prefisse per blocco (FM = 1 FMA in più). Spettro condiviso tra voci con cache a chiave (ricalcolo solo se cambia un input). Parziali oltre 0.45·fs (incl. unison + deviazione FM) sfumate a 0, parziali a 0 = zero CPU. Partial ENV analitico per parziale (tempo × n^Scaling). Unison/drift dal Voice. Synth: engine indice 1.
- tests/test_additive.cpp: fast math, forme, sine/saw, Nyquist (no aliasing), odd/even/tilt/slope/balance/density, morph/distribution, ratio/fixed/detune/fine/inharm/stretch/shift, mute/solo/polarity/phase, Partial/Spectral ENV, FM (Bessel), AM (+controfase), stereo, unison/glide/release, integrazione Synth + stato. 84/84 verdi (Linux).
- CPU (Release, Linux, container): 24 voci × 64 parziali ≈ 11% core @48 kHz (obiettivo < 15%); + FM/Partial ENV/Stereo mod ≈ 17%. Bench: `chroma_tests "Additive: CPU*"`.
- Aperti: editor a barre/numerico in fase GUI; SIMD esplicito (NEON/AVX) non necessario per ora (auto-vettorizzazione SSE2/NEON).

## FASE 8 – Engine PHASE DISTORTION ✅
- params/layer_engine_pd.yaml (scope layer, 55 param): 2 Line (On, Wave ×8 Saw/Square/Pulse/Double Sine/Saw-Pulse/Reso 1–3, Octave, Coarse, Fine, Pitch, Phase, Level, Pan), Distortion per linea (PD Amount, Shape Linear/Smooth/Ripple/Steps/Custom, Shape Morph, Curve, Symmetry, Asymmetry, Offset, Warp, Window, PD Mix), PD ENV per linea (ADSR, Amount, Velocity, Key Track), Ring Mod, Noise Mod, PD Output. ENV 2/3, LFO, Amp Env e Mod Matrix = quelli del Layer (destinazioni PD Amount, Morph, Symmetry, Offset, Warp, Pitch, Level, Pan…).
- src/engines/pd/PdCurve.h: curva di fase Custom (≤32 punti, Linear/Smooth/Cubic, estremi fissi (0,0)/(1,1)) → tabella lock-free; in LayerParamStore (stato PD_CURVE + copy/paste Layer). Editor a punti in fase GUI.
- src/engines/pd/PdEngine: fase master continua, mappa a nodi per forma (Amount 0 = sinusoide pura), easing dei segmenti per Shape + morph nel dominio della fase, Curve/Warp con curva razionale (pendenze limitate), lettura coseno o risonanza finestrata (Reso, k = 1–16× limitato sotto Nyquist); ginocchio minimo legato alla frequenza (band-limited) + oversampling 2× default (1/2/4/8) e decimatore halfband; tutti i controlli rampati per campione; Ring/Noise sulla Linea 2; DC blocker; linee spente = zero CPU. Synth: engine indice 4 (sostituisce Sine).
- tests/test_pd.cpp: chiusura/continuità della mappa (forme × shape × parametri casuali), sine pura ad Amount 0, brillantezza vs Amount, spettri (Square solo dispari, Symmetry 50%/100%), Reso, pitch, modulazione senza click, aliasing 4× < −70 dB, Shape/Morph/Curve/Custom, Warp/Asym/Offset, PD ENV, Ring/Noise, unison/pan, integrazione Synth + stato. 97/97 verdi (Linux).
- CPU (Release, Linux): voce 2 linee con morph/curve/ring a 2× ≈ 2.3% core @48 kHz. Bench: `chroma_tests "PD: CPU*"`.
- Aperti: editor a punti della curva Custom e GUI PD in fase GUI; SIMD/ottimizzazione per unison alto (mappa preparata per campione).

## FASE 9 – Preset, Global, MIDI ✅ (senza preset di fabbrica)
- params/global.yaml: Master Tune (A4 415–466 Hz), Oversampling realtime (2×) / offline (4×) → setOversampling di tutti gli engine (isNonRealtime). Global + MPE + MIDI map + microtuning = impostazioni globali: nel progetto, esclusi da preset/undo.
- src/shared/Tuning: parser .scl (cents/ratio), .kbm (mappa, riferimento, tasti 'x' = 12-TET), .tun AnaMark ([Tuning]/[Exact Tuning], BaseFreq) → tabella 128 offset; Voice interpola l'offset sul glidePitch (+ Master Tune). Stato CORE/TUNING.
- MIDI learn (Synth): learn armato → primo CC sull'audio thread, finalizzato da idle(); layer param via store atomico, host param override immediato + push all'host dal message thread; 1 CC = 1 parametro, canale specifico opzionale; MIDIMAP nello stato + file .c5midi.
- src/preset/PresetManager: Multi (.c5multi, stato completo) / Layer (.c5layer, qualunque slot), metadati nome/categoria/autore/tag, ricerca a parole, filtri, preferiti (favourites.txt), Save/Load/Rename/Delete/Import/Export; libreria ~/Library/Audio/Presets/TMFU audiovibe/CHROMA5YNTH.
- History (undo/redo 50 passi a snapshot, checkpoint dopo 0.4 s di inattività via contatori di modifica; A/B con copia), Randomizer (globale/Layer/sezione o prefisso, quantità, parametri protetti), Init. Preset load/A-B/Randomize/Init annullabili.
- GUI generica: toolbar Presets/Undo/Redo/A/B/A>B/Randomize+quantità/Init/Tuning/MIDI, PresetBrowser overlay, clic destro sui controlli → MIDI Learn (menu in italiano). Manuale: docs/manual/presets-global.md.
- tests/test_global.cpp (16 casi): tuning/parsers, pitch misurato con Master Tune e microtuning, oversampling RT/offline, MIDI learn, undo/redo/idle, A/B, randomize, init, preset Multi/Layer, browser. 113/113 verdi (Linux).
- Aperti: MIDI learn solo sui parametri host nella GUI generica (layer param via API, GUI definitiva); tools/presetnorm e 64 preset di fabbrica in fase dedicata.

## FASE 10 – GUI definitiva ✅
- Editor nuovo (src/PluginEditor.*, src/gui/ParamUI.*, MainPanels.*): header + toolbar (preset, undo/redo, A/B, randomize, init, tuning, MIDI, zoom, About), barra Layer 1–12 (engine ▾, on/M/S, voci, meter; clic destro copia/incolla/init/preset Layer), schede Engine/Voice/Modulation/Layer FX/Master FX/Global, colonna Master (meter L/R, volume, polifonia) + Macro + Scale (tastiera a maschere di Layer).
- Scala vettoriale 75–200 % (base 1280×800 via AffineTransform), oltre il 200 % il layout si allarga; limite massimo = schermo intero (+ voce "Schermo intero" nel menu zoom). Dimensione salvata nello stato host.
- Binding generico host/Layer (`Binding`, Knob/Segmented::bind), pannelli generati da params (Desc + unit/description nel generatore), header power, viste ENV/LFO, Mod Matrix a tabella (destinazioni a sottomenu), clic destro: default, MIDI learn, assegna a Macro, slot automazione host (aperti fase 1/9 chiusi).
- Editor dedicati: Additive Custom Spectrum A/B (barre per campo), curva di fase PD (punti), import banco FM .syx, apertura WT Editor.
- 60 fps senza pesare sull'audio: VBlankAttachment limitato a 60 Hz, meter lock-free (picchi atomici scritti 1×/blocco), controlli a 30 Hz solo pagina visibile (≈ 0.003 ms/pass), meter opachi.
- Fix: stringhe UTF-8 (sezioni "·", tooltip italiani) via `utf8()`.
- tests/test_gui.cpp: scala/limiti/stato, tutte le pagine engine, meter; snapshot dev `"GUI: snapshots"`. 116/116 verdi (Linux, xvfb).
- Aperti: editor tabella LFO Custom; GUI tuning/MPE avanzata solo via menu.
