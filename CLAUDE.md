# DR32 — Move Drum Rack clone, 32 pads

## 📗 Read `../schwung-current/docs/MODULES.md` before touching module.json

It is the authoritative contract for module composition — param schema, UI hierarchy, chain
params, the drum-surface declarations, plugin API. **Read the CURRENT one in `schwung-current/`,
not `schwung/`** (that copy is months stale and predates param pages). **Do not infer the schema
from other modules' `module.json`.** This module shipped broken once by doing exactly that: it
used `label` where the schema wants `name`, invented a kit-browser arrangement instead of the
`filepath` param type, and declared UI params the DSP had no `get_param` readback for. Symptom:
loads fine, logs nothing, menu does nothing.

## 🧭 The six pages, and why each one is shaped that way (2026-09-09)

```
Category <items>   Init · Acoustic · Electronic · Hybrid · My Kits   <- first bank
  Kit    <preset>  a flat list of just that category
Pad      PAD   ENGN  STRT  END   TRSP  DETN  CHOKE VOL    <- level `pads` (STRT/END: samples)
Shape    ATK   DCY   HOLD  ENV   CUT   RES   TYPE  FILT   <- level `pad_shape` (samples only)
<engine> the synth voice's pages, gated on ui_engine      <- levels `eng_*` (generated)
Mix      VVOL  VOL   PAN   LINK  SNDA  SNDB  PUNCH PTIME  <- level `pad_mix`
Stereo   WMODE WIDE  WFREQ                              <- level `pad_stereo`
Master   MASTR
```

