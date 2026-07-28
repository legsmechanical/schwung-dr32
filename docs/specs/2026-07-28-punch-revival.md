# Bringing back Punch — scope

Josh, 2026-07-28, after asking whether the native pad inserts could give the
per-pad transient shaping a bespoke shaper would have. They can: **Punch is the
native effect that does it**, and unlike a custom shaper its settings live in
the `.ablpreset` and open on Move.

Punch is currently disabled. This is the plan to earn it back.

## Where it stands

Dropped with the other playback effects on 2026-07-26 (`f612e33`), but it was
already disabled before that, because the first implementation measured **worse
than not implementing it at all**:

| effect | dry fallback | first implementation |
|---|---:|---:|
| 8-bit | −29.7 dB | −0.0 dB |
| **Punch** | **−2.4 dB** | **+1.1 dB** |
| FM | −6.0 dB | −2.1 dB |

The enable bar in `dr32_fx_modelled()` is unchanged: it goes back on **only**
once it measurably beats −2.4 dB, with the number recorded.

## What is already known exactly

From the binary trace (`DRUM_EFFECTS_RECON.md`, "Punch", kernel `FUN_01a546dc`,
setters `FUN_01a37368` / `FUN_01a373b4`):

```c
punch_gain         = 1 + amount^3;                 // EXACT
punch_time_samples = sample_rate * time_seconds;   // EXACT
u                  = (time_seconds - 0.06) / 0.94; // EXACT
punch_curve        = polynomial(cbrt(u));          // ← the unknown
```

Plus: a two-region curve — square-root attack up to an **amount-derived**
boundary, then a power-law tail to `punch_time_samples` — a smoothed target
gain, and a hard **minimum instantaneous gain of 0.15**.

Parameter ranges (already in `dr32_fx_clamp`): Amount `0..1`, Time `0.06..1.0 s`
— note the 0.06 matches the `u` formula's offset exactly, which is a good sign
the trace is right.

## ⚠ What the old implementation actually got wrong

Re-reading it against the trace, the unknown polynomial was not the only
problem. It hard-coded **two things the document says are variable**:

```c
float boundary = 0.25f;                                  // doc: AMOUNT-derived
target = 1.0f + (punch_gain - 1.0f) * powf(1.0f - ..., 2.0f);   // doc: polynomial(cbrt(u))
```

- the attack/tail **boundary was fixed at 0.25** where the trace says it is
  derived from *amount*;
- the tail **exponent was fixed at 2.0** where the trace says the curve comes
  from a polynomial in `cbrt(u)` — i.e. it varies with *time*.

So the model had no way to track either parameter, which is consistent with it
scoring worse than dry. Both are directly measurable (below). The smoothing
coefficient (`0.05`) is also unattributed and measurable.

## The method — measure the curve, don't fit through a null

The key realisation: **Punch is a pure gain envelope applied to the sample**, so
it does not need to be inferred through a null test. Render the same fixture
twice and divide:

```
A:  Effect_Type = Standard          -> the dry render
B:  Effect_Type = Punch (a, t)      -> the same signal, gain-shaped

gain(n) = B(n) / A(n)     wherever |A(n)| is above a floor
```

Both renders are byte-reproducible and sample-aligned, so this recovers the
punch curve **directly and exactly** — no model is assumed in the extraction.
Mask samples where the dry render is near zero, and take the median across the
two channels.

A is rendered **once** and reused for every grid point: the dry render does not
depend on the punch parameters.

### What falls straight out of the extracted curve

- **peak gain** — check against `1 + amount^3`. This is the falsifiable test of
  the whole extraction: if the measured peak does not follow that law, the
  method is wrong and everything after it is noise. Do this first, on one point.
- **boundary** — the position of the sqrt→tail knee, per amount.
- **tail exponent** — fit per grid point, then plot against `cbrt(u)`. If the
  trace is right this should be a clean low-order polynomial; if it is a mess,
  the two-region reading is wrong and that is worth knowing early.
- **the 0.15 floor** and the **smoothing time constant** — both visible in the
  curve's shape at the extremes.

### Grid

Amount × Time, 5 × 5 = 25 renders plus one dry. Renders are seconds each.
Add the corners (amount 0 and 1, time 0.06 and 1.0) since the laws are most
identifiable there.

## Tooling — all of it already exists

- `tools/make_fixture.mjs --effect=Punch` — already supported.
- `tools/set_cell_params.mjs "Effect_PunchAmount=...,Effect_PunchTime=..."` —
  exists precisely so a fixture can carry arbitrary effect parameters without a
  flag per parameter.
- `tools/oracle.sh` — the stock-engine render.
- `tools/fx_suite.sh` — reports null depth per effect; the acceptance run.

Nothing new to build before the first capture.

## Acceptance

1. `dr32_fx_modelled(DR32_FX_PUNCH)` returns 1 **only** with a recorded number
   better than −2.4 dB.
2. The null is measured with `tools/fx_suite.sh`, on real drum samples, not on
   the tone used for the fit.
3. The extracted-vs-modelled curve is checked across the grid, not only at the
   default — a curve that matches at one setting and drifts at the edges is the
   failure mode that produced +1.1 dB last time.

## Cost

| phase | cost |
|---|---|
| Peak-gain sanity check (`1 + amount^3`) | 2 renders |
| Grid capture | 25 renders + 1 dry, one session |
| Fit boundary / exponent / smoothing | the real work, ~1 session |
| Implement + acceptance null | ~1 session |

⚠ Captures need `STOP_STACK=1` — the Move goes down for the batch, so this
pairs naturally with the SuperEco go/no-go rather than being a separate
interruption.

## Why this is worth more than a bespoke per-pad shaper

A custom shaper had no home in the file format: its settings would live in slot
state and follow the slot rather than the kit. Punch is a native parameter that
DR32 **already parses and preserves** — so a kit shaped with it round-trips
byte-exact and still opens on native Move. Same musical goal, no format debt.
