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
