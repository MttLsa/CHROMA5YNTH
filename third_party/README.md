Third-party code (MIT licence, see the LICENSE.txt files):

- `signalsmith-stretch/` — Signalsmith Stretch 1.3.2 (polyphonic pitch/time stretching), https://github.com/Signalsmith-Audio/signalsmith-stretch
- `signalsmith-linear/` — Signalsmith Linear 0.6.6 (FFT/STFT used by Stretch), https://github.com/Signalsmith-Audio/linear

Local changes: the `#include` of `stft.h` in `signalsmith-stretch.h` points to the vendored copy; `findPeaks()` measures
each peak's exact frequency from the phase advance of its strongest bin (marked `CHROMA5YNTH local change`), for GLOBAL SCALE.
Same code as Groovulation's MELODIC modules engine (copied from `TMFU-audiovibe/Groovulation/third_party`).
