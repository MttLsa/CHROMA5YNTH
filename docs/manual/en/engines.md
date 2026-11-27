# Engines

Each Layer uses one of five engines. Voice, filter, envelopes, LFOs, Mod Matrix and Layer FX are shared
(see [Voice, filters and modulation](modulation.md)); only engine-specific parts are described here.
The oscillators of Analog, FM6, Wavetable and PD run with internal oversampling (**Oversampling** 2× in real time,
**Offline Oversampling** 4× when bouncing; *Global* tab). Disabled sources use no CPU.

## Analog
Virtual-analog subtractive engine with band-limited oscillators.

- **3 OSC**: On, Wave (Sine, Triangle, Saw, Square, Pulse, SuperSaw, Half, Narrow), Octave ±4, Semitone ±12,
  Fine ±100 ct, Level, Pan, Phase, Phase Reset, Wave Mod, **Route** (Filter / Bypass); Pulse Width 1–99 % only with
  Pulse; SuperSaw Detune/Spread/Voices 2–7 and Stereo Width only with SuperSaw. An OSC at Level 0 is still a silent
  sync/FM/ring source.
- **Mod**: Hard Sync 2→1, FM 3→1 and 2→1 (linear, audio rate), Ring 1×2.
- **Sub**: Sine/Square, −1/−2 octaves below OSC 1, Level, Phase, Route.
- **Noise**: Level, White/Pink, Color, Pan, Stereo Width, Route.
- **Output**: overall level of the sources before the filter. (The former Mixer module is gone: level and pan live in
  the oscillators.)

## Additive
Bank of up to **256 partials** (default 64); partials above Nyquist are faded out automatically.

- **Spectrum A/B** (Sine, Saw, Square, Triangle, Pulse 25%, Flat, Organ, Hollow, Bell, Vocal A, Vocal O, Strings,
  Glass, Custom) with **Morph** between A and B and **Distribution** (Spectrum, Harmonic, Odd, Octaves).
- Shaping: Tilt, Odd/Even, Balance, Brightness/Slope, Density, Inharmonicity, Stretch, Shift, Phase Reset.
- **Partial ENV**: per-partial envelope with time scaling by partial index.
- **Spectral ENV**: envelope moving spectral tilt/position over time.
- **FM** (Amount, Ratio, Frequency, Feedback, Depth, Env) and **AM** (Amount, Freq, Depth in odd/even anti-phase, Wave, Env).
- **Stereo**: Width, Spread, partial stereo position, Stereo Modulation, Mid/Side.
- **Custom editor** (Engine tab): bars for Amp, Ratio, Phase, Pan, Detune… of single partials, for Custom
  spectra A and B (saved with the Layer).

## FM6
**6-operator** FM (phase modulation).

- Per operator: On, Ratio/Fixed, Ratio, Coarse, Fine, Fixed Hz, Detune, Wave (Sine, Half Sine, Abs Sine,
  Pulse Sine, Alt Sine, Camel Sine, Square, Saw), Phase, Phase Reset, Sync, Feedback, Output Level,
  Velocity and AM Sens, Pan, key scaling (Breakpoint, Depth, ±Lin/±Exp curves), Rate Scaling.
  **Output Level**: amplitude = value² (50 % ≈ −12 dB as a carrier; clearly audible modulation over most of the
  travel). .syx banks are converted with the same law, so they sound as before.
- **Per-operator ENV**: AHDSR with curves, Loop and Time Scale.
- **Algorithm**: 32 classic algorithms + **Custom** (6×6 matrix + outputs; the diagonal is feedback).
  **Algorithm A/B + Morph** blends connections, feedback and carriers between two algorithms.
- Global **Mod Index**, **Pitch Env**, LFO depths to pitch, amplitude and operators.
- **.syx import**: standard 32-voice 6-operator sysex banks → converted patch (envelopes approximated as AHDSR).
- Output level law: −6 dB per 1/12 of travel.

## Wavetable
Three wavetable oscillators with band-limited mipmaps.

- **Table**: 46 factory tables (Basic Shapes, PWM, Vowels, Formant Sweep, Bell, Drawbars, FM, Wavefolder,
  Choir, Brass, Glass, Growl…) + **User 1–3**.
- Per OSC: Position, Scan, Volume, Octave/Semi/Fine, Detune Hz, Phase, Random Phase, Reset, Unison 1–16
  (Detune/Spread/Width), Pan, **Warp** (Bend +/−/±, PWM, Sync, Mirror, Asym, Quantize, FM, AM, Ring) with Amount and
  source (OSC 1–3, Sub).
- **Advanced**: Freeze, Blur, Tilt, Formant, Stretch, PD, Bit Depth, SR Reduction, Granular + Grain Size.
- Sub and Noise; **Filter 2** with Serial/Parallel routing, Mix, Pan and Stereo (Filter 2: Env = Mod Env 2, LFO = LFO 2).
- **WT Editor** (button on the Engine tab): WAV Import/Export (2048 samples per frame, up to 256 frames),
  Add/Remove/Duplicate, Resample/Morph (None, Linear, Spectral), Smooth, Normalize, freehand drawing,
  harmonic editor (amplitude/phase), 3D view. **Apply** writes the table to a User slot of the Layer
  (saved in the project and in presets).

## Phase Distortion (PD)
Two **Lines** that bend the phase of a cosine.

- Per Line: On, Wave (Saw, Square, Pulse, Double Sine, Saw-Pulse, Reso 1–3), Octave, Coarse, Fine, Pitch,
  Phase, Level, Pan.
- **Distortion**: PD Amount (0 = pure sine), Shape (Linear, Smooth, Ripple, Steps, Custom), Shape Morph,
  Curve, Symmetry, Asymmetry, Offset, Warp, Window, PD Mix.
- **PD ENV** per Line: ADSR, Amount, Velocity, Key Track.
- **Ring Mod** and **Noise Mod** on Line 2, **PD Output**.
- **Custom curve**: point editor (up to 32 points, Linear/Smooth/Cubic) on the Engine tab, saved with the Layer.
