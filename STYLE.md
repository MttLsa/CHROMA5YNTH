# STYLE – GUI CHROMA5YNTH (stile TMFU audiovibe, da DUALBOX)
Codice: `src/gui/StyleLookAndFeel.{h,cpp}` (namespace `chroma::style` + `StyleLookAndFeel`). Linguaggio: piatto, minimale, scuro.

## Colori (ARGB esatti, solo via `chroma::style`)
| Token | Hex | Uso |
|---|---|---|
| bg | `#1e1e1e` | sfondo finestra |
| panel | `#2a2a2a` | corpo pannelli |
| panel2 | `#333333` | pulsanti, combo, disco knob, segmenti spenti, popup |
| header | `#3a3a3a` | header pannelli, tooltip, title bar |
| outline | `#121212` | bordi 1 px, fondo dei segmented |
| track | `#4a4a4a` | traccia knob/slider |
| text / textDim | `#d0d0d0` / `#8c8c8c` | testo / caption, frecce, versione |
| accent | `#4f8cff` | blu: synth selezionato, schede selezionate, NOTE ASSIGN, GLOBAL SCALE |
| accent2 | `#b4dc3c` | lime (non più usato nella vista principale) |
| accentG | `#eeeeee` | bianco: pulsanti selezionati della toolbar, Master, Macro |
| module | `#ff9636` | arancione: moduli e loro parametri (knob, segmented, viste ENV/LFO, Mod Matrix) |
Testo su accento: nero. Disabilitato: alpha 0.5 o textDim.

## Font
Inter (OFL, `assets/fonts/`, binary data `ChromaAssets`): Regular testi, SemiBold titoli/selezione. Sempre `style::font (h, bold)`.
Dimensioni: nome CHROMA5YNTH 31 **Urbanist Light** (come l'intestazione di GROOVULATION) kerning 0.02; titolo pannello 12 SemiBold MAIUSCOLO; pulsanti ≤12.5; combo 10–12; caption/segmented 11; valori 11; maker line 9.5 kerning 0.12; popup 13.

## Dimensioni
margin 8 px; header editor 56 px (nome "CHROMA5YNTH" in Urbanist Light, sotto "TMFU audiovibe", 12 barrette 6×20 arcobaleno rosso→viola, una per nota, sfumate in verticale chiaro→saturo (`style::rainbow`), sottotitolo textDim, versione a destra).
Raggi: pannello 4, controlli 2.5, segment 3/2. Bordi 1 px outline.

## Knob (`Knob` + `FineSlider`)
- Caption sopra (textDim), valore sotto (text; accent se hover/drag/focus), altezza caption = clamp(h/6, 10, 16).
- Arco 252° (start 1.2π, end 2.8π), spessore = max(2, r·0.16), traccia `track`, valore `accent`; bipolare se min<0<max (parte dal centro).
- Disco interno panel2 +5 %, indicatore text da 25 % a 95 % del raggio, cap rounded.
- Drag 700 px per tutto il range, Shift ×4 più fine; doppio clic = default; frecce: step o 0.2 % (Shift 0.02 %). Cella tipica 76×96.

## Pannelli e controlli
- `Panel`: corpo panel, header 20 px `header` con tacca accent 3 px a sinistra, contenuto `content()` (reduced 6,4).
- choice/bool → `Segmented` (fondo outline, cella attiva accent + testo nero SemiBold); float/int → `Knob`. Pulsanti: panel2, attivo accent.
- Slider lineari: traccia 4 px, thumb text 6×16 r2. Combo: freccia triangolare textDim a destra.
- Nessun colore/font hard-coded fuori da `chroma::style`; nuove sezioni = nuovi `Panel`.

## GUI definitiva (§7, `src/PluginEditor.cpp`, `src/gui/ParamUI.*`, `src/gui/MainPanels.*`)
- Base logica 1280×800 disegnata su un `Root` scalato con `AffineTransform`: scala = min(W/1280, H/800) limitata a 75–200 %; oltre, il layout cresce (fino all'area dello schermo, limite massimo = display più grande). Dimensione salvata nello stato host (`uiWidth/uiHeight`).
- Struttura: header + toolbar, barra Layer 1–12 (numero, engine ▾, M/S/power, meter), centro a schede (Engine, Voice, Modulation, Layer FX, Master FX, Global) con moduli in **righe nell'ordine del segnale** (`Page::newRow`, larghezza per peso; filtro ultimo nella scheda Engine, Op n accanto a Op n Env), knob 52 px / celle 66 px, colonna destra Master (meter L/R) + Macro + NOTE ASSIGN (tastiera con menu Synth 1–12/Off) + GLOBAL SCALE. Controlli condizionali (`SectionPanel::setCellCondition`), moduli non assegnati grigi (`setInUse`), valori digitabili (Knob: cifre o doppio clic sul valore).
- Pannelli generati da `lp::descs` / `gui::params` (mai parametri hard-coded): un `*_on` iniziale diventa il power nell'header, prefisso comune tolto dalle caption, vista ENV/LFO automatica.
- Aggiornamento su v-blank ≤ 60 fps: meter (lock-free, `Synth::takeLayerPeak/takeMasterPeak`), controlli a 30 Hz solo per la pagina visibile. Stringhe con caratteri non ASCII sempre via `utf8 ("…")`.
