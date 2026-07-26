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

## Current results (2026-07-25)

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

## Open

- The onset residual on very short attacks (~1.5 ms of a 4-sample attack). The
  trace mentions an "edge/window gain term" in the render kernel that we do not
  model; that is the prime suspect.
- The steady-state −65.7 dB floor is probably the 96 kHz → 44.1 kHz read: at a
  2.18 ratio the interpolation detail matters, and the native accumulator's
  precision is not yet attributed.
- Effects (M2): each of the ten paths now has a numeric target instead of a
  prose description. `make_fixture.mjs --effect=<name>` builds the fixture.
