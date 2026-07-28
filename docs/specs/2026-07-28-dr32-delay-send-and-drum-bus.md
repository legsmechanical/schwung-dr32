# DR32 — Delay send type + always-on Drum Bus

Spec, 2026-07-28. Two independent changes that share one file (`dsp/dr32_fxbus.cpp`),
so they are specified together and can ship together.

1. **Delay** joins Plate and Spaces as a selectable send effect.
2. **Drum Bus** stops being a selectable effect and becomes a fixed, always-on
   insert at the end of the kit's signal chain.

---

## 1. Why Delay, and what the native device actually is

Surveyed all **77** native drum-kit presets on the device
(`/data/CoreLibrary/Track Presets/Drums`). Every native drum rack has **exactly one
return chain holding exactly one device**:

| device `kind` | kits |
|---|---|
| `reverb` | 61 (all `RoomType: "SuperEco"`) |
| `delay` | 12 |
| `chorus` | 3 (all `Mode: "Vibrato"`) |
| `phaser` | 1 (`Mode: "Doubler"`) |

Delay is the whole second tier. DR32 currently cannot represent those 12 kits'
returns at all.

### Measured settings of all 12 native delay returns

| kit | 16thL | 16thR | sync | pingpong | fb | filter Hz | bw (oct) | modAmt |
|---|---|---|---|---|---|---|---|---|
| Tamuz | 1 | 4 | ✓ | – | 0.60 | 530 | 3.86 | 0.219 |
| BNYX Boot | 1 | 4 | ✓ | – | 0.22 | 915 | 4.41 | 0 |
| Chicago | 3 | 4 | – | ✓ | 0.12 | 2562 | 8.67 | 0.580 |
| Clockwork | 2 | 4 | ✓ | – | 0.65 | 726 | 3.33 | 0 |
| Grate | 1 | 16 | ✓ | ✓ | 0.51 | 1020 | 4.00 | 0 |
| Lay Down | 2 | 4 | ✓ | ✓ | 0.50 | 5394 | 8.00 | 0 |
| Akustichord | 1 | 4 | ✓ | ✓ | 0.50 | 1620 | 4.25 | 0 |
| Heavy Mellow | 1 | 4 | ✓ | – | 0.39 | 820 | 3.18 | 0 |
| KUCKA 1 | 3 | 4 | ✓ | ✓ | 0.25 | 3913 | 4.41 | 0.030 |
| Railway | 1 | 4 | ✓ | – | 0.73 | 1620 | 4.25 | 0 |
| Rattle | 1 | 4 | ✓ | ✓ | 0.37 | 557 | 3.42 | 0 |
| Riddim Rager | 1 | 4 | ✓ | ✓ | 0.73 | 1620 | 4.25 | 0 |

What that dictates, and what it lets us drop:

- **Tempo-synced in whole 16ths.** `DelayLine_SyncedSixteenth{L,R}` is a *count of
  16th notes*, 1..16 — no triplets, no dotted values in this device's synced mode.
  Sync is on in 11 of 12.
- **L and R are independently timed and almost always differ**: L is 1–3 sixteenths,
  R is 4 in ten of twelve kits. That asymmetry *is* the sound; a single shared time
  knob would not reproduce any of these presets.
- **The feedback filter is always on**, a bandpass at 530 Hz–5.4 kHz with ~4 octaves
  of bandwidth (median 1.3 kHz / 4.2 oct; the two 8-oct outliers are nearly wide open).
- **`SmoothingMode` is `"Repitch"` in all twelve** — retiming repitches the tail,
  tape-style. There is no other mode in the corpus, so implement that one and skip
  the mode switch.
- **Modulation is off in 9 of 12** and small in the rest (0.03–0.58 with rates from
  0.27 Hz to 23 Hz — no coherent idiom). **Dropped** from this spec.

### Fidelity status — read before treating this as a null test

DR32's sends are **not** reconstructions. Plate is a Dattorro tank and Spaces is
Airwindows Verbity2; neither is Move's `reverb` device, because that device was never
reverse-engineered. The Delay is the same kind of thing: a DR32 delay whose *controls
and defaults* are taken from the native device, not a bit-target.

