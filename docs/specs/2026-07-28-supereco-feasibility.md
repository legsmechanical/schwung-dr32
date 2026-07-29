# Modelling Move's own reverb (SuperEco) — feasibility

Investigation, 2026-07-28, at Josh's request. **Go/no-go capture run the same
day — result at the end: GO.**
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

---

## GO/NO-GO RESULT — 2026-07-28: **GO**

One capture batch, stack down once (`fx_suite.sh` pattern), four renders, stack
back up clean.

The benchmark song's track 0 already carries a `reverb` / `RoomType: SuperEco`
on its rack's single return chain, with every pad's send **enabled at −70 dB** —
so the fixture needed only the new `--send=` flag (now in `make_fixture.mjs`).

| render | rms |
|---|---:|
| send −70 dB (dry) | 0.049486 |
| send 0 dB (wet) | 0.051842 |
| **difference** | **0.014333** (−10.8 dB vs dry) |

**The return chain IS rendered in the offline graph**, and subtraction isolates
it exactly. The isolated signal is unmistakably a reverb tail:

```
   0-100 ms  0.022334      500- 600 ms  0.006509
 100-200 ms  0.048516      700- 800 ms  0.003206
 200-300 ms  0.029322      900-1000 ms  0.001236
 300-400 ms  0.012256     1100-1200 ms  0.000612
```

Peak at 100-200 ms, smooth decay past 1.2 s. Nothing about it looks like a
rendering artefact.

So the method in this document is confirmed end to end: **the campaign is
viable**, it needs no audio rig, and the next step is the IR capture (point a
pad at a click) plus the parameter grid.

Remaining before the fit: `make_fixture.mjs` still needs the RETURN-DEVICE
parameter overrides (RoomSize / DecayTime / PreDelay / the shelves). The send
half is done.

---

## FIRST PARAMETER GRID — 2026-07-28: inconclusive, and why

Captured a click-excited IR (attack 0.0001 / hold 0.001 / decay 0.002) plus a
3x3 grid over `AllPassGain` x `RoomSize`, isolated by subtraction as designed.

| AllPassGain | RoomSize | RT60 | peak rms |
|---:|---:|---:|---:|
| 0.60 | 10 / 45 / 95 | 2.04 / 2.12 / 2.24 s | 0.0253 / 0.0213 / 0.0192 |
| 0.85 | 10 / 45 / 95 | 2.00 / 2.24 / 2.12 s | 0.0275 / 0.0191 / 0.0203 |
| 0.97 | 10 / 45 / 95 | 1.96 / 2.16 / 2.12 s | 0.0297 / 0.0208 / 0.0213 |

**`RoomSize` clearly works** — peak level falls monotonically as the room grows,
0.0297 -> 0.0192, which is what spreading the same energy over a bigger space
does. **`AllPassGain` moves neither RT60 nor level**, and RT60 sits at 2.0-2.2 s
everywhere.

⚠ Two readings, and the next capture has to separate them:

1. **`DecayTime` does not exist on this device instance.** The stock kits'
   reverbs carry it (Ahlimba Kit: 1442.9) but the benchmark's does not, and
   `make_fixture --return=` deliberately only sets keys that are already
   present. So whatever governs decay in SuperEco mode may simply not have been
   swept yet — `AllPassSize`, `DiffuseDelay` and `MixDiffuse`/`MixReflect` are
   the untried candidates.
2. **The render is 2 s and the measurement window is the same length.** An RT60
   estimate that lands at ~2 s across every setting is exactly what a truncated
   measurement looks like, so it must be re-run at 10 s before any of these
   numbers are trusted.

Do NOT fit anything to this table. The isolation method is proven (see the
go/no-go above); the parameter mapping is not.

---

## SECOND GRID — 2026-07-29: DecayTime found, and the decay law measured

Josh supplied three facts that unblocked this: the send and insert reverbs are
the same device and an insert can run 100% wet; an impulse sample can be placed
on the device; and **Ableton Live is on the workstation and loads Move sets with
every reverb parameter exposed**.

