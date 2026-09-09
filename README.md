# DR32 — Drum Rack 32

### 📖 [**Read the manual →**](https://legsmechanical.github.io/schwung-dr32/manual.html)

> Version `0.3.0`. **Requires Schwung ≥ 1.3.0** — that is where the module-bus
> contract landed, and the per-pad sends reach nothing without it.
>
> **Install** from Schwung Manager once
> [the catalog entry](https://github.com/charlesvestal/schwung/pull/484) merges; until then, from
> the [v0.3.0 release](https://github.com/legsmechanical/schwung-dr32/releases/tag/v0.3.0).

A clone of Ableton Move's native **Drum Rack**, extended from 16 pads to **32**, running as a
[Schwung](https://github.com/charlesvestal/schwung) sound-generator module.

It loads Move's own `.ablpreset` drum kits straight out of the Core and User libraries and plays
them through a reconstruction of the same Drum Sampler voice — so a kit built on the device opens
here and sounds like itself, with twice the pads.

## What it does

**32 pads, Move's own kits.** DR32 opens **empty** — an instrument that arrives already full
decides for you. The **Kits** page is a category list (Acoustic · Electronic · Hybrid · My Kits)
that opens into just that category's kits.

⚠️ The list is built by DR32, not by the file browser, and that is deliberate: the browser filters
by file EXTENSION, and the user's Track Presets folder holds every instrument's presets — on a
real device, 365 files of which only 75 are drum racks. DR32 filters by content, so every entry
offered will actually load.

Samples are swapped per pad through a single **Sample** browser, which opens in the folder that
pad's current sample came from.

**The Drum Sampler voice**, per pad: playback region (start / length), transpose and detune, choke
groups, velocity modulation, pan and volume, sends, and Punch. Two envelope modes (**A-H-D** and
**A-S-R**) and four filter types (Lowpass 12 dB, Lowpass, Highpass, Peak).

**Two sends, per pad, into the host's return buses.** Every pad has its own Send A and Send B
amount in dB, which is what a send amount means in a Move kit — so an imported kit's send levels
arrive pointing at real returns. The effect on a return is the host's: put any chain you like
there and every pad can tap it at its own level.

⚠️ **This is why the module requires Schwung ≥ 1.3.0**: the module-bus contract
(`voice_send_params`) landed there. On an older host the two knobs still turn, save and restore,
but nothing reads them. DR32 carried its own reverbs until 2026-09-08 and no longer does: a module holding its own returns is a second, worse copy of something the host
provides, reachable only from inside that module. Those effects were not deleted — they were
lifted out whole to become a standalone reverb module, and the DR32 tag `fxbus-final` is the
pointer to them.

**Native Schwung pages, no custom UI.** Since 0.2.0 every page is the host's own knob grid, planned
from the hierarchy the module serves. `pad_layout: "drums"` puts a pad map in the header, and the
pages draw the sample waveform with a trim editor, the amp envelope, the filter curve and a fader.
Pad names come from the loaded kit and follow a sample the moment you change it.

Six pages, in order:

| | |
|---|---|
| **Category** | Acoustic · Electronic · Hybrid · My Kits — click one to open its kits |
| **Kit** | the kits in that category |
| **Sample** | Pad · Sample · Start · End · Transpose · Detune · Choke · Browse |
| **Shape** | Attack · Decay · Hold · Envelope · Cutoff · Reso · Type · Filter |
| **Mix** | Vel Vol · Volume · Pan · Link · Send A · Send B · Punch · Punch Time |
| **Master** | level for the whole kit |

**Sample, Shape and Mix are three views of one pad.** Pick the pad on any of them and it is picked
on all three — they are sibling child levels sharing one focus, so there is no separate pad-picker
page to jog past.

**It always follows the pad you hit** — there is no Follow toggle. Hitting a pad moves the editor
to it while the transport is stopped; with a pattern running, focus only moves on a host that
vouches for a live press (dAVEBOx does; an upstream contract for it is in progress). The **Pad**
knob on the Sample bank changes the edited pad by hand, for when focus cannot follow.

**Link** sets one control across every pad at once. Arm it, sweep a knob, and every pad takes that
value; it releases the moment you touch a different control, so the next knob is that pad's alone.
It never spreads what makes a pad a distinct pad — the sample, the note, the browse position — and
it is off whenever DR32 loads.

## Known limits

- 🔴 **Browsing kits has no undo.** Moving the cursor in the Kit list loads what you land on, a
  moment after you stop — so you can hear your way through a category, and the kit you were on is
  gone. Save first. The host offers a module no commit signal on that page, so DR32 cannot tell
  "scrolled past" from "chose this".
- **Pads 17–32 sit outside Move's 4×4 pad map**, so the small map in the header does not light for
  them.
- **Kit import does not arm a return.** Return-chain FX in an `.ablpreset` is parsed and preserved
  but not acted on, while per-pad *send amounts* are imported — so a stock kit arrives with correct
  levels pointing at whatever the user has (or has not) put on Send A and Send B.
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
export SCHWUNG_SRC=../schwung-current    # a checkout of the host DR32 targets
tests/run.sh                # off-device: WAV loader, voice, kit parsing, the kit browser, state, JSON round-trip
node tools/pages_check.mjs  # upstream's own validator over the SERVED hierarchy; writes a preview fixture
```

**The acceptance test is a null test, not an ear test** — see [`docs/NULL_TESTING.md`](docs/NULL_TESTING.md).
That bar exists because implementing effects from prose without a numeric target made three of them
measurably *worse* than not implementing them at all. (`tools/fx_suite.sh`, which reported null
depth per send effect, went with the effects themselves.)

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
| `docs/manual.html` | the user manual — [published via Pages](https://legsmechanical.github.io/schwung-dr32/manual.html) |
| `docs/specs/` | design notes |

Development conventions and the traps worth knowing are in [`CLAUDE.md`](CLAUDE.md).
