// dr32_engine_api.h — how ANOTHER MODULE offers DR32 a synth engine.
//
// SPDX-License-Identifier: MIT
// (This one header, so that a module of any licence can include it. DR32
// itself is GPL-3.0-or-later.)
//
// ⭐ WHAT YOU SHIP. One extra shared object beside your module's own files:
//
//     modules/sound_generators/<your module>/dr32_engine.so
//
// exporting one function, `dr32_engine_plugin`. DR32 finds the file by its
// NAME in its sibling module folders, loads it, and your models appear in its
// engine picker under your module's name. Nothing in your module.json, no DR32
// release, no host change.
//
// ⭐ WHAT AN ENGINE IS. One drum VOICE and nothing else: a trigger in, MONO
// audio out. Volume, pan, velocity-to-volume, choke, the per-pad bus, the
// sends, the stereo stage, state, copy/paste and every page's layout are
// DR32's. Leave your module's own master stage (reverb, limiter, sequencer)
// out; DR32 has one of each per pad or per kit already.
//
// ⭐ ENGINE vs MODEL. An ENGINE is a DSP class with a parameter table. A MODEL
// is one named starting point for it ("Kick", "Closed Hat"): the engine to run
// and a value for each of its parameters. The picker offers models; a pad RUNS
// an engine, and from then on the values are the pad's own.
//
// ⚠ THE RULES THE AUDIO THREAD HOLDS YOU TO
//   - `create` / `destroy` run on the host thread and may allocate. EVERYTHING
//     ELSE runs on the audio thread: no allocation, no file I/O, no locks.
//   - One instance per PAD, up to 32 at once. Keep an instance small.
//   - `render` OVERWRITES `out` with n mono float frames (n <= 1024) and
//     returns 0 once the voice has been silent long enough to stop computing;
//     DR32 then skips it until the next `note_on`.
//   - `set` takes effect on a sounding voice where the DSP allows.
//   - 44100 Hz today; honour the `sample_rate` you are given.
//
// ⚠ BUILD IT SELF-CONTAINED: `-fvisibility=hidden -Wl,-Bsymbolic`, with only
// the entry point exported. Your module's dsp.so may be loaded in the same
// process, and without that the two copies of your code can bind to each
// other's globals.
//
// ⚠ KEYS ARE BARE AND PERMANENT. A parameter's `key` ("pitch") is yours; DR32
// prefixes it with your plugin id and engine slug, and that full key is what a
// saved kit stores. So are a model's `slug` and your plugin `id`. Rename one
// and saved kits lose it. Add parameters and models freely.

#ifndef DR32_ENGINE_API_H
#define DR32_ENGINE_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define DR32X_API_VERSION 1
#define DR32X_ENTRY       "dr32_engine_plugin"
#define DR32X_FILE        "dr32_engine.so"

#define DR32X_MAX_PARAMS  32   /* per engine                          */
#define DR32X_PAGE_KNOBS  8    /* per page: a bank holds eight knobs  */

/** One parameter, in DISPLAY units: the numbers the knob shows and a saved
 *  kit stores. Convert to your DSP's own unit in `set`.
 *  ⓘ The host's knob is LINEAR (a detent is 0.5% of the range). If a range
 *  crowds the useful values into a few detents, narrow the range. */
typedef struct dr32x_param {
    const char *key;        /* bare: [a-z0-9_], <= 24 chars: "pitch"           */
    const char *name;       /* knob label, <= 32 chars: "Pitch"                */
    const char *short_name; /* the cell's label, <= 8 chars: "PITCH"           */
    float       min, max, def, step;   /* a step < 1 makes it a float knob     */
    const char *unit;       /* "hz", "%", "dB", "st", "ms" ... or NULL         */
    const char *page;       /* the bank it sits on, <= 16 chars: "Tone". Not a
                             * name DR32 uses: Pad, Shape, Mix, Stereo, Master,
                             * Resample, Category, Kit.                        */
    const char *options;    /* an ENUM: "Sine|Saw"; value = the index. Or NULL */
} dr32x_param;

typedef struct dr32x_engine {
    const char *slug;       /* [a-z0-9_], <= 16 chars: "fm2"                   */
    const char *name;       /* "FM2"                                           */
    int         nparams;    /* 1..DR32X_MAX_PARAMS                             */
    const dr32x_param *params;

    void *(*create)(int sample_rate);
    void  (*destroy)(void *e);
    /* Write parameter `idx` (display units). */
    void  (*set)(void *e, int idx, float display);
    /* Start a hit. vel01 is velocity/127; tune_st is the pad's pitch offset in
     * semitones (transpose + detune), 0 = the engine's own pitch. */
    void  (*note_on)(void *e, float vel01, float tune_st);
    /* Cut the voice short (choke group, all-off). DR32 ramps its own gain too;
     * this only lets the engine stop costing CPU sooner. */
    void  (*choke)(void *e);
    /* n MONO frames into `out`; 0 once silent. */
    int   (*render)(void *e, float *out, int n);
} dr32x_engine;

typedef struct dr32x_model {
    const char *slug;       /* [a-z0-9_], <= 24 chars, unique in the plugin    */
    const char *name;       /* shown on the pad and in the picker              */
    int         engine;     /* index into the plugin's `engines`               */
    const float *values;    /* that engine's nparams values, display units     */
    float       volume_db;  /* the pad Volume to start from                    */
    float       pan;        /* the pad Pan to start from, -50..+50             */
} dr32x_model;

typedef struct dr32x_plugin {
    unsigned    api_version;    /* DR32X_API_VERSION                           */
    unsigned    struct_size;    /* sizeof(dr32x_plugin)                        */
    const char *id;             /* [a-z0-9], <= 16 chars: "omega"              */
    const char *name;           /* the picker's section: "Omega"               */
    int         nengines;
    const dr32x_engine *engines;
    int         nmodels;
    const dr32x_model  *models;
} dr32x_plugin;

/** What DR32 tells the plugin. Valid only during the call. */
typedef struct dr32x_host {
    unsigned    api_version;    /* the version DR32 speaks                     */
    int         sample_rate;
    const char *module_dir;     /* YOUR module's folder (for samples, tables)  */
} dr32x_host;

/** The one export. Called once per process, off the audio thread; may read
 *  files. Return NULL to offer nothing. What it returns must stay valid for
 *  the life of the process. */
const dr32x_plugin *dr32_engine_plugin(const dr32x_host *host);

#ifdef __cplusplus
}
#endif

#endif
