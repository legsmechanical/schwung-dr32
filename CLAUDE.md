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
  or `ui_live_note` (a host that emits the note names it outright, no race). Stock upstream 1.2.0
  has neither; **upstream PR #426 makes `child_press_param` the host's contract** (open 2026-09-06).
- A host that has vouched even once OWNS liveness from then on (`dr32_kit.host_vouches`): bare
  notes never move focus again, whatever the transport says — davebox's sequencer dragged focus
  because davebox reports no transport to the plugin.
- `ui_auto_select_pad` ("Follow Pads") gates both. The kit browser suspends it via
  `browser_hooks` while open.

Pinned by `tests/test_kit.c` (both regimes, vouch consumption across a transport start, the
upper bank). **Before changing any of this, say so**: dAVEBOx sound mode depends on it.

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
