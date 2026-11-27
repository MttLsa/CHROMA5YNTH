# Effetti e Master

## Layer FX
Catena insert per ogni Layer, in ordine fisso; ogni effetto ha On/Off e quando è spento non usa CPU.
La catena si mette a riposo dopo 2.5 s di silenzio.

**Distortion → Chorus → Phaser → Flanger → Delay → Reverb → EQ → Compressor**

| Effetto | Parametri principali |
|---|---|
| Distortion | Type (Soft, Hard, Tube, Fuzz, Op-Amp Clip, Pedal Clip, Bitcrush), Drive, Tone, Mix, Output. Oversampling 2×, compensazione di volume. |
| Chorus | Rate, Depth, Delay, Feedback, Width, Mix |
| Phaser | Rate, Depth, Feedback, Frequency, Stages 2–12, Mix |
| Flanger | Rate, Depth, Feedback, Delay, Stereo, Mix |
| Delay | Time L/R, Sync, Feedback, HP/LP nel loop, Ping-Pong, Width, Mix |
| Reverb | Size, Decay, Pre-delay, Damping, Diffusion, Low/High Cut, Width, Mix |
| EQ | 3 bande: Low Shelf, Peak, High Shelf (Freq, Gain, Q) |
| Compressor | Threshold, Ratio, Attack, Release, Knee, Makeup, Mix |

I parametri continui delle Layer FX sono destinazioni della Mod Matrix.

## Master FX
Sulla somma dei Layer (scheda *Master FX*), tutti automatizzabili dalla DAW:

| Effetto | Tipi / parametri |
|---|---|
| Drive | Tube, Overdrive, Op-Amp Clip, Pedal Clip, Fuzz, Tape |
| Chorus | Chorus I, Chorus II (BBD), Dimension 1–4 (ensemble in controfase), Custom |
| Delay | Digital, Tape, BBD, Ping-Pong, Dual L/R, Multitap; sync al tempo della DAW, modulazione, ducking |
| Reverb | Room, Plate, Hall, Large Hall |
| Master Filter | HP / BP / LP 12 dB con risonanza |

## Uscita
**Master Volume** (−60…+6 dB) → **Limiter** true-peak a −1 dBTP (sempre attivo, look-ahead, latenza riportata alla DAW).
**Preset Trim** (non visibile) è il trim di loudness scritto nei preset di fabbrica e si somma al Master Volume.