- ⭑ **The Stereo page: WMODE · WIDE · WFREQ · TIME · COMP · LATE** (Josh, 2026-09-22: *"a haas stereo spread to each
  drum's mix page ... and a knob to set a crossover below which the sound is not spread"*; then,
  having heard both, *"I like both, and can see the use in each depending on context"* — so both
  modes stay, on their own page, `pad_stereo`, after Mix; names his). Per pad, after everything
  the pad does, in the one `pad_render` both render paths share.
  - **WMODE Comb** (`wide_run`): a complementary-comb (Lauridsen) widener,
    `side = g·HP(mid)(t−8 ms)`, `L += side`, `R −= side`, `g = wide/100`. No lean, and the MONO
    SUM IS THE DRY PAD exactly. Linear: the side is 20·log10(|g|) dB under the mid.
  - **WMODE Haas** (`haas_run`): one side's high band delayed 15 ms × (wide/100)², each channel
    split LR4 so the lows stay in phase. It LEANS toward the leading side (precedence effect) —
    that is its character; Josh called it "a lot more character". Curved because width comes on
    under ~3 ms; capped at 15 ms because past that a drum flams.
  - **WMODE Disperse** (`disperse_run`, branch `wide-disperse`): **Polyverse Wider, MEASURED** —
    Josh rendered a click through Wider in Ableton (Width 0–200, a left-only input, Low Bypass
    200 Hz; 32-bit float) and the responses were fitted. Model, every part to <0.3% before
    interpolation: `L += F(L)`, `R −= F(R)` (PER CHANNEL), `F = g·AP5(delay(HP x))`,
    `g = min(W/100, 1)`, `delay = 0.0300 ms·W` (the second half of Wider's knob only lengthens
    the delay — video 1's "more all-pass stages above 100%" was wrong), AP5 = five FIXED
    first-order all-passes (corners 4.4, 41.5, 232, 1281 Hz and one near Nyquist), Low Bypass =
    LR4 (our WFREQ). WIDE spans Wider's 0–200% as `2·|WIDE|`. Measured against the renders:
    0.3–1.5% complex error 20 Hz–8 kHz, level within 0.1 dB to 11 kHz, ≤ +1.6 dB brighter at
    20 kHz (cubic interpolation; LINEAR was 1–3 dB darker than Wider up top). The comparison
    tool lives only in the session scratchpad; the renders are in `temp/wider-test/renders/` and
    are NOT in this repo (they are Wider's output). `test_wide.c` pins the laws (mid exact, −12/
    −6/0 dB, the delay, the 200 Hz group delay, per-channel). Cost +0.23–0.29 µs/pad (Mac).
    Named "Disperse", not "Wider": that is Polyverse's product name. With three options WMODE no
    longer flips on click (host: only 2-option enums flip).
  - **TIME** (0–12 ms, 0 = Auto): the widener's delay, independent of WIDE (Josh: *"the
    independent delay time knob"*). Auto keeps each mode's own (Comb 8 ms; Disperse Wider's law),
    so a fresh pad is unchanged. **Haas with TIME set** takes a Haas plugin's layout (Josh: *"let's
    do that for haas"*): TIME the delay, |WIDE| the delayed side's MIX (dry → fully delayed), the
    sign still the side; Auto keeps the curve. Not here: true time-intensity trading (a LOUDER late
    side). The cell reads "0.0 ms" for Auto — a float cell can only print its number.
  - **LATE** (Haas only, −12..+12 dB): the delayed side's level above the crossover — time-
    intensity trading (Josh: *"No delayed-side level ... let's try this"*): up counters the lean,
    down deepens it. Shown only in Haas: `visible_if wide_mode == "Haas"`. ⚠ That is the FOURTH
    distinct visible_if param (ui_engine, ui_family, filter_type, wide_mode) and the host
    evaluates at most four — `check_module_json` fails a fifth.
  - **COMP** (Off | On, flips on click): trims the pad by 1/√(1+g²) so stereo loudness holds as
    it widens (a mid/side widener adds up to +3 dB per ear), at the price of the MONO sum, which
    drops by as much. Off (default) keeps the mono sum exact, as Wider does. Haas is never trimmed
    (it adds no energy).
  - ⚠ **Engine renames leave stale copy keys** — FM's old `fm_*` keys sat in every copy list for
    a day (11 KB of the served hierarchy) because isGenKey stopped recognising the prefix.
    `gen_engine_ui` now refuses an engine-shaped key (`xx_`) that no engine owns.
  - **WIDE is −100..+100 in all modes; the SIGN MIRRORS.** Haas: + delays the right, − the left
    (how a kit's leans are balanced). Comb: − flips the side's sign (the comb teeth mirror).
  - WMODE is a two-option enum, so the grid FLIPS it on click (`flipsOnClick`, any 2-option enum)
    — the one-click A/B at the same WIDE. A mode change clears the shared buffer.
  - `wide_freq` 20..4000 Hz (default 150; 20 = full band). Cost on the Mac: comb +0.02 µs/pad full
    band, +0.16 with the crossover; Haas +0.05 / +0.18.
  - ⚠ **Wide 0 is a TRUE bypass**, proven by a hash probe over sample pads against the pre-Wide
    build. (Forcing the stage on at 0 in Comb is an EQUIVALENT mutation: +0·x changes nothing.)
  - ⚠ Comb at |100%| can peak a SIDE at twice the dry level (the mono sum cannot). Loud pads can
    clip the split path's int16 — which in a test reads as a "lost tail". Keep test tones quiet.
  - ⚠ **A pad renders while `pad_live`, not only while sounding** — the delayed signal outlives
    the source. BOTH render loops ask `pad_live`. It only shows on a SAMPLE pad (a sample stops
    dead; a synth has 100 ms of near-silence before its gate), so `test_wide.c` checks the tail
    with an abruptly-ending WAV on both paths.
  - It is "where the pad sits", like the sends: kept across sample <-> synth, in the state blob,
    never in the `.ablpreset`. `check_module_json` partitions FOUR banks: pads, Shape, Mix, Stereo.
    (Punch went to its own page to make room for Wide on Mix, and came back when Stereo took it.)
- ⭐ **Choosing a category LOADS what the kit list lands on** (Josh, 2026-09-22: *"to get a kit to
  load, have to first scroll to it"*). The host's preset page writes `kit_index` ONLY when the jog
  moves — an arrival writes nothing and a click inside it leaves without writing
  (`page_controller.mjs`) — so the landing kit was unreachable, and a one-kit category (Init)
  unloadable. A host rule, every module's; fixed module-side on the one write we get, `kit_cat`:
  the loaded kit's own category lands ON it and reloads nothing; any other lands on kit 0 and
  auditions it (deferred, like a detent). ⚠ Only once `dr32_kits_ready`: mid-scan, "not found" can
  mean "not scanned yet", and loading on it would replace the user's kit. That guard is reasoned,
  NOT tested (a partial scan's order is readdir's); `test_browser.c` §8 pins the rest.
- ⭑ **The engine/sample browser (`src/browser.js`) is the host's list**: five rows at y
  10/19/28/37/46, labels at x 9, the selection kept off the last row, `drawScrollbar`'s dotted track
  in column 126 only when the list scrolls (Josh: *"scroll bar and 5th line"*, like Modules / My
  Presets). Transcribed from `menu_layout.mjs` / `list_geometry.mjs`, not imported — a canvas
  cannot rely on the host's measurer. `check_browser_nav` §13 pins it.
- **DR32 opens EMPTY** (Josh, 2026-09-09). No default kit — it used to load the 707.
  `create_instance` is on the SPI callback and a kit load reads up to 32 WAVs there, so this is
  also the faster start. ⚠ The state BASELINE is still captured: an empty kit is a fine baseline,
  and skipping it would leave `state_baseline` NULL and degrade every save to a full dump.
- **The Kits browser is DR32's own CATALOGUE, not the file browser** (`dsp/dr32_kits.h`). It
  filters by CONTENT, because the host's browser filters by extension and the user's preset tree
  holds every instrument's presets — 365 files, 75 drum racks, measured on the device. ⚠ The scan
  is INCREMENTAL because `get_param` is on the SPI callback; do not make it a single pass, and do
  not scan at `create_instance` either.
- ⭑ **LINK is ONE-SHOT PER PARAMETER, and off by default.** Arm it, sweep one knob — every pad
  takes that value — and the moment a DIFFERENT field is written it releases, with that write
  landing on the focused pad alone. Reaching for another knob IS the "done" signal, so the
  releasing write is deliberately never itself linked. ⚠ `ui_*` writes do not count as a different
  parameter: focus following a hit mid-sweep must not disarm it. Not persisted — a mode that came
  back armed would flatten a kit on the next turn. The exclusion list in `link_fans_out`
  (`sample*`, `note`, `sending_note`, `browse`, `play`, `ui_*`) is the design, not caution: those
  are what make a pad a distinct pad.
- 🔴 **The preset page auditions with NO undo.** Writing `kit_index` loads, replacing all 32 pads,
  and the host offers no `live_preview`/`browser_hooks` there — the module cannot even tell
  "scrolled past" from "chose this", because the click only navigates away and Back writes
  nothing. Accepted for now; on the board.
- **Kits is FIRST because `root` has no knobs at all.** Root's own grid page is always emitted
  first and always named "Main"; a level with no knobs emits no page (`isMenuLevel` in
  `page_plan.mjs`), so stripping root leaves the first bank to be whatever it navigates to
  first. **Do not put a knob on `root`** — it would silently become page 1 and push Kits behind
  it.
- ⚠ **There is no "Follow Pads" toggle any more** (Josh, 2026-09-08): DR32 always follows. The
  `ui_auto_select_pad` FIELD and its `set_param` path are still there and must stay — the file
  browsers suspend follow through `browser_hooks` while open, and `tests/test_kit.c` drives both
  focus regimes. It initialises to 1 and is declared nowhere.
- ⚠ **The host's "Selected Pad" page is suppressed by the PAD knob at k1, and that is the only
  way to suppress it.** `childPickerNeeded` (`page_plan.mjs`) drops the generated picker exactly
  when the `child_index_param` is reachable — and only a `knobs[]` entry counts, because
  `allListedKeys` deliberately ignores `ui_`-prefixed keys listed in `params[]`. Take the k1
  knob away and the picker page comes back. **`node tools/pages_check.mjs` prints every page
  with its kind**, so this is visible rather than something to reason about.
- **One `sample` cell, not a Move/User pair.** Rooted at `/data` because the two sample
  libraries have no closer common ancestor and one cell means one root. It costs nothing in
  practice: `buildFilepathBrowserState` prefers the CURRENT VALUE's directory over `start_path`,
  so a pad that holds a sample opens where that sample lives, and only an empty pad ever sees
  `/data`. `start`/`end` point at it via `filepath_param: "sample"` — **davebox reads that
  declaration to find the wave editor's file**, so changing the key changes what davebox
  resolves.
- ⭑ **PAD NAMES FOLLOW THE SAMPLE ONLY BECAUSE DR32 SERVES `is_loading`.** Re-splicing
  `child_names` is half the job: the host does **not** re-read `ui_hierarchy` after a knob turn or
  a filepath commit — `armContractSettle` fires for a *selection* (an items row, a preset step) and
  nothing else. The one module-side lever is `is_loading`, which the shadow grid polls every 8
  frames and which re-plans the page on the **loading → ready EDGE**
  (`shadow_ui_param_pages.mjs`). So a sample swap arms a **348 ms pulse** (`DR32_NAMES_SETTLE_BLOCKS
  = 120`, re-armed on every step so a browse *sweep* costs one re-read, not one per detent) and the
  falling edge is what makes the header change.
  ⚠⚠ **`is_loading` must ALWAYS answer, and only ever `"1"` or `"0"`.** An unserved key reads `""`,
  and the host then sets `_loadingInterval = Infinity` and never asks again **for the life of the
  component**; the controller's own probe (`isLoadingSays`) latches `isLoadingSupported = false` on
  anything but those two strings. One wrong answer and this never works again, silently.
  ⚠ The window is a **lower bound on the host's poll**, not a load time — nothing is loading, the
  swap already happened synchronously in `set_param`. It has to span at least one poll
  (`LOADING_POLL_TICKS = 8` ≈ 133 ms at 60 Hz) or no edge is ever observed.
  ⭑ Safe to serve: the component entry gate reads `is_loading` **only when `ui_hierarchy` answers
  `""`**, which DR32 never does, so a "1" cannot hold the editor shut. `tests/test_state.c` walks
  both ends of the edge plus the re-arm and the idle case; four mutations fire.
- **`vel_vol` carries `viz: false`.** The fader detector claims params NAMED like a level and
  "Vel Vol" reads as one, but it is a modulation AMOUNT — a fader would be the same lie about it
  that a fader would be about Pan.
- ⚠ **A knob GAP cannot be declared.** `knobKeys` filters null entries out, so authored knobs are
  packed — an intended blank in a `knobs` array closes up rather than reserving a slot.

## 🥁 Synth engines: any pad can be a SIMIAN, URCHIN, 9W9, 6W6, 8W8, CW-78, ChowKick or FM voice (branch `multiengine`)

Josh's design: *"the UI, signal path, etc. is all DR32, but each pad can pick a sample engine
(what's there now) or a synthesis engine per pad."* Josh's rules:

- **Sample pads: no change at all.** Proven, not claimed: a hash probe over filters, chokes,
  envelopes, Punch and both render paths gives the SAME hash on `master` and on this branch, and a
  1 Hz cutoff change moves it. **Re-run it after touching the kit or voice path.** The probe is a
  throwaway, so rebuild it the same way: render a fixed scenario and FNV the floats.
- **Synth pads get no DR32 Shape stage, neither envelope nor filter** (Josh: "neither - but i want to
  keep the option open to having filter turned on for everything"). The engine renders MONO into
  `kit->eng_mono` and `synth_render` pans it. A post-engine filter is one call between the two, at
  the marked hook point.
- **The Sample knob became the ENGINE picker** (`src/browser.js`): Sample ▸ libraries, then one
  section per engine family listing its models.

**Shape of it:**
- `dsp/dr32_engine.h`: the C boundary. An ENGINE is a DSP class plus its param table
  (`dr32_engine_ops`). A MODEL is a named starting row (a Kick, a Closed Hat) from the upstream
  instrument's own init kit. A pad RUNS an engine; the picker offers MODELS.
- `dsp/engines/`: one C++ TU per engine family. The Faust sources are vendored UNMODIFIED from
  `schwung-simian` / `schwung-urchin`, and `generated/` is `scripts/gen_engines.sh` output. 🔴 **The
  class names are the point:** both ports generate `DSP_Drum` with one include guard and the same
  table names. Across TUs the linker merges the inline methods, so a SIMIAN voice can run URCHIN's
  `compute`. Every class gets an engine-unique `-cn`. ⚠ Each engine keeps ITS OWN `.lib` files and
  its own `--import-dir`; the two `shared.lib` files are different libraries with the same name.
- A pad is a sample pad OR a synth pad, never both. `padN_model = <slug>` unloads the sample.
  Loading a sample, a kit, or `clear` retires the engine (one-deep, like `retired`).
- `ui_engine` is the gate: the FOCUSED pad's engine, derived and read-only. It is in
  `chain_params` and on NO level (docs/MODULES.md, "The gate does not need a cell"). Every engine
  page is a pad level with `visible_if ui_engine == id`. Shape, Start/End and Punch are gated
  `== 0`. **Needs host #533** for the pages to follow a pad press. On an older host every engine's
  pages show at once, which is cluttered and never wrong.
- DR32 owns the mix. Each engine's own Gain, Pan and reverb send are pinned, and a model's gain and
  pan become the pad's Volume and Pan. Velocity is the ENGINE's (SIMIAN's Vel Gain feeds its
  saturation; URCHIN's velocity is strike energy), so a model starts the pad's `vel_vol` at 0.
- The trigger edge is forced PER VOICE (`faust_voice.h`): one frame at 0, then the velocity.
  Faust edge-detects `Trigger`, and a press and release inside one block would otherwise be no hit.
- ⚠ **all-off / panic MUTES a synth pad; it does not stop it.** Stopping froze a ringing URCHIN
  model mid-tail, and the frozen state resumed under the next hit (a measurably quieter, wrong
  second hit). Muted, it rings out unheard until the engine's silence gate ends it. A choke does
  the same, with DR32's own 3 ms ramp on top.
- A synth pad's `sample` READS as its model's name, because the ENGN cell draws the basename of
  that value. That name is never saved (`dr32_state.c` skips it) and a write of it is ignored, so
  it is never taken for a path.

**Rules that bite:**
- 🔴 **`tools/gen_engine_ui.mjs` OWNS** the engine pages, their root nav entries, every pad level's
  `child_copy_keys`, the `ui_engine` chain param and the picker's model list (between browser.js's
  GENERATED markers). It builds them from the engine tables through `tools/dump_engines.c`.
  **Change an engine param in its `.cpp` table and re-run it**; `--check` fails the suite until
  you do.
- **Copy/Clear act on the level you STAND on**, so every pad level carries the SAME
  `child_copy_keys`, with `model` right after `sample` (written in order; the model must exist
  before its knobs land). `check_module_json` pins it.
- ⭑ **The engine pages are NOT in module.json; they are `src/engine_ui.json`, and the DSP merges
  them into the hierarchy it serves** (`merge_engine_ui` in `dsp/dr32.c`: nav entries after
  Shape, levels at the end). The reason is `chain_params.c parse_chain_params`: it refuses a
  module.json over 64 KB, and it is where the per-pad send ranges come from. Over the line, the
  sends go silent. module.json is 18 KB now. The engine params are consequently absent from the
  host's C metadata; nothing reads them there. `build.sh` must ship the file
  (`check_build_script` pins it), and `check_module_json` checks the MERGED document.
- The SERVED hierarchy crosses the host's value channel (SHADOW_PARAM_VALUE_LEN: **128 KB** since
  host 1.3.0, upstream #444; 64 KB before), so it is minified, and an engine page's
  `child_copy_keys` carry only ITS engine's keys (it only shows on that engine's pads). Served
  size with two names spliced into 16 pad levels: 33 KB.
- ⭑ **URCHIN's Media stage runs PER PAD** (`dsp/engines/urchin/faust/media.dsp`, assembled
  VERBATIM from URCHIN's `output.dsp`): the Vinyl/Tape noise (which follows the drum's own level),
  Sat, Rate and Bits, on a "Media" page for all three URCHIN engines. Josh: *"that processing is
  pretty integral to the sound"*. Left out by decision: the reverb (the sends do that), Low
  Cut / High Cut (*"let's drop the low/high cut"*), the limiter, and the volume. A pad whose Media
  is all neutral skips the stage entirely, and that is a true bypass (tested bit-identical).
  Engine params may be ENUMS (`dr32_eparam.options`, "Vinyl|Tape"), read and written by name.
- **Both render paths** go through the one `pad_render` dispatch, so a synth pad cannot render on
  one entry point and not the other. `tests/test_synth_pads.c` drives both; removing synth pads
  from the split loop fails 4 checks.
- Engine keys are prefixed per engine (`sm_`, `ud_`, `us_`, `uc_`) because one hierarchy holds
  them all, and a repeated key makes the host's C loader drop ALL metadata (per-pad sends go
  silent). The URCHIN snare is its own engine with its own keys for the same reason: a condition
  cannot say "drum OR snare", and duplicating the drum pages would repeat their keys.
- LINK spreads an engine param only to pads running the same engine (the key simply is not the
  other pads'). `model` is never linked.

### 🥁 The kit ports: 9W9, 6W6, 8W8, CW-78 (2026-09-22)

Josh: *"let's start porting the other engines over"*. Four whole drum MACHINES by athousanddetails
(GPL-3.0), vendored UNMODIFIED under `dsp/engines/{9w9,6w6,8w8,cw78}/` (commits in NOTICES.md).
**One DR32 engine per LANE of a machine** (49 lanes, ids from each `DR32_ENG_*_BASE`), one model
per lane from the machine's own defaults. Each pad owns a WHOLE machine and renders only its lane,
through that lane's own drive/distortion stage and the machine's default master volume. The
machines' reverb, delay, master distortion, glue and CW-78's rhythm player are NOT run.

- ⭐ **"The machine's own voice" is PROVEN, not claimed.** `tests/test_kit_ports.c` renders every
  lane through the pad AND through the whole vendored machine (its own `*_render`, only that lane
  hit) and requires them **sample-identical** until DR32's gate stops the pad. It holds on clang and
  on bookworm GCC. Mutations it catches: dropping the master volume, checking liveness per block
  instead of per sample (CW-78 residue), no-op knob writes re-quantising 9W9's defaults.
- **Adapters `#include` the vendored engine source** (`9w9/er99_engine.c`, `*/..._engine.cpp`) to
  reach the per-lane render helpers the machines keep `static`. The C++ ones wrap it in a namespace
  (insurance: today the ports namespace their own classes and share no symbol; see the comment in
  `6w6_engine.cpp`). ⚠ **9W9 stays C** (`dsp/engines/*.c`, its own glob in build.sh/run.sh): C++'s
  float overloads would change its arithmetic.
- ⚠ **The machines never stop a lane** (their guard is the choke gain, 1.0 forever after a hit),
  so the pad gate is DR32's (`kit_port.h`, 100 ms below 0.001), AND a lane the machine says is
  finished (`active()` false, choke gain 0) ends the pad at once. Liveness is checked PER SAMPLE,
  as the machines do; a dead lane's sample is an exact 0 and its drive stage is not run.
- **One page per MACHINE, not per lane** — Josh's call (2026-09-22: "let's keep what you've got").
  A page per lane (~50 more levels) WOULD fit the 128 KB channel, at an estimated ~100 KB; one per
  machine keeps the room for more engines. ⚠ Two different limits: the module.json FILE is capped
  at 64 KB by the host's loader; the SERVED hierarchy at 128 KB. Lanes share keys (`n9_`, `s6_`, `e8_`, `c7_` + tune/decay/drive/dist/
  vel); the page is gated on the second derived gate **`ui_family`** (DR32_FAM_*, in chain_params,
  on no level, read-only like `ui_engine`), and a lane's own knob on `ui_engine` inside it. A
  condition names ONE value, so a key must be on every lane, one lane (`equals`) or all but one
  (`not_equals`); `gen_engine_ui.mjs` refuses anything else. That is why 9W9's toms each have
  their own `n9_<lane>_attack`. Gates in use: ui_engine, ui_family, filter_type (≤ 4, checked).
- **Knobs are the machines' 0–127 pots** and go through the machines' own setters. ⚠ 9W9 holds its
  factory values in ENGINEERING units, off the pot grid: a write that does not move the knob is
  skipped, or a model load would nudge every default.
- **Transpose moves the lane's Tune**: added for a semitone (LIN) pot, multiplied for a ratio/Hz
  (EXP) pot, never on 9W9's kick (Tune = sweep time). The pot is not moved. Pinned exactly:
  N knob-steps' worth of semitones must sound like Tune + N steps.
- **9W9's hats/ride/crash are WAVs** (`src/samples/9w9/`, ER-99's), decoded once at class init from
  `<module dir>/samples/9w9/` (the dir comes from `create_instance`, `dr32_engines_set_module_dir`).
  `build.sh` ships `samples/` and **`install.sh` copies directories (`scp -r`)**, both pinned by
  `check_build_script`. A files-only install ships everything but the cymbals.
- Costs: ~385 KB per pad (the machine struct, mostly its unused delay line; 9W9 zero-fills it, so
  its pages are resident). CPU per voice is below every URCHIN voice (bench, 2026-09-22).
- Velocity is the MACHINE's (per-pad `*_vel` = its master velocity depth; on 8W8/CW-78 it is a
  trigger voltage, i.e. timbre), so `vel_vol` starts at 0 as for the Faust engines.