### Live answered the open question for free

`.als` is gzipped XML, so dumping a set's Reverb needs no device time at all. It
has **33 parameters with exactly the names in the Move `.abl` JSON** — same
device — and among them:

```
DecayTime    3310.78     (milliseconds)
MixDirect    0.746
RoomType     1           (Live uses an index; the .abl writes "SuperEco")
```

So **`DecayTime` is real, it is the decay control, and it is in milliseconds.**
The first grid never swept it because the benchmark preset omits the key and
`--return=` only wrote keys that already existed. That, plus 2 s renders, was
the whole story of the flat 2.0-2.2 s readings.

### Three tooling fixes this needed

1. **`--sample=`**, to point a pad at an impulse. ⚠ It must emit the
   **core-library pack** scheme, not user-library: EnginePerfTool has no user
   library configured and dies on
   `ASSERT 'config.userLibraryPath().has_value()'`
   (`shared/abl-uri-scheme/src/AbletonScheme.cpp:163`). The file goes in
   `/data/CoreLibrary/Samples/`, which needs **root** to write.
2. **`--return=` now creates a missing key** instead of skipping it.
3. 20 s renders (882000 frames) rather than 2 s.

### ⚠ MixDirect = 0 MUTES the return

Setting it to 0 to get "100% wet" produced nine byte-identical renders with no
tail at all — only the dry click. On a return this parameter is not a dry/wet
control; leaving it alone is correct, and with a true impulse the dry is a
single sample anyway, so the tail is already isolated with no subtraction.

### The decay law

20 s renders, single-sample impulse, RT60 by least-squares fit of the
log-envelope over the -5 to -35 dB span (the -30 dB-doubled estimate distorts on
a non-exponential tail):

| DecayTime | RoomSize 20 | 60 | 99 | mean | RT60/DecayTime |
|---:|---:|---:|---:|---:|---:|
| 400 ms | 0.337 | 0.413 | 0.443 | **0.398 s** | 0.99 |
| 1200 ms | 1.047 | 1.081 | 1.104 | **1.077 s** | 0.90 |
| 3600 ms | 2.290 | 2.428 | 2.640 | **2.453 s** | 0.68 |

**RT60 tracks DecayTime and compresses as it grows** — near 1:1 at 400 ms,
falling to 0.68 by 3600 ms. `RoomSize` is a real but secondary effect, adding
10-15% to the tail across its range.

That is the first genuine law out of this campaign. Next: more DecayTime points
to pin the compression curve, then the per-octave damping laws (the shelves and
the band filter), then the early-reflection pattern.

---

## THIRD GRID — 2026-07-29: real ranges, and the decay SATURATES

Two leads from Josh: use the decompile, and compare the stock reverb presets.
Both paid off, and neither needed device time.

### The decompile: seven user-facing controls

`analysis/audio-effects/REVERB_RECON.md` — the device-definitions builder
registers exactly **seven** parameters, and the other 26 are reachable only
through the preset/`.abl` route:

```
DecayTime · RoomSize · BandFreq · PreDelay · StereoSeparation · ShelfLoGain · MixDirect
```

It also records that the converter constructors were **not** decompiled, so
ranges, defaults and units are exactly the gap measurement fills. Good division
of labour: static analysis gives the parameter set and which ones are user
facing, capture gives the laws.

### The stock presets: real ranges, and one model

19 stock Reverb presets in `/data/CoreLibrary/Audio Effects/Reverb/`. Comparing
them tells us which of the 33 parameters matter and over what span:

| parameter | distinct values | range across presets |
|---|---:|---|
| DecayTime | 19 | **600 .. 19522 ms** |
| BandFreq | 18 | 265 .. 7124 Hz |
| PreDelay | 18 | 0.95 .. 103.6 ms |
| RoomSize | 16 | **0.27 .. 500** |
| AllPassSize | 16 | 0.178 .. 1.000 |
| MixDirect | 12 | 0.197 .. 0.500 |
| ShelfLoGain | 9 | 0.200 .. 1.000 |