This is a deliberate exception to CLAUDE.md's "any deviation is a fidelity bug", which
governs the **drum sampler voice**. A null test against a native delay return *is*
feasible later (the 12 kits above are ready-made fixtures for `tools/fx_suite.sh`) —
it is out of scope here, and nothing in this spec should be described as measured
against native.

---

## 2. Delay — DSP design

New type `DR32_EFX_DELAY` in `dr32_efx_type`, name `"Delay"`. Implemented as a
`struct Delay` in the anonymous namespace of `dsp/dr32_fxbus.cpp`, resident in `Slot`
alongside the reverbs (all algorithms stay allocated — a type change happens on the
audio thread and must not allocate).

### Controls — five, not four

The reverbs use four generic slots (`p1..p3` + `predelay`). Delay needs five. There is
already a fifth float in the cache: `send_p[2][5]`'s index 4, the **vestigial `mix`**
that stopped meaning anything when kit inserts were removed (`dr32_fxbus.h:84-90`
says so explicitly). Repurpose it rather than growing the arrays:

- `dr32_fxbus_set_send_params()` grows a fifth argument `p5`.
- `dr32_efx_defaults()` keeps filling 5 floats; `o[4]` becomes "the fifth control"
  instead of "mix". Reverb `o[4]` values (0.28 / 0.25) are now dead and should be
  set to 0 with the stale comment removed.

| slot | Delay control | range / meaning | default |
|---|---|---|---|
| p1 | **Time L** | 1..16 sixteenths | 1 |
| p2 | **Time R** | 1..16 sixteenths | 4 |
| p3 | **Feedback** | 0..0.95 | 0.50 |
| p4 | **Tone** | bandpass centre, 200 Hz..6 kHz log | 0.55 (≈1.3 kHz) |
| p5 | **Ping-Pong** | 0..1 crossfeed | 0 |

⚠ Unlike every other control in the fxbus, **Time L/R are stored in sixteenths
(1..16), not normalised 0..1.** The cache is already per-type in meaning (`size` vs
`comp`), so this is consistent — but it is the kind of thing that silently produces a
1-sample delay if assumed normalised. State it at the cache declaration.

Defaults are the corpus's dominant configuration (L=1, R=4), feedback its median, tone
its median centre.

### Structure

Two delay lines, one per channel, each with its own length.

```
in ──┬─────────────────────────────► outL/outR (the return is 100% wet)
     │
   ┌─┴──────────────┐
   │ line L, line R │◄── feedback: (1-x)·self + x·other,  x = ping-pong
   └────────┬───────┘
            └─ bandpass in the loop (LP at f·4, HP at f/4)
```

- **Ping-Pong is continuous crossfeed**, not a switch. `x = 0` gives two independent
  stereo lines (native `PingPong: false`); `x = 1` gives full alternating crossfeed
  (native `PingPong: true`). The corpus's two states are the endpoints, and everything
  between is a legitimate width control. A continuous float also avoids parsing an
  enum string in the generic-slot path.
- **Tone** is one knob driving the native pair: centre `f` swept log 200 Hz..6 kHz,
  bandwidth pinned at the measured norm of **4 octaves** — one-pole LP at `f·4`,
  one-pole HP at `f/4`, both inside the feedback loop. Feedback filtering is what
  keeps repeats from accumulating into mush, which is why the native device has it on
  in every kit.
- **Repitch retiming.** Target length changes (tempo or knob) are not applied
  instantly: the active length slews toward the target at a bounded rate (~1 sample
  per 8 output samples), which repitches the tail exactly as `SmoothingMode:
  "Repitch"` does. Fractional read position with linear interpolation — matching the
  voice's reader, which is linear by measurement (CLAUDE.md).
- **Feedback is hard-capped at 0.95** so a send return, which has no dry path to
  balance it against, cannot run away.

### Tempo

`host_api_v1.get_bpm()` exists (`schwung/src/host/plugin_api_v1.h:87`) and returns
120.0 as its documented fallback, so synced time always resolves — but the pointer
**may be NULL on older hosts and must be guarded**.

`dr32.c` holds `g_host`, so:

- `render_block()` reads `g_host->get_bpm()` (guarded) and calls a new
  `dr32_kit_set_bpm(&in->kit, bpm)` before `dr32_kit_render()`.
