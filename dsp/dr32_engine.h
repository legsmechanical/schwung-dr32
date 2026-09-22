// dr32_engine.h — the synthesis engines a pad can play instead of a sample.
//
// ⭐ WHAT AN ENGINE IS HERE. One drum VOICE and nothing else: a Faust DSP that
// turns a trigger into mono audio. Everything around it is DR32's — volume,
// pan, velocity-to-volume, choke, the per-pad bus, the host's sends — so a
// synth pad and a sample pad meet the rest of the kit through the same stage.
// The ports these engines come from (schwung-simian, schwung-urchin) also have
// a kit-wide master stage (reverb, limiter, lo-fi); none of it is here.
//
// ⭐ ENGINE vs MODEL. An ENGINE is a DSP class with its own parameter table
// (SIMIAN's voice, URCHIN's drum, snare and cymbal). A MODEL is one named
// starting point for an engine — "Kick", "Closed Hat" — taken from the
// upstream instrument's own init kit. The picker offers models; a pad RUNS an
// engine. Choosing a model sets the engine and loads that model's values, and
// from then on the values are the pad's own.
//
// ⚠ ENGINE IDS ARE PERSISTED (the `model` key in the state blob names the
// engine through its model) and ui_engine's VALUES are what module.json's
// visible_if gates compare against. Append engines; never renumber them.
//
// Threading: create/destroy allocate and run on the host thread (set_param).
// Everything else is audio-thread safe: no allocation, no I/O.

#ifndef DR32_ENGINE_H
#define DR32_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ui_engine values. 0 is the sample voice, which is not an engine in this
 * table — it is DR32's own dr32_voice. */
enum {
    DR32_ENG_SAMPLE        = 0,
    DR32_ENG_SIMIAN        = 1,
    DR32_ENG_URCHIN_DRUM   = 2,
    DR32_ENG_URCHIN_SNARE  = 3,
    DR32_ENG_URCHIN_CYMBAL = 4,
    /* The kit ports (9W9, 6W6, 8W8, CW-78): ONE ENGINE PER LANE of the
     * machine, a contiguous run each, in the machine's own lane order. */
    DR32_ENG_9W9_BASE      = 5,     /* 11 lanes:  5..15 */
    DR32_ENG_6W6_BASE      = 16,    /*  8 lanes: 16..23 */
    DR32_ENG_8W8_BASE      = 24,    /* 16 lanes: 24..39 */
    DR32_ENG_CW78_BASE     = 40,    /* 14 lanes: 40..53 */
    DR32_ENG_CHOWKICK      = 54,
    DR32_ENG_FM            = 55,
    DR32_ENG_COUNT         = 56,
};

/* ui_family values: which INSTRUMENT the focused pad's engine comes from.
 * The kit ports share one set of pages per instrument (their lanes have the
 * same panel: Tune, Decay, Drive, ...) gated on this, and a lane's own extra
 * knob is gated on ui_engine within them. One page set per lane would not fit
 * the served hierarchy (tools/gen_engine_ui.mjs). Append; never renumber. */
enum {
    DR32_FAM_SAMPLE = 0,
    DR32_FAM_SIMIAN = 1,
    DR32_FAM_URCHIN = 2,
    DR32_FAM_9W9    = 3,
    DR32_FAM_6W6    = 4,
    DR32_FAM_8W8    = 5,
    DR32_FAM_CW78   = 6,
    DR32_FAM_CHOWKICK = 7,
    DR32_FAM_FM     = 8,
};

#define DR32_ENG_MAX_PARAMS 32

/** One engine parameter, in DISPLAY units — the numbers the knob shows and
 *  the state blob stores. The engine converts to its zone's unit itself. */
typedef struct {
    const char *key;        /* bare key, engine-prefixed: "sm_pitch" */
    const char *name;       /* knob label                            */
    const char *short_name; /* the cell's 4-5 character label        */
    float       min, max, def, step;
    const char *unit;       /* "hz", "%", "dB", "st", "ms", "in", or NULL */
    const char *page;       /* the bank this knob sits on: "Tone", "Shell" */
    const char *options;    /* an ENUM: "Vinyl|Tape", value = the index. NULL otherwise */
} dr32_eparam;