### 🥁 ChowKick (2026-09-22)

Josh: *"port it over as a dr32 engine"*, then *"we definitely need it optimized as possible"*.
One engine (`DR32_ENG_CHOWKICK`, prefix `ck_`, pages Pulse · Body · Noise); its five factory
presets are the models. Local clone: `schwung-chowkick` (with submodules JUCE, chowdsp_utils,
chowdsp_wdf, tuning-library).

- **A SCALAR REWRITE, not vendored**: ChowKick is JUCE (too heavy for a module, and GPLv3-only),
  so `chowkick_engine.cpp` ports its `src/dsp/` line for line; chowdsp_wdf (BSD) is vendored
  unmodified and runs the diode circuit. JUCE behaviours are reimplemented, never copied.
- ⭐ **Proven against ChowKick itself**: `tools/chowkick_ref/` builds ChowKick's OWN DSP classes
  against the JUCE/chowdsp it pins (a console app, ~a minute) and `make_golden.sh` writes
  `tests/fixtures/chowkick/*.golden`. `test_chowkick.c` holds the port to them: attack within
  -40 dB, envelope within 0.5 dB every 100 ms for 2 s, pitch exact. NOT bit-identical by choice
  (xsimd 4-lane math vs std::; bit-exact would cost ~4x) — the error is phase drift in long tails.
  ⚠ **zsh does not word-split `$var`**: pass the preset arguments from bash or they arrive as ONE
  argument and only the first parameter is set (every preset rendered identically until caught).