- The kit forwards it to the fxbus, which **recomputes lengths only when the BPM
  actually changes** — a float compare per block, no per-block division.
- Length in samples: `sixteenths · (60/BPM/4) · fs`.

**Buffer sizing.** 16 sixteenths = 4 beats = 4 s at 60 BPM. Allocate **4 s stereo per
send slot** (≈1.4 MB per slot at 44.1 kHz, ≈2.8 MB for both) at create time, and clamp
the computed length to it — below 60 BPM the longest divisions stop tracking tempo,
which is the right trade against an unbounded allocation. Allocation happens in
`dr32_fxbus_create()`, never on the audio thread.

### Param plumbing (`dsp/dr32_params.c`)

Extend the existing alias scheme — distinct keys, same slots, in both
`dr32_read_param` and `dr32_apply_param` (they must stay in lockstep; a key that
applies but does not read back is the "every knob reads zero" failure MODULES.md
warns about):

| idx | existing aliases | add |
|---|---|---|
| 0 | `size`, `comp`, `p1` | `time_l` |
| 1 | `damp`, `crunch`, `p2` | `time_r` |
| 2 | `decay`, `attack`, `p3` | `feedback` |
| 3 | `predelay`, `sustain` | `tone` |
| 4 | *(new)* | `pingpong`, `p5` |

`send_p[slot]` is already width 5. `dr32_apply_param`'s call to
`dr32_fxbus_set_send_params` gains `cache[4]`.

---

## 3. Delay — UI

### `src/module.json`

Add `"Delay"` to both `send1_type` / `send2_type` option lists, and add five params
per send level: `sendN_time_l`, `sendN_time_r`, `sendN_feedback`, `sendN_tone`,
`sendN_pingpong`.

That makes 9 controls on a level where only 4–5 apply at once, so gate them with
**`visible_if`** (MODULES.md:1039 — `equals` / `not_equals`, evaluated dynamically,
hidden entries drop out of list navigation *and* knob mappings):

- reverb params (`size`, `damp`, `decay`, `predelay`): `{"param": "sendN_type",
  "not_equals": "Delay"}`
- delay params: `{"param": "sendN_type", "equals": "Delay"}`

`type` and `return` stay unconditional. Types:

| key | type | min | max | step |
|---|---|---|---|---|
| `sendN_time_l`, `sendN_time_r` | `int` | 1 | 16 | 1 |
| `sendN_feedback` | `float` | 0.0 | 0.95 | 0.01 |
| `sendN_tone` | `float` | 0.0 | 1.0 | 0.01 |
| `sendN_pingpong` | `float` | 0.0 | 1.0 | 0.01 |

⚠ `tools/check_module_json.mjs` runs in `tests/run.sh` and rejects duplicate keys
across the whole hierarchy. The five new keys are per-send, so they are distinct —
but run it, don't assume.

### `src/canvas.config.js`

`sendBank()` currently returns a fixed six-cell `knobs` array labelled for a reverb
(`Size / Damp / Dcy / Pre`). With Delay selected those labels are simply wrong.

Fix with **`bank.dynamicCells(ctx, s)`**, which the kit already supports
(`schwung-canvaskit/core/engine.js:59-61`, and DR32 already uses `dynamicCells` for
the browse picker): return the reverb cell set or the delay cell set according to
`ctx.getParam("sendN_type")`.

```
Off / Plate / Spaces   Type  Size  Damp  Dcy  Pre  Ret          (6 cells)
Delay                  Type  TimL  TimR  FB   Tone PP    Ret    (7 cells)
```

Square labels: `kSendSq` gains `"DLY"`.

⚠ **`padBanks` maps SPEC → BANK and the engine reads the BANK.** A field added to the
spec and not forwarded in that mapping is silently ignored — that is exactly how
`dynamicCells` was dropped on the floor once already. `sendBank()` builds its object
literally rather than going through that map, so this specific hazard does not apply
here — but check it, because the pad banks *do* go through it.

`writeInvalidates` already flushes on `/^send\d_type$/` — a type change swaps the
whole visible parameter set, so the cache must not survive it. Unchanged.

---

## 4. Drum Bus — always-on master insert

### What changes

