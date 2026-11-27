# Engine

Ogni Layer usa uno dei cinque engine. Voice, filtro, inviluppi, LFO, Mod Matrix e Layer FX sono comuni
(vedi [Voce, filtri e modulazione](modulazione.md)); qui sono descritte solo le parti specifiche.
Gli oscillatori di Analog, FM6, Wavetable e PD lavorano in oversampling interno (**Oversampling** 2× in tempo reale,
**Offline Oversampling** 4× in bounce; scheda *Global*). Sorgenti spente non usano CPU.

## Analog
Sottrattivo virtuale-analogico con oscillatori band-limited.

- **3 OSC**: On, Wave (Sine, Triangle, Saw, Square, Pulse, SuperSaw, Half, Narrow), Octave ±4, Semitone ±12,
  Fine ±100 ct, Level, Pan, Phase, Phase Reset, Wave Mod, **Route** (Filter / Bypass del filtro); Pulse Width 1–99 %
  solo con Pulse; SuperSaw Detune/Spread/Voices 2–7 e Stereo Width solo con SuperSaw. Un OSC a Level 0 resta
  sorgente silenziosa di sync/FM/ring.
- **Mod**: Hard Sync 2→1, FM 3→1 e 2→1 (lineare, audio-rate), Ring 1×2.
- **Sub**: Sine/Square, −1/−2 ottave rispetto a OSC 1, Level, Phase, Route.
- **Noise**: Level, White/Pink, Color, Pan, Stereo Width, Route.
- **Output**: livello complessivo delle sorgenti prima del filtro. (Il vecchio modulo Mixer non c'è più: livello e pan
  sono negli oscillatori.)

## Additive
Banco fino a **256 parziali** (default 64); le parziali oltre Nyquist vengono sfumate automaticamente.

- **Spectrum A/B** (Sine, Saw, Square, Triangle, Pulse 25%, Flat, Organ, Hollow, Bell, Vocal A, Vocal O, Strings,
  Glass, Custom) con **Morph** tra A e B e **Distribution** (Spectrum, Harmonic, Odd, Octaves).
- Modellazione: Tilt, Odd/Even, Balance, Brightness/Slope, Density, Inharmonicity, Stretch, Shift, Phase Reset.
- **Partial ENV**: inviluppo per parziale con scaling del tempo in funzione dell'indice.
- **Spectral ENV**: inviluppo che sposta tilt/posizione dello spettro nel tempo.
- **FM** (Amount, Ratio, Frequency, Feedback, Depth, Env) e **AM** (Amount, Freq, Depth in controfase pari/dispari, Wave, Env).
- **Stereo**: Width, Spread, posizione stereo delle parziali, Stereo Modulation, Mid/Side.
- **Editor Custom** (scheda Engine): barre per Amp, Ratio, Phase, Pan, Detune… delle singole parziali, per gli
  spettri Custom A e B (salvati con il Layer).

## FM6
Sintesi FM (phase modulation) a **6 operatori**.

- Per operatore: On, Ratio/Fixed, Ratio, Coarse, Fine, Fixed Hz, Detune, Wave (Sine, Half Sine, Abs Sine,
  Pulse Sine, Alt Sine, Camel Sine, Square, Saw), Phase, Phase Reset, Sync, Feedback, Output Level,
  Velocity e AM Sens, Pan, key scaling (Breakpoint, Depth, curve ±Lin/±Exp), Rate Scaling.
  **Output Level**: ampiezza = valore² (50 % ≈ −12 dB come carrier, indice di modulazione ben udibile su quasi tutta la
  corsa). I banchi .syx vengono convertiti con la stessa legge, quindi suonano come prima.
- **ENV per operatore**: AHDSR con curve, Loop e Time Scale.
- **Algorithm**: 32 algoritmi classici + **Custom** (matrice 6×6 + uscite; la diagonale è il feedback).
  **Algorithm A/B + Morph** sfuma connessioni, feedback e carrier tra due algoritmi.
- **Mod Index** globale, **Pitch Env**, profondità LFO verso pitch, ampiezza e operatori.
- **Import .syx**: banchi a 32 voci in formato sysex standard (6 operatori) → patch convertita
  (inviluppi approssimati ad AHDSR).
- Livello d'uscita: −6 dB ogni 1/12 di corsa.

## Wavetable
Tre oscillatori wavetable con mipmap band-limited.

- **Table**: 46 tabelle di fabbrica (Basic Shapes, PWM, Vowels, Formant Sweep, Bell, Drawbars, FM, Wavefolder,
  Choir, Brass, Glass, Growl…) + **User 1–3**.
- Per OSC: Position, Scan, Volume, Octave/Semi/Fine, Detune Hz, Phase, Random Phase, Reset, Unison 1–16
  (Detune/Spread/Width), Pan, **Warp** (Bend +/−/±, PWM, Sync, Mirror, Asym, Quantize, FM, AM, Ring) con Amount e
  sorgente (OSC 1–3, Sub).
- **Advanced**: Freeze, Blur, Tilt, Formant, Stretch, PD, Bit Depth, SR Reduction, Granular + Grain Size.
- Sub e Noise; **Filter 2** con routing Serial/Parallel, Mix, Pan e Stereo (Filter 2: Env = Mod Env 2, LFO = LFO 2).
- **WT Editor** (pulsante nella scheda Engine): Import/Export WAV (2048 campioni per frame, max 256 frame),
  Add/Remove/Duplicate, Resample/Morph (None, Linear, Spectral), Smooth, Normalize, disegno a mano libera,
  editor armonico (ampiezza/fase), vista 3D. **Apply** scrive la tabella in uno slot User del Layer
  (salvato nel progetto e nei preset).

## Phase Distortion (PD)
Due **Line** che deformano la fase di un coseno.

- Per Line: On, Wave (Saw, Square, Pulse, Double Sine, Saw-Pulse, Reso 1–3), Octave, Coarse, Fine, Pitch,
  Phase, Level, Pan.
- **Distortion**: PD Amount (0 = sinusoide pura), Shape (Linear, Smooth, Ripple, Steps, Custom), Shape Morph,
  Curve, Symmetry, Asymmetry, Offset, Warp, Window, PD Mix.
- **PD ENV** per Line: ADSR, Amount, Velocity, Key Track.
- **Ring Mod** e **Noise Mod** sulla Line 2, **PD Output**.
- **Curva Custom**: editor a punti (fino a 32, Linear/Smooth/Cubic) nella scheda Engine, salvata con il Layer.
