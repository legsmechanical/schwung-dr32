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
Category <items>   Acoustic · Electronic · Hybrid · My Kits   <- first bank
  Kit    <preset>  a flat list of just that category
Sample   PAD   SMPL  STRT  END   TRSP  DETN  CHOKE BRWS   <- level `pads`
Shape    ATK   DCY   HOLD  ENV   CUT   RES   TYPE  FILT   <- level `pad_shape`
Mix      VVOL  VOL   PAN   LINK  SNDA  SNDB  PUNCH PTIME  <- level `pad_mix`
Master   MASTR
```

- ⭑⭑ **THE PAD KNOBS ARE THREE SIBLING CHILD LEVELS, ONE PER BANK** (Josh, 2026-09-09: *"put each
  pad page on its own bank so the list is easy to navigate"*). They were one 24-knob level that the
  planner chunked into "Pads / Pads 2 / Pads 3" — pages with no names, reachable only by jogging
  past each other. Three levels give three named banks in the nav list at no cost, because
  everything that makes them ONE rack is declared rather than inferred:
  - **All three name the same `child_index_param`** (`ui_current_pad`), so they are three views of
    one focus. `childPickerNeeded` (`page_plan.mjs`) dedupes on exactly that — the FIRST level to
    plan a picker keeps it, the rest defer — and `allListedKeys` is GLOBAL, so the PAD knob on the
    Sample bank suppresses the picker for all three. **Do not take the PAD knob off the Sample
    bank**: three picker pages come back.
  - 🔴 **`child_note_base` is declared ONCE, on `pads`, and nowhere else.** `voicesOf`
    (`voices.mjs`) emits a voice per child of EVERY level that declares a note map, so all three
    declaring it published **96 voices at notes 36..131** and the drum surface seated three copies
    of the kit. Caught by `pages_check`, which walks the voice list; the pages themselves looked
    perfect. Shape and Mix are further EDITING VIEWS of the same 32 pads, not 32 more pads.
  - ⚠ **`child_press_param` / `child_press_note_param` stay on `pads` alone.** dAVEBOx takes *"the
    FIRST child level declaring `child_press_param`"* (`davebox/ui/ui_discover.mjs`), and the host's
    rule is the same — a second declaration is a second answer to a question with one answer.
  - The three banks **partition** the pad params: every key on exactly one, none dropped.
    `tools/check_module_json.mjs` pins the partition, the single note map, the single press
    declaration, the shared index base and the 8-knob ceiling per bank; six mutations fire.
  - ⚠ **`pads` keeps its name.** It is the level `check_module_json.mjs` addresses by name and the
    first davebox walks; renaming it to `pad_sample` reads better and breaks both.
- ⚠ **`child_names` is spliced before EVERY `"child_index_param"`, not the first.** Each bank names
  the pads it draws, so a splice that stopped at the first anchor left Shape and Mix reading
  "Pad 7" while Sample said "Kick 707" — not a page that looks broken, one that looks like a
  different pad. `dr32_refresh_hierarchy` loops, and ⚠ it must **step past** the anchor each time:
  resuming the search AT it finds the same one again and re-splices into its own output until the
  buffer fills, which fails soft as the plain document, so the only symptom is names that never
  appear at all.
- **The OLED header reads `module.json`'s `name`, so `name` is `"DrumRack32"`** (Josh, 2026-09-09:
  *"replace the drum rack 32 on the header with dr32 to save space"*, then *"make DrumRack32 the
  title"*). ⚠ **The header and the module PICKER read the SAME field** — `scanModulesForType`
  pushes `name: json.name || entry` — so there is no way to be long in the list and short in the
  band; `"DrumRack32"` is the compromise that fits both. `headerTitle`
  (`shadow_ui_param_pages.mjs`) resolves `getModuleDisplayName` → `moduleNameCache[id]` → the
  `name` field; **`abbrev` is only the fallback until module.json has been read**, so setting
  `abbrev: "DR32"` alone changed nothing. The long form lives in `description`, `docs/manual.html`
  and `src/help.json`.

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

## 🎛 The UI is the host's param-pages grid — DR32 ships no UI of its own (since 0.2.0, 2026-09-05)

DR32 targets **upstream Schwung ≥ 1.2.0** (and needs **≥ 1.3.0** for its per-pad sends to reach
the host's return buses at all). Every page — the 32 pads, the kit browser — is planned by the
host from the hierarchy the DSP serves, using upstream's
built-in pictures (envelope, filter curve, fader, switch, sample waveform + wave editor) and the
1.2.0 drum-surface contract. The canvaskit Pad Editor (`canvas.js`) and the fork-only host keys
it needed (`host_canvas_ui`, `canvas_takes_click`) are **gone**; do not bring them back.

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
  'apt-get -qq update >/dev/null && apt-get -qq install -y gcc nodejs >/dev/null; tests/run.sh'
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
