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

An effect is only enabled in `dr32_fx_modelled()` once it **measurably beats the dry fallback**.
Implementing from prose without a numeric target made 8-bit, Punch and FM worse than not
implementing them at all.

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
