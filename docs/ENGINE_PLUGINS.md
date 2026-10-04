# DR32 engine plugins

How to make a Schwung module's voices playable inside DR32.

DR32 is a 32-pad drum rack. Each pad plays either a sample or a **synth
engine**. Engines can come from other modules: a module ships one extra file,
and DR32 finds it and lists that module's sounds in its engine picker. There is
nothing to register, no DR32 release to wait for, and no host change.

The contract is one C header, [`dsp/dr32_engine_api.h`](../dsp/dr32_engine_api.h).
It is MIT-licensed so that a module of any licence can copy it into its own
tree. This document explains it; where the two disagree, the header is right.

## Contents

1. [What you ship](#what-you-ship)
2. [What an engine is](#what-an-engine-is)
3. [Start from the template](#start-from-the-template)
4. [A complete example](#a-complete-example)
5. [The API](#the-api)
6. [Parameters and pages](#parameters-and-pages)
7. [Models](#models)
8. [What DR32 does around your engine](#what-dr32-does-around-your-engine)
9. [Rules](#rules)
10. [Names are permanent](#names-are-permanent)
11. [Building](#building)
12. [Checking it on your own machine](#checking-it-on-your-own-machine)
13. [Installing and checking that it loaded](#installing-and-checking-that-it-loaded)
14. [Adapting an existing module](#adapting-an-existing-module)
15. [Limits](#limits)
16. [Checklist](#checklist)

## What you ship

One shared object, beside your module's own files:

```
modules/sound_generators/<your module>/dr32_engine.so
```

It exports one function, `dr32_engine_plugin`. DR32 looks for a file with
exactly that name in each of its sibling module folders, loads the ones it
finds, and adds a section to its picker for each.

Your module's `dsp.so` and `module.json` are not involved. Your module keeps
working exactly as it did, whether or not DR32 is installed.

## What an engine is

An engine is **one drum voice**: a trigger goes in, mono audio comes out.

Three terms are used throughout:

| Term | Meaning |
|---|---|
| **Plugin** | Your `dr32_engine.so`. It becomes one section of DR32's picker, named after your module. |
| **Engine** | A DSP class with its own parameter table. A pad *runs* an engine. |
| **Model** | A named starting point for an engine ("808 Kick", "Closed Hat"): which engine, and a value for each of its parameters. The picker lists models. |

A plugin offers one or more engines and one or more models. Choosing a model
puts its engine on the pad and loads the model's values. From then on the
values belong to the pad: the user can turn every knob, and the kit saves what
they turned.

Leave your module's kit-wide machinery out: its mixer, effects buses, master
section, sequencer, kit morphing. DR32 has its own mix, sends and choke groups
for every pad.

## Start from the template

[`docs/plugin_template/`](plugin_template/) is a working plugin to copy:

| File | What it is |
|---|---|
| [`dr32_engine.c`](plugin_template/dr32_engine.c) | One engine, two pages, three models. The parts to replace are marked `CHANGE`. |
| [`Makefile.snippet`](plugin_template/Makefile.snippet) | The build line for the device, and one for checking it natively. |

Copy it into your module together with two headers from `dsp/`:

| Header | What it is |
|---|---|
| [`dr32_engine_api.h`](../dsp/dr32_engine_api.h) | The contract. Required. |
| [`dr32_engine_kit.h`](../dsp/dr32_engine_kit.h) | Optional helpers: parameter-row macros, the export attribute, knob mappings, tune to a frequency ratio, stereo to mono. |

DR32's test suite builds the template and runs the checker over it, so it
stays in step with the contract.

## A complete example

This is a whole plugin written against the bare API, with no helpers: a
decaying sine with two knobs and two models.

```c
#include "dr32_engine_api.h"
#include <math.h>
#include <stdlib.h>

typedef struct { float sr, phase, env, hz, decay_ms, tune; } toy;

enum { P_PITCH, P_DECAY, P_COUNT };

static const dr32x_param PARAMS[P_COUNT] = {
    /* key      name     short    min   max   def  step unit  page    options */
    { "pitch", "Pitch", "PITCH",  40,   400,  110, 1,   "hz", "Tone", NULL },
    { "decay", "Decay", "DECAY",  10,   2000, 200, 1,   "ms", "Tone", NULL },
};

static void *toy_create(int sample_rate) {
    toy *t = calloc(1, sizeof *t);
    if (t) t->sr = (float)sample_rate;
    return t;
}
static void toy_destroy(void *e) { free(e); }

static void toy_set(void *e, int idx, float v) {
    toy *t = e;
    if (idx == P_PITCH) t->hz = v;
    if (idx == P_DECAY) t->decay_ms = v;
}

static void toy_note_on(void *e, float vel01, float tune_st) {
    toy *t = e;
    t->env = vel01;
    t->phase = 0;
    t->tune = powf(2.0f, tune_st / 12.0f);
}

static int toy_render(void *e, float *out, int n) {
    toy *t = e;
    float k = expf(-1.0f / (0.001f * t->decay_ms * t->sr));
    float inc = t->hz * t->tune / t->sr;
    for (int i = 0; i < n; i++) {
        out[i] = sinf(6.2831853f * t->phase) * t->env;
        t->env *= k;
        t->phase += inc;
        if (t->phase >= 1) t->phase -= 1;
    }
    return DR32X_RENDER_ALIVE;        /* DR32 stops calling once it is quiet */
}

static const dr32x_engine ENGINES[] = {
    { "sine", "Toy Sine", P_COUNT, PARAMS,
      toy_create, toy_destroy, toy_set, toy_note_on, NULL /* choke */, toy_render },
};

static const float LOW[P_COUNT]  = { 60, 400 };
static const float HIGH[P_COUNT] = { 330, 80 };
static const dr32x_model MODELS[] = {
    /* slug   name        engine values volume_db pan */
    { "low",  "Toy Low",  0,     LOW,   -6.0f,    0 },
    { "high", "Toy High", 0,     HIGH,  -9.0f,    0 },
};

static const dr32x_plugin PLUGIN = {
    DR32X_API_VERSION, sizeof(dr32x_plugin), "toy", "Toy", 1, ENGINES, 2, MODELS,
};

__attribute__((visibility("default")))
const dr32x_plugin *dr32_engine_plugin(const dr32x_host *host) {
    (void)host;
    return &PLUGIN;
}
```

Installed, this adds a **Toy** section to the picker with "Toy Low" and "Toy
High". A pad playing either one has a **Tone** page with Pitch and Decay.

## The API

### The entry point

```c
const dr32x_plugin *dr32_engine_plugin(const dr32x_host *host);
```

Called once per process, off the audio thread, so it may read files and build
tables. Return `NULL` to offer nothing. Whatever it returns, and everything
that points to, must stay valid for the life of the process: use static
storage.

`host` tells you what DR32 expects, and is only valid during the call:

| Field | Meaning |
|---|---|
| `api_version` | The version DR32 speaks. Return `NULL` if it is not `DR32X_API_VERSION`. |
| `sample_rate` | 44100 today. Return `NULL` if you cannot run at the rate given. |
| `module_dir` | Your module's own folder, for samples or tables you need to read. |

### `dr32x_plugin`

| Field | Meaning |
|---|---|
| `api_version` | `DR32X_API_VERSION` |
| `struct_size` | `sizeof(dr32x_plugin)` |
| `id` | Your plugin's permanent id: `[a-z0-9]`, 1 to 12 characters. Normally your module id. |
| `name` | The picker section's label, up to 24 characters. |
| `nengines`, `engines` | Your engines. |
| `nmodels`, `models` | Your models. |

### `dr32x_engine`

| Field | Meaning |
|---|---|
| `slug` | Permanent: `[a-z0-9_]`, 1 to 12 characters, unique in the plugin. |
| `name` | Up to 24 characters. |
| `nparams`, `params` | 1 to 32 parameters. See [Parameters and pages](#parameters-and-pages). |
| `create(sample_rate)` | Make one voice. Never the audio thread; may allocate. Return `NULL` on failure. |
| `destroy(e)` | Free it. Never the audio thread. |
| `set(e, idx, display)` | Write parameter `idx`, in display units. Audio thread. |
| `note_on(e, vel01, tune_st)` | Start a hit. Audio thread. |
| `choke(e)` | **Optional** (may be `NULL`). The pad was cut short. Audio thread. |
| `render(e, out, n)` | Write `n` mono frames; return whether the voice is still going. Audio thread. |

`choke` is the only function that may be left out.

`render` returns one of:

| Value | Meaning |
|---|---|
| `DR32X_RENDER_ALIVE` (1) | Still going. DR32 ends the voice once its output has stayed under -80 dB for 100 ms. |
| `DR32X_RENDER_DONE` (0) | Finished. DR32 stops calling until the next `note_on`. |
| `DR32X_RENDER_HOLD` (2) | Still going, and not to be ended on silence. For a voice with a gap longer than 100 ms in it, such as a late burst or a slow repeat. It must return `DONE` itself when it ends. |

A simple engine returns `ALIVE` every time and lets DR32 decide.

### `dr32x_model`

| Field | Meaning |
|---|---|
| `slug` | Permanent: `[a-z0-9_]`, 1 to 24 characters, unique in the plugin. |
| `name` | Shown in the picker and as the pad's name, up to 24 characters. |
| `engine` | Index into your `engines` array. |
| `values` | One value per parameter of that engine, in display units, each inside the parameter's range. |
| `volume_db` | The pad Volume this model starts at. |
| `pan` | The pad Pan this model starts at, -50 to +50. |

## Parameters and pages

```c
typedef struct dr32x_param {
    const char *key;        /* "pitch"   */
    const char *name;       /* "Pitch"   */
    const char *short_name; /* "PITCH"   */
    float       min, max, def, step;
    const char *unit;       /* "hz", or NULL */
    const char *page;       /* "Tone"    */
    const char *options;    /* "Sine|Saw", or NULL */
} dr32x_param;
```

**Display units.** `min`, `max`, `def` and the values `set` receives are the
numbers the knob shows and the kit saves. Convert to your DSP's internal unit
inside `set`. You never need to report a value back: DR32 holds each pad's
values itself.

**Pages.** `page` names the bank a knob sits on. DR32 builds one page per
distinct name, in the order the names first appear in your table, and lays the
knobs out in table order. A page holds at most **8** knobs. An engine has at
most **32** parameters.

Do not name a page after one of DR32's own: Pad, Shape, Mix, Stereo, Master,
Resample, Category, Kit. A plugin that does is refused.

**Integer or float.** A parameter whose `min`, `max`, `def` and `step` are all
whole numbers is an integer knob. Give it a fractional `step` (0.1, 0.01) to
make it a float knob.

**Enums.** Set `options` to the choices separated by `|`. The value is the
index, so `min` must be 0, `max` must be the number of options minus one, and
`def` a whole number in between. `set` receives the index as a float. The whole
string is at most 255 characters and each option under 64.

**The knob is linear.** One detent moves 0.5% of the range, and there is no
taper. A range of 20 to 20000 Hz therefore puts every useful low frequency in
the first few detents. Two ways round it:

- Narrow the range to what the engine is for. DR32's own FM drums are four
  engines on one core for this reason: a kick's pitch runs 20 to 300 Hz, a
  snare's 60 to 1000.
- Use a 0 to 100 knob and map it exponentially inside `set`. The knob then
  shows a position, not a time or a frequency. The helper header has the
  mapping (`dr32x_exp`) and its inverse.

**Units.** `unit` is a short label ("hz", "ms", "%", "dB", "st") or `NULL`.

## Models

A model is the reason a user picks your engine, so offer the sounds your module
is known for. Your module's own presets or init kit are the natural source.

A model must give a value for **every** parameter of its engine. Anything about
a sound that is not a parameter cannot differ between two models of the same
engine: the engine is created the same way for both, and then receives the
model's values through `set`. If two of your presets differ in something you
have not exposed, either expose it or make them separate engines.

`volume_db` and `pan` become the pad's own Volume and Pan when the model is
chosen. Use `volume_db` to level-match your models against each other.

## What DR32 does around your engine

When a model is chosen, DR32 calls `create`, then `set` once for every
parameter with the model's values. After that:

| | Who does it |
|---|---|
| Volume, pan, mute | DR32 |
| Choke groups | DR32. It fades the pad's output over about 3 ms, for any engine. |
| Ending a quiet voice | DR32, unless you return `HOLD` |
| Sends, module buses, stereo widening | DR32, after your output |
| Velocity | **Yours.** `vel01` is velocity / 127. DR32's own velocity-to-volume starts at 0 on a synth pad, so the response is whatever you make of `vel01`. |
| Pitch | **Yours.** `tune_st` is the pad's transpose plus detune, in semitones, and may be fractional. 0 means your engine's own pitch. |
| Saving and loading | DR32. It stores the model and each knob's value. |
| Copy, paste and clear of a pad | DR32 |
| Resampling a pad to a sample | DR32, by running your engine offline |
| Pages, knob layout, the picker | DR32, from your tables |

Your output is mono. DR32 pans it.

## Rules

**Threads.** `create` and `destroy` never run on the audio thread and may
allocate. They can be called from more than one thread (DR32 resamples a pad on
a worker, with an instance of its own), so they must not share unguarded state.
`set`, `note_on`, `choke` and `render` run on the audio thread: no allocation,
no file I/O, no locks, no logging.

**One instance per pad.** Up to 32 can exist at once, and each is created and
destroyed independently. Keep instance state inside the instance: no globals
that a voice writes to. Shared read-only tables are fine; build them in the
entry point.

**`render`** overwrites `out` with `n` mono float frames, `n` at most 1024.
You do not have to detect silence: return `DR32X_RENDER_ALIVE` and DR32 stops
calling a voice that has stayed under -80 dB for 100 ms, until the next
`note_on`. That is what keeps 32 pads affordable. Return `DR32X_RENDER_DONE`
if you know sooner, and `DR32X_RENDER_HOLD` if your voice has a silent gap
that must not be mistaken for its end.

**`set`** may be called while the voice is sounding and should take effect on
it where the DSP allows.

**`choke`** is optional. DR32's own fade is what the listener hears, so a
choke group works on your engine whether or not you supply it. Without it, a
choked voice keeps rendering unheard until it decays. Supply it if your tails
are long and you want the CPU back: fade over a few milliseconds rather than
cutting to zero, so the two ramps do not click, then return `DONE`.

**`note_on`** restarts the voice. A second hit before the first has finished is
a retrigger of the same instance, not a second voice.

**Size.** Each pad holds a whole instance. If your voice struct is embedded in
a large module-wide struct, allocate only what a single voice touches.

## Names are permanent

A saved kit stores a plugin pad as:

- the model, `<plugin id>/<model slug>`, for example `mysynth/kick`
- each knob, under `x_<plugin id>_<engine slug>_<key>`, for example
  `x_mysynth_drum_decay`

So the plugin `id`, every engine `slug`, every model `slug` and every parameter
`key` are permanent once users have saved kits. Renaming one makes those kits
lose it. You may freely:

- add models
- add parameters to an engine (old kits take the new parameter's value from the
  model)
- add engines
- change labels: `name`, `short_name`, `page`, `unit`

Changing a parameter's range or meaning under the same key changes how old kits
sound.

To keep the full key within DR32's limits, the plugin id and engine slug are at
most 12 characters and a parameter key at most 16.

**If your module is not installed** when a kit is opened, the pad is silent and
reads "`<model>` missing". DR32 keeps the model and its saved values and writes
them back out on the next save, so installing your module later brings the pad
back.

## Building

Build the plugin as its own shared object for the Move (Linux, aarch64),
separate from your `dsp.so`:

```sh
aarch64-linux-gnu-gcc -O2 -shared -fPIC \
    -fvisibility=hidden -Wl,-Bsymbolic \
    -o dist/<module>/dr32_engine.so \
    src/dr32_engine.c <your voice sources> -lm
```

Two flags matter:

- **`-fvisibility=hidden`**, with `__attribute__((visibility("default")))`
  (or `DR32X_EXPORT` from the helper header) on `dr32_engine_plugin` only.
  Nothing else should be exported.
- **`-Wl,-Bsymbolic`**. Your `dsp.so` may be loaded in the same process, with
  the same symbol names. Without this the plugin can bind to the other copy's
  globals.

Check the result exports exactly one symbol:

```sh
aarch64-linux-gnu-nm -D --defined-only dr32_engine.so
```

Copy `dr32_engine_api.h`, and `dr32_engine_kit.h` if you use it, into your
source tree. Add `dr32_engine.so` to your
release tarball and install script so it lands in your module's folder.

## Checking it on your own machine

`tools/plugin_check.c` loads a plugin the way DR32 does, applies DR32's own
rules to it (the same code the loader runs), and plays it. No Move, and no
DR32 install, is needed.

Build the checker once, from a DR32 checkout:

```sh
cc -O2 -Idsp -o dr32-plugin-check tools/plugin_check.c dsp/dr32_plugin_validate.c -lm -ldl
```

Build your plugin **natively**, for the machine you are on, and run it:

```sh
cc -O2 -shared -fPIC -fvisibility=hidden -o /tmp/dr32_engine.so src/dr32_engine.c -lm
./dr32-plugin-check /tmp/dr32_engine.so
```

It reports:

- **Whether DR32 would load it**, and the reason if not.
- **The pages** DR32 will build, with each knob's range, and the keys a saved
  kit will hold.
- **Every model:** whether it sounds, its peak level, how long it lasts,
  whether it ends, and a rough cost per block on your machine.
- **Every knob:** whether turning it from minimum to maximum changes the sound.
  A knob that changes nothing on any model tried is reported.
- **Transpose, velocity, choke and retrigger** for each engine.

```
mysynth ("My Synth"): 1 engine, 3 models

engine drum ("My Drum"), 8 knobs, no choke (DR32 fades it)
  page Tone        PITCH[30..400hz]  DECAY[0..100]  SWEEP[0..48st]  SWP T[0..100]  WAVE[2]  DRIVE[0..100%]
  page Noise       NOISE[0..100%]  N DEC[0..100]
  saved as  x_mysynth_drum_<key>, model slugs mysynth/<model>

models (velocity 100, no transpose):
  kick                     drum     peak   -4.0 dBFS   3578 ms    8.6 us/block
  tom                      drum     peak   -4.0 dBFS   2200 ms    5.7 us/block
  zap                      drum     peak   -4.0 dBFS    702 ms    1.3 us/block

engine drum, tried on model kick and others:
  every knob changes the sound
  transpose +12 st: zero crossings x2.00
  velocity 30 vs 100: -10.5 dB

OK: 0 errors, 0 warnings
```

It exits 1 on an `ERROR`: something DR32 would refuse, or a model that is
silent, never ends, or produces a NaN. A `WARN` is worth a look but is not a
refusal. Some are expected: a noise voice does not change pitch with
transpose.

It cannot tell you how the plugin sounds, what it costs on the Move's CPU, or
whether its id collides with another plugin installed on a device.

## Installing and checking that it loaded

Put `dr32_engine.so` in your module's folder on the device, then restart the
Move. DR32 scans once per start, so a plugin copied in while it is running does
not appear until the next one.

After DR32 has loaded once, read its report:

```sh
cat /data/UserData/schwung/modules/sound_generators/dr32/plugins.log
```

A plugin that loaded:

```
dr32: engine plugin mysynth: 2 engines, 12 models, 6 pages
```

A plugin is accepted or refused **whole**. A refusal says why:

```
dr32: engine plugin toy refused: engine sine param 3: its page has more than 8 knobs
```

Every reason a plugin can be refused:

| Report | Cause |
|---|---|
| `... did not load: <dlerror text>` | Not a loadable shared object for this device, or it has an unresolved symbol. |
| `exports no dr32_engine_plugin` | The entry point is missing or hidden. |
| `offers no engines` | The entry point returned `NULL`. |
| `speaks API n, DR32 speaks m` | `api_version` mismatch. |
| `struct_size is too small` | `struct_size` is not `sizeof(dr32x_plugin)`. |
| `id must be [a-z0-9], 1-12 chars` | |
| `id is one of DR32's own engine families` | The id is taken by a built-in. |
| `id is already an engine family` | Another installed plugin uses that id. |
| `no engines` / `no models` | |
| `an engine's slug or name is malformed` | Wrong characters or too long. |
| `two engines share a slug` | |
| `an engine is missing a function` | A function other than `choke` is `NULL`. |
| `an engine needs 1-32 params` | |
| `engine X param N: key, name, short_name, page or unit is malformed` | Empty, too long, or wrong characters. |
| `engine X param N: key is repeated` | |
| `engine X param N: an enum runs 0..count-1` | `min`, `max` or `def` do not match the options. |
| `engine X param N: needs min < max, def inside, step > 0` | |
| `engine X param N: its page has the name of one of DR32's own` | |
| `engine X param N: its page has more than 8 knobs` | |
| `a model's slug or name is malformed` | |
| `two models share a slug` | |
| `a model names no engine or has no values` | |
| `a model's value is outside its param's range` | |
| `too many plugin engines installed` / `... models installed` | See [Limits](#limits). |

## Adapting an existing module

How much work this is depends on one thing: whether a single voice of your
module can already be called on its own.

**Voices already behind an interface.** If each voice type is reached through
its own functions (trigger, render, set a parameter), the adapter is one new
file that maps DR32's six calls onto them, and a build target that links it
with your voice sources. Nothing in the module itself changes.

**Voice code inline in the render loop.** If all voices are rendered inside one
loop in the module's main render function, there is nothing to call for a
single voice. Two steps make one callable:

1. Move the body of the voice loop, unchanged, into a function of its own, and
   have the main render function call it.
2. If the voice code is `static`, have the adapter `#include` the module's
   source file instead of linking it, so it can reach those functions.

The adapter then holds one module instance per pad, plays one voice of it, and
leaves the module's kit-wide controls at their neutral values.

What to expect either way:

- **Find the voice boundary first.** What does one voice read that belongs to
  the whole module? Typical answers are kit-wide macros, a shared sample bank,
  another voice's LFO, a master tempo. Each needs a neutral value or a private
  copy in the adapter.
- **Per-voice parameters often outnumber 32.** Start from the fields your own
  presets set. Those are what distinguish one of your sounds from another, so
  they are what a model needs.
- **Presets become models.** Derive each model's values from the preset itself
  in the entry point: load it into a scratch voice and read the fields back as
  display values. That keeps the plugin in step with the module's presets.
- **Check your instance size.** If a voice lives inside a struct that also
  holds kits, delay lines or reverb tanks, make those sizes overridable in the
  plugin build so a pad does not carry them.
- **Prove a refactor.** If you have to move code to expose a voice, render your
  module before and after and compare the output. A hash over a spread of
  presets is enough, as long as you also confirm that a deliberate change moves
  the hash.

## Limits

- **Mono output.** There is no stereo engine.
- **No host services.** An engine gets no tempo, no transport, no MIDI beyond
  the hit, and no host API. Engines that sequence themselves do not fit.
- **One voice per pad.** No polyphony within a pad.
- **32 parameters per engine**, 8 per page.
- **No conditional knobs.** Every parameter of an engine is always shown. If a
  knob only applies in one mode, that mode is a candidate for its own engine.
- **No custom drawing.** Pages are DR32's standard knob cells.
- **Capacity across all installed plugins:** 64 engines, 256 models, 192 pages.
- **Scanned once per start**, from the folders beside DR32's own
  (`modules/sound_generators/`).
- **Page budget.** The host caps a module's served page description at 128 KB.
  DR32 serves pages only for the engines a kit is using, and a page costs
  roughly 2 to 3 KB, so one kit has room for about 40 engine pages in total
  across every engine on its pads. An engine with three pages leaves room for
  more variety in a kit than one with six.

## Checklist

- [ ] `dr32_engine.so` exports `dr32_engine_plugin` and nothing else
- [ ] Built with `-fvisibility=hidden -Wl,-Bsymbolic` for aarch64
- [ ] The entry point returns `NULL` for an API version or sample rate it cannot serve
- [ ] `dr32-plugin-check` reports no errors, and you have read its warnings
- [ ] Nothing on the audio thread allocates, locks, logs or touches a file
- [ ] `render` returns `HOLD` only for a voice with a silent gap, and then `DONE` when it ends
- [ ] If you supply `choke`, it fades instead of cutting
- [ ] `tune_st` moves the pitch
- [ ] At most 8 knobs per page and 32 per engine, and no page named after one of DR32's
- [ ] Every model has a value for every parameter, inside its range
- [ ] Ids, slugs and keys are ones you can live with permanently
- [ ] The file is in your release tarball and install script
- [ ] `plugins.log` lists your plugin after a restart
