# Presets, global settings and MIDI

## Presets
- **Multi preset** (`.c5multi`): full state (Layers, Master, Macros, Automation Slots). Global settings are not
  included: Master Tune, Oversampling, MPE, MIDI learn mappings and microtuning stay as they are.
- **Layer preset** (`.c5layer`): a single synth (common + engine parameters), loadable into any slot 1–12.
- Library: `~/Library/Audio/Presets/TMFU audiovibe/CHROMA5YNTH/{Multi,Layer}`; favourites in `favourites.txt`.
- Browser (**Presets** button): Multi/Layer, search (all words over name, category, author, tags), category/tag/author
  filters, favourites, Save / Load (double-click) / Rename / Delete / Import / Export.
- **Factory presets**: **24 Multis** using all 12 synths with NOTE ASSIGN (Synth 1 = C, Synth 2 = C#, … Synth 12 = B):
  the 12 synths of a Multi share a sound family but each has its own character/timbre/filter. The list starts with
  *01 Bass Multi*, then Pad, Synth, Lead, Keys, Pluck, Chord, Percussive, Bell, Organ, Strings, Brass and 12 free
  Multis. **128 Layers** (one synth, every note): Bass, Keys, Lead, Pad, Pluck, Chord and Percussive. They are embedded in the plug-in and copied to `{Multi,Layer}/Factory` on first use.
  List: [factory-presets.md](factory-presets.md).
- **Loudness**: each factory preset is rendered offline (note C3, velocity 100, 4 s) and trimmed to −14 LUFS
  short-term max, never above −1 dBTP (**Preset Trim** for Multis, Layer **Trim** for Layer presets).

## Undo/Redo, A/B, Randomize, Init
- **Undo/Redo** (50 steps): every change becomes a step after 0.4 s of inactivity; preset loading, A/B, Randomize and
  Init are undoable. Global settings are excluded.
- **A/B**: two independent states; B starts as a copy of A. **A>B** copies the current state to the other slot.
- **Randomize** (amount slider): global (active Layers + Master FX), Layer, or section. Never touched: Master Volume,
  polyphony, Scale, MPE, Global, Macros, Automation Slots, Layer On/Engine/Volume/Mute/Solo/Transpose/ranges.
- **Init**: resets **only the selected synth** (its On state is kept); the other 11 synths, Master, global settings and
  MIDI mappings stay.

## MIDI learn
- Right-click a control → *MIDI Learn*, then move a controller: the CC (any channel) is assigned.
  One CC controls one parameter; *remove MIDI Learn* clears the mapping.
- **MIDI** menu: save/load mappings (`.c5midi`), clear all. Mappings are saved with the project.

## Tuning
- **Master Tune**: A4 from 415 to 466 Hz.
- **Microtuning** (**Tuning** menu): Scala `.scl` files (a `.kbm` with the same name in the same folder is used as
  keyboard mapping) or AnaMark `.tun`. Default `.scl` mapping: degree 0 on C3 (note 60), A4 = 440 Hz.
  Unmapped keys stay equal-tempered. Microtuning adds to Master Tune and is saved with the project.

## Oversampling
**Oversampling** (real time, default 2×) and **Offline Oversampling** (render/bounce, default 4×) for the oscillators
of Analog, FM6, Wavetable and PD.
