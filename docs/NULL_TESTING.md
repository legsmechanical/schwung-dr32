# Null testing against the stock engine

DR32's acceptance test is not "does it sound right" — it is **how many dB below
the native signal the difference sits**. `EnginePerfTool` on the Move renders
the real engine graph through an offline driver, deterministically (repeat runs
are byte-identical), so it is an oracle we can diff against.

## The loop

```sh
B="../move original reconstruct/capture/opt-move/BenchmarkSongs/16drums.abl"
M=<local sample mirror>          # contains CoreLibrary/ and UserLibrary/

# 1. build an isolation fixture (any pad param can be overridden)
node tools/make_fixture.mjs "$B" build/fix/x.abl --notes=36 --decay=1.0 --hold=0.001

# 2. render it on the device through the STOCK engine
tools/oracle.sh build/fix/x.abl build/fix/x-native.wav 44100

# 3. render the same song through DR32, off-device
node tools/score.mjs build/fix/x.abl build/fix/x.score --samples="$M"
./build/render_score build/fix/x.score build/fix/x-dr32.wav 44100

# 4. diff
node tools/nulltest.mjs build/fix/x-native.wav build/fix/x-dr32.wav
```

`make_fixture.mjs` keeps only track 0 of the stock benchmark — **its pad sends
are all at −70 dB**, so the reverb return contributes nothing and the dry rack
can be matched without modelling FX. It forces the track fader to 0 dB and
replaces the clip with a sparse deterministic pattern.

The renderer goes through `dsp/dr32_params.c`, the same dispatch the plugin
uses, so what is validated is what ships.

## Reading the output

- **NULL DEPTH** — headline: RMS of (native − dr32) relative to native's RMS.
- **best-fit gain** — the single scalar that best maps dr32 onto native, and the
  null after removing it. If gain-compensated null is far deeper than raw null,
  the error is a **level law**; if it barely moves, the error is spectral or
  temporal and no gain will fix it. This is what isolated the missing cell
  `Volume` (−15.79 dB best-fit, instantly explained).
- **alignment** — the stock renderer has a fixed startup latency (~64 frames);
  integer and sub-sample alignment are searched so the residual measures the
  engine, not the offset.

## Dry-path results (2026-07-25)

| Fixture | Null depth | Note |
|---|---:|---|
| long attack (0.5 s) | **−78.6 dB** | essentially exact |
| kick, steady state (skip first 64 frames) | **−65.7 dB** | |
| kick, whole render (attack = 0.0001 s) | −44.1 dB | error confined to the first ~1.5 ms |
| decay 1 s | −40.2 dB | same onset-localised error |

Skipping the first 8/16/32/64 frames moves the kick null −44.4 / −46.5 / −54.7 /
−65.7 dB, and it is flat thereafter — so the entire remaining discrepancy is the
onset of a 4-sample attack, not the body of the sound.

### What this loop has already caught

Errors found by measurement that listening would not reliably have surfaced:

1. **cell `Volume` ignored** — a separate gain from the chain mixer's volume;
   stock kits set it to −15 dB. Whole kit was 15 dB too loud.
2. **Sample rate ignored** — the Core Library ships 96 kHz samples and the
   native note-start derives its ratio from the source rate. Those pads played
   2.18× too slow (over an octave flat).
3. **Envelope decay was linear** — the native decay is exponential, losing a
   fixed ≈83.9 dB over the decay time regardless of its length (measured at
   1 s and 10 s, agreeing within 0.13 dB).
4. **Envelope phase** — native emits `env[i] = i·rate` (first sample of a note
   is exactly zero); advancing before use shifts the whole attack a sample early.
   Onset ratios 0.495 / 0.666 / 0.747 / 0.907 = i/(i+1) pinned this exactly.
5. **`ableton:/packs/abl-core-library` unresolved** and **AIFF unsupported** —
   between them, most factory content silently failed to load.

## ⛳ Playback effects are DROPPED (2026-07-26, Josh's call)

Pads play the plain sampler path. `Effect_Type` and all nine effects' parameters
are still parsed and preserved in the writer's raw document, so saves stay
lossless and kits still open correctly on native Move — only playback drops them.