**Fixed in every one of the 19:** `RoomType=SuperEco`, `SizeSmoothing=Fast`,
`BandLowOn`, `ShelfLowOn`, `ShelfHighOn` all true, `FreezeOn` false. So the
filter sections are always on and a model needs them.

⭑ **One model, everywhere.** `RoomType` is `"SuperEco"` in **all 466** reverb
instances across the entire CoreLibrary — every drum kit, every track preset,
every audio-effect preset. The binary does carry a `ReverbQualityMode` enum
(with `LinearConverter` and `StringConverter` specialisations, i.e. real
alternatives exist internally, and the recon note reads it as a quality
dimension multiplying the kernel bank), but Move never instantiates anything
else. **Modelling SuperEco is modelling the whole device** — no mode switching,
no per-preset variation.

### The laws, over the real ranges

30 s renders, single-sample impulse, RT60 by log-envelope regression.

**DecayTime — and it SATURATES:**

| DecayTime | RT60 | RT60/DecayTime |
|---:|---:|---:|
| 600 ms | 0.60 s | **1.01** |
| 1500 ms | 1.33 s | 0.89 |
| 4000 ms | 2.62 s | 0.66 |
| 10000 ms | 4.50 s | 0.45 |
| 19500 ms | 5.44 s | **0.28** |

One-to-one at the short end, compressing hard as it grows, and heading for an
asymptote around 5-6 s. **Move's reverb cannot produce an arbitrarily long
tail**, whatever the preset asks for — which is presumably exactly what
"SuperEco" buys. The earlier 3-point sweep to 3600 ms only saw the top of this
curve and read it as a mild droop.

**RoomSize** — mild on decay, real on onset:

| RoomSize | 0.3 | 10 | 60 | 200 | 500 |
|---|---:|---:|---:|---:|---:|
| RT60 | 1.35 | 1.42 | 1.61 | 1.90 | 1.88 s |
| onset | 0 | 0 | 0 | 0 | 20 ms |

**MixDirect** — a pure output level on the wet signal:

| MixDirect | 0 | 0.2 | 0.35 | 0.5 | 1.0 |
|---|---:|---:|---:|---:|---:|
| RT60 | — | 1.61 | 1.61 | 1.61 | 1.61 s |
| peak | **silent** | 0.9 | 1.5 | 2.0 | 2.8 (x10^-6) |

RT60 is identical at every non-zero setting, so it is level only — and 0 mutes
the device outright, which is what made the "100% wet insert" attempt render
nine silent files. Despite the name it scales the reverb's own output rather
than a dry path.

---

## THE MANUAL — 2026-07-29 (Live 12 manual, 28.32, pp. 608-612)

Josh pointed at the Live manual's Reverb section. It names every control, which
maps the 33 serialized parameters onto a real UI and **corrects three things I
had inferred wrongly**. No captures were run for this.

### The mapping

| Manual UI | `.abl` parameter | note |
|---|---|---|
| Input Filter: Lo Cut / Hi Cut, centre + bandwidth | `BandFreq` `BandWidth` `BandLowOn` `BandHighOn` | ⚠ this is the **INPUT** filter |
| Early Reflections: Spin on / Amount / Rate | `SpinOn` `EarlyReflectModDepth` `EarlyReflectModFreq` | |
| Early Reflections: **Shape** | `DiffuseDelay` | prominence of the ERs and their overlap with the tail |
| Diffusion Network: high shelf | `ShelfHighOn` `ShelfHiFreq` `ShelfHiGain` `HighFilterType` | |
| Diffusion Network: low shelf | `ShelfLowOn` `ShelfLoFreq` `ShelfLoGain` | |
| Diffusion Network: **Diffusion** / **Scale** | `AllPassGain` / `AllPassSize` | density and coarseness |
| Chorus: Amount / Rate | `SizeModDepth` `SizeModFreq` `ChorusOn` | |
| Global: Predelay / Size / Decay / Stereo / Smooth | `PreDelay` `RoomSize` `DecayTime` `StereoSeparation` `SizeSmoothing` | |
| Global: Freeze / Flat / Cut | `FreezeOn` `FlatOn` `CutOn` | |
| Global: **Density** | **`RoomType`** | ⭑ see below |
| Output: Reflect / Diffuse / **Dry/Wet** | `MixReflect` `MixDiffuse` **`MixDirect`** | |

