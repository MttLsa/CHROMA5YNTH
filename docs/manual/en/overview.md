# Overview

## Layers
CHROMA5YNTH contains up to **12 Layers**, each a complete synthesizer. A new instance starts with Layer 1 only.
Per Layer: On/Off, **Engine** (Analog, Additive, FM6, Wavetable, PD), Volume, Pan, Mute, Solo, Transpose ±48 st,
key range, velocity range, MIDI channel (All/1–16), Copy/Paste/Init and Save/Load as a Layer preset.
A Layer that is off uses no CPU. Switching engine keeps the previous engine's parameters (undoable).

## Signal flow
```
Engine → Filter → Layer FX → GLOBAL SCALE → Layer mixer   (×12 Layer)
→ Sum → Master FX → Master Filter → Master Volume → Limiter
```
The final limiter is true-peak (4×, look-ahead) with a −1 dBTP ceiling; its latency is reported to the host.

## Polyphony
- Global pool of **12 / 24 / 36 voices** (default 24) shared by all Layers.
- Each Layer has **Max Voices** (limited by the pool). Unison voices live inside a voice and do not use the pool.
- Global voice stealing: oldest first, then quietest, with a 2 ms crossfade.

## NOTE ASSIGN
A one-octave keyboard in the Master column (C, C#, … B). **Click a note** → drop-down menu with **Synth 1–12** and
**Off**: that note (in every octave) plays only the chosen synth; Off = silent note. The key shows the number of the
assigned synth. NOTE ASSIGN does not retune: it assigns notes to synths.

- **NOTE ASSIGN off** (header switch): every active synth plays every note (classic layering).
- Factory Multis assign Synth 1 to C, Synth 2 to C#, … Synth 12 to B.

## GLOBAL SCALE
**Real-time** pitch correction of each synth's output (after the Layer FX) towards the notes of the chosen scale (the
Ableton Live scale list, root C–B). Useful when automation or modulation physically changes the pitch of the sound
(FM ratio, wavetable position, Pitch…): the sound is pulled continuously and smoothly to the nearest scale note. It
does not change parameters, only the sound. Audio engine and pitch recognition are the same as Groovulation's MELODIC
modules (polyphonic phase vocoder + YIN fundamental detection).

- **Mode Poly**: every spectral component moves to the nearest scale note (chords, inharmonic sounds).
- **Mode Mono**: detects the fundamental and shifts the whole sound by the same interval: harmonic timbre preserved
  (monophonic leads and basses).
- **Amount**: strength (0 % = original sound). **Smooth**: width of the transition between two notes (0 % = hard
  step, higher = softer glide). Accuracy ≈ 1 cent on a steady tone.
- Latency ≈ 64 ms while on (reported to the DAW, which compensates it on playback); off = no latency, no CPU.

## MPE
Per-note pitch (48 st bend range in MPE), Pressure and Slide (CC74). Lower/Upper zone via the **MPE Zone** parameter
(Master). All engines receive MPE Pitch/Pressure/Slide as Mod Matrix sources.

## Host automation
To avoid exposing thousands of parameters the host sees: all Master and Master FX parameters, 8 **Macros**, the common
parameters of the 12 Layers and 256 **Automation Slots**. Right-click any control → *assign to automation slot*:
the slot then drives that internal parameter. Parameter IDs are stable across versions.

## Interface
- **Header and toolbar**: Presets, Undo/Redo, A/B, A>B, Randomize (+ amount), Init (selected synth only), Tuning,
  MIDI, zoom, About.
- **Synth bar 1–12**: engine ▾, On, M (mute), S (solo), active voices and meter. Right-click: copy/paste/init, Layer presets.
- **Tabs**: *Engine*, *Voice*, *Modulation*, *Layer FX*, *Master FX*, *Global*. Modules follow the signal flow (left to
  right, then top to bottom): on the Engine tab the **Filter** is the last module of the chain; in FM6 each operator
  sits next to its envelope (Op 1 – Op 1 Env, …); in Wavetable each OSC sits next to its Advanced module; in PD each
  Line is followed by its Distortion and PD Env.
- **Colours**: orange = modules and parameters; blue = selected synth and tab, NOTE ASSIGN, GLOBAL SCALE; white =
  selected toolbar buttons, Master and Macro.
- **Master column**: L/R meters, Master Volume, Polyphony, 8 Macros, NOTE ASSIGN and GLOBAL SCALE.
- **Mode-dependent controls** appear only when relevant: SuperSaw controls only with the SuperSaw wave, Pulse Width
  only with Pulse, Unison Detune/Spread/Pan only with Unison > 1, Glide Time/Mode/Curve only with Glide on, LFO Rate
  or Division according to Sync, FM operator Ratio or Fixed Hz according to its mode.
- **Mod Env 2, Mod Env 3 and LFO 1–4** that are not assigned (Mod Matrix or the fixed routings: Filter LFO Amount,
  Wavetable Filter 2 Env/LFO, FM6 LFO depths) are greyed out and disabled ("not assigned"). Mod Env 1 is the filter
  envelope (**Filter Envelope**); with Wavetable, Mod Env 2 is the Filter 2 envelope.
- Changing engine closes the windows of other engines (e.g. the Wavetable Editor).
- **Zoom** 75–200 % (zoom menu or drag the corner); beyond 200 % the layout grows up to full screen.
  The size is saved with the project.
- **Controls**: drag to adjust, Shift + drag for fine adjustment, double-click = default value.
  **Typed values**: click a knob and type a number (or double-click the value), Enter applies, Esc cancels; units
  are understood (`250 ms`, `1.2 s`, `1.2k`, `45 %`), list entries by name.
  Right-click: default value, MIDI Learn, assign to Macro, assign to automation slot.
