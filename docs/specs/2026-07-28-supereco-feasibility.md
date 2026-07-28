# Modelling Move's own reverb (SuperEco) — feasibility

Investigation only, 2026-07-28, at Josh's request. **No captures were run.**
This reports the method and the cost so the decision to commit can be made with
numbers rather than optimism.

## Verdict: feasible, and far cheaper than the rig campaigns

**This needs no audio rig, no interface, no ears, and no acoustic path.**
`/opt/move/EnginePerfTool --fake-driver --render-audio` drives the real engine
graph through an offline driver and writes a float WAV. Repeat runs are
**byte-identical** (`docs/NULL_TESTING.md`). So the oracle is file-in / file-out
and deterministic — none of the measurement traps that dominate the Typhon and
echidna work apply here: no 16-bit USB granulation, no L6 duplex stalls, no
level-matching, no "was the source clean" doubt.

That is a categorically cheaper instrument than
[[schwung-fidelity-methodology]] assumes, and it is the single most important
fact in this document.

## Why it is worth doing at all

**61 of the 77 stock drum kits put a `reverb` on their return, every one of them
`RoomType: "SuperEco"`.** It is, by a distance, the reverb Move users actually
hear on drums. DR32 currently offers Plate, Spaces, Gated, Digital and Hall —
five good reverbs, none of which is *the* one the factory kits carry.

It is also the last unmodelled piece of the native return path: the delay is
shipped, and chorus/phaser were measured and deliberately dropped (only 4 kits,
and both are overwhelmingly insert devices).

## The method

### 1. Isolating the return — by SUBTRACTION, not by muting

Sends are **post-fader** in the native rack, so the obvious move — mute the dry,
keep the send — does not exist: pulling the pad fader pulls the send with it.

The engine's determinism makes that irrelevant. Render the same fixture twice:

```
A:  pad send = -70 dB   ->  dry only
B:  pad send =   0 dB   ->  dry + return
B - A                   ->  the return, exactly, sample-aligned
```

Both renders are byte-reproducible, so the subtraction is exact rather than
approximate. No modelling is needed to isolate the thing being modelled, which
is normally the hardest part of a fidelity campaign.

### 2. Getting an impulse response

Point the pad at a one-sample click (or the shortest attack/decay the cell
allows) and `B - A` **is** the reverb's stereo impulse response. Everything the
model needs is then readable directly off the IR:

- pre-delay and the early-reflection pattern
- per-octave RT60 (the metric this project already trusts — see the reverb
  measurement traps memory about short windows lying)
- echo density / build-up time
- stereo behaviour: correlation, mono-fold, L-only spread

### 3. Parameter laws

The 61 kits share `RoomType` but differ in `RoomSize`, `DecayTime`, `PreDelay`,
the shelf and band filters, `StereoSeparation`, `MixDiffuse`/`MixReflect` and
the mod-depth pair. The fixture writes those values into the return device, so
each grid point is one render. A 5-parameter × 5-point sweep is ~25 renders plus
baselines — minutes of device time, not sessions.

### 4. Fitting

Fit an algorithmic model to the measured IRs (per-octave RT60 laws vs
DecayTime/RoomSize, the shelf laws, pre-delay, density). Convolution is NOT the
answer here — a 2-3 s stereo IR partitioned at 44.1 kHz would dwarf the whole
module's CPU budget, and it could not track the parameters anyway.

## What must be verified first (two cheap checks)

1. **Does the offline graph include the return chain at all?** One A/B render —
   send at −70 dB vs 0 dB — must differ. If `EnginePerfTool` renders only the
   track's own devices and skips returns, the whole method collapses and the
   answer is "not feasible this way". **This is the go/no-go, and it is a single
   capture.**
2. **`tools/make_fixture.mjs` cannot currently touch the send or the return
   device.** It sets pad sends to −70 dB deliberately (return silent) and its
   `OVERRIDES` table only reaches `drumCell` params plus pan/volume/choke. It
   needs `--send=` and return-device parameter overrides. Small, mechanical.

## Cost

| Phase | Cost |
|---|---|
| Go/no-go A/B render | one capture |
| Extend `make_fixture.mjs` | under an hour |
| IR capture + parameter sweep | ~30 renders, one session |
| Fit + implement + null-test | the real work: 1-2 sessions |

## ⚠ Operational constraint

`tools/oracle.sh` must run with **`STOP_STACK=1`**. Two full device lockups
(needing a power cycle) followed captures against a live stack, and the script
documents that history. So a capture batch **takes the Move down for its
duration** — it cannot happen while Josh is playing, and it needs his go-ahead
([[schwung-rig-all-clear-before-measuring]]).

## Recommendation

Run the go/no-go A/B render first, alone, and stop there if the return does not
appear in the offline graph. It is one capture and it decides everything.
