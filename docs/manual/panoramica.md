# Panoramica

## Layer
CHROMA5YNTH contiene fino a **12 Layer**, ognuno un sintetizzatore completo. All'apertura è attivo solo il Layer 1.
Per ogni Layer: On/Off, **Engine** (Analog, Additive, FM6, Wavetable, PD), Volume, Pan, Mute, Solo, Transpose ±48 st,
key range, velocity range, canale MIDI (All/1–16), Copy/Paste/Init e Save/Load come Layer preset.
Un Layer spento non usa CPU. Cambiare engine conserva i parametri dell'engine precedente (annullabile con Undo).

## Flusso del segnale
```
Engine → Filter → Layer FX → Layer mixer   (×12 Layer)
→ Somma → Master FX → Master Filter → Master Volume → Limiter
```
Il limiter finale è true-peak (4×, look-ahead) con tetto a −1 dBTP; la sua latenza è comunicata alla DAW.

## Polifonia
- Pool globale di **12 / 24 / 36 voci** (default 24), condiviso da tutti i Layer.
- Ogni Layer ha **Max Voices** (limitato dal pool). Le voci di unison sono interne alla voce e non consumano il pool.
- Voice stealing globale: prima la voce più vecchia, poi la più silenziosa, con crossfade di 2 ms.

## SCALE
Sezione nella colonna Master: una tastiera di un'ottava con 12 slot (Do, Do#, … Si). In ogni slot scegli quali Layer
suonano quella classe di nota, su tutte le ottave. SCALE non intona: assegna le note ai Layer.

- **SCALE Off**: ogni Layer attivo suona tutte le note (layering classico).
- Slot vuoto: la nota è muta.
Esempi: split tasti bianchi/neri, un Layer per grado della scala, accordi diatonici tramite Transpose dei Layer.

## MPE
Pitch per nota (bend range 48 st in MPE), Pressure e Slide (CC74). Zona Lower/Upper con il parametro **MPE Zone** (Master).
Tutti gli engine ricevono MPE Pitch/Pressure/Slide come sorgenti della Mod Matrix.

## Automazione dalla DAW
Per non esporre migliaia di parametri, la DAW vede: tutti i parametri Master e Master FX, 8 **Macro**, i parametri
comuni dei 12 Layer e 256 **Automation Slot**. Clic destro su qualunque controllo → *Assegna a slot automazione*:
lo slot guida quel parametro interno. Gli ID dei parametri restano stabili tra le versioni.

## Interfaccia
- **Header e toolbar**: Presets, Undo/Redo, A/B, A>B, Randomize (+ quantità), Init, Tuning, MIDI, zoom, About.
- **Barra Layer 1–12**: engine ▾, On, M (mute), S (solo), voci attive e meter. Clic destro: copia/incolla/init, Layer preset.
- **Schede**: *Engine*, *Voice*, *Modulation*, *Layer FX*, *Master FX*, *Global*.
- **Colonna Master**: meter L/R, Master Volume, Polyphony, 8 Macro e SCALE.
- **Zoom** 75–200 % (menu zoom o trascinando l'angolo); oltre il 200 % il layout si allarga fino allo schermo intero.
  La dimensione è salvata nel progetto.
- **Controlli**: trascina per regolare, Shift + trascina per la regolazione fine, doppio clic = valore di default.
  Clic destro: valore di default, MIDI Learn, assegna a Macro, assegna a slot automazione.
- Tooltip e menu contestuali sono in italiano; i nomi di moduli e parametri sono in inglese.