The scoreboard below is kept as the RECORD OF WHY, not as a live target. With
effects off, an effect fixture measures "how far dry playback is from a native
render that has the effect on" — which is a big number by construction and says
nothing about an implementation.

**The live fidelity number is `standard` (-41.9 dB)**, plus the dry-path results
above (-78.6 dB long attack, -65.7 dB steady state).

Two were genuinely working when they were switched off and would be cheap to
bring back: **Pitch Env (-39.3 dB, constants proven by sweep)** and
**Loop (-35.8 dB)**. Re-enable in `dr32_fx_modelled()` with the number that
justifies it.

## Per-effect scoreboard as of the drop (2026-07-26)

`tools/fx_suite.sh capture` then `tools/fx_suite.sh`. One note (36), hold 0.001,
decay 2.0, one pad param set per effect.

| Effect | Null | State |
|---|---:|---|
| standard | **−41.9 dB** | shared reader; onset-limited |
| pitchenv | **−39.3 dB** | constants proven by sweep |
| eightbit | −29.7 dB | **not implemented** — this is the Standard path scoring |
| ringmod | −24.9 dB | amount law exact; oscillator STATE term not modelled |
| loop | −24.0 dB | hard wrap; transition crossfade not modelled |
| subosc | −21.6 dB | gain law exact; generator is a plain sine, native uses a resonator |
| noise | −13.1 dB | stochastic — **cannot** null sample-exactly, see below |
| fm | −6.0 dB | **not implemented** — no step modulation yet |
| punch | −2.4 dB | curve structure only; tail exponent unknown |
| stretch | +3.1 dB | granular path not modelled; factor≠1 currently just slows the reader |

Read these honestly: several are **not implemented**, and their numbers are what
the fallback path happens to score, not a measure of an implementation.

### The enable policy (`dr32_fx_modelled`)

Implementing 8-bit, FM and Punch from the reconstruction's prose — without a
numeric target to check against — made all three **worse than not implementing
them**:

| Effect | dry fallback | first implementation |
|---|---:|---:|
| eightbit | −29.7 dB | **−0.0 dB** |
| punch | −2.4 dB | **+1.1 dB** |
| fm | −6.0 dB | **−2.1 dB** |

So an effect is only enabled once it measurably beats the fallback. The others
degrade to the plain reader — the closest available approximation — instead of
applying DSP we know to be wrong. A half-finished effect is not partial credit;
it is a regression the user would hear.

Currently enabled: Standard, Pitch Env, Loop, Ring Mod, Sub Osc, Stretch
(factor 1), Noise. Disabled: 8-bit, Punch, FM.

**Noise will never null sample-exactly.** Its generator is a PRNG whose seeding
is not attributed, so two correct implementations still diverge sample by
sample. It needs a statistical acceptance instead: matching level, spectrum and
the amount curve. Do not chase its null depth.

## Open

- The onset residual on very short attacks (~1.5 ms of a 4-sample attack). The
  trace mentions an "edge/window gain term" in the render kernel that we do not
  model; that is the prime suspect.
- The steady-state −65.7 dB floor is probably the 96 kHz → 44.1 kHz read: at a
  2.18 ratio the interpolation detail matters, and the native accumulator's
  precision is not yet attributed.
- Effects (M2): each of the ten paths now has a numeric target instead of a
  prose description. `make_fixture.mjs --effect=<name>` builds the fixture.

---

# The REVERB null test (`tools/verb_suite.sh`)

The Native send is a port of Move's own Reverb, so it gets its own path — much
shorter than the pad-effect one above, because a reverb reference needs no song.
The references in `build/ir/` are single-sample impulse renders through a return
chain, so the tail is already isolated and **no subtraction and no device time**
are required. They are checked in; re-capture with `tools/oracle.sh` only if the
fixtures change.

```sh
tests/run.sh                  # builds dist/tests/render_verb
tools/verb_suite.sh           # score + render + report over build/ir/*.abl
tools/verb_suite.sh x.abl     # one preset, ad hoc
```

## The direct-parameter path, and why it exists

