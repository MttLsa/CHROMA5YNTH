# Installation

## Requirements
- macOS 10.13 or later, Intel or Apple Silicon Mac (Universal binary).
- A host that loads **VST3** or **Audio Unit** plug-ins (Logic Pro, Ableton Live, Reaper, Bitwig, Cubase, Studio One…).
- About 100 MB of RAM per instance (the factory wavetable bank is shared).

## Installer
Download `CHROMA5YNTH-<version>-macOS.pkg` from the **Releases** page of the GitHub repository and open it.
It installs:

| Format | Location |
|---|---|
| VST3 | `/Library/Audio/Plug-Ins/VST3/CHROMA5YNTH.vst3` |
| Audio Unit | `/Library/Audio/Plug-Ins/Components/CHROMA5YNTH.component` |

Use **Customize** to install a single format. In your host the instrument is listed under the manufacturer
**TMFU audiovibe** (Audio Unit: *TMFU audiovibe: CHROMA5YNTH*).

## Opening an unsigned installer
The installer is not signed with an Apple Developer ID, so macOS blocks it the first time
("cannot be opened because it is from an unidentified developer"). To open it:

1. Double-click the `.pkg` and dismiss the warning with **OK** (or **Done**).
2. Open **System Settings → Privacy & Security** (macOS 12 and earlier: *System Preferences → Security & Privacy → General*).
3. At the bottom of the *Security* section a message about `CHROMA5YNTH-…pkg` appears: click **Open Anyway**.
4. Confirm with your administrator password and click **Open** again.

Alternatively: right-click (Control-click) the `.pkg` → **Open** → **Open** (macOS up to 14).

> Windows is not supported in this version.

## After installing
- **Logic Pro / GarageBand**: if the plug-in does not appear, restart the host; Logic validates the Audio Unit on first
  launch (*Plug-in Manager* to re-run validation).
- **Ableton Live**: *Settings → Plug-Ins → Use VST3 / Audio Units system folders*, then **Rescan**.
- On first load the factory presets are copied to `~/Library/Audio/Presets/TMFU audiovibe/CHROMA5YNTH/`.

## Uninstalling
Delete `CHROMA5YNTH.vst3` and `CHROMA5YNTH.component` from the folders above. Your presets stay in
`~/Library/Audio/Presets/TMFU audiovibe/CHROMA5YNTH/` (delete them manually if no longer needed).
