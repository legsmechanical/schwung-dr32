# Porting SuperEco, and what really caps its tail

2026-07-29. Supersedes the modelling half of
`2026-07-28-supereco-feasibility.md` — and **overturns that document's per-band
conclusion**, which was a measurement artifact.

No device time was taken for any of this. Everything below is either read off
the `MoveOriginal` binary or measured from IRs already banked in `build/ir/`.

---

## What shipped

`dsp/dr32_supereco.h` — Move's own Reverb, **complete apart from two named
gaps**, ported from the decompile rather than fitted to captures, and wired up
as a new send type `Native` (`DR32_EFX_NATIVE`).

The late network landed first; the front end followed the same day, once the
parallel decompile session produced `REVERB_FRONT_END_RECON.md`.

Everything in it is read off the binary:

**Late network**
- the 4-lane orthonormal feedback matrix, exact float bits
- the three delay-length families A/B/C, and the RoomSize **cube-root** law
- the decay law, including the stock clamped-`exp2` approximation
- the per-lane TPT shelf pair, including the stock `tan` approximation
- the feedback all-pass recurrence and the stereo parity folds

**Front end**
- the early-reflection tap-position law, and its four per-lane quadrature
  oscillators running at `fs/8` — the device's real modulation resolution
- two-point **linear** tap interpolation (confirmed from the `fmls`/`fmla`
  pair, so the usual "a cubic reader would be nicer" is simply wrong here)
- the decaying tap gains, pre-delay-compensated, with lane 2's **negative**
  trim
- the four pre-diffusion delays and their orthonormal coefficient rows
- the two-state diffuser, whose five coefficients turn out to be **derived**
  rather than constant: a fixed-Q lowpass whose cutoff is 12 kHz scaled by the
  last pre-diffusion gain, so the diffuser opens and closes with `DecayTime`
  and `DiffuseDelay`

