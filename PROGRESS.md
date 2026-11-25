# PROGRESS (max 10 righe per fase)

## FASE 0 – Scaffolding ✅
- Identità: "TMFU audiovibe", codici Tmfu/Chr5, bundle com.tmfuaudiovibe.chroma5ynth (CMake, installer, About, manuale).
- CMake + JUCE 8.0.9 / Catch2 3.7.1 via FetchContent; target VST3/AU(mac)/Standalone; sorgenti condivisi plugin+test via `chroma_configure_target`.
- `params/*.yaml` (schema in params/README.md) → `tools/gen_params.py` → build/generated/{ParamIDs.h, ParamLayout.cpp, GuiLayout.h} + docs/manual/parameters.md.
- Param iniziali: master_volume, master_polyphony. Editor generico da GuiLayout + About.
- Installer: installer/macos/build_pkg.sh (.pkg VST3+AU), installer/windows/chroma5ynth.iss (Inno, VST3).
- CI .github/workflows/build.yml: macOS Universal + Win x64, ctest, pluginval (strict 10), auval, artifact installer, release su tag chroma5ynth-v*, firma/notarizzazione opzionale via secrets.
- CI verde (run 45685177069): macOS (macos-15-intel, Universal) build+test+auval+pluginval+.pkg; Windows build+test+pluginval+.exe.

## FASE 0.5 – Stile grafico ✅
- Riferimento: TMFU-audiovibe/DUALBOX `Source/gui/LookAndFeel` (piatto, scuro, accento blu/lime, Inter).
- `assets/fonts/` Inter Regular/SemiBold + OFL.txt → `juce_add_binary_data(chroma_assets)` (namespace `ChromaAssets`). Nessuna immagine necessaria.
- `src/gui/StyleLookAndFeel.{h,cpp}`: `chroma::style` (colori, font, misure), StyleLookAndFeel, FineSlider, Knob, Segmented, Panel, drawHeader.
- `STYLE.md` (regole), CLAUDE.md: riga "GUI: usa sempre STYLE.md e StyleLookAndFeel"; font in assets/fonts.
- Editor generico: header wordmark + Panel per sezione, Knob (float/int), Segmented (choice/bool). Build + ctest verdi (Linux).
