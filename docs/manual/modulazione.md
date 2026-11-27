# Voce, filtri e modulazione

Questi moduli sono comuni a tutti gli engine (scheda *Voice* e *Modulation*; il **Filter** è l'ultimo modulo della scheda *Engine*).

## Voice
- **Mode**: Mono, Poly, Unison, Legato. **Max Voices** per Layer.
- **Unison** 1–16 con Detune, Spread, Pan (Wavetable usa l'unison per oscillatore).
- **Note Priority**: Last, First, Lowest, Highest. Retrigger.
- **Glide**: On/Off, 0–5 s, Always/Legato, Linear/Exponential.

## Performance
Pitch Bend Range ±1–48, Velocity Curve, Aftertouch Sensitivity, Sustain, Sostenuto, Expression, Mod Wheel Amount.

## Filter Library
Filtri ZDF/TPT con non-linearità nel loop e auto-oscillazione intonata. Parametri: On, **Model**, Cutoff 20 Hz–20 kHz,
Resonance, Drive, Key Track, Env Amount (→ Mod Env 1), Velocity, LFO Amount (→ LFO 1). Default: spento.

| Tipo | Modelli |
|---|---|
| Analogici | Ladder LP24, Diode LP, SVF LP12/BP12/HP12/Notch, OTA LP24, Cascade LP24, Sallen-Key LP12/HP12, Bridge LP12, Classic LP24 |
| Digitali | Digital LP12/24/36/48, Digital HP12/24, Digital BP, Digital Notch, Comb +, Comb −, Formant (A-E-I-O-U) |

## Drift
**On**, **Amount**: micro-variazioni lente e casuali del pitch per voce/oscillatore (±12 ct al massimo) e tolleranza
dei componenti del filtro (cutoff/risonanza diversi per voce). **Drift Only**: filtro bypassato, drift di pitch attivo.

## Inviluppi (ENV)
4 inviluppi per Layer: **Amp Env** e **Mod Env 1–3**. Attack 0–10 s, Hold 0–10 s, Decay 0–10 s, Sustain 0–100 %,
Release 0–20 s, curva per segmento (Linear/Exponential/Logarithmic), Velocity, Key Tracking, Loop, Retrigger,
Time Scale, Amount ±100 %.

## LFO
4 LFO per Layer. Shape: Sine, Triangle, Square, Saw Up, Saw Down, S&H, Random Smooth, Custom. Rate 0.01–30 Hz
oppure **Sync** (1/1…1/32, terzine T, puntate D), Phase 0–360°, Offset, Delay, Fade In/Out, Mode
Free/Retrigger/One-shot, Smooth, Stereo Phase, polarità Unipolar/Bipolar, Amount.

## Mod Matrix
16 slot per Layer: **Source**, **Destination**, Amount ±100 %, **Via** (sorgente che scala l'amount), Curve
(Linear, Exponential, Logarithmic, S-Curve), Smoothing, Invert, Enable.

- **Sorgenti**: LFO 1–4, Amp Env, Mod Env 1–3, Velocity, Release Velocity, Aftertouch, MPE Pressure/Slide/Pitch,
  Mod Wheel, Pitch Wheel, Key Track, Note Number, Random (per nota), Breath (CC2), Expression (CC11),
  MIDI CC (numero a scelta), Macro 1–8.
- **Destinazioni**: qualunque parametro continuo del Layer, incluse le Layer FX (menu a sottomenu per sezione).

## Macro
8 Macro nella colonna Master, automatizzabili dalla DAW. Clic destro su un controllo → *Assegna a Macro* → Macro 1–8.
Una Macro può guidare qualunque parametro, anche di Layer diversi; può essere anche sorgente nella Mod Matrix.
