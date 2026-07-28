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

---

## 12. Three more reverb models — Gated, Digital, Hall (2026-07-28)

Josh: "let's just stick with reverb... maybe we should investigate a few more
models to add." Built, tests green at **104 checks**, ARM build packaged. **Not
deployed.**

### The models were already in the tree

`dsp/vendor/space_extra.h` implements TEN types behind the exact interface the
Plate already uses, and every `Slot` already constructs and resets the whole
struct — **2.44 MB per slot, 4.9 MB per instance, nine tenths of it
unreachable**. Adding a type is wiring, voicing and tests; no new vendoring, no
new memory, and the licence position is unchanged (`SOURCES.md`). The
`5.5e-36l` long-double dither trap is already de-fanged in all four Airwindows
headers — worth confirming, since it costs ~1.2% of a Move core per stage.

Shipping: **Gated** (the plate tank chopped by the input envelope, `Decay` reads
as gate HOLD), **Digital** (4-line FDN at fs/2, 12-bit loop grain, the 80s rack
sound), **Hall** (Airwindows Chamber).

### Hall: the defect that got it pulled, fixed properly

Room and Hall were removed in `cef30f4` because **Chamber runs as two
independent mono reverbs** — an L-only impulse put −107 dB in the right channel.
Feeding it through the per-channel diffuser (the fix that saved Spaces) was NOT
enough: the diffuser is per-channel, so an L-only hit still left the right side
empty. Re-measured at **−112 dB**, i.e. the original defect exactly. Hall now
takes the **mono SUM** into both diffuser sides — different prime lengths keep
the two tails decorrelated — and measures **−0.0 dB** L-only→R, corr +0.04,
mono-fold −2.85 dB. Only Hall does this: the plate's figure-eight tank and the
FDN couple their channels internally (+0.1 and −0.4 dB) and forcing a mono sum
on the plate would change a voicing that has already been measured and tuned.

`07ca02c` also recorded the hall as cathedral-sized. Measured: decay scaled at
0.62 still gave **5.83 s at half knob**. Scaled to **0.20** it spans 0.37→2.60 s
with the default at 1.17 s.

### ⚠ Lo-Fi was measured and DROPPED

SpaceExtra's LoFi type (the same FDN at fs/3, 7-bit) is not offered. Two
findings, one fixed and one fatal:

- **Fixed:** its hiss floor was an unconditional MONO draw added to both
  outputs. On an insert with a dry/wet blend that is lo-fi character; on a
  100%-wet send return it measured a permanent −53 dBFS hiss whenever the type
  was merely *armed*, L/R correlation **0.97**, mono-fold −0.03 dB, and an RT60
  that never ended — which also defeats the bus's idle-skip, so the slot never
  stops costing CPU. The noise is now per-channel and gated by the tail's own
  envelope (a DR32-marked change in the vendored header, which already carried
  precedent for one).
- **Fatal:** with the hiss gone its decay knob is **dead** — RT60 spans 0.19 s
  to 0.26 s across the whole control. Raising the bit depth 7→11 only reached
  0.41 s, so the short tail is structural to that voicing rather than a
  quantisation floor. The effect is usable; the control is not. Dropped rather
  than shipped with a dead knob, and the finding is recorded next to Digital.

### The test that generalises it

Every reverb now loops over one `kVerbs` table for tails, stereo correlation,
mono-fold, default RT60 **and a new decay-range check** — `RT60(decay=1)` must
exceed `1.5 × RT60(decay=0)`. That is the Lo-Fi defect turned into a gate every
future model has to pass. Plus a gate-specific test: the hold must lengthen the
tail AND the tail must actually CHOP (measured −111 dB past the gate, where a
plate would merely fade).

Measured at defaults:

| type | RT60 | decay knob | corr | L-only → R | mono-fold |
|---|---:|---|---:|---:|---:|
| Plate | 1.53 s | 0.90 → 4.28 s | +0.00 | +0.1 dB | −2.98 dB |
| Spaces | 0.88 s | 0.28 → 2.91 s | +0.05 | −9.0 dB | −2.72 dB |
| Gated | 0.42 s | 0.22 → 0.67 s | −0.00 | +0.2 dB | −2.95 dB |
| Digital | 0.86 s | 0.52 → 2.67 s | −0.06 | −0.4 dB | −3.25 dB |
| Hall | 1.17 s | 0.36 → 2.89 s | +0.04 | −0.0 dB | −2.85 dB |

### UI

`mode` now partitions **three** ways — `Verb` / `Gate` / `Delay` — so the gated
page can swap its Decay row for Gate Hold while every row stays a single
equality (visible_if takes one condition on one param). The canvas gets a third
cell set with HOLD in place of DCY.

