# DR32 — Drum Rack 32

> ⚠️ **WORK IN PROGRESS.** Usable but unfinished, and not released. Version `0.1.0`,
> no catalog entry, and several behaviours below are still open questions rather
> than decisions. Expect breaking changes.

A clone of Ableton Move's native **Drum Rack**, extended from 16 pads to **32**, running as a
[Schwung](https://github.com/charlesvestal/schwung) sound-generator module.

It loads Move's own `.ablpreset` drum kits straight out of the Core and User libraries and plays
them through a reconstruction of the same Drum Sampler voice — so a kit built on the device opens
here and sounds like itself, with twice the pads.

## What it does

**32 pads, Move's own kits.** Kits load from `/data/CoreLibrary/Track Presets/Drums` and
`/data/UserData/UserLibrary/Track Presets`, with live preview while browsing. Samples can be
swapped per pad from either library.

**The Drum Sampler voice**, per pad: playback region (start / length), transpose and detune, choke
groups, velocity modulation, pan and volume, sends, and Punch. Two envelope modes (**A-H-D** and
**A-S-R**) and four filter types (Lowpass 12 dB, Lowpass, Highpass, Peak).

**Two sends and a drum bus.** Each send offers Plate, Spaces, Delay, Gated, Digital, Hall, NonLin
and `Native` reverb/delay types; the bus has compression, crunch, attack/sustain shaping and
dry/wet.

**A canvas pad editor** — the module declares `host_canvas_ui`, so the 32-pad grid is drawn by the
module itself rather than adapted from a parameter list. It is also hostable: dAVEBOx runs DR32 in
a chain slot and edits it in place.

## Status — what is not finished

- **`Native` reverb has never been heard.** It is a port of Move's own SuperEco reverb, measured
  rather than tuned: energy-decay-curve deviation is 0.29 dB above 5 kHz but **5.28 dB below
  200 Hz**, which is the remaining modelling gap. An ear check is owed, as is a confirmation of its
  output level.
- **Shippability of `Native` is undecided.** Unlike the rest of DR32 it is transcription rather
  than behavioural reconstruction, and that has distribution implications which have not been
  settled. Treat it as provisional.
- **Kit import does not arm the reverb.** Return-chain FX in an `.ablpreset` is parsed and
  preserved but left inert, while per-pad *send amounts* are imported — so a stock kit currently
  feeds correct levels into an unconfigured reverb.
- **Per-pad playback effects are deliberately dropped.** `Effect_Type` and all nine effects'
  parameters are still parsed and written back, so kits stay lossless and reopen correctly on a
  native Move — but playback ignores them and every pad plays the plain sampler.

## Building and installing

```sh
./scripts/build.sh          # cross-compiles the DSP for the device (Docker)
./scripts/install.sh        # deploys and ALWAYS restarts the stack
```

⚠️ **A restart is mandatory after every deploy.** Swapping the synth out and back in is not enough —
the old `dsp.so` stays live and the deploy silently looks like a no-op. `install.sh` handles it;
`SKIP_RESTART=1` opts out when batching several deploys.

⚠️ **If `build.sh` fails, `dist/` keeps the previous build and `install.sh` will happily ship it.**
Check for `==> done:` before trusting an install.

## Testing

```sh
tests/run.sh                # off-device: WAV loader, voice, kit parsing, JSON round-trip
tools/fx_suite.sh           # null-test report, per effect
```

**The acceptance test is a null test, not an ear test** — see [`docs/NULL_TESTING.md`](docs/NULL_TESTING.md).
`tools/fx_suite.sh capture` renders native references on the device and the suite reports null depth
per effect. That bar exists because implementing effects from prose without a numeric target made
three of them measurably *worse* than not implementing them at all.

## A note on fidelity

The engine is a **reconstruction, not a design**. Its laws come from analysis of the stock engine
and from measurement against it, which means **any deviation is a fidelity bug even when it sounds
better**. Two already corrected: the sample reader uses linear interpolation (Catmull-Rom is nicer
and wrong), and velocity→volume is a dB law centred on velocity 70, not a linear blend.

## Layout

| path | |
|---|---|
| `dsp/` | the DSP: voice, kit loader, `.ablpreset` parsing, effects, state |
| `src/` | `module.json` and the canvas UI |
| `lib/` | `.ablpreset` reading/writing |
| `tests/` | off-device unit tests |
| `tools/` | the null-test harness and capture scripts |
| `docs/specs/` | design notes, including the reverb port |

Development conventions and the traps worth knowing are in [`CLAUDE.md`](CLAUDE.md).
