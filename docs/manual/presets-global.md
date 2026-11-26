# Preset, impostazioni globali e MIDI

## Preset
- **Multi preset** (`.c5multi`): stato completo (Layer, Master, Macro, Automation Slot). Non contiene le impostazioni
  globali: Master Tune, Oversampling, MPE, mappature MIDI learn e microtuning restano quelli correnti.
- **Layer preset** (`.c5layer`): un singolo synth (parametri comuni + engine), caricabile in qualunque slot 1–12.
- Libreria: `~/Library/Audio/Presets/TMFU audiovibe/CHROMA5YNTH/{Multi,Layer}`; preferiti in `favourites.txt`.
- Browser (pulsante **Presets**): Multi/Layer, ricerca (tutte le parole su nome, categoria, autore, tag), filtri
  categoria/tag/autore, preferiti, Save / Load (doppio clic) / Rename / Delete / Import / Export.
- **Preset di fabbrica**: 64 Multi (40 patch a un Layer, 8 per engine, + 24 Multi che usano SCALE) e le 40 patch anche
  come Layer preset. Sono incorporati nel plugin e copiati in `{Multi,Layer}/Factory` al primo uso (di nuovo solo
  quando cambia il set di fabbrica). Elenco: [factory-presets.md](factory-presets.md).
- **Normalizzazione**: ogni preset di fabbrica è renderizzato offline (nota C3, velocity 100, 4 s) e riceve un trim
  per arrivare a −14 LUFS short-term max, mai sopra −1 dBTP: **Preset Trim** (Multi, sommato al Master Volume) e
  **Trim** del Layer (Layer preset, dopo le Layer FX). Strumento: `tools/presetnorm` (vedi `presets/README.md`).

## Undo/Redo, A/B, Randomize, Init
- **Undo/Redo**: ogni modifica (anche dei parametri interni dei Layer) diventa un passo dopo 0.4 s di inattività;
  caricamento preset, A/B, Randomize e Init sono annullabili. Le impostazioni globali sono escluse.
- **A/B**: due stati indipendenti; B alla prima apertura è una copia di A. **A>B** copia lo stato corrente nell'altro slot.
- **Randomize** (quantità con lo slider): globale (Layer accesi + Master FX), Layer, oppure sezione (es. "Layer · Filter",
  "Analog" = tutte le sezioni Analog, "Master FX · Reverb"). Mai toccati: Master Volume, polifonia, Scale, MPE,
  Global, Macro, Automation Slot, On/Engine/Volume/Mute/Solo/Transpose/range dei Layer; "Layer · Voice" e
  "Layer · Performance" solo se scelte esplicitamente come sezione.
- **Init**: riporta la patch ai default (le impostazioni globali e le mappature MIDI restano).

## MIDI learn
- Clic destro su un controllo → *MIDI Learn*, poi muovere un controller: il CC (qualsiasi canale) viene assegnato.
  Un CC controlla un solo parametro; *Rimuovi MIDI Learn* cancella l'assegnazione.
- Menu **MIDI**: salva/carica le mappature (`.c5midi`), cancella tutto. Le mappature sono salvate nel progetto.

## Accordatura
- **Master Tune**: La4 da 415 a 466 Hz.
- **Microtuning** (menu **Tuning**): file Scala `.scl` (un `.kbm` con lo stesso nome nella stessa cartella viene usato
  come mappatura) o AnaMark `.tun`. Mappatura di default `.scl`: grado 0 sul Do3 (nota 60), La4 = 440 Hz.
  Tasti non mappati restano temperati. Il microtuning si somma al Master Tune ed è salvato nel progetto.

## Oversampling
- **Oversampling** (tempo reale, default 2×) e **Offline Oversampling** (render/bounce, default 4×) per gli
  oscillatori di Analog, FM6, Wavetable e PD.