---

## 13. NonLin — the RMX16 trick (2026-07-28)

Josh, after asking whether Digital was the LX/AMS sound: "lets do a non linear
model." Built, **118 checks green**, ARM build packaged. Not deployed.

### What makes it not-a-gate

The picker already has Gated, and the distinction is the whole point. A gate
follows the input and chops an exponential decay — the level is always falling
underneath, and you hear a reverb being cut off. **NonLin overrides the decay
itself**: the window holds a roughly constant level (or deliberately RISES,
which no natural space does, and which is most of the character) and then stops
dead. That is what "nonlinear" names — the envelope is not an exponential.

Built from the plate tank with feedback pinned near maximum, so its natural fall
across the window is a couple of dB and the synthetic envelope does the rest.
The period units used a dense FIR; here that would be ~13k taps per channel for
a 300 ms window, hopeless per-sample on a Move core.

Controls: Size, Damping, **Length** (50..600 ms), **Shape** (falling / flat /
rising, ±9 dB across the window, normalised so a rising window is not also a
louder one), Pre-delay. Length and Shape are slots 2 and 4 — the same slots the
reverbs use for decay and nothing.

### ⚠ The tank arrives 19-53 ms late, and it always has

Measured: the shared tank's FIRST ARRIVAL is 19 ms at minimum size and 53 ms at
maximum. **This is true of the shipped Plate too** — measured identically, 0.0000
rms for the first 40 ms at its defaults. On a plate that reads as a pre-delay
you can dial around. For NonLin it is a hole exactly where the effect lives,
since these programs are a dense burst that starts WITH the hit.

Fixed by adding the diffuser's own output — already dense, already decorrelated,
arriving inside a millisecond — at 0.5 gain, for this type only. First arrival
is now **0.3-0.4 ms at every size**.

The Plate's own 40 ms onset is left alone: it is a voicing that has been
measured, tuned and heard, and this spec is not the place to change it. Recorded
here because it is worth knowing and nobody had measured it before.

### Measured

| shape | 20 ms | 100 ms | 180 ms | 260 ms | past the window |
|---|---:|---:|---:|---:|---:|
| falling | 0.0 | −1.9 | −3.8 | −14.6 | **−240 dB** |
| flat | 0.0 | −0.2 | +0.3 | −7.6 | **−240 dB** |
| rising | 0.0 | +2.8 | +5.6 | −0.6 | **−240 dB** |

Length knob: 50 → 599 ms. Window flatness at Shape 0.5: **−0.3 dB** across
40→200 ms, against the plate's **+4.1 dB** over the same span.

### Two test traps paid for here

- **The comparison source has to be a BURST.** The first version used the
  suite's `hit()`, which keeps feeding the reverb for hundreds of ms — so a
  plate's level tracks the input instead of decaying, and the flatness
  comparison measured nothing. A 64-sample windowed burst: long enough to
  trigger (a bare impulse is not a transient to a 0.5 ms follower), short enough
  that everything after it is tail.
- **Flatness is the ABSOLUTE deviation, not a signed drop.** The plate does not
  fall across 40→200 ms, it RISES ~4 dB, because its first arrival is ~40 ms and
  it is still building density at 80 ms. A signed comparison called a rising
  plate "flatter" than a flat window.
- ⚠ And one plain bug: the cliff was measured out to 700 ms in a 500 ms buffer,
  reading off the end of `wet` into the next static array — which reported a
  healthy signal past a window that had actually closed.

---

## 14. The Plate's 44 ms onset — FIXED (Josh, 2026-07-28)

Reported in §13 as a finding and left alone; Josh: "that was actually bugging me
and I was going to deal with it later, but may as well since we're here."

### Cause

**Every output tap in `plateTick` reads a TANK line.** The shortest is
`len_d1_ * 0.19`, about 28 ms at default size and 44 ms measured end to end. So
the plate produced literally nothing for its first 40 ms — at 90 BPM that is a
third of a beat after the drum — and it read as a pre-delay nobody asked for, on
top of the pre-delay control that already exists.

Meanwhile the **four input diffusers are 3.6 / 5 / 9 / 12.7 ms** at 44.1 kHz and
were never tapped for output at all. Those are exactly the times a real plate's
first reflections arrive, so the energy was already in the box.

### Fix

Two early taps per channel off the input diffusers, different lines per side so
they stay decorrelated, at 0.42 gain. **Output only** — they do not feed the
tank, so there is no new feedback path and the tank's own state is untouched.

| region | difference vs the previous build |
|---|---:|
| 0-50 ms (the point) | **+13.7 dB** |
| 200-500 ms | −23.4 dB |
| past 500 ms | −83.0 dB |

