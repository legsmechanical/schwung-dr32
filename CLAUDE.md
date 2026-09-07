# DR32 — Move Drum Rack clone, 32 pads

## 📗 Read `../schwung-current/docs/MODULES.md` before touching module.json

It is the authoritative contract for module composition — param schema, UI hierarchy, chain
params, the drum-surface declarations, plugin API. **Read the CURRENT one in `schwung-current/`,
not `schwung/`** (that copy is months stale and predates param pages). **Do not infer the schema
from other modules' `module.json`.** This module shipped broken once by doing exactly that: it
used `label` where the schema wants `name`, invented a kit-browser arrangement instead of the
`filepath` param type, and declared UI params the DSP had no `get_param` readback for. Symptom:
loads fine, logs nothing, menu does nothing.

## 🎛 The UI is the host's param-pages grid — DR32 ships no UI of its own (since 0.2.0, 2026-09-05)

DR32 targets **upstream Schwung ≥ 1.2.0**. Every page — the 32 pads, both sends, the drum bus,
the kit browser — is planned by the host from the hierarchy the DSP serves, using upstream's
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
- **The 32 pads are one child level** (`child_prefix: "pad"`, `child_count: 32`), which is why
  `dsp/dr32_params.c` speaks `pad<N>_<key>` (0-based) and the `pad_<key>` alias for the focused
  pad. `child_index_param: "ui_current_pad"` makes the DSP the owner of focus in both
  directions (the host reads it to follow, the pad picker writes it). `child_note_base: 36`.
- **Every key the UI displays must be readable back via `dr32_read_param`.** `end` is a UI
  alias of `length` (`start + length`, written back as a length) so the host's trim editor can
  draw start..end while the `.ablpreset` keeps Move's own `Voice_PlaybackLength`.
- **A viz group must sit contiguously on ONE row of four.** Knob order in the `pads` level is
  therefore load-bearing: attack/decay, cutoff/resonance/filter_type. Verify with
  `node tools/pages_check.mjs` (below) — it runs upstream's validator and prints every page.
- **The send pages moved to `dr32-fx`** and so did the check. They hang off DSP-derived
  read-only params (`mode`, `env`, `sync`, `tank`) because `visible_if` takes one condition
  on one param; they are served by the DSP and declared nowhere, deliberately. Every preset
  must fit ONE page of eight — `pages_check` enforces it, against `fx/module.json`.
  Repointing it there caught two gates the same day: `size`/`damp`/`predelay` were gated on
  `mode != "Delay"`, correct while every preset was a send and a reverb's knobs on a
  saturator once Crunch and Comp joined the list (hence `tank`); and `diffusion` named a
  param the module does not serve, so the read answered null and **`visible_if` failed
  OPEN** on all seven presets.

### How pad focus follows a hit — two regimes, and why

Upstream's rule is **the module owns focus; nothing is inferred from what is played**, because a
running pattern is all note-ons and a live hit is indistinguishable from a sequenced one by the
time it reaches `on_midi` (measured on device: identical status/channel/note/source).

- **Transport stopped** (`dr32_kit.transport_running == 0`, mirrored from the host's
  `get_beat_position()` / `get_clock_status()` every render block in `dr32.c`): a note-on IS a
  hand, so `dr32_kit_note_on` moves `ui_current_pad` outright. Works on any host.
- **Transport running**: only a host vouch moves it — `ui_live_press` ("a finger did that",
  correlated with the last/next note inside `DR32_LIVE_MATCH_BLOCKS` = 20 × 2.902 ms = **58.0 ms**)
  or `ui_live_note` (a host that emits the note names it outright, no race). Stock upstream 1.2
  has neither; **a PR making `child_press_param` an upstream contract is the planned fix**
  (`_worklogs/NEXT-PROMPT-dr32.md`).
- `ui_auto_select_pad` ("Follow Pads") gates both. The kit browser suspends it via
  `browser_hooks` while open.

Pinned by `tests/test_kit.c` (both regimes, vouch consumption across a transport start, the
upper bank). **Before changing any of this, say so**: dAVEBOx sound mode depends on it.

