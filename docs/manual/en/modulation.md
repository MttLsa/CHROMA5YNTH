# Voice, filters and modulation

These modules are shared by all engines (*Voice* and *Modulation* tabs; the **Filter** is the last module of the *Engine* tab).

## Voice
- **Mode**: Mono, Poly, Unison, Legato. **Max Voices** per Layer.
- **Unison** 1–16 with Detune, Spread, Pan (Wavetable uses per-oscillator unison).
- **Note Priority**: Last, First, Lowest, Highest. Retrigger.
- **Glide**: On/Off, 0–5 s, Always/Legato, Linear/Exponential.

## Performance
Pitch Bend Range ±1–48, Velocity Curve, Aftertouch Sensitivity, Sustain, Sostenuto, Expression, Mod Wheel Amount.

## Filter Library
ZDF/TPT filters with in-loop non-linearity and in-tune self-oscillation. Parameters: On, **Model**,
Cutoff 20 Hz–20 kHz, Resonance, Drive, Key Track, Env Amount (→ Mod Env 1), Velocity, LFO Amount (→ LFO 1). Off by default.

| Kind | Models |
|---|---|
| Analog | Ladder LP24, Diode LP, SVF LP12/BP12/HP12/Notch, OTA LP24, Cascade LP24, Sallen-Key LP12/HP12, Bridge LP12, Classic LP24 |
| Digital | Digital LP12/24/36/48, Digital HP12/24, Digital BP, Digital Notch, Comb +, Comb −, Formant (A-E-I-O-U) |

## Drift
**On**, **Amount**: slow random pitch micro-variations per voice/oscillator (up to ±12 ct) and filter component
tolerance (cutoff/resonance differ per voice). **Drift Only**: filter bypassed, pitch drift active.

## Envelopes (ENV)
4 envelopes per Layer: **Amp Env** and **Mod Env 1–3**. Attack 0–10 s, Hold 0–10 s, Decay 0–10 s, Sustain 0–100 %,
Release 0–20 s, per-segment curve (Linear/Exponential/Logarithmic), Velocity, Key Tracking, Loop, Retrigger,
Time Scale, Amount ±100 %.

## LFO
4 LFOs per Layer. Shape: Sine, Triangle, Square, Saw Up, Saw Down, S&H, Random Smooth, Custom. Rate 0.01–30 Hz
or **Sync** (1/1…1/32, triplets T, dotted D), Phase 0–360°, Offset, Delay, Fade In/Out, Mode
Free/Retrigger/One-shot, Smooth, Stereo Phase, Unipolar/Bipolar, Amount.

## Mod Matrix
16 slots per Layer: **Source**, **Destination**, Amount ±100 %, **Via** (source scaling the amount), Curve
(Linear, Exponential, Logarithmic, S-Curve), Smoothing, Invert, Enable.

- **Sources**: LFO 1–4, Amp Env, Mod Env 1–3, Velocity, Release Velocity, Aftertouch, MPE Pressure/Slide/Pitch,
  Mod Wheel, Pitch Wheel, Key Track, Note Number, Random (per note), Breath (CC2), Expression (CC11),
  MIDI CC (any number), Macro 1–8.
- **Destinations**: any continuous Layer parameter, including Layer FX (sub-menus by section).

## Macros
8 Macros in the Master column, automatable from the host. Right-click a control → *assign to Macro* → Macro 1–8.
A Macro can drive any parameter, also across Layers, and is also a Mod Matrix source.
