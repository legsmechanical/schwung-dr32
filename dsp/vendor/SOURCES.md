# DR32 vendored DSP — provenance

DR32 ships **MIT**. Everything here is MIT-compatible; the per-file origin is
recorded so the licence position stays checkable rather than assumed.

| File | Contents | Origin | Licence |
|---|---|---|---|
| `airwin_spaces.h` | `Verbity2` — DR32 **Spaces**, the one flexible room-to-hall reverb | Airwindows © Chris Johnson, transplanted verbatim by `tools/port_airwindows.py` | **MIT** (Airwindows upstream) |
| `airwin_dyn.h` | `Pop3` — Drum Bus **Compress** | as above | **MIT** |
| `airwin_verb.h` | `Chamber` (golden-ratio Householder feedforward), `InfinityVerb` (allpass + Householder feedback, freezable) | Airwindows algorithms © Chris Johnson, ported to RT-safe C++ structs in `schwung-echidna-fx` | **MIT** (Airwindows upstream) |
| `galactic.h` | `Galactic` large shimmer space | Airwindows © Chris Johnson, same port | **MIT** |
| `space_extra.h` | `SpaceExtra` — Dattorro figure-eight **plate** tank, plus shimmer/gated/duck/reverse variants | **Original DSP by Josh (Filliformes)**, from `schwung-echidna-fx` | authored by the copyright holder; used here under **MIT** with his agreement (the source repo is GPL-3.0 by choice, not by dependency) |
| `audio_utils.h` | shared helpers | `schwung-echidna-fx` | as above |

## Not vendored, and why

- **`ottx`** — a port of *vitOTTx*, i.e. **Vital**, which is GPL-3.0. Genuinely
  viral: it cannot come into an MIT module. Use the Airwindows dynamics instead.
- **TAL Reverb** (in `schwung-noisemaker`) — **GPL v2** (© Patrick Kunz).

## Attribution requirement

Airwindows is MIT and requires the copyright notice to be retained. The notices
at the top of each vendored header must not be stripped.
