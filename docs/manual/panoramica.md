# Panoramica

## Layer
CHROMA5YNTH contiene fino a **12 Layer**, ognuno un sintetizzatore completo. All'apertura è attivo solo il Layer 1.
Per ogni Layer: On/Off, **Engine** (Analog, Additive, FM6, Wavetable, PD), Volume, Pan, Mute, Solo, Transpose ±48 st,
key range, velocity range, canale MIDI (All/1–16), Copy/Paste/Init e Save/Load come Layer preset.
Un Layer spento non usa CPU. Cambiare engine conserva i parametri dell'engine precedente (annullabile con Undo).

## Flusso del segnale
```
Engine → Filter → Layer FX → GLOBAL SCALE → Layer mixer   (×12 Layer)
→ Somma → Master FX → Master Filter → Master Volume → Limiter
```
Il limiter finale è true-peak (4×, look-ahead) con tetto a −1 dBTP; la sua latenza è comunicata alla DAW.

## Polifonia
- Pool globale di **12 / 24 / 36 voci** (default 24), condiviso da tutti i Layer.
- Ogni Layer ha **Max Voices** (limitato dal pool). Le voci di unison sono interne alla voce e non consumano il pool.
- Voice stealing globale: prima la voce più vecchia, poi la più silenziosa, con crossfade di 2 ms.

## NOTE ASSIGN
Sezione nella colonna Master: una tastiera di un'ottava (Do, Do#, … Si). **Clic su una nota** → menu a tendina con
**Synth 1–12** e **Off**: la nota (su tutte le ottave) suona solo il synth scelto; Off = nota muta. Sul tasto è
scritto il numero del synth assegnato. NOTE ASSIGN non intona: assegna le note ai synth.

- **NOTE ASSIGN spento** (interruttore nell'intestazione): ogni synth attivo suona tutte le note (layering classico).
- I Multi di fabbrica assegnano il Synth 1 al Do, il Synth 2 al Do#, … il Synth 12 al Si.

## GLOBAL SCALE
Correzione d'intonazione **in tempo reale** sull'uscita di ogni synth (dopo le Layer FX), verso le note della scala
scelta (le stesse scale di Ableton Live, tonica Do–Si). Utile quando automazioni o modulazioni cambiano fisicamente il
pitch del suono (es. Ratio FM, posizione wavetable, Pitch): il suono viene "attratto" in modo continuo e graduale sulla
nota di scala più vicina. Non modifica i parametri, solo il suono. Motore audio e riconoscimento del pitch sono gli
stessi dei moduli MELODIC di Groovulation (phase vocoder polifonico + rilevamento YIN della fondamentale).

- **Mode Poly**: ogni componente dello spettro viene spostata sulla nota di scala più vicina (accordi, suoni inarmonici).
- **Mode Mono**: riconosce la fondamentale e sposta tutto il suono dello stesso intervallo: timbro armonico intatto
  (ideale per lead e bassi monofonici).
- **Amount**: forza della correzione (0 % = suono originale). **Smooth**: larghezza della transizione tra due note
  (0 % = scatto netto, valori alti = passaggio più morbido). Precisione ≈ 1 cent su un suono stabile.
- Latenza ≈ 64 ms quando è attivo (comunicata alla DAW, che la compensa in riproduzione); spento = zero latenza e zero CPU.

## MPE
Pitch per nota (bend range 48 st in MPE), Pressure e Slide (CC74). Zona Lower/Upper con il parametro **MPE Zone** (Master).
Tutti gli engine ricevono MPE Pitch/Pressure/Slide come sorgenti della Mod Matrix.

## Automazione dalla DAW
Per non esporre migliaia di parametri, la DAW vede: tutti i parametri Master e Master FX, 8 **Macro**, i parametri
comuni dei 12 Layer e 256 **Automation Slot**. Clic destro su qualunque controllo → *Assegna a slot automazione*:
lo slot guida quel parametro interno. Gli ID dei parametri restano stabili tra le versioni.

## Interfaccia
- **Header e toolbar**: Presets, Undo/Redo, A/B, A>B, Randomize (+ quantità), Init (solo il synth selezionato),
  Tuning, MIDI, zoom, About.
- **Barra Synth 1–12**: engine ▾, On, M (mute), S (solo), voci attive e meter. Clic destro: copia/incolla/init, Layer preset.
- **Schede**: *Engine*, *Voice*, *Modulation*, *Layer FX*, *Master FX*, *Global*. I moduli sono disposti nell'ordine
  del segnale (da sinistra a destra, poi dall'alto in basso): nella scheda Engine il **Filter** è l'ultimo modulo della
  catena; in FM6 ogni operatore ha accanto il suo inviluppo (Op 1 – Op 1 Env, …); in Wavetable ogni OSC ha accanto il
  suo modulo Advanced; in PD ogni Line è seguita dalla sua Distortion e dal suo PD Env.
- **Colori**: arancione = moduli e parametri; blu = synth e scheda selezionati, NOTE ASSIGN e GLOBAL SCALE;
  bianco = pulsanti della barra in alto selezionati, Master e Macro.
- **Colonna Master**: meter L/R, Master Volume, Polyphony, 8 Macro, NOTE ASSIGN e GLOBAL SCALE.
- **Controlli che dipendono da una modalità** compaiono solo quando servono: per esempio i parametri SuperSaw solo con
  la forma SuperSaw, Pulse Width solo con Pulse, Unison Detune/Spread/Pan solo con Unison > 1, Glide Time/Mode/Curve
  solo con Glide acceso, Rate o Division dell'LFO secondo Sync, Ratio o Fixed Hz dell'operatore FM secondo la modalità.
- **Mod Env 2, Mod Env 3 e LFO 1–4** non assegnati (nella Mod Matrix o nelle mandate fisse: Filter LFO Amount,
  Filter 2 Env/LFO del Wavetable, profondità LFO dell'FM6) sono grigi e disattivati, con la scritta "not assigned".
  Mod Env 1 è l'inviluppo del filtro (**Filter Envelope**); con Wavetable, Mod Env 2 è l'inviluppo del Filter 2.
- Cambiando engine si chiudono le finestre degli altri engine (es. il Wavetable Editor).
- **Zoom** 75–200 % (menu zoom o trascinando l'angolo); oltre il 200 % il layout si allarga fino allo schermo intero.
  La dimensione è salvata nel progetto.
- **Controlli**: trascina per regolare, Shift + trascina per la regolazione fine, doppio clic = valore di default.
  **Valori da tastiera**: clic sulla manopola e digita un numero (oppure doppio clic sul valore), Invio conferma, Esc
  annulla; si possono usare le unità (`250 ms`, `1.2 s`, `1.2k`, `45 %`) e, per le liste, il nome della voce.
  Clic destro: valore di default, MIDI Learn, assegna a Macro, assegna a slot automazione.
- Tooltip e menu contestuali sono in italiano; i nomi di moduli e parametri sono in inglese.
