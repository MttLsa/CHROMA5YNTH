# Effects and Master

## Layer FX
Insert chain per Layer, in fixed order; each effect has On/Off and uses no CPU when off.
The chain goes to sleep after 2.5 s of silence.

**Distortion → Chorus → Phaser → Flanger → Delay → Reverb → EQ → Compressor**

| Effect | Main parameters |
|---|---|
| Distortion | Type (Soft, Hard, Tube, Fuzz, Op-Amp Clip, Pedal Clip, Bitcrush), Drive, Tone, Mix, Output. 2× oversampling, level compensation. |
| Chorus | Rate, Depth, Delay, Feedback, Width, Mix |
| Phaser | Rate, Depth, Feedback, Frequency, Stages 2–12, Mix |
| Flanger | Rate, Depth, Feedback, Delay, Stereo, Mix |
| Delay | Time L/R, Sync, Feedback, HP/LP in the loop, Ping-Pong, Width, Mix |
| Reverb | Size, Decay, Pre-delay, Damping, Diffusion, Low/High Cut, Width, Mix |
| EQ | 3 bands: Low Shelf, Peak, High Shelf (Freq, Gain, Q) |
| Compressor | Threshold, Ratio, Attack, Release, Knee, Makeup, Mix |

Continuous Layer FX parameters are Mod Matrix destinations.

## Master FX
On the sum of all Layers (*Master FX* tab), all automatable from the host:

| Effect | Types / parameters |
|---|---|
| Drive | Tube, Overdrive, Op-Amp Clip, Pedal Clip, Fuzz, Tape |
| Chorus | Chorus I, Chorus II (BBD), Dimension 1–4 (anti-phase ensemble), Custom |
| Delay | Digital, Tape, BBD, Ping-Pong, Dual L/R, Multitap; host tempo sync, modulation, ducking |
| Reverb | Room, Plate, Hall, Large Hall |
| Master Filter | HP / BP / LP 12 dB with resonance |

## Output
**Master Volume** (−60…+6 dB) → true-peak **Limiter** at −1 dBTP (always on, look-ahead, latency reported to the host).
**Preset Trim** (hidden) is the loudness trim stored in factory presets and is added to Master Volume.
