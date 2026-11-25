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
- Aperti: GUI FM6 + selezione file .syx (fase GUI); Op Env a 4 rate/level non nativi (approssimazione ADSR); SIMD su unison.