## ⚠⚠ dAVEBOx sound mode reads three keys from the `pads` level — keep them

```json
"child_select_param":     "ui_current_pad",   /* read: which pad is focused */
"child_press_param":      "ui_live_press",    /* write "1": a finger did that */
"child_press_note_param": "ui_live_note"      /* write the note: a host that knows it */
```

davebox (`dbxhost/davebox/ui/ui_discover.mjs`) hosts DR32 in a chain slot and writes the vouch
itself from its own pad handler. Upstream ignores unknown keys, so they cost nothing there.
**They are not cruft — do not remove them** until davebox has moved to `child_index_param`
(dbxhost's 1.2.0 survey lists that as "pull, assess"). `check_module_json.mjs` pins them.

## 🚌 Schwung buses: the 32 pads are 32 voices (`split_voices` + `render_split`)

DR32 is the first module on upstream's per-voice render contract, so a slot can put the
kick on one insert chain and the snare on another. Two opt-ins, both additive:

- **`get_param("split_voices")`** — a flat ordered array, one entry per pad, built by
  `dr32_split_voices_json` (`dsp/dr32_kit.c`). **Entry *i* is buffer *i*** in the render
  below, so the order is the pad order and never changes.
- **`move_plugin_render_split`** (`dsp/dr32.c`) — dlsym'd off `dsp.so`, never a field on
  `plugin_api_v2_t`. A host that does not know about it calls `render_block` and gets
  exactly what it always got.

**The ids are POSITIONAL (`pad1`..`pad32`) and that is the whole design.** A bus stores
voice *ids*; loading a kit changes all 32 samples and no pad's position, so an id keyed to
a sample name would orphan every bus assignment on the next kit. The **label** carries the
sample name. They are 1-based while `dr32_params`' keys are 0-based (`pad0_attack`) —
different namespaces that never meet; the host treats an id as opaque.

**The render ACCUMULATES and its destinations ALIAS.** The host clears them, then hands
two pads on one bus the *same* pointer. So nothing in the split path may `memset` a
destination — that is the carry-over mistake from a single-output render, and it deletes
another bus's audio with no error anywhere. `dr32_kit_render_voice` fills a *private*
float scratch (`dr32_instance::scratch`, reused per pad) and `dr32.c` converts and sums
into the int16 destination; summing in int16 is forced by the aliasing, since there is no
per-bus float buffer to sum into.

### 🥁 The Drum Bus and the sends are the HOST's

**DR32 no longer contains a mixer.** The always-on Drum Bus is a **declared voice bus** of
four `dr32-fx` inserts (`capabilities.default_buses`), and the pads' Send 1 / Send 2 feed
the host's two **global** sends through `voice_send_params`. The effects did not go
anywhere: `dr32-fx` is one binary holding the four Drum Buss stages *and* all eight send
types (Plate, Spaces, Delay, Gated, Digital, Hall, NonLin, Native) as presets.

What that buys is the whole argument for it. Inside the kit the four stages were not
reorderable, not bypassable with the host's own gesture, not LFO targets, and not
swappable for anything else. As a bus they are all four, and you can drop a drive between
two of them.

**This reverses a decision recorded here.** The note used to read *"the internal sends stay
exactly as they are — dropping them for the platform's would cost DR32 its 64 per-pad send
levels against a bus's 8"*. That was true when it was written and is not any more: the host
grew **per-voice sends**, so a pad's send level is per pad again (32 pads × 2 = 64),
taken from its own audio pre-insert. The reason to keep them had been removed upstream.

**Ordering inside `move_plugin_render_split` is no longer load-bearing.** It was — all 32
voices, then the returns, then the Drum Bus over `main_out` in place — and getting it wrong
made the glue miss the kit or process the unrouted pads twice. With both of those gone it
is a plain accumulate. The **int16 round trip went with it**: gluing in place meant reading
`main_out` back to float and writing it out again, one extra quantisation (~-90 dBFS) forced
by destinations that alias and so have no float buffer to live in. There is nothing to read
back now.

`dr32_kit_finish_main` is kept as a **documented no-op**. It is a seam two binaries share;
removing the symbol is an ABI break for no gain.

**The retired keys are still accepted.** `bus_*`, `send1_*` and `send2_*` are stored, read
back, and connected to nothing. 137 factory kits and every saved slot carry them, and
rejecting a key makes a restore fail on state that is otherwise fine. A DSP *cannot* migrate
them — turning `bus_crunch = 0.4` into an insert means writing into the host's bus, which
this module cannot see and which the user may already have arranged differently. They are
off every page in `module.json`, so there is no knob that does nothing.

**One thing genuinely goes away**: a factory kit's per-pad send amount now points at the
host's Send A, which is empty until you put something on it. Out of the box a `.abl` kit
loads dry. `dr32-fx` set to **Native** on Send A is the closest thing to what it used to do,
and it is one insert.

Pinned by `tests/test_split.c`, which INVERTS its old Drum Bus block rather than dropping
it: same shape, same violently non-neutral settings, and the assertion is now that the
buffer must **not** move by a single LSB on either entry point. Any surviving call into the
old container shows up there. The mixed path is unchanged and stays hashed: a 400-block
score FNVs to `634c6afc892c35f0` before and after.

## ⚠ The engine is a reconstruction, not a design

The DSP laws come from `../move original reconstruct/analysis/native-instruments/`
(`DRUM_RACK_ARCHITECTURE.md`, `DRUM_SAMPLER_TRACE.md`, `DRUM_FILTER_RECON.md`,
`DRUM_EFFECTS_RECON.md`) and from measurement against the stock engine. **Any deviation is a
fidelity bug, even when it sounds better.** Two examples already corrected: the native reader
uses LINEAR interpolation (Catmull-Rom is "nicer" and wrong), and velocity→volume is a dB law
centred on velocity 70, not a linear blend.

## Testing

```sh
tests/run.sh                     # off-device: WAV loader, voice, kit, state, JSON round-trip
node tools/pages_check.mjs       # upstream's validator + voice resolver over the SERVED hierarchy
node ../schwung-current/tools/param-pages/preview.mjs dr32 --all --layout movy \
     --fixture dist/tests/dr32-fixture.json --png dist/tests/pages   # render every page
```

`tests/test_state.c` writes the hierarchy the plugin actually serves to
`dist/tests/served_hierarchy.json`; `pages_check` reads THAT, not `module.json`, and writes a
one-module fixture for upstream's preview tools. Look at the PNGs before a deploy — a viz group
that broke the row rule draws as plain dials with no error anywhere.

**The acceptance test for the engine is a null test, not an ear test** — see
`docs/NULL_TESTING.md`. `tools/fx_suite.sh capture` renders native references on the device
(stack stopped for the batch, via the canonical `scripts/restart_move.sh MOVE_ACTION=stop`);
`tools/fx_suite.sh` reports null depth per effect.

**Playback effects are DROPPED** (Josh, 2026-07-26) — every pad plays the plain sampler.
`Effect_Type` and all nine effects' params are still parsed and preserved on save, so kits stay
lossless and still open on native Move; only playback ignores them.

If one is ever brought back, the bar in `dr32_fx_modelled()` stands: enable it only once it
**measurably beats the dry fallback** in `tools/fx_suite.sh`, and record the number. Implementing
from prose without a numeric target made 8-bit, Punch and FM *worse* than not implementing them.
Pitch Env (-39.3 dB) and Loop (-35.8 dB) were working when switched off.

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

A full VM also makes `docker image inspect` fail intermittently, which looks like the toolchain
image vanishing. `build.sh` now fails loudly on a full VM; it prefers the native arm64
`davebox-builder` image, falling back through `schwung-builder` → `move-anything-builder`.

⚠ Do not run `EnginePerfTool` captures against a live Move stack — that is the suspected cause
of two full device lockups needing a power cycle.

⚠ `build/` is TRACKED (only `build/fx/` is ignored, while `dist/` is), so every local build
dirties the tree. Never blind `git add -A`. Untracking it is Josh's call, not a fix to slip in.