### ⭑⭑ "RoomType" is DENSITY — a CPU/quality tier, not a room shape

> "The Density chooser controls the tradeoff between reverb quality and
> performance. Sparse uses minimal CPU resources, while High delivers the
> richest reverberation."

Live offers Sparse / Low / High. Move serializes `RoomType: "SuperEco"`, which
is evidently **a tier below Sparse**, specific to the hardware. That fits the
binary carrying a `ReverbQualityMode` enum, and it settles what SuperEco *is*:
not a different algorithm, the same algorithm run at the cheapest density.

**And it explains the decay saturation we measured.** A sparser diffusion
network has fewer recirculating paths, so it cannot hold energy long enough to
deliver a long tail — the measured RT60 falls further behind `DecayTime` the
longer the tail is asked to be. That is a consequence of the density tier, not a
separate mystery.

### ⭑ Decay is DEFINED as RT60

> "The Decay control adjusts the time required for this reverb tail to drop to
> 1/1000th (-60 dB) of its initial amplitude."

So `DecayTime` **is** RT60 by the device's own definition, and our measurement
says how badly SuperEco fails to deliver it:

| DecayTime | RT60 delivered | shortfall |
|---:|---:|---|
| 600 ms | 0.60 s | none — exactly on spec |
| 1500 ms | 1.33 s | −11% |
| 4000 ms | 2.62 s | −34% |
| 10000 ms | 4.50 s | −55% |
| 19500 ms | 5.44 s | **−72%** |

That is a much sharper statement than "the ratio drifts": the device is on spec
up to about a second and then increasingly cannot deliver what it is asked for.

### ⚠ Three corrections to earlier notes in this document