So the late field is the same tank it always was, but the tail is **not**
bit-identical: the input allpasses ring on for a few hundred ms and those rings
are now audible. An earlier draft of the comment claimed bit-identity — the
measurement says otherwise and the comment now says what was measured.

### Result

| type | onset before | onset after |
|---|---:|---:|
| Plate | 44.2 ms | **4.4 ms** |
| Gated | ~44 ms | **4.4 ms** |
| NonLin | (n/a) | **0.3 ms** |
| Spaces | 13.2 ms | 13.2 ms (untouched) |
| Digital | 27.9 ms | 27.9 ms (untouched) |
| Hall | 46.3 ms | 46.3 ms (untouched) |

Hall and Digital are deliberately left long: a hall with a 40 ms build is a
hall, not a defect, and both are different algorithms that never went through
the plate tank. The new onset test allows them 60 ms and holds everything else
under 20 ms.

NonLin's own early blend was re-measured now that the tank has early taps of its
own: without it NonLin arrives at 4.3 ms with its first 5 ms at 19% of the
window level, which for a burst effect is a soft front. Kept, reduced 0.5 → 0.35
as a top-up rather than the whole early field.

---

## 15. Release control, and "is there any real difference between Gated and NonLin?"

Josh asked for a Release on both envelope types, then asked the better question.
The honest answer was **no, barely** — measured, not argued.

### The measurement that made the case

Equal window lengths (~250 ms), same burst, level across the window in dB:

```
          40ms   80ms  120ms  160ms  200ms  240ms  280ms
Gated     -0.0   +6.0   +6.2   +6.1   +5.6   +4.1   +4.8
NonLin    -0.0   +3.9   +1.3   +3.1   -0.6   -4.5   -240
```

§13 claimed a gate "chops a falling decay while NonLin holds flat". Wrong for
this implementation: `GatedRev` pinned its tank decay at 0.72, and the tank's
density build dominates the first ~180 ms, so the gated window **rose +6 dB and
stayed there**. It was a flat window with a chop — which is NonLin.

### Option 1, as chosen: make the gate genuinely gate-like

Two changes, both measured:

- **The tank's own decay is now a control** (slot 4, `tail`; the gate's hold
  already owns slot 2, so calling it "decay" would have been ambiguous). Spread
  is 18 dB by 530 ms at a long hold.
- **Default size 0.70 -> 0.20.** This was the real error, and it was mine: a
  gate needs something to gate, and at 0.70 the tank is still building density
  through the entire hold. At 0.20 it peaks around 65 ms and falls cleanly
  (+6.9 -> -10.4 dB by 240 ms) — quick dense build, audible decay, then the
  chop.

After: Gated moves 12 dB across its window against NonLin's 4.5 dB.

### ⚠ But envelope shape is the WRONG thing to assert

With the gate's decay exposed and NonLin's Shape control, their ranges
legitimately overlap — a gate at full tail measures **-2.8 dB** peak-to-end, a
rising NonLin **-2.7 dB**. Tuning one to differ from the other would be true
only until someone turned a knob.

What separates them is structural and survives any setting. On a **sustained**
source (no transients after the start):

```
              0   100   200   300   400   500   600   700   800  (ms)
Gated        -0    +7    +8    +9    +8    +8    +8    +8    +9
NonLin       -0    +4    +4  -240  -240  -240  -240  -240  -240
```

The gate's hold **re-arms** for as long as the input is above threshold, so it
stays open; NonLin runs a **fixed window from a transient** and cuts, and a
continuous source never gives it a new transient to fire on. That is now the
test, at ±40 dB of separation rather than a tuned 1.8x on an envelope span.

### The lesson, which is the test and not the tuning

**A new type has to be measured against its NEIGHBOUR.** NonLin passed a
flatness test against the *plate* and shipped anyway, because the plate was
never the thing it could be confused with. Sitting next to Gated in the same
picker, it was nearly the same effect and nothing in the suite objected.

### Release

Both envelope types take a Release, 1 ms .. 500 ms logarithmic (the useful half
is the short end, where it decides chop vs thump). Defaults reproduce what each
had fixed before: Gated ~6 ms (its `gateGain_` one-pole was 0.004), NonLin
~1.5 ms (its fixed linear cliff). NonLin's release runs **after** the window
rather than eating the end of it, so lengthening it does not shorten what you
set.

Menu visibility needed one more derived param — `sendN_env`, "Env" for the two
envelope types and "-" otherwise — since Release must show for Gate OR NonLin
and `visible_if` takes a single condition.

Pages are now exactly at the kit's 8-cell limit:
`Gated: Type Size Damp Hold | Tail Rel Pre Ret`,
`NonLin: Type Size Damp Len | Shape Rel Pre Ret`.