`DrumBuss` (`dsp/dr32_fxbus.cpp:65`) stops being reachable as a send type and becomes
a single instance owned by `dr32_fxbus`, run over the **summed** output.

Placement, in `dr32_kit_render()` terms (`dsp/dr32_kit.c:282-318`):

```
pads (dry) ──┬──────────────────────────────────► sum ─┐
             └─ send 1/2 ─► FX ─► return gain ────────►│
                                                       ▼
                                        DRUM BUS (always on)
                                                       ▼
                                            master_gain (trim)
                                                       ▼
                                              int16 clamp, out
```

The bus runs at the **end of `dr32_fxbus_process()`**, after the send returns are
summed into `out` and before `dr32_kit_render()` applies `master_gain`. Master stays a
final trim, which is what it is.

Note the consequence, since it is the point of the feature: **the send returns go
through the bus too.** A reverb tail is compressed and saturated with the dry kit, the
way a drum bus on a real desk works, not glued only to the dry pads.

### It must be free when it is not used

Always-on cannot cost 0.37% of a core on every instance that never touches it.
`DrumBuss` already gates `compOn` / `atkOn` / `susOn`, but the saturator and DC
blocker still run.

Add a `neutral()` test — `comp ≤ 0.001 && crunch ≤ 0.001 && |attack−0.5| ≤ 0.001 &&
|sustain−0.5| ≤ 0.001` — and **skip the stage entirely** when it holds. Since the
defaults are exactly neutral (see `dr32_efx_defaults`'s `DR32_EFX_DRUMBUSS` case and
its note that Attack/Sustain are bipolar about 0.5), a DR32 that never opens the Drum
Bus page is byte-identical to today's and costs one float compare per block.

⚠ **0.0 is not neutral for Attack or Sustain** — it pulls the tail down ~8 dB. The
defaults must be `{0, 0, 0.5, 0.5}`, and a fresh instance must start there rather than
at zeroed memory. This is already written down in the header and has bitten before.

### API and params

```c
void dr32_fxbus_set_bus_params(dr32_fxbus *fx, float comp, float crunch,
                               float attack, float sustain);
```

`dr32_kit` gains `float bus_p[4]` (cache, so the UI can set one at a time, same as
sends) initialised to `{0, 0, 0.5, 0.5}` in `dr32_kit_init`. Keys, in both apply and
read: `bus_comp`, `bus_crunch`, `bus_attack`, `bus_sustain`.

**Persistence comes from declaring them in `ui_hierarchy`.** DR32 implements no
`"state"` key — the host reconstructs a slot by walking the declared params and
calling `get_param` for each. A param the DSP accepts but the hierarchy does not
declare is not saved and not restored.

`dr32_fxbus_reset()` must reset the bus (comp, both envelope detectors, the DC
blocker) alongside the sends — kit change and panic both land there.

### Removing it as a send type

Per the standing DR32 rule ([[schwung-dr32-no-backcompat-while-developing]]) — no
back-compat while developing, the native `.ablpreset` format excepted — delete it
outright rather than hiding it:

- drop `DR32_EFX_DRUMBUSS` from `dr32_efx_type`
- drop its cases in `dr32_efx_name`, `dr32_efx_from_name`, `dr32_efx_defaults`,
  `Slot::processBlock`, `Slot::apply`
- drop the `comp` / `crunch` / `attack` / `sustain` aliases from the **send** slot
  paths in `dr32_params.c` (they move to the `bus_*` keys)
- the `DrumBuss` member moves off `Slot` and onto `dr32_fxbus`

`kSendTypes` in the canvas already omits it, so the UI needs no change on this side.
A saved state naming `"Drum Bus"` as a send type now resolves to `Off` via the
existing unknown-name fallback — acceptable and intentional.

### UI

- `src/module.json`: new `bus` level, `"name": "Drum Bus"`, four params (`float`,
  0..1, step 0.01; Attack and Sustain default 0.5), listed under the existing `fx`
  level next to Send 1 and Send 2. `"knobs"` = all four.
- `src/canvas.config.js`: a `busBank()` — Comp / Crunch / Atk / Sus — appended to
  `DR32_BANKS`, plus a `{ name: "Drum Bus", bank: 5 }` section and its icon.
  ⚠ `CONFIG.icons` currently holds 5 entries for 4 sections; adding a section makes
  that 5-for-5 by accident. Check what the extra entry was doing before assuming it
  lines up.