⭑ The front end runs on **its own decay rate** — `(DiffuseDelay + 0.5) /
(1.15 x DecayTime)`. `DiffuseDelay` (the manual's "Shape") enters there and
nowhere else.

### Still deliberately absent rather than guessed

- **The input Band filter's coefficient builder.** The recurrence is known; the
  map from `BandFreq`/`BandWidth` to G/K/mixes is not. It is an input tone
  control, not part of the tail mechanism.
- **The final mixer's exact form.** `MixReflect`/`MixDiffuse` are applied here
  as plain gains on the early and diffuse stereo pairs. That is the one
  structural guess in the file, and it is a material one — at the stock values
  (1.79 / 2.00) the early and diffuse fields carry most of the output level.
- **Late-lane modulation** (`SizeModFreq`/`SizeModDepth`), as distinct from the
  early-tap motion, which is implemented.

### Two questions the front-end recon settled

**Offset bases.** `FUN_01b7cec0`, `FUN_01b7d584` and `FUN_01b7d9e4` all take
`full_state + 0xa90`; the kernel takes `full_state`. The two `0x5b0` writes do
not alias — the builder's is full-state `0x1040`, the kernel's really is the
first pre-diffusion coefficient row. **Both notes were right.**

**Family C.** It is a second in-loop all-pass, as implemented. All three
families come from `FUN_01b7cec0`, which works on the *late* subobject, and the
pre-diffusion delays are a separate front-end stage with their own length law
(`0.54 x cuberoot(RoomSize) x 32.036`). The `familyCInLoop` flag is kept only so
the alternative stays one edit away.

### The decay law holds exactly

`tests/test_fxbus.c` asserts it. RT60 predicted as
`ln(1000) * 1.15 / 8.833317 = 0.8993 x DecayTime`:

| DecayTime | predicted | late network only | complete engine |
|---:|---:|---:|---:|
| 2.0 s | 1.80 s | **1.80 s** | **1.81 s** |
| 8.0 s | 7.19 s | **7.20 s** | **7.19 s** |

Across the full sweep, with both shelves at unity, the ratio sits at 0.900 at
every point out to 19.5 s. That is the binary's own constants coming back out
of a running implementation, which is about as good as a port can be checked
before the null test.

⭑ **Adding the whole front end moved it by 0.01 s.** That independently confirms
the front-end recon's own §8 inference — early reflection, pre-diffusion and the
diffuser are feed-forward into `U`, so once their rings empty the recirculating
late residual bypasses them entirely and they cannot contribute a continuing
loss. Two routes, same answer.

(At DecayTime 0.6 s the ratio does drop to 0.69, because at that length the
early and diffuse fields are a large fraction of the total and the Schroeder fit
lands partly on them. It is the measurement's scope, not the decay law.)

### `stockExp` reproduces `expf` to ~2e-6 — and cannot exceed 1.0

The device does not call `expf`. Transcribed from `FUN_01b7cec0`
(`reverb-coalesced-rebuild.c:718-795`), the approximation agrees with `expf` to
2.4e-6 relative over the whole range the decay builder uses. Its one real quirk
is structural: the base-2 exponent is clamped to `[0,127]`, and 127 is the
unity exponent, so **`stockExp` can never return more than 1.0**. A shelf gain
above unity is silently flattened. That is faithful and it is implemented.

### Cost

Cheapest reverb in the module even with the front end in — 0.62% of one core
against the Plate's 0.72% for 30 s of audio, measured on the host. ⚠ Host numbers, not device numbers;
`tools/bench_fx.c` exists for the aarch64 measurement and has still not been
run. "SuperEco" appears to be earned.

---

## ⭑ The RT60 ceiling is the SHELVES. The earlier "ruled out" was wrong.

The open question in this campaign was why RT60 falls increasingly short of
`0.8993 x DecayTime` above ~1.5 s and tops out near 5.5 s. Two hypotheses had
been recorded, and **both were wrong**:

**Not the `stockExp` clamp.** The clamp is not binding for long decays, the
polynomial is exact at both endpoints (its four coefficients sum to 1), and the
float precision floor near exponent 127 works out to a ceiling near 55,000 s,
not 5.5. Do not re-chase it.

**Not "broadband, therefore not the shelves".** That inference came from the
per-band table in the feasibility doc, which showed all three bands saturating
within 0.3 s of each other. It was an artifact of the analysis filter.

### What the port says

Switching the shelves off is a one-line experiment in a port and impossible in
a capture. At DecayTime 19.5 s:

| shelves | RT60 |
|---|---:|
| both stock (Lo 0.6167, Hi 0.8833) | 6.52 s |
| low shelf only | 17.33 s |
| high shelf only | 13.87 s |
| neither | 17.54 s |

Both shelves **cut** — the loop is a band-pass between `ShelfLoFreq` and
`ShelfHiFreq` — and the loss is applied once per round trip, so it is
independent of `DecayTime` and imposes a hard ceiling no `DecayTime` can
exceed. Together they take 17.5 s down to 6.6 s. That is the mechanism.

### Why the bands looked flat

`tools/irtools.py`'s `_bandpass` was a one-pole high-pass into a one-pole
low-pass: **6 dB/octave**. Against an ordinary spectrum that is fine. Against
this reverb it is not, because the bands differ in *decay rate* as well as
level — the mid band both rings loudest and lasts longest, so its tail leaks
through the skirts of every other band's filter and *is* the tail you measure
there. Every band reads back the mid band's RT60, and the result looks flat.

Reproduced exactly in the port, at DecayTime 19.5 s:

| band | 1 pole-pair (Q=1.4) | 6 cascaded (~72 dB/oct) |
|---:|---:|---:|
| 150 Hz | 5.75 s | **2.44 s** |
| 300 Hz | 6.29 s | **2.72 s** |
| 1 kHz | 7.19 s | 7.11 s |
| 3 kHz | 6.61 s | 6.57 s |
| 8 kHz | 6.35 s | 6.30 s |

(Complete engine. The late network alone gave the same picture: 6.65 -> 2.57 s
at 150 Hz against 7.38 -> 7.25 s at 1 kHz. The front end does not change the
conclusion, which is what §8 of the front-end recon predicts.)

### And on the real device IRs

`_bandpass` is now six cascaded pole-pairs. Re-running the **already-banked**
device captures — no new device time — the flatness disappears:

| DecayTime | 0.899 x DT | broadband | <671 Hz | mid | >1470 Hz | (<671 Hz, before) |
|---:|---:|---:|---:|---:|---:|---:|
| 600 | 0.54 | 0.60 | 0.47 | 0.60 | 0.57 | 0.63 |
| 1500 | 1.35 | 1.33 | 0.87 | 1.40 | 1.26 | 1.30 |
| 4000 | 3.60 | 2.62 | 1.27 | 2.70 | 2.44 | 2.64 |
| 10000 | 8.99 | 4.50 | 1.69 | 4.94 | 3.91 | 4.59 |
| 19500 | 17.54 | 5.44 | **1.85** | 5.90 | 4.70 | 5.53 |

The low band saturates at **1.85 s** while the mid reaches 5.90 s. The bands
have completely different ceilings, which is exactly what a shelf-driven limit
predicts — and the feasibility doc had itself predicted a ~2.38 s low-shelf
ceiling before discarding the idea. **The question is closed.**

### The lesson, which is not about reverb

The wrong conclusion did not come from a wrong number. It came from a
measurement whose selectivity was never checked against the thing it had to
separate. A 6 dB/octave filter is a perfectly good filter; it is not a good
*discriminator* between two signals that differ by 10 dB and by a factor of
three in decay rate. The check that would have caught it is cheap and general:
**re-run the measurement at a different filter order and see whether the answer
moves.** If it moves, the filter was the instrument, not the signal.

---

## What this cost the Native send

The ceiling mechanism is a control decision, not just trivia. "Damping" on the
`Native` send is **both shelf gains**, because the low shelf is where most of a
drum's energy is: pinning it open (the first version here) left a damping knob
that moved RT60 by 7% and read as broken. Sweeping both moves it 7.20 s ->
3.17 s at the top of the decay range.

⭑ One knob reaches the factory setting exactly. The stock drum kits' pair
(`ShelfLoGain` 0.6167, `ShelfHiGain` 0.8833) lies on a straight line through
unity, so a single control at 0.59 lands on **both** — which is the send's
default.

## Still open

- A **direct-parameter path** for the null test. The eight generic send slots
  cannot carry the device's 33 parameters, so `tools/fx_suite.sh` will need a
  test-only entry point that sets `RoomSize`/`DecayTime`/the shelves raw. This
  is now the last thing between the port and an actual null number.
- The two named gaps above — the Band filter builder and the final mixer's
  form. The mixer is the one that will show up in a null test.
- Device CPU, via `tools/bench_fx.c`.
- Kit import could now select `Native` automatically when a kit's return chain
  is a native Reverb, and carry its `RoomSize`/`DecayTime` across. Return
  chains are not imported at all today.
