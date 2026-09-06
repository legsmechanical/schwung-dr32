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
- The send pages hang off DSP-derived read-only params (`send1_mode`, `send1_env`,
  `send1_sync`) because `visible_if` takes one condition on one param. They are served by the
  DSP and declared nowhere; that is deliberate. Every armed type must fit ONE page of eight —
  `pages_check` enforces it.

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

### 🥁 Routing a pad to a bus takes it OFF the drum bus

One sentence, and it is the user-facing behaviour: **a voice you route to a bus leaves the
kit's drum bus**, exactly as routing a channel to a subgroup takes it out of the main mix
on a desk. Pads you do not route stay on main and are glued as they always were.

So the order inside `move_plugin_render_split` is load-bearing:

1. **all 32 voices** — an unrouted pad's destination *is* `main_out`, so it lands there by
   the same aliasing everything else uses;
2. **the send returns**, added to `main_out`;
3. **the Drum Bus over `main_out` IN PLACE** (`dr32_kit_finish_main`), so it glues the
   unrouted pads plus the returns — the same signal mixed mode gives it, minus exactly the
   pads the user routed away.

Get that order wrong and the glue either misses the kit (the first cut started the main
section from silence, so the Drum Bus compressed the *reverb returns* the moment any pad
was routed — a surprising, audible change triggered by an unrelated action) or processes
the unrouted pads twice. If every pad is routed, main holds only the returns and the glue
processes just those; that is correct under this semantic, not a bug.

Want glue over everything *including* the buses? The platform already does it one tier up:
the chain host sums buses into main **before** the slot's own 8 FX, "so a slot compressor
sees the whole kit". Nothing for DR32 to do about that case.

**The in-place glue costs an int16 round trip.** The Drum Bus is float DSP and `main_out`
is int16, so `dr32.c` reads the destination back (`dr32_from_i16`), runs the finish, and
writes it back — one extra quantisation, ~-90 dBFS under the unrouted pads, forced by a
contract whose destinations alias and therefore have no float buffer to live in. The
write-back ROUNDS (`dr32_to_i16_round`) rather than truncating, so a neutral bus with no
sends is an exact identity instead of shaving an LSB off every pad every block. The glue
itself runs in the **pre-master-gain domain**, matching `dr32_kit_render`, because a
compressor is level-dependent.

The internal sends stay exactly as they are — dropping them for the platform's would cost
DR32 its 64 per-pad send levels against a bus's 8 (upstream `docs/CHAIN.md`, "Per-voice
sends").

`dr32_kit_finish_main` runs every block whether or not the caller wants the audio: it is
what drains the send buses. Skipping it replays a block's sends on top of the next.

Pinned by `tests/test_split.c` — the untouched destination, the aliased sum, an unrouted
pad reaching the glue, a routed one never landing on main, the all-bused case, parity with
`render_block` under a live Drum Bus, and the 4096-byte ceiling the host reads the list
through. The mixed path is unchanged and stays hashed: a 400-block score FNVs to
`634c6afc892c35f0` before and after.

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
