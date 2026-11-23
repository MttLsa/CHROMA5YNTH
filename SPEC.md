# CHROMA5YNTH – Specifica v1 (compatta)
Legenda: [DEFAULT] = scelta presa, modificabile dall'autore. [AGGIUNTA] = non presente nella bozza originale.

## §1 ARCHITETTURA
- Fino a 12 Layer (synth) numerati 1–12. Default: solo Layer 1 attivo. Layer aggiungibili, accendibili, spegnibili.
- Per Layer [AGGIUNTA]: On/Off, Engine (ANALOG | ADDITIVE | FM6 | WAVETABLE | PD), Volume, Pan, Mute, Solo, Transpose ±48, Key range, Velocity range, MIDI channel (All/1–16), Copy/Paste/Init layer, Save/Load layer preset.
- Cambio engine: conserva i parametri dell'engine precedente finché non si salva (undo possibile).
- Flusso segnale: Engine → Layer FX (insert) → Layer mixer → Somma → Master FX → Master Filter → Master Level/Limiter.
- MPE: per-nota Pitch (bend range 48 default in MPE), Pressure, Slide (CC74). Zona Lower/Upper configurabile. Tutti gli engine ricevono le sorgenti MPE nella mod matrix.

### §1.1 SCALE (sezione Master)
- 12 slot = classi di nota C, C#, D … B (non intona, assegna). Per slot: selezione multipla dei Layer 1–12.
- Nota MIDI in ingresso → classe di nota → suonano solo i Layer assegnati a quello slot, al pitch della nota (si ripete su tutte le ottave).
- SCALE Off = ogni Layer attivo suona tutte le note (layering classico).
- Slot vuoto = nota muta (deciso dall'autore).
- Nessun preset di scale. UI stile "Scale" di Ableton: tastiera di un'ottava, ogni tasto mostra i numeri dei layer assegnati.

### §1.2 POLIFONIA
- Pool globale condiviso: 12 / 24 / 36 voci. Default 24.
- Ogni Layer ha Max Voices (limitato dal pool). Le voci di unison sono interne alla voce e non consumano il pool [DEFAULT].
- Voice stealing globale: più vecchia, poi più silenziosa.

### §1.3 AUTOMAZIONE HOST [AGGIUNTA]
- 12 layer × 5 engine produrrebbero migliaia di parametri (ingestibile in Logic/Ableton). Quindi [DEFAULT]:
  esposti all'host: tutti i parametri Master + 8 Macro + parametri comuni dei Layer (§1) + 256 "Automation Slot" assegnabili con click destro → "Assign to automation" su qualunque knob. ID stabili tra versioni.

## §2 MODULI CONDIVISI (implementati una volta)
- ENV: Attack 0–10 s, Hold 0–10 s, Decay 0–10 s, Sustain 0–100%, Release 0–20 s; curva per segmento (lin/exp/log); Velocity amt; Key tracking; Loop; Retrigger; Time scaling; Amount ±100%.
- LFO: Sine, Triangle, Square, Saw Up, Saw Down, S&H, Random smooth, Custom (disegnabile); Rate 0.01–30 Hz; Sync On/Off con 1/1…1/32 + terzine + puntate; Phase 0–360°; Offset; Delay; Fade in/out 0–10 s; Mode Free/Retrigger/One-shot; Smooth; Stereo phase; Polarità uni/bi; Amount.
- MOD MATRIX: 16 slot. Slot = Source, Destination, Amount ±100%, Via (sorgente che scala l'amount), Curve (lin/exp/log/S), Smoothing, Invert, Enable.
  Sorgenti: LFO dell'engine, ENV dell'engine, Velocity, Release velocity, Aftertouch, MPE Pressure/Slide/Pitch, Mod Wheel, Pitch Wheel, Key track, Note number, Random per nota, Breath (CC2), Expression (CC11), qualunque MIDI CC, Macro 1–8.
  Destinazioni: qualunque parametro continuo del Layer (incluse le FX del Layer).
  Modulazione audio-rate dove richiesto (FM), control-rate per il resto.
- VOICE: Mono / Poly / Unison / Legato; Max voices; Unison 1–16, Detune, Spread, Pan; Voice stealing; Note priority Last/First/Lowest/Highest; Retrigger; Glide On/Off, 0–5 s, Always/Legato, Linear/Exponential.
- PERFORMANCE: Pitch bend range ±1–48; Velocity curve; Aftertouch sens.; Sustain; Sostenuto; Expression; Mod wheel amount.
- FILTER LIBRARY (ZDF, non-lineare, auto-oscillazione). Parametri comuni: Cutoff 20 Hz–20 kHz, Resonance 0–100%, Drive, Key track 0–100%, Env amount ±100%, Velocity, LFO amount.
  Modelli analogici (nome UI generico → riferimento tecnico):
  Ladder LP24 (Moog transistor ladder) · Diode LP (TB-303) · SVF 12 LP/BP/HP/Notch (Oberheim SEM) · OTA LP24 (Roland IR3109, Jupiter/Juno) · Cascade LP24 (CEM3320/SSM2040, Prophet-5/OB-Xa) · Sallen-Key LP12/HP12 (Korg MS-20) · Steiner (Steiner-Parker) · ARP LP24 (ARP 4072).
  Modelli digitali: LP12/24/36/48, HP12/24, BP, Notch, Comb +/−, Formant (vocali A-E-I-O-U).
- DRIFT: On/Off, Drift amount: micro-variazioni lente e casuali di pitch per voce/oscillatore + tolleranza componenti del filtro. Opzione "Drift only" = filtro bypassato, drift di pitch attivo. Disponibile in tutti gli engine [DEFAULT] (nella bozza era solo ANALOG).
- LAYER FX (insert, ordine fisso, ciascuno On/Off, spento = zero CPU): Distortion → Chorus → Phaser → Flanger → Delay → Reverb → EQ → Compressor.
  Distortion: Type (Soft, Hard, Tube, Fuzz, Op-amp clip [RAT], Hard-clip pedal [DS-1], Bitcrush), Drive, Tone, Mix, Output.
  Chorus: Rate, Depth, Delay, Feedback, Width, Mix. Phaser: Rate, Depth, Feedback, Frequency, Stages 2–12, Mix. Flanger: Rate, Depth, Feedback, Delay, Stereo, Mix.
  Delay: Time L/R, Sync, Feedback, HP/LP, Ping-pong, Width, Mix. Reverb: Size, Decay, Pre-delay, Damping, Diffusion, Low/High cut, Width, Mix.
  EQ: 3 bande (Low shelf, Peak, High shelf) Freq/Gain/Q. Compressor: Threshold, Ratio, Attack, Release, Knee, Makeup, Mix.

## §3 ENGINE (solo le parti specifiche; tutto il resto da §2)

### §3A ANALOG
- 3 OSC, ciascuno: On, Wave (Sine, Triangle, Saw, Square, Pulse, SuperSaw, Half, Narrow), Octave ±4, Semitone ±12, Fine ±100 cent, Level, Pan, Phase 0–360°, Phase reset, Pulse width 1–99%, SuperSaw Detune/Spread/Voices 2–7, Stereo width, Wave mod 0–100%.
  [AGGIUNTA] Hard sync 2→1, FM 3→1 e 2→1 (audio-rate), Ring mod 1×2.
  (Il rumore è solo nel generatore Noise, per non duplicarlo negli oscillatori.)
- MIXER: Volume, Pan, Mute/Solo per OSC 1–3; Noise level; Sub level; Master osc level. Routing al filtro: ogni sorgente Filter/Bypass.
- SUB: On, Sine/Square, −1/−2 ott., Level, Phase 0–360°.
- NOISE: White/Pink, Level, Color, Pan, Stereo width.
- FILTER: un filtro dalla Filter Library + DRIFT. Filter ENV + Amp ENV (§2). Filter keyboard follow On/Off.
- 3 LFO [DEFAULT, bozza: almeno 2]. Mod Matrix §2.

### §3B ADDITIVE
- Parziali: 1–256 (default 64). Oltre Nyquist mutati automaticamente.
- Per parziale (editor a barre disegnabile + numerico): Ratio/Frequenza, Amp, Phase, Detune, Fine, Pan, Polarity, Mute, Solo.
- Spettro globale: Distribution, Tilt, Odd level, Even level, Balance, Slope, Brightness, Density, Inharmonicity, Stretch, Shift, Morph (spettro A ↔ B).
- Amp ENV (§2). Partial ENV: Amount, ADSR, Curve, Scaling, Distribution. Spectral ENV: ADSR, Amount, Curve, Tilt, Position.
- FM: Amount, Frequency, Ratio, Feedback, Depth, Env amount. AM: Amount, Freq, Depth, Wave, Env amount.
- Stereo: Width, Spread, Partial stereo position, Stereo modulation, Mid/Side balance.
- 2 LFO, Mod Matrix. Implementazione: banco oscillatori SIMD o IFFT overlap-add (scegli il più efficiente a 256 parziali × 24 voci).

### §3C FM6
- 6 operatori, ciascuno: Enable, Ratio/Fixed, Ratio, Coarse, Fine, Fixed Hz, Detune, Wave (Sine + 7 forme), Phase, Phase reset, Sync, Feedback, Output level, Velocity sens, AM sens, Pan.
  Key scaling: Breakpoint, Left/Right depth, Left/Right curve (−lin, −exp, +exp, +lin), Rate scaling.
  ENV per operatore (§2, con Loop e Time scaling).
- ALGORITMI: 32 algoritmi classici a 6 operatori + modalità Custom (matrice 6×6: qualunque op → qualunque op con amount e polarità, feedback su qualunque op, output level per carrier) + Algorithm Morph tra due algoritmi.
- PITCH ENV: ADSR + curve, Amount, Velocity, Key track, Retrigger.
- FILTER opzionale post-FM (Filter Library) + Filter ENV.
- 2 LFO (con Delay, Key sync, Pitch/Amp/Op depth), Mod Matrix (destinazioni extra: Op freq, Op level, Op feedback, Mod index).
- Import banchi .syx 6-op (formato 32 voci) [AGGIUNTA, solo formato file].

### §3D WAVETABLE
- 3 OSC, ciascuno: On, Table, Position, Scan, Volume, Octave, Semitone, Fine, Detune, Phase, Random phase, Phase reset, Unison 1–16 + Detune/Spread/Width, Pan, Warp mode (Bend±, PWM, Sync, Mirror, Asym, Quantize, FM da OSC/Sub, AM, Ring) + Warp amount. Sub OSC + Noise OSC con livello.
- 2 FILTRI dalla Filter Library: routing Serie/Parallelo, Mix, Pan, Stereo.
- Amp AHDSR + 3 Mod ENV (Loop, Trigger mode). 4 LFO. Mod Matrix + 8 Macro del Layer.
- EDITOR: Import/Export WAV (2048 campioni/frame, fino a 256 frame), Add/Remove/Duplicate frame, Interpolazione (none/linear/spectral), Morphing, Smoothing, Normalize, Draw mode, Editor armonico via FFT (ampiezza + fase), visualizzazione 3D.
- Tabelle mipmap band-limited. Wavetable di fabbrica generate proceduralmente (min. 40).
- Advanced: Spectral freeze/blur/tilt, Formant shift, Harmonic stretch, Phase distortion, Bit depth, SR reduction, Granular playback.

### §3E PHASE DISTORTION
- Principio centrale: il timbro nasce dalla distorsione di fase, nessun filtro sottrattivo necessario. Ogni parametro PD modulabile indipendentemente.
- 2 linee oscillatore [DEFAULT, bozza: 1] con Ring/Noise mod tra le linee.
- OSC: Pitch, Base wave (Saw, Square, Pulse, Double sine, Saw-pulse, Reso 1/2/3 con finestra Saw/Triangle/Trapezoid), Phase offset, Octave, Coarse, Fine.
- PD: Amount, Shape, Curve, Symmetry, Asymmetry, Offset, Warp, Window, Mix, Custom phase curve (editor a punti con interpolazione), Shape morph.
- PD ENV: ADSR, Amount, Velocity, Key track. + 2 ENV liberi, 2 LFO, Amp ENV, Mod Matrix (destinazioni PD Amount, Shape/Morph, Symmetry, Offset, Warp, Pitch, Amp, Pan).
- Continuità di fase garantita durante la modulazione, oversampling sul mapping di fase.

## §4 MASTER
- SCALE (§1.1), POLYPHONY 12/24/36 (§1.2).
- MASTER FX (ciascuno On/Off), ordine Drive → Chorus → Delay → Reverb → Filter [DEFAULT]:
  - CHORUS: modi "Chorus I", "Chorus II" (BBD stile Juno-60 I/II), "Dimension" (4 modi, stile Dimension D a cui si ispira il modello di Arturia). Rate/Depth solo in modalità Custom, Mix.
  - DRIVE: Tube, Overdrive, Op-amp clip [RAT], Hard-clip pedal [DS-1], Fuzz, Tape. Drive, Tone, Mix, Output.
  - DELAY: Digital, Tape, BBD analog, Ping-pong, Dual L/R, Multitap. Time/Sync, Feedback, HP/LP, Width, Modulation, Ducking, Mix.
  - REVERB: algoritmico (FDN 16 linee + modello plate Dattorro, no convoluzione). Tipi Room, Plate, Hall, Large Hall. Size, Decay, Pre-delay, Damping, Diffusion, Modulation, Low cut, High cut, Width, Mix.
  - FILTER: 3 knob HP / BP / LP, ciascuno On/Off. Chiusura speculare: HP sale da 20 Hz, LP scende da 20 kHz, BP centro + larghezza. Resonance per knob [DEFAULT].
- 4 LFO MASTER assegnabili a qualunque parametro della sezione Master (modulo LFO §2, sync al tempo host).
- 8 MACRO GLOBALI [AGGIUNTA]: assegnabili a qualunque parametro di qualunque Layer o del Master; Name, Value, Min, Max per assegnazione.
- MASTER LEVEL + limiter true-peak (look-ahead, oversampling 4×), ceiling −1 dBTP fisso. Meter Peak, RMS, LUFS short-term. CPU meter.

## §5 GLOBAL SETTINGS
- MIDI: MPE On/Off + zone, MIDI learn (click destro su ogni knob), mappature CC salvabili.
- Master tune (A4 415–466 Hz). Microtuning .scl/.tun [AGGIUNTA, opzionale].
- Qualità: Oversampling 1/2/4/8× (separato per realtime e render offline). Gestione sample rate 44.1–192 kHz.
- Undo/Redo, A/B Compare, Randomize (globale / layer / sezione, con quantità), Init.
- Parameter smoothing su tutto. CPU optimization (§CLAUDE.md DSP).

## §6 PRESET
- Due livelli: Multi preset (stato completo) e Layer preset (singolo synth, caricabile in qualunque slot).
- Browser: categorie, tag, autore, preferiti, ricerca, Save/Load/Rename/Delete, Import/Export.
- Normalizzazione: tool CLI `tools/presetnorm` che renderizza offline ogni preset (nota C3, velocity 100, 4 s) e salva un trim di gain per arrivare a −14 LUFS short-term max (deciso), mai sopra −1 dBTP.
- Preset di fabbrica: 64 [DEFAULT], tutti con nomi originali.

## §7 GUI
- Ridimensionabile (75–200%), vettoriale, tema scuro. Barra Layer 1–12 in alto (numero, engine, on/off, mute/solo), pannello engine al centro, Master + Scale in basso/destra.
- Prima versione: GUI generica generata da params.yaml. GUI definitiva solo nella fase dedicata.

## §8 CONSEGNA
- Repo GitHub. GitHub Actions: build macOS Universal + Windows x64, pluginval, auval, test.
- Installer: macOS .pkg (VST3 → /Library/Audio/Plug-Ins/VST3, AU → /Library/Audio/Plug-Ins/Components), Windows .exe (Inno Setup, VST3 → C:\Program Files\Common Files\VST3).
- Nessun Apple Developer ID: installer NON firmati. Prevedere comunque nel workflow firma/notarizzazione opzionale attivata solo se in futuro ci sono i secrets. Il manuale deve spiegare come aprire un installer non firmato su macOS (Impostazioni di Sistema → Privacy e sicurezza → "Apri comunque") e su Windows (SmartScreen → "Ulteriori informazioni" → "Esegui comunque").
- Release GitHub con tag = link pubblici agli installer.
- Manuale: `docs/manual/` in Markdown (tabelle parametri generate da params.yaml) → GitHub Pages + PDF allegato alla release.