- ⭑ **The optimisation**: the diode circuit is 60-70% of the cost and only shapes the pulse; after
  it, with zero input, it sits at a float FIXED POINT (not 0). Once a zero-input block repeats one
  value it is held until the next pulse — bit-identical (`test_chowkick.c` runs 20 s with and
  without, via `dr32_chowkick_no_hold`). 10-13 -> 3.4-5.2 us/block sounding.
- Link-to-note, MTS, polyphony and Level are dropped (DR32 transpose drives the filter through
  `freqMult`, as ChowKick's own hook allows). Wonky Synth is linked in the plugin; here it sits at
  its saved 80 Hz, and its golden is rendered unlinked to match. It rings ~30 s by design.
- Engine params may now be FLOATS (`step` < 1): `gen_engine_ui` emits `type: float` with a step.

### 🎛 FM (2026-09-22) — four engines on one core

Josh: *"from scratch"*, after looking at ctag-fh-kiel/md-drum-synth (an EFM-style FM drum test app,
AI-written, with **NO LICENCE**, so it can't be vendored or ported). `fm_engine.cpp` is DR32's own.
⚠ Keep it clean-room: take nothing from that repo.

⭐ **Why four engines, not one** (Josh's first listen, verbatim in `_worklogs/schwung-dr32.md`:
pitch/decays "way too compressed at the minimum side", "tweaks ... more suitable to the drum
type", hats "more bell-like than cymbal-like"). **The host knob is LINEAR: a detent is 0.5% of
the range** (`knob_engine.mjs`, no taper field). One engine's 20–2000 Hz / 5–4000 ms left a kick
~3 semitones a detent. The fix is RANGES, not a curve: one engine per drum type, each with its own
ranges and only its knobs, all on one core (Josh: *"keep all the engines fm-based"*).

| engine (id, prefix) | models | shape |
|---|---|---|
| FM Kick (55, `fk_`) | Kick, Tom | Pitch 20–300 Hz, Sweep Decay ≤ 200 ms, Noise Decay ≤ 200 ms |
| FM Snare (56, `fs_`) | Snare, Clap, Rim | Pitch 60–1000, **Bursts / Burst Gap** (was "Claps": read as a clap sound) |
| FM Metal (57, `fx_`) | Closed/Open Hat, Cymbal, Cowbell | THREE pairs at the 808's six cymbal ratios, Spread, no sweep |
| FM Perc (58, `fp_`) | Percussion, Zap, Drip, Glitch, Clank | wide ranges ON PURPOSE + **Mangle** (Noise FM, Ring, Crush, Bits) |

- Pages: Tone · FM (Metal: Metal) · Noise · (Mangle) · **Output** · Velocity. **Level is knob 1**
  of Tone and Noise (Josh). Output = **Tone<>Noise** (`TN-NS`, Josh; key `*_mix`; −100 tone only … 0 both … +100 noise only; Josh's
  "Mixer page"), Drive, Low Cut, **High Cut** (off at 20 kHz). ⚠ The page is NOT called "Mix":
  DR32's own per-pad Mix page is in the same nav list.
- **Sweep is bipolar** (−48..+48 st; negative rises into the note).
- ⭐ **Metal is not a bell because of CROSS-modulation**: each pair's carrier is bent by the one
  before, and feedback runs twice FM's depth. Measured as the share of 43 Hz bins above 4 kHz within
  20 dB of the peak: the first engine's hats **0.13–0.17**, three plain pairs 0.25–0.5, with
  cross-mod **0.62 / 0.87 / 0.92** (CH/OH/Cymbal). `test_fm` pins > 0.5.
- The table suffix after the prefix is the same knob on every engine; `test_fm.c` runs every check
  on every engine that has the knob. Pitch within 1%, decay −60 dB ± 2 dB, sweep start PREDICTED
  from the decay law (both signs), bursts, mix ends, high cut, velocity law, mangle, density, live
  knob, stop. Mutations: mix (both sides), high cut, cross-mod, bits, crush, noise FM, ring,
  bursts, a unipolar sweep all caught; halving Metal's feedback is NOT (it thins, it doesn't break).
- ⚠ Served hierarchy is now **~98 KB of the 128 KB** value channel (engine_ui.json 75 KB, the FM
  split added ~35 KB). The next engines need the room back: trim FM's pages' copy lists first.
- Cost: 0.5–3 µs/block sounding on the workstation; zeros without compute once both envelopes are
  under −120 dB.
- Old FM pads/sets (engine 55 with `fm_` keys) are not carried over (Josh: no backward compat).

- **Licence:** GPL-3.0-or-later since the engines (they are GPL; the combined `dsp.so` is too).
  `NOTICES.md` carries the MIT notice for the earlier code, including Charles's two PRs.

Tests: `tests/test_engine.c` (every model sounds, is a hit, is deterministic, starts in its own
block, retriggers, goes quiet; tune; keys: one knob wherever a key appears),
`tests/test_kit_ports.c` (the A/B against the machines, transpose = Tune, every knob reaches the
machine) and `tests/test_synth_pads.c` (ui_engine/ui_family, params, both render paths,
level/pan, choke across kinds, panic, back to sample, kit load, LINK, state round trip, 9W9's
WAVs, plugin names/split/state/module dir).

## 🎛 The UI is the host's param-pages grid — DR32 ships no UI of its own (since 0.2.0, 2026-09-05)

DR32 targets **upstream Schwung ≥ 1.2.0** (and needs **≥ 1.3.0** for its per-pad sends to reach
the host's return buses at all). Every page — the 32 pads, the kit browser — is planned by the
host from the hierarchy the DSP serves, using upstream's
built-in pictures (envelope, filter curve, fader, switch, sample waveform + wave editor) and the
1.2.0 drum-surface contract. The canvaskit Pad Editor (the OLD `canvas.js`) and the fork-only
host keys it needed (`host_canvas_ui`, `canvas_takes_click`) are **gone**; do not bring them back.
⭑ **`src/canvas.js` exists again, and it is something else**: the PAD cell's big number (Josh,
2026-09-22), a per-cell `custom:padnum` widget that draws **the host's OWN big number**. The
built-in (`WIDGET_BIGNUM`) cannot be asked for: the host picks it for a whole-number knob spanning
<= 24 or named like a count, and PAD is 1..32. So the widget imports the host's
`shared/param_pages/font_big_num.mjs` by a path RELATIVE to the installed module
(`../../../shared/...`, which resolves per host tree, dbx-host's under dAVEBOx) and places it as
`drawBigNumber` does. A failed import fails the whole script: the kind goes unregistered and the
host draws its arc knob.
🔴 **How it reaches the host is the trap.** The host loads `canvas.js` only if the `chain_params`
it reads FROM THE PLUGIN declares a `custom:` kind, and **its fallback for a plugin that serves
none carries no `viz`**, so the kind cannot live in module.json. And DR32's metadata comes from
the INLINE hierarchy params (the host ignores module.json's own `chain_params` list when any
exist), so moving `ui_current_pad` out of its level would have left the PAD knob undeclared.
So `dsp.so` serves `src/chain_params.json` (written by `gen_engine_ui.mjs`), which is **the host's
own fallback plus that one viz**. It must be nothing more: the modulation refresh (`chain_mod.c`)
re-parses whatever the plugin serves and REPLACES the slot's metadata with it, and that metadata
holds the per-pad send ranges (the 09-19 silent-sends failure). `tools/check_chain_params.mjs`
proves it with the host's own `chain_params.c`, compiled from `SCHWUNG_SRC`: the re-parse matches
the host's parse of module.json struct for struct, and the page sees the fallback's fields plus
the viz (checked against v1.4.0, #533 and dbxhost; five mutations caught).
`tools/check_pad_cell.mjs` loads canvas.js from a temp copy of the DEVICE layout (so the relative
import is tested) and requires pads 1..32 and the unread "--" to be pixel-identical to the host's
`drawBigNumber` (sheet at `build/pad_cell.png`). `build.sh` ships both files;
`check_build_script` fails if it doesn't, or if `install.sh` deletes a shipped file (it used to
`rm` canvas.js after copying, a Pad Editor leftover).

Key facts for this module specifically:

- **The served hierarchy is `module.json`'s, plus pad names.** `dsp/dr32.c` reads its own
  `module.json` at instance creation and serves it from `get_param("ui_hierarchy")` (a sound
  generator's `module.json` hierarchy is never read by the host — MODULES.md says so). After
  every kit load and per-pad sample swap it splices a `child_names` array (sample basenames)
  into the `pads` level, **immediately before `"child_index_param"`**. That key is the splice
  anchor; `tools/check_module_json.mjs` pins it. Lose it and the DSP silently serves the plain
  document: pages still plan, focus stops following, every voice reads "Pad N".
- **The 32 pads are three child levels sharing one focus** (each `child_prefix: "pad"`,
  `child_count: 32` — see the page map above), which is why
  `dsp/dr32_params.c` speaks `pad<N>_<key>` (0-based) and the `pad_<key>` alias for the focused
  pad. `child_index_param: "ui_current_pad"` makes the DSP the owner of focus in both
  directions (the host reads it to follow, the Pad knob writes it). `child_note_base: 36`.
- **`child_key_overrides` carries the keys that are NOT per-pad.** `ui_current_pad` (all three
  banks) and `link` (Mix) sit on child levels so they can have cells there, and a template with no
  `{index}` resolves to the literal key (`child_key.mjs`, `resolveChildKey`) — without the
  override they would become `pad3_link`, which addresses nothing. `master` needs no override: it
  is on its own `output` level, which has no children.
- **Every key the UI displays must be readable back via `dr32_read_param`.** `end` is a UI
  alias of `length` (`start + length`, written back as a length) so the host's trim editor can
  draw start..end while the `.ablpreset` keeps Move's own `Voice_PlaybackLength`.
- **A viz group must sit contiguously on ONE row of four.** Knob order within a bank is
  therefore load-bearing: attack/decay/hold and cutoff/resonance/filter_type are the two rows of
  Shape for exactly that reason, and start/end are k3/k4 of Sample. Verify with
  `node tools/pages_check.mjs` (below) — it runs upstream's validator and prints every page.

### How pad focus follows a hit — two regimes, and why

Upstream's rule is **the module owns focus; nothing is inferred from what is played**, because a
running pattern is all note-ons and a live hit is indistinguishable from a sequenced one by the
time it reaches `on_midi` (measured on device: identical status/channel/note/source).

- **Transport stopped** (`dr32_kit.transport_running == 0`, mirrored from the host's
  `get_beat_position()` / `get_clock_status()` every render block in `dr32.c`): a note-on IS a
  hand, so `dr32_kit_note_on` moves `ui_current_pad` outright. Works on any host.
- **Transport running**: only a host vouch moves it — `ui_live_press` ("a finger did that",
  correlated with the last/next note inside `DR32_LIVE_MATCH_BLOCKS` = 20 × 2.902 ms = **58.0 ms**)
  or `ui_live_note` (a host that emits the note names it outright, no race). Stock upstream 1.2.0
  has neither; **upstream PR #426 makes `child_press_param` the host's contract** (open 2026-09-06).
- A host that has vouched even once OWNS liveness from then on (`dr32_kit.host_vouches`): bare
  notes never move focus again, whatever the transport says — davebox's sequencer dragged focus
  because davebox reports no transport to the plugin.
- `ui_auto_select_pad` gates both. It is **always on** — there is no toggle since 2026-09-08 —
  and the file browsers suspend it via `browser_hooks` while open.

Pinned by `tests/test_kit.c` (both regimes, vouch consumption across a transport start, the
upper bank). **Before changing any of this, say so**: dAVEBOx sound mode depends on it.

## 🚌 Module buses — DR32 declares its 32 pads, and MUST keep working on BOTH hosts

Since 2026-09-08 (branch `dr32-module-buses`) DR32 publishes its voices so a host can group a
subset onto a **bus** with its own inserts and sends:

- `get_param("split_voices")` → `[{"id":"pad1","label":"Kick"}, …]`, **all 32 pads, empty ones
  included**
- exported symbol **`move_plugin_render_split`** → `dr32_kit_render_split`

⭐ **THE BOTH-HOSTS RULE (Josh): DR32 has to run on stock Schwung AND under dAVEBOx.** It does, and
not because it was tested on both — because **both halves are opt-in from the HOST's side**. A host
asks for `split_voices` only if it knows the key, and `dlsym`s `move_plugin_render_split` only if it
knows the symbol. Stock **< 1.3.0** does neither, so DR32 takes its ordinary `render_block` path,
unchanged. **Do not add anything that makes bus support mandatory** — no capability probe, no
required key, no behaviour that only makes sense with a bus-aware host.

⚠⚠ **THE TWO RENDER PATHS MUST STAY STATE-COMPATIBLE.** The host switches between `render_block`
and `render_split` **at runtime, per frame**, by whether any voice is currently on a bus —
assigning one pad flips the entry point mid-stream with no reload. So both must advance the SAME
voices, envelopes and block counter. `dr32_kit_render_split` mirrors `dr32_kit_render`'s loop for
exactly this reason; **if you change one, change the other in the same commit.**
`tests/test_split.c` pins it: with nothing routed out, the split path must match `render_block`.
If they diverge, every kit changes the moment a user touches a bus, and it presents as *"the drums
got quieter"* — nowhere near this code.

🔴 **AND IT IS NOT ONLY DSP STATE — ANYTHING EITHER PATH DOES ON A CLOCK GOES IN BOTH.**
`move_plugin_render_split` synced the transport and did **not** service the deferred kit load, which
is only serviced from a render callback. Assign one pad to a bus and the host switches entry point
mid-stream, so that became the callback that runs: the kit browser moved its cursor, changed the
name on screen, and **loaded nothing, forever, with nothing logged**. Reported from the device
(2026-09-09) as *"I can load 1 kit after launch, but can't change it after that"* — the one that
still worked was the state restore, which writes `kit` directly and never defers.
⚠ The comment above that call read *"the same per-block housekeeping render_block does, and it may
not be skipped on this path"* while skipping half of it. [[explaining-is-not-checking]].
⚠ `tests/test_browser.c` now drives the browser down BOTH paths; checks 1–5 passed throughout the
bug because they drive `render_block`, so the split check is a separate assertion rather than one
more block loop.

**Other rules the split render must keep:**
- **ACCUMULATE, never clear.** The host clears the destinations first, and **they alias** — two
  pads on one bus are handed the SAME pointer, and their sum is supposed to happen inside our
  render. A `memset` there erases another pad's audio.
- **All 32 pads are listed, empties included.** The index IS the `voice_out[]` index, so dropping
  empties shifts every pad behind them onto the wrong buffer.
- 🔴 **Ids are `pad1..pad32` — 1-BASED, and they must MATCH `split_pad_key`.** The host substitutes
  an id into `{id}_send_a` **verbatim**, so the two have to agree or every send level lands on the
  pad next door and the end one addresses nothing — **silently**, because nothing in the host errors
  on a key that does not resolve. It is simply a send that never moves.
  ⚠⚠ **The base has flipped twice, so verify it against the code rather than against prose.** Ids
  were 1-based, went 0-based on 2026-09-08 to match a 0-based param surface, and went back to
  1-based on 09-09 when the whole surface became 1-based (the Pad knob reads 1–32 and
  `split_pad_key` now does `idx -= 1`). **This paragraph itself claimed 0-BASED until 09-09, three
  releases after it stopped being true** — a reader "fixing" the code to match would have broken
  every send with nothing to show for it.
  ⭑ The check that settles it is not prose: `tests/test_state.c` SUBSTITUTES each published id into
  the template and drives the result through `set_param`/`get_param` with a distinct value per pad
  and per send, so a key resolving to the wrong pad reads back somebody else's number. It also pins
  the first id as `pad1` and the 32nd as `pad32`. Ids stay stable across content changes; the LABELS
  follow the loaded sample, which is what lets a saved bus assignment survive a kit change.
- **A short `n_voices`, or a NULL entry, falls back to `main_out`** — a host that asks about fewer
  voices than we have must still hear the whole kit.
- **Never a `static` scratch buffer.** DR32 is MULTI-INSTANCE; two slots would share it on the
  audio thread. Per-kit fields (`scratch`, `split_dry`).

### The third half: DR32 owns each voice's send LEVEL, and nothing else about the send

`get_param("voice_send_params")` → `["{id}_send_a","{id}_send_b"]`. **Array position is the send
index** — `[0]` is Send A, `[1]` is Send B — and more than two is refused outright by the host
rather than truncated. The host reads these levels; it does not own them, does not draw a fader
for them and does not save them. They are ours, on our own pad pages and in our own `state` blob,
and they arrive from a Move kit's per-pad send amounts, which is what they have always meant on
the hardware.

⚠ The `pads` level MUST keep declaring `send_a`/`send_b` with `min`, `max` and `unit: "dB"`. That
is where the host reads the scale from, and **if it cannot find it, it refuses the send rather
than guessing** — nothing heard, nothing mis-scaled, no error.

⚠ These keys are read **on the audio callback**, a few per frame. Keep `get_param` for them a
constant.

⭑ **DR32's own send/return framework is GONE** (2026-09-08), and so are the two stages that
preceded it out the door: the kit inserts (2026-07-27) and the always-on Drum Bus (`7da1f5f`).
All three went for one reason — *a second, worse copy of a facility the host provides, worse
because it was reachable only from inside DR32.* The Drum Bus case is the sharpest: it was glue
over the kit's SUMMED mix, so a pad routed to a host bus left before it and the stage applied to
some pads and not others depending on routing. **Do not reintroduce a whole-instrument stage**
without deciding what it means for a routed-out voice, and **do not reintroduce internal
returns.** The effects themselves were not deleted — they were lifted whole into their own repo
to become a standalone reverb module; tag **`fxbus-final`** is the pointer.

⭑ Consequence, and it is deliberate: **on a host below 1.3.0 the two per-pad send knobs do
nothing.** They turn, save and restore; nothing reads them and there is no internal return left
to feed. Josh's ruling, 2026-09-08.

Cross-host contract, and the host side of all this: `../dbxhost/docs/MODULE_BUSES.md`.

## ⚠⚠ dAVEBOx sound mode reads two keys from the `pads` level — keep them

```json
"child_press_param":      "ui_live_press",    /* write "1": a finger did that (also upstream #426) */
"child_press_note_param": "ui_live_note"      /* write the note: a host that EMITS it names the pad outright */
```

davebox (`dbxhost/davebox/ui/ui_discover.mjs`) hosts DR32 in a chain slot. Since 2026-09-06 it
reads focus through upstream's `child_index_param` (so `child_select_param` is gone from here),
and `child_press_param` is the same key upstream #426 adopted. `child_press_note_param` is
davebox-only and deliberately kept: a sequencer that emits the note can name the pad exactly,
where the vouch can only race a 58 ms window. Upstream ignores unknown keys.
**Do not remove them.** `check_module_json.mjs` pins both.

## ⚠ The engine is a reconstruction, not a design

The DSP laws come from `../move original reconstruct/analysis/native-instruments/`
(`DRUM_RACK_ARCHITECTURE.md`, `DRUM_SAMPLER_TRACE.md`, `DRUM_FILTER_RECON.md`,
`DRUM_EFFECTS_RECON.md`) and from measurement against the stock engine. **Any deviation is a
fidelity bug, even when it sounds better.** Two examples already corrected: the native reader
uses LINEAR interpolation (Catmull-Rom is "nicer" and wrong), and velocity→volume is a dB law
centred on velocity 70, not a linear blend.

## Testing

🔴 **LOCAL GREEN IS NOT GREEN — THE SUITE COMPILES ON glibc, AND macOS HIDES THAT FROM YOU.**
Every `tests/test_*.c` opens with `#define _XOPEN_SOURCE 700` because the suite builds `-std=c11`
and glibc then declares nothing outside it: `mkdtemp`, `setenv`, `utimensat` and `struct timespec`
vanish, and so does `M_PI`. macOS headers expose all of them regardless, so the whole suite passed
locally and **failed the release build with `-Werror` errors across three files** — none of which
had ever been compiled on glibc (2026-09-09).
⚠⚠ **`_GNU_SOURCE`, and it took three wrong macros to get there — each fix causing the next
failure.** `_POSIX_C_SOURCE 200809L` restored `mkdtemp` and took `M_PI` away (XSI, not POSIX).
`_XOPEN_SOURCE 700` restored both on glibc and then hid `mkdtemp` **on macOS** — because Darwin
exposes everything by DEFAULT and only begins restricting once you name a standard, so naming one
asks for *less* there. `_GNU_SOURCE` is the only one that is purely additive on glibc and simply
not consulted on Darwin. Keep it uniform across the suite; a new test file must not have to pick,
and picking wrong is invisible on whichever platform you happen to use.
⭑ **Verify a C change in the container, not just locally:**
```sh
docker run --rm -v "$PWD":/work -w /work debian:bookworm bash -c \
  'apt-get -qq update >/dev/null && apt-get -qq install -y gcc g++ nodejs >/dev/null; tests/run.sh'
```
⚠ **The DISTRO also chooses whether code compiles**, not just the libc: Ubuntu enables
`_FORTIFY_SOURCE` at `-O2` and Debian does not, which makes `system()` `warn_unused_result` on one
and not the other. That killed a release build no local run could reproduce. **The CI test step now
runs inside `debian:bookworm` too**, so the command above IS the command the workflow runs — and a
change touching platform surface is worth a pass on `ubuntu:24.04` as well.
(`check_help` SKIPS in a container — no host checkout — and a skip counts as a pass, so run the
suite locally before tagging.) [[local-green-on-a-different-libc-is-not-green]]

⚠ **The order matters and it is not optional.** `tests/run.sh` WIPES `dist/tests` (its link
lines glob `dr32_*.o`, so a stale object from a deleted source would still be linked), and it is
`pages_check` — not the test run — that writes the preview fixture.

⭑ **`DR32_KIT_ROOTS="<core>:<user>"` makes the kit browser reachable off-device** — the two real
roots are absolute `/data` paths, and without the override the only coverage was the catalogue's
boundary conditions. `tests/test_browser.c` uses it to drive the whole path the host drives (items
list → `kit_cat` → `kit_count` → `kit_index` → settle → loaded), on **both** render entry points.

```sh
export SCHWUNG_SRC=../schwung-current/.worktrees/v1.3.3   # the host DR32 actually targets
tests/run.sh                     # 1. off-device suite; writes dist/tests/served_hierarchy.json
node tools/pages_check.mjs       # 2. upstream's validator over the SERVED hierarchy; writes the fixture
node "$SCHWUNG_SRC/tools/param-pages/preview.mjs" dr32 --all --layout movy \
     --fixture dist/tests/dr32-fixture.json --png dist/tests/pages   # 3. render every page
```

⚠ **`../schwung-current` itself is STALE at v1.2.0-16** — it is the upstream-PR home and its
checkout belongs to whatever branch is being prepared there. DR32 targets 1.3.x, so point
`SCHWUNG_SRC` at a worktree of the tag instead of switching that checkout:
`git -C ../schwung-current worktree add --detach .worktrees/v1.3.3 v1.3.3`.

`tests/test_state.c` writes the hierarchy the plugin actually serves to
`dist/tests/served_hierarchy.json`; `pages_check` reads THAT, not `module.json`, and writes a
one-module fixture for upstream's preview tools. Look at the PNGs before a deploy — a viz group
that broke the row rule draws as plain dials with no error anywhere.

**The acceptance test for the engine is a null test, not an ear test** — see
`docs/NULL_TESTING.md`. (`tools/fx_suite.sh`, which reported null depth per SEND effect, went
with the send effects themselves; the drum-engine half of the rig stays.)

**Playback effects are DROPPED** (Josh, 2026-07-26) — every pad plays the plain sampler.
`Effect_Type` and all nine effects' params are still parsed and preserved on save, so kits stay
lossless and still open on native Move; only playback ignores them.

If one is ever brought back, the bar in `dr32_fx_modelled()` stands: enable it only once it
**measurably beats the dry fallback**, measured, with the number recorded. Implementing from prose
without a numeric target made 8-bit, Punch and FM *worse* than not implementing them. Pitch Env
(-39.3 dB) and Loop (-35.8 dB) were working when switched off. (The harness that produced those
numbers was `tools/fx_suite.sh`; it left with the send effects, so reviving an effect means
reviving a way to score it first.)

## Device

```sh
./scripts/build.sh && ./scripts/install.sh    # install ALWAYS restarts the stack
```

**⚠ A restart is required after every deploy — swapping the synth out and back in
is NOT enough.** Without a restart the old `dsp.so`/`module.json` stay live and the
deploy silently appears to have done nothing (this cost a debugging cycle where a
"deployed" fix wasn't running at all). `install.sh` therefore always runs the
canonical `scripts/restart_move.sh`; `SKIP_RESTART=1` opts out if you want to
batch several deploys.

**⚠ If `build.sh` fails, `dist/` keeps the PREVIOUS build and `install.sh` will
happily ship it.** Always check that build.sh printed `==> done:` before trusting
an install.

**⚠ apt "invalid signature" during a docker build = the Docker VM's disk is FULL**, not an
architecture or GPG problem (I misdiagnosed it as arm64/emulation for several cycles). Check
and reclaim:

```sh
docker run --rm ubuntu:22.04 df -h /     # 0 available = this is your bug
docker builder prune -af                 # reclaims build cache, images untouched
```

⚠⚠ **The "image vanishing" symptom is NOT disk pressure.** `docker image inspect` false-negatives
on an image that is present, listed and runnable — observed **5/5** on `schwung-builder` with
20 GB free, while `docker run schwung-builder aarch64-linux-gnu-gcc --version` worked. This file
used to blame a full VM, and that misattribution is why a silent compiler switch was tolerated as
a known flake. **`build.sh` no longer uses `inspect` at all**: running the image is both the
presence test and the capability test. It still fails loudly on a genuinely full VM.

### 🔴 THE COMPILER IS PINNED, AND THE PIN IS CHECKED IN THE ARTIFACT (2026-09-09)

⚠⚠ **Selecting a toolchain by "whichever image is present" chooses which COMPILER builds the
module, and the images on this machine DISAGREE**: `schwung-builder` and `davebox-builder` are
gcc **12.2.0**, `move-anything-builder` is gcc **11.4.0**. Identical source, different binary —
proven back to back: `11.4 → a49f4fe9`, `12.2 → c191a9c3`.

**And the fallthrough was silent.** The full-VM symptom above made the preferred image look
absent, so the old probe loop moved to the next candidate. The only trace was one line of build
output nobody reads. **Three commits carried a gcc 11.4 artifact that had been reported as the
verified 12.2 build**, and the hash in git stopped describing what was on the device.

- `DR32_BUILDER` names the image (default `schwung-builder`). That is a **preference**, not the
  guarantee — an image can move under a floating base tag without changing its name.
- **The guarantee is read out of the ARTIFACT.** gcc writes its version into the `.so`'s
  `.comment` section, so `dsp.so` has always been self-identifying; it was simply never checked.
  `build.sh` greps it and **fails** unless it matches `DR32_GCC` (default `12.2.0`).
- **A failed assert deletes `dist/<id>/`** — the DIRECTORY `install.sh` actually ships. Deleting
  only the tarball was the first version of that guard and it guarded nothing.
- Pinned by `tools/check_build_script.mjs` (in `tests/run.sh`), six mutations, all firing.

⭑ **Verify a suspect artifact anywhere, including on the device:**
```sh
strings dsp.so | grep '^GCC:'      # must say 12.2.0
```

⚠ Do not run `EnginePerfTool` captures against a live Move stack — that is the suspected cause
of two full device lockups needing a power cycle.

✅ **`build/` is UNTRACKED** (Josh, 2026-09-09 — it was his call and he gave it). Build output is
not source: while it was tracked, every local build dirtied the tree, `git add -A` was a live
hazard, and the **release workflow failed on it** — the DSP is cross-compiled in Docker as root, so
`build/obj` came back root-owned and `git checkout` could not unlink it, *after* the release had
already been published.
⚠ The 196 files removed from the index are still **on disk**; 186 of them were orphaned reverb-era
captures and renders (`build/cap`, `ir`, `fix`, `verbpresets`, `fx`) whose tooling left with
DrumVerb, referenced by nothing in the tree. If any device capture there is worth keeping, it needs
a deliberate home — `build/` is now swept.
