# DR32 — Move Drum Rack clone, 32 pads

## 📗 Read `../schwung/docs/MODULES.md` before touching module.json or the UI

It is the authoritative contract for module composition — param schema, UI hierarchy, chain
params, knob mappings, plugin API. **Do not infer the schema from other modules' `module.json`.**
This module shipped broken once by doing exactly that: it used `label` where the schema wants
`name`, invented a kit-browser arrangement instead of the `filepath` param type, and declared UI
params the DSP had no `get_param` readback for. Symptom: loads fine, logs nothing, menu does
nothing.

Key facts for this module specifically:

- The 32 pads use the Shadow UI **child selector** (`child_prefix: "pad"`, `child_count: 32`),
  which generates `pad<N>_<key>` param keys — exactly the keys `dsp/dr32_params.c` speaks.
  That is why the DSP's flat key scheme looks the way it does; don't "tidy" it.
- Every key the UI displays must be readable back via `dr32_read_param`.

## ⚠⚠ An external consumer depends on `ui_live_press` / `ui_current_pad`

**dAVEBOx "sound mode"** (`schwung-davebox`, branch `sound-mode`) hosts DR32 in a chain slot and
edits it in place. Under it **DR32's canvas never runs** — davebox loads `canvas.js` only to
harvest `bank_editor._test.BANKS`, then restores the globals — so `CONFIG.onMidi` is not there to
vouch that a pad press was live. **davebox vouches in its place**, writing `ui_live_press` itself
from its own pad handler.

It finds those keys from two fields on the `pads` level of `module.json`:

```json
"child_select_param": "ui_current_pad",   /* read: which pad is focused */
"child_press_param":  "ui_live_press"     /* write "1": a finger did that */
```

**They are not cruft — do not remove them.** They are a generic host-side convention (any tool
hosting any module can use them), not a DR32 invention.

**Before changing how focus works** — the `live_armed` / `last_hit_pad` correlation in
`dr32_kit.c` + `dr32_params.c`, or the meaning of either key — say so, because davebox breaks
silently: it writes the vouch, nothing correlates it, and focus simply stops following. There is
no error. Timing already measured from the davebox side: the correlation window is
`DR32_LIVE_MATCH_BLOCKS`=20 × 2.902 ms/block (`FRAMES_PER_BLOCK`=128 @44.1 kHz) = **58.0 ms**, and
a vouch arriving later than that is silently lost.

Detail + a pad-lag lead: `_worklogs/schwung-dr32.md` and `_worklogs/schwung-davebox.md` (round 30).

## ⚠ The engine is a reconstruction, not a design

The DSP laws come from `../move original reconstruct/analysis/native-instruments/`
(`DRUM_RACK_ARCHITECTURE.md`, `DRUM_SAMPLER_TRACE.md`, `DRUM_FILTER_RECON.md`,
`DRUM_EFFECTS_RECON.md`) and from measurement against the stock engine. **Any deviation is a
fidelity bug, even when it sounds better.** Two examples already corrected: the native reader
uses LINEAR interpolation (Catmull-Rom is "nicer" and wrong), and velocity→volume is a dB law
centred on velocity 70, not a linear blend.

## Testing

```sh
tests/run.sh                     # off-device: WAV loader, voice, kit, JSON round-trip
```

**The acceptance test is a null test, not an ear test** — see `docs/NULL_TESTING.md`.
`tools/fx_suite.sh capture` renders native references on the device (stack stopped for the
batch, via the canonical `scripts/restart_move.sh MOVE_ACTION=stop`); `tools/fx_suite.sh`
reports null depth per effect.

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
