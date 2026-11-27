# Overview

## Layers
CHROMA5YNTH contains up to **12 Layers**, each a complete synthesizer. A new instance starts with Layer 1 only.
Per Layer: On/Off, **Engine** (Analog, Additive, FM6, Wavetable, PD), Volume, Pan, Mute, Solo, Transpose ±48 st,
key range, velocity range, MIDI channel (All/1–16), Copy/Paste/Init and Save/Load as a Layer preset.
A Layer that is off uses no CPU. Switching engine keeps the previous engine's parameters (undoable).

## Signal flow
```
Engine → Filter → Layer FX → Layer mixer   (×12 Layer)
→ Sum → Master FX → Master Filter → Master Volume → Limiter
```
The final limiter is true-peak (4×, look-ahead) with a −1 dBTP ceiling; its latency is reported to the host.

## Polyphony
- Global pool of **12 / 24 / 36 voices** (default 24) shared by all Layers.
- Each Layer has **Max Voices** (limited by the pool). Unison voices live inside a voice and do not use the pool.
- Global voice stealing: oldest first, then quietest, with a 2 ms crossfade.

## SCALE
A one-octave keyboard in the Master column with 12 slots (C, C#, … B). In each slot choose which Layers play that
pitch class, in every octave. SCALE does not retune: it assigns notes to Layers.

- **SCALE Off**: every active Layer plays every note (classic layering).
- Empty slot: the note is silent.
Examples: white/black key split, one Layer per scale degree, diatonic chords via Layer Transpose.

## MPE
Per-note pitch (48 st bend range in MPE), Pressure and Slide (CC74). Lower/Upper zone via the **MPE Zone** parameter
(Master). All engines receive MPE Pitch/Pressure/Slide as Mod Matrix sources.

## Host automation
To avoid exposing thousands of parameters the host sees: all Master and Master FX parameters, 8 **Macros**, the common
parameters of the 12 Layers and 256 **Automation Slots**. Right-click any control → *assign to automation slot*:
the slot then drives that internal parameter. Parameter IDs are stable across versions.

## Interface
- **Header and toolbar**: Presets, Undo/Redo, A/B, A>B, Randomize (+ amount), Init, Tuning, MIDI, zoom, About.
- **Layer bar 1–12**: engine ▾, On, M (mute), S (solo), active voices and meter. Right-click: copy/paste/init, Layer presets.
- **Tabs**: *Engine*, *Voice*, *Modulation*, *Layer FX*, *Master FX*, *Global*.
- **Master column**: L/R meters, Master Volume, Polyphony, 8 Macros and SCALE.
- **Zoom** 75–200 % (zoom menu or drag the corner); beyond 200 % the layout grows up to full screen.
  The size is saved with the project.
- **Controls**: drag to adjust, Shift + drag for fine adjustment, double-click = default value.
  Right-click: default value, MIDI Learn, assign to Macro, assign to automation slot.