### Naming

Keep **"Drum Bus"**, not Ableton's "Drum Buss" — it is what the code, the UI and the
worklogs already say, and the C++ struct's `DrumBuss` spelling is internal.

---

## 5. Interaction with the standalone Drum Bus FX module

The board has a spec'd standalone `audio_fx` Drum Bus that lifts the `DrumBuss` struct
whole. **Both can exist** — DR32's is glue on its own mix; the standalone one is for
anything else in a chain. This spec does not block it, and the struct stays the single
source for both. Whoever cuts the standalone module copies the struct at that point;
do not pre-factor a shared header for a second consumer that does not exist yet.

---

## 6. Tests

`tests/run.sh` (46+47+74+51+37 checks, all green as of `3c4d729`) — extend
`tests/test_fxbus.c`:

**Delay**
1. **Sync law.** At 120 BPM, `time_r = 4` sixteenths → an impulse reappears at
   0.500 s ±1 sample. At 90 BPM → 0.667 s. Catches an off-by-4 in the division maths.
2. **L and R are independent.** `time_l = 1`, `time_r = 4` → first repeat on L only,
   at 0.125 s; R silent until 0.500 s.
3. **Ping-pong endpoints.** `pingpong = 0`: an impulse on L alone never appears on R.
   `pingpong = 1`: repeat *n* alternates channels.
4. **Feedback is bounded.** `feedback` at its max, 30 s of a repeating impulse → peak
   stays finite and below full scale. This is the runaway guard.
5. **Tone moves the loop, not the first tap.** Sweeping `tone` changes the spectral
   centroid of repeat 4 substantially while repeat 1 stays within a small tolerance —
   the filter is in the feedback path, not on the output.
6. **Buffer clamp.** 40 BPM with `time_r = 16` does not read out of bounds and does
   not crash (run under `-fsanitize=address` locally if quick).

**Drum Bus**
7. **Neutral is bit-transparent.** Render a fixture with the bus at defaults and
   compare against the same render with the stage compiled out — must be
   **bit-identical**, not "close". This is the whole justification for always-on.
8. **Engaged is not.** Crunch at 1.0 changes the output; the neutral test above is
   therefore proving a real bypass and not a dead code path.
9. **Returns pass through the bus.** With send 1 on and the dry pads silent, moving a
   bus control still changes the output.
10. **Reset drops state.** After a loud hit, `dr32_fxbus_reset()` then silence in →
    silence out (no envelope or DC-blocker residue).

Plus `node tools/check_module_json.mjs src/module.json` for the new keys and
`visible_if` blocks — already part of `run.sh`.

⚠ `run.sh` was **RED at `651adcc`** on an unrelated `-Werror` break, which shipped a
DSP change unverified. Run it; do not assume it was green.

**Verify the canvas by RENDERING** (`schwung-canvaskit/preview.mjs`) with each send
type selected. The per-type cell swap is precisely the class of change that looks
right in the numbers and wrong on the device.

---

## 7. What this does not do

- **No native return mapping.** `lib/ablpreset.mjs` parses `returnChains` into
  `sendFx` and keeps it **verbatim and inert** (`lib/ablpreset.mjs:282-288`). Loading a
  kit whose return is a delay still will not *configure* DR32's send — that is the
  same parsed-but-inert bucket as M2's effect param units, and belongs with that work.
  Doing it here would mean a device-param → DR32-control mapping for reverb as well,
  which is a separate arc.
- **No chorus or phaser.** 4 kits between them; revisit after Delay lands.
- **No delay modulation section.** Off in 9 of 12 native kits, and no coherent idiom
  in the other 3.
- **No null test against the native delay.** Feasible later — the 12 kits above are
  ready-made fixtures — but this is a DR32 delay, not a reconstruction.

## 8. Work order

1. `dr32_fxbus.h/.cpp` — `p5` argument, `struct Delay`, `DR32_EFX_DELAY`, BPM setter;
   hoist `DrumBuss` to the bus with the `neutral()` bypass; remove `DR32_EFX_DRUMBUSS`.