typedef struct {
    int         id;         /* DR32_ENG_*                              */
    const char *slug;       /* "simian", "urchin_drum" ...             */
    const char *name;       /* "Simian", "Urchin Drum" ...             */
    const char *prefix;     /* the params' key prefix, "sm_"           */
    int         nparams;
    const dr32_eparam *params;

    void *(*create)(int sample_rate);
    void  (*destroy)(void *e);
    /* Write one parameter (display units). Takes effect on the running voice
     * where the DSP allows — these are live Faust zones, not note-on copies. */
    void  (*set)(void *e, int idx, float display);
    /* Start a hit. vel01 is velocity/127; tune_st is the pad's total pitch
     * offset in semitones (transpose + detune). */
    void  (*note_on)(void *e, float vel01, float tune_st);
    /* Cut the voice short (choke group, all-off). DR32 also ramps its own
     * gain; this only lets an engine stop costing CPU sooner. */
    void  (*choke)(void *e);
    /* Render n MONO frames into `out` (overwrites). Returns 0 once the voice
     * has been silent long enough to stop computing — the caller then skips it
     * until the next note_on. */
    int   (*render)(void *e, float *out, int n);
    int         family;     /* DR32_FAM_*                              */
} dr32_engine_ops;

typedef struct {
    const char *slug;       /* persisted: "simian/kick"                */
    const char *name;       /* shown on the pad and in the picker      */
    int         engine;     /* DR32_ENG_*                              */
    const float *values;    /* engine->nparams values, display units   */
    float       volume_db;  /* DR32 pad volume to start from           */
    float       pan;        /* DR32 pan, -50..+50                      */
} dr32_model;

/** Engine by id, NULL for DR32_ENG_SAMPLE or anything unknown. */
const dr32_engine_ops *dr32_engine_get(int id);

/** Run every engine's one-time class initialisation (shared tables). Host
 *  thread, once per process before the first create. Idempotent. */
void dr32_engines_init(int sample_rate);

/** Where the module is installed. 9W9's hats and cymbals are SAMPLES (as on a
 *  real 909) read from <dir>/samples/9w9/ at class init; without a directory
 *  those lanes are silent. Host thread, before the first create. */
void dr32_engines_set_module_dir(const char *dir);
const char *dr32_engines_module_dir(void);

int               dr32_model_count(void);
const dr32_model *dr32_model_at(int i);
/** Index of the model with this slug, or -1. */
int               dr32_model_find(const char *slug);

/** Index of `key` in the engine's table, or -1. `key` is the full prefixed
 *  key ("sm_pitch"). */
int dr32_engine_param_index(const dr32_engine_ops *e, const char *key);

/* Per-engine tables and models, defined in each engine's TU. */
extern const dr32_engine_ops dr32_engine_simian;
extern const dr32_engine_ops dr32_engine_urchin_drum;
extern const dr32_engine_ops dr32_engine_urchin_snare;
extern const dr32_engine_ops dr32_engine_urchin_cymbal;
extern const dr32_engine_ops dr32_engine_chowkick;
extern const dr32_engine_ops dr32_engine_fm;
/* The kit ports: each family's lane engines, in id order from its BASE. */
const dr32_engine_ops *dr32_9w9_engine(int lane, int *count);
const dr32_engine_ops *dr32_6w6_engine(int lane, int *count);
const dr32_engine_ops *dr32_8w8_engine(int lane, int *count);
const dr32_engine_ops *dr32_cw78_engine(int lane, int *count);
/* Each engine TU's models, in picker order. */
const dr32_model *dr32_simian_models(int *count);
const dr32_model *dr32_urchin_models(int *count);
const dr32_model *dr32_9w9_models(int *count);
const dr32_model *dr32_6w6_models(int *count);
const dr32_model *dr32_8w8_models(int *count);
const dr32_model *dr32_cw78_models(int *count);
const dr32_model *dr32_chowkick_models(int *count);
const dr32_model *dr32_fm_models(int *count);
void dr32_simian_class_init(int sample_rate);
void dr32_urchin_class_init(int sample_rate);
void dr32_9w9_class_init(int sample_rate);

#ifdef __cplusplus
}
#endif

#endif