A stock Reverb carries **33 parameters**; a DR32 send has **eight** generic
control slots. The musical knob mapping in `dr32_fxbus.cpp` therefore cannot
express a stock preset at all, and driving the port through it would be testing
a different reverb. So there is a second, test-only path that takes the `.abl`
JSON's own keys and values with no DR32-side interpretation in between:

```
build/ir/x.abl  --tools/verb_score.mjs-->  x.verb  --dist/tests/render_verb-->  x-dr32.wav
```

`dr32_fxbus_native_set_raw()` is that path. It is **not** reachable from the UI,
the kit format or saved state, and it must stay that way — it is a measurement
instrument, not a feature. `dr32_fxbus_native_raw_commit()` also switches the
send into null-test mode, which turns off two things that exist to make Native
sit politely beside the other send types and would otherwise corrupt the
measurement: the output level trim, and the idle-skip (whose output gate at
-120 dB would put a floor under the null depth that has nothing to do with the
model).

## ⚠ Every unmodelled parameter is REPORTED

`setRaw()` returns three outcomes, and the third is the point:

| | |
|---:|---|
| `1` | applied |
| `0` | a real Reverb parameter the port knowingly does not model yet |
| `-1` | not a Reverb parameter at all — the renderer refuses to run |

A renderer that silently swallowed the middle case would produce a bad null
number with nothing to say why, which is worse than no test. `render_verb`
prints them; `verb_suite.sh` leaves them in `build/fx/<case>.log`.
`verb_score.mjs` is equally strict on its own side and exits non-zero rather
than drop a key it cannot represent.

## ⚠⚠ A SAMPLE-LEVEL NULL DOES NOT TRANSFER TO A REVERB

This is the important thing on this page, and it took building the null path to
find out.

Measured against the device, the tails' waveform correlation is **-0.008** and
the null depth is **-0.34 dB**. That is not a verdict on the model. Two reverbs
whose delay lines differ by a single sample produce completely decorrelated
tails while sounding identical, so the null is **all-or-nothing**: it reads 0 dB
for everything except a bit-exact port, and cannot tell "very close" from
"wildly wrong".

DR32's usual rule — *the acceptance test is a null test, not an ear test* —
holds for the pad effects, which are deterministic transforms of one sample. It
does not hold for a reverb, and pretending otherwise would have meant reporting
0 dB forever and calling it a failure.

**The acceptance metric for the reverb is therefore ENERGY DECAY CURVE
deviation per band**, which is graded, insensitive to phase, and is what a
reverb is actually judged on. `tools/verb_null.py` reports both — the null for
the record and to catch a gross error, the EDC deviation as the number that
means something.

⚠ One trap worth keeping: the reference is a WHOLE-TRACK render, so the dry
click goes to master alongside the return. That single dry sample carries about
50x the energy of the entire tail, and our render has no dry path — so with it
included, the least-squares fit correctly concludes "subtract almost nothing"
and the null reads 0 dB however good the model is. `--skip=` (default 512
frames) steps past it.

Current state, driven from each preset's own 33 parameters:

| band | EDC deviation (rms) |
|---|---:|
| 5-16 kHz | **0.29 dB** |
| 1.5-5 kHz | **0.36 dB** |
| 671 Hz - 1.5 kHz | **0.95 dB** |
| 200-671 Hz | 2.21 dB |
| 20-200 Hz | **5.28 dB** |

The model is very close above ~700 Hz and drifts at the bottom. The low end is
the remaining work, and it is consistent with the low-band RT60 running slightly
long (1.85 s device against 2.11 s ours).

## ⚠ The null column is not yet a measure of the model

Four parts of the device are still unmodelled, and one of them is the **final
mixer**, which sets the entire output structure — two stereo delays, a fixed
1.39 crossfeed and a `swapLR` width term, none of which the port's placeholder
reproduces. Until that lands, a null number mostly measures the placeholder.

So the suite reports **RT60, broadband and per band**, which reads the late
loop's behaviour rather than the output mix, and is meaningful today. See
`docs/specs/2026-07-29-supereco-port.md` for what the numbers currently say.
