# DR32 — Drum Rack 32

> ⚠️ **WORK IN PROGRESS.** Usable but unfinished, and not released. Version `0.2.0`,
> no catalog entry, and several behaviours below are still open questions rather
> than decisions. Expect breaking changes. **Requires Schwung ≥ 1.2.0.**

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

**The mixer is the host's.** DR32 ships a second module, **`dr32-fx`** — one binary holding the
four Drum Buss stages (Crunch, Attack, Sustain, Comp) and eight reverb/delay types (Plate, Spaces,
Delay, Gated, Digital, Hall, NonLin and `Native`), each as a preset. Loading DR32 declares a
**Drum Bus** holding all four stages, so it comes up the way it always did; from there they are
ordinary inserts. You can reorder them, bypass one with the host's own gesture, aim an LFO at one,
drop a drive between two, or swap the compressor for something else entirely — none of which was
possible while they lived inside the synth.

**Every pad is a Schwung bus voice**, so a slot can send the kick to one insert chain and the snare
to another; and every pad's **Send 1 / Send 2** feed the host's two global sends, per pad, taken
pre-insert. Put `dr32-fx` on a send and you have DR32's reverbs back — shared with everything else
in the set rather than trapped in one slot.

> **Coming from an earlier version:** a `.abl` kit's per-pad send amounts now point at the host's
> Send A, which is empty until you put something on it, so a stock kit loads **dry**. `dr32-fx` set
> to `Native` on Send A is the closest thing to what it used to do. The old `bus_*` / `send1_*` /
> `send2_*` keys are still accepted so saved slots restore, but they drive nothing.

**Native Schwung pages, no custom UI.** Since 0.2.0 every page is the host's own knob grid, planned
from the hierarchy the module serves: the pads are one 32-instance child level with `pad_layout:
"drums"`, so the header shows a pad map, the grid follows the pad you hit, and each pad's page draws
the sample waveform with a trim editor, the amp envelope, the filter curve and a fader. Pad names
come from the loaded kit. Hitting a pad moves the editor to it while the transport is stopped; with
a pattern running, focus only moves on a host that vouches for a live press (dAVEBOx does; an
upstream contract for it is in progress).

## Status — what is not finished

- **`Native` reverb has never been heard.** It is a port of Move's own SuperEco reverb, measured
  rather than tuned: energy-decay-curve deviation is 0.29 dB above 5 kHz but **5.28 dB below
  200 Hz**, which is the remaining modelling gap. An ear check is owed, as is a confirmation of its
  output level.
- **Shippability of `Native` is undecided.** Unlike the rest of DR32 it is transcription rather
  than behavioural reconstruction, and that has distribution implications which have not been
  settled. Treat it as provisional.
- **Kit import does not arm the reverb.** Return-chain FX in an `.ablpreset` is parsed and
  preserved but left inert, while per-pad *send amounts* are imported — so a stock kit feeds
  correct levels into whatever you have put on the host's Send A, and into nothing if that is
  empty. Arming it automatically would mean a module writing into a device-wide send that every
  other slot shares, which is worse than loading dry.
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
tests/run.sh                # off-device: WAV loader, voice, kit parsing, state, JSON round-trip
node tools/pages_check.mjs  # upstream's own validator over the SERVED hierarchy; writes a preview fixture
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
| `src/` | `module.json` (the served hierarchy) and the play-view `ui.js` |
| `lib/` | `.ablpreset` reading/writing |
| `tests/` | off-device unit tests |
| `tools/` | the null-test harness and capture scripts |
| `docs/specs/` | design notes, including the reverb port |

Development conventions and the traps worth knowing are in [`CLAUDE.md`](CLAUDE.md).