2. `dr32_kit.h/.c` — `bus_p[4]`, `dr32_kit_set_bpm`, forward both.
3. `dr32.c` — guarded `get_bpm` in `render_block`.
4. `dr32_params.c` — new aliases in apply **and** read.
5. `src/module.json` — Delay options, five send params with `visible_if`, `bus` level.
6. `src/canvas.config.js` — `dynamicCells` on the send banks, `busBank()`, section,
   `"DLY"`. `build.sh` does **not** regenerate the canvas — it only copies
   `src/canvas.js` — so run the kit build by hand and **commit the generated file**:

   ```sh
   node ../schwung-canvaskit/build.mjs src/canvas.config.js src/canvas.js
   ```

   DR32's committed `canvas.js` was generated at **canvaskit v37**, which is the kit's
   current HEAD, so this regenerates in place with no version jump. It also means the
   canvas is a **UI-only** artifact: the host re-reads it on every editor open, so
   iterating on the UI alone needs a copy, not a full install.
7. Tests, then render the canvas, then `tests/run.sh`.
8. **Ask before deploying** — Josh tests on his own cadence.

---

## 9. As built (2026-07-28)

Implemented, `tests/run.sh` green at **62+47+74+51+37**, ARM build packaged.
Four things came out different from the spec above, all of them found by tests:

1. **A delay whose input is silent must not be treated as idle.** The bus stops
   processing a slot after ~4 s of silence at its INPUT. That is right for a
   reverb and wrong for a delay — silence between hits is the state a delay is
   *for*, and 16 sixteenths at 120 BPM is 2 s per repeat. The idle counter is now
   **armed by the input and held open by the output**: anything still making
   sound keeps its slot alive, whatever the algorithm. Not in the spec; it would
   have shipped as "the repeats stop after a few seconds".

2. **An empty line has nothing to repitch.** The slew made the delay spend its
   first seconds sliding up from wherever it started, so the first repeat came
   back at the wrong time. A `primed` flag now makes a time change LAND while
   the lines are empty and glide only once there is something in them.

3. **Slew rate is 0.5 samples/sample, not 0.125.** At 0.125 the largest jump took
   four seconds to cross, which reads as a broken control rather than a glide.
   0.5 means the read pointer runs at 0.5x/1.5x while travelling — an audible
   whoosh — and the biggest jump settles in about 1.5 s.

4. **Time L/R are integer cells (`pint`), not continuous.** A continuous cell
   would have sent 3.24 sixteenths: a synced control landing off the grid.

Verified by rendering (`preview.mjs` wrapper, send type forced to each value):
the Send page swaps between `Type/Size/Damp/Dcy/Pre/Ret` and
`Type/TimL/TimR/FB/Tone/PP/Ret`, the touched-knob header reads the full param
name, and the Drum Bus page and its picker row draw correctly.

⚠ `CONFIG.icons` is indexed by **bank**, not by section — bank 4 (Send 2) has no
picker row but still occupies a slot, so the Drum Bus at bank 5 needed a sixth
entry or it would have inherited Send 2's icon.

**Not deployed.** Waiting on Josh's go-ahead.

---

## 10. Device round 1 — three defects Josh found (2026-07-28, same day)

Deployed, played, and all three of these came back from the device. Fixed,
re-tested (fx-bus file now 68 checks), redeployed 16:20.

1. **⭑ Ping-pong did nothing when L and R times were equal.** Crossing only the
   FEEDBACK is arithmetically a no-op when `outL == outR`, which is the most
   ordinary setup there is: equal times, centre-panned pad. The INPUT has to be
   steered as well — at full ping-pong the hit is summed to mono and injected
   into the LEFT line only, so repeat 1 returns left, the crossed feedback puts
   repeat 2 right, and it alternates.

   **The test was the reason this shipped.** It drove ping-pong with an L-ONLY
   impulse, which is exactly the one input that produces a right-channel repeat
   without the fix. A test whose signal is special-cased to the mechanism you
   implemented proves the mechanism runs, not that the control works. The new
   test uses a CENTRED hit with equal times, and asserts alternation
   (repeat 1 L-only, repeat 2 R-only) plus the converse — ping-pong at 0 must
   leave a centred hit balanced.