1. **`BandFreq`/`BandWidth` are the INPUT filter**, applied before the reverb —
   not tail damping. The tail's frequency-dependent decay is the two **shelves**
   in the Diffusion Network ("The high-frequency decay models the absorption of
   sound energy due to air, walls and other materials"). So the per-octave
   damping campaign should sweep the shelves, and treat Band* as an input EQ.
2. **`MixDirect` is Dry/Wet**, not a "direct output level". The empirical result
   stands — 0 silences a return and it does not affect RT60 — and now has a
   reason: on a return chain there is no dry path, so a fully-dry setting has
   nothing to pass.
3. **`DiffuseDelay` is "Shape"**, the early-reflection control, not a delay time.
   Small values let the reflections decay gradually with the diffusion starting
   sooner; large values make them decay fast with a later diffuse onset.

### What this changes about the plan

- Sweep the **shelves** for the damping laws, not the band filter.
- `RoomSize`'s manual description matches what we measured: "a very large size
  will lend a shifting, diffused delay effect... a very small value will give it
  a highly colored, metallic feel", and the stock range is 0.27-500.
- `Density`/`RoomType` needs no sweeping at all — Move only ever ships SuperEco.
- `Smooth` is always `Fast` on Move and only governs behaviour *while Size
  changes*, so it is irrelevant to a static model.

---

## THE DECOMPILE CAUGHT UP — 2026-07-29, and it changes the goal

`move original reconstruct/analysis/audio-effects/` gained ~30 files today,
including `REVERB_QUALITY_NETWORK_RECON.md`. **The reverb is essentially
solved statically.** This changes the campaign from "fit a model to
measurements" to "port the algorithm and use measurements to VERIFY it".

### What is now exact

**`RoomType` IS `ReverbQualityMode`**, confirming the manual's Density reading
and quantifying it:

| value | name | feedback lanes |
|---:|---|---:|
| 0 | **SuperEco** | **4** |
| 1 | Eco | 6 |
| 2 | Mid | 8 |
| 3 | High | 10 |

`N = 2 * RoomType + 4`, and the N x N feedback matrices are **orthonormal**
(verified to ~1e-6), i.e. energy-preserving diffusion. SuperEco is the same
algorithm at 4 lanes — not a different reverb.

**The decay law:**

```
decayRate       = 1 / (1.15 * DecayTimeSeconds)
feedbackGain[i] = exp(-8.833317 * pathTime[i] / (1.15 * DecayTimeSeconds))
```

**Delay lengths**, with the four SuperEco constants per family given
explicitly, and ⭑ `RoomSize` entering as a **cube root**:

```
r         = max(cuberoot(RoomSize), 0.0001)
tankScale = 0.93 * r
lengthA[i] = fs * 0.001 * tankScale * 0.500 * A[i]
lengthB[i] = fs * 0.001 * tankScale * AllPassSize * 0.750 * B[i]
lengthC[i] = fs * 0.001 * tankScale * AllPassSize * 0.395 * C[i]
```

Plus the shelf builder (TPT state-variable, K = 1.4357497692108154, the stock
`tan` rational approximation and clamped polynomial `exp2`), the Lowpass-mode
one-pole alternative, and Freeze as `decayRate = 0` making every gain unity.

### ⭑ Cross-check against our captures — the law is confirmed at 1.5 s

Over elapsed time a lane makes `x/pathTime` passes, so the feedback term gives
`amplitude(x) = exp(-8.833317 * x / (1.15 * T))`, hence

```
RT60 = ln(1000) * 1.15 / 8.833317 = 0.8993 x DecayTime
```

**independent of path time — and therefore of RoomSize.** Measured:

| DecayTime | predicted | measured | delta |
|---:|---:|---:|---:|
| 600 ms | 0.54 | 0.60 | +11% |
| 1500 ms | **1.35** | **1.33** | **−1%** |
| 4000 ms | 3.60 | 2.62 | −27% |
| 10000 ms | 8.99 | 4.50 | −50% |
| 19500 ms | 17.54 | 5.44 | −69% |

**1% agreement at 1500 ms** — two completely independent routes to the same
number, which validates both the decompiled constant and the capture rig. It
also independently confirms the RoomSize prediction: our sweep moved RT60 only
1.35 -> 1.90 s across 0.3..500, where the law says it should not move at all.

### ⚠ What the saturation is NOT

The obvious candidate was shelf damping. Using the benchmark's own shelves
(`ShelfLoGain` 0.6167, `ShelfHiGain` 0.8833) and the builder's
`gain = G^(6x)` form — also path-time independent — gives ceilings of 2.38 s
(low band) and 9.28 s (high band). Adding the low-shelf rate to the feedback
rate predicts 0.44 / 0.86 / 1.43 / 1.88 / 2.10 s against measured 0.60 / 1.33 /
2.62 / 4.50 / 5.44 — **over-predicting the damping roughly two-fold**.

So a single broadband shelf term is the wrong model. The shelves only attenuate
outside 671 Hz .. 1470 Hz here, and a broadband RT60 is dominated by whichever
band decays slowest — plausibly the midband between them, where neither shelf
acts. **The next measurement is therefore RT60 PER BAND**, which separates the
three regions and identifies what actually limits the tail.

### What this means for DR32

Fitting curves is now the wrong approach. With the topology, the matrices, the
delay constants, the decay law and the shelf recurrences all recovered, the
sensible path is to **implement SuperEco directly** — 4 lanes, orthonormal
mixing, the three delay families, the exact feedback-gain law — and use the
capture rig as the null test rather than as the source of the model.
