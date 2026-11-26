# Installazione

## Requisiti
- macOS 10.13 o successivo, Mac Intel o Apple Silicon (binario Universal).
- Una DAW che carica plugin **VST3** o **Audio Unit** (Logic Pro, Ableton Live, Reaper, Bitwig, Cubase, Studio One…).
- Circa 100 MB di RAM per istanza (banco wavetable di fabbrica condiviso).

## Installer
Scarica `CHROMA5YNTH-<versione>-macOS.pkg` dalla pagina **Releases** del repository GitHub e aprilo.
L'installer copia:

| Formato | Percorso |
|---|---|
| VST3 | `/Library/Audio/Plug-Ins/VST3/CHROMA5YNTH.vst3` |
| Audio Unit | `/Library/Audio/Plug-Ins/Components/CHROMA5YNTH.component` |

Con **Personalizza** puoi installare un solo formato. Nella DAW il plugin compare come strumento del produttore
**TMFU audiovibe** (Audio Unit: *TMFU audiovibe: CHROMA5YNTH*).

## Aprire un installer non firmato
L'installer non è firmato con un Apple Developer ID, quindi al primo avvio macOS lo blocca
("impossibile aprire perché proviene da uno sviluppatore non identificato"). Per aprirlo:

1. Fai doppio clic sul `.pkg` e chiudi l'avviso con **OK** (oppure **Fine**).
2. Apri **Impostazioni di Sistema → Privacy e sicurezza** (macOS 12 e precedenti: *Preferenze di Sistema → Sicurezza e privacy → Generali*).
3. In fondo alla sezione *Sicurezza* compare il messaggio relativo a `CHROMA5YNTH-…pkg`: fai clic su **Apri comunque**.
4. Conferma con la password di amministratore e di nuovo **Apri**.

In alternativa: clic destro (Ctrl-clic) sul `.pkg` → **Apri** → **Apri** (macOS fino a 14).

Se in futuro le build saranno firmate e notarizzate, questi passaggi non saranno più necessari.

> Windows: non supportato in questa versione.

## Dopo l'installazione
- **Logic Pro / GarageBand**: se il plugin non compare, riavvia la DAW; Logic verifica l'Audio Unit al primo avvio
  (*Gestione plug-in* per ripetere la verifica).
- **Ableton Live**: *Impostazioni → Plug-in → Usa cartella di sistema VST3 / Audio Unit*, poi **Ripeti scansione**.
- Al primo caricamento i preset di fabbrica vengono copiati in
  `~/Library/Audio/Presets/TMFU audiovibe/CHROMA5YNTH/`.

## Disinstallazione
Elimina `CHROMA5YNTH.vst3` e `CHROMA5YNTH.component` dalle cartelle indicate sopra. I tuoi preset restano in
`~/Library/Audio/Presets/TMFU audiovibe/CHROMA5YNTH/` (cancellali a mano se non servono più).