2. **Attack and Sustain read as unipolar.** They were bipolar in the DSP all
   along but exposed as 0..1 with neutral at 0.5, so they drew as ordinary
   knobs whose off position sat in the middle. Now **-1..+1, neutral 0**, with
   the conversion to DrumBuss's internal 0..1-about-0.5 living at exactly one
   boundary (`dr32_fxbus_set_bus_params`).

3. **Mix went missing.** Josh: "drum bus may be missing some params that were
   originally there... I could be wrong." He was right — `insertBank()` at
   `229f219^` had Comp/Crunch/Atk/Sus **plus Mix (Dry/Wet)**. §2 above called
   the fifth cache slot vestigial, which is true for a SEND (always 100% wet)
   and false for an INSERT, where the blend is parallel compression. Restored,
   default 1.0 so it only ever takes the bus away.

   The neutral bypass is deliberately NOT keyed on Mix: at neutral the stage
   passes its input through, so blending it against the dry is still the dry.

---

## 11. Free-running (unsynced) delay mode — Josh's ask, 2026-07-28

Built, tests green at 76 checks, ARM build packaged, **deployed 17:06**.

### Shape

`Time Mode` = **Sync / Free** per send. Synced times stay a count of sixteenths;
free times are **milliseconds, 10..2000, logarithmic** on the canvas (linear
would spend most of the knob above half a second, where a drum delay rarely
lives). Both pairs are stored AT ONCE and both survive a flip of the flag —
which is what the native device does: its Chicago Kit sits at `SyncL=False`
while still carrying `SyncedSixteenth 3/4`. Flipping recalls what that mode last
had rather than reinterpreting one number in the wrong unit.

Free defaults are 125 / 500 ms — exactly what the synced default produces at
120 BPM, so switching to Free does not move the delay, it just stops it
following the tempo.

### The one design constraint worth recording

**`visible_if` takes a SINGLE condition on a SINGLE param** — `equals`,
`not_equals`, `gt`, `lt`, `truthy` (`shadow_ui.js:2119`). No AND, no lists. So
"the armed type is Delay **and** it is running free" cannot be written directly,
and the menu needs it three ways (reverb rows, synced rows, free rows).

The fix is a read-only param, `sendN_mode`, that the DSP derives and publishes:
`"Verb"` / `"Sync"` / `"Free"`. Every row on the page is then a single equality
against it, and the two-condition problem disappears. Worth reaching for again
whenever a page has more than two mutually exclusive shapes.

### ⚠ A real defect this uncovered — out-of-bounds read in the delay

The free-time test asked for 300 ms and got back a single sample of **1/1024**:
the impulse's energy did not smear across two taps, it VANISHED.

`readAt()` was computing the read position in FLOAT — `rp = w - d`, wrap in
float, then split into index and fraction. That is wrong twice, and both bites
are silent:

- **Precision.** A float carries 24 mantissa bits, so near a wrapped position of
  ~176400 (a 4 s line) one ulp is **1/64 of a sample**. The sub-sample fraction
  of the delay was being quantised by how far into the buffer the write cursor
  happened to be — a moving target that has nothing to do with the delay time.
- **Range.** `cap - tiny` **rounds up to exactly `cap`** in float, so `(int)rp`
  indexed one past the end of the line: an out-of-bounds read on the audio
  thread, presenting as a slightly wrong sample.

Now the wrap is integer and the write cursor never enters the float maths;
only the delay length does. Confirmed clean under `-fsanitize=address,undefined`.

**Why nothing caught it before:** every synced time in the suite lands on an
exact integer number of samples (4 sixteenths at 120 BPM = 22050 exactly), so
the fractional path was never exercised. The free-mode test is the first
non-integer delay in the module's history. The regression test asserts
ENERGY, not peak — a fractional tap legitimately splits across two samples, so
peak alone would have to be loose enough to miss this.

### UI note

A 2-option enum defaults to the kit's hbar toggle, which draws as an unlabelled
empty/filled bar — fine for on/off, useless for a mode, because "filled" does
not say whether it means Sync or Free. `widget: "enumsq"` gives the labelled
square (SYN / FRE). The kit's own comment on `widgetFor()` calls out exactly
this case; it is a kit FEATURE, not a kit change.
