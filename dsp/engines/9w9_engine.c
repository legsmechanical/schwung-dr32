/*
 * 9w9_engine.c — the 9W9 (TR-909 style) lanes as DR32 engines.
 *
 * The machine is schwung-9W9 (GPL-3.0; er-99's port), vendored unmodified
 * under 9w9/ and #included here so its render helpers (static in
 * er99_engine.c) can play one lane. See kit_port.h for what "one lane per
 * pad" keeps and drops.
 *
 * ⚠ C, NOT C++, ON PURPOSE. The machine is C11 and it compiles as C++ too, but
 * C++'s <cmath> overloads would turn its float calls into different functions
 * (fabs(float) resolves to the float overload in C++, the double one in C), so
 * the same source would not make the same sound.
 *
 * ⭑ THE HATS AND CYMBALS ARE SAMPLES, as on a real 909 (its cymbals are 6-bit
 * PCM). The machine loads its WAVs per instance; here they are decoded ONCE,
 * at class init, from <module dir>/samples/9w9/, and every pad's sampler
 * borrows them. Without a module directory those four lanes are silent and
 * everything else plays.
 */
#include "9w9/er99_engine.c"

#include "kit_port.h"
#include "../dr32_engine.h"

static const char *const DIST = "Diode (909)|Hard Clip|SAT|BFZ|PDIST|Wavefolder|Bitcrush";

typedef struct {
    const char *key;        /* DR32 key        */
    const char *name, *short_name;
    const char *options;    /* enum, or NULL   */
} n9_row;

/* ⚠ ORDER IS THE PAGE ORDER AND THE STATE ORDER within a lane. */
static const n9_row TUNE  = {"n9_tune",  "Tune",       "TUNE",  NULL};
/* ⭑ The snare's Decay is its TONE knob: the 909 snare has no decay pot, and
 * Tone is the snappy noise's decay time (sd_c_noise_decay) — so it sits on the
 * shared Decay knob rather than being the one lane besides the rim without
 * one. */
static const n9_row DECAY = {"n9_decay", "Decay",      "DECAY", NULL};
static const n9_row DRIVE = {"n9_drive", "Drive",      "DRIVE", NULL};
static const n9_row DISTR = {"n9_dist",  "Distortion", "DIST",  DIST};
/* The machine's MASTER velocity depth, here per pad. */
static const n9_row VEL   = {"n9_vel",   "Velocity",   "VEL",   NULL};

static const n9_row BD_ATTACK = {"n9_bd_attack", "Attack",   "ATTCK", NULL};
static const n9_row BD_PDEPTH = {"n9_bd_pdepth", "P. Depth", "PDPTH", NULL};
static const n9_row BD_PITCH  = {"n9_bd_pitch",  "Pitch",    "PITCH", NULL};
static const n9_row SD_SNAPPY = {"n9_sd_snappy", "Snappy",   "SNAPY", NULL};
static const n9_row LT_ATTACK = {"n9_lt_attack", "Attack",   "ATTCK", NULL};
static const n9_row MT_ATTACK = {"n9_mt_attack", "Attack",   "ATTCK", NULL};
static const n9_row HT_ATTACK = {"n9_ht_attack", "Attack",   "ATTCK", NULL};

#define N9_ROWS 8
typedef struct {
    int         trig;       /* ER99_* trigger          */
    const char *id, *slug, *name;
    int         tune_kind;
    const n9_row *rows[N9_ROWS];
    const char   *ekeys[N9_ROWS];   /* the machine's key per row */
} n9_lane;

/* The machine's page order, which is the picker's. Tune: Hz on the drums, a
 * playback ratio on the samplers — both multiply. The KICK's Tune is its
 * sweep time, not a pitch (its base is fixed at 49 Hz, as on the 909), so a
 * pad's transpose does not move it. */
static const n9_lane LANES[] = {
    {ER99_BD, "bd", "9w9/kick", "Bass Drum", KP_TUNE_NONE,
        {&TUNE, &DECAY, &BD_ATTACK, &BD_PDEPTH, &BD_PITCH, &DRIVE, &DISTR, &VEL},
        {"bd_c_tune", "bd_c_decay", "bd_c_attack", "bd_c_sweep_depth", "bd_c_pitch_mod",
         "bd_c_drive", "bd_c_dist_type", "vel_depth"}},
    {ER99_SD, "sd", "9w9/snare", "Snare", KP_TUNE_RATIO,
        {&TUNE, &DECAY, &SD_SNAPPY, &DRIVE, &DISTR, &VEL},
        {"sd_c_tune", "sd_c_noise_decay", "sd_c_snappy", "sd_c_drive", "sd_c_dist_type", "vel_depth"}},
    {ER99_LT, "lt", "9w9/low_tom", "Low Tom", KP_TUNE_RATIO,
        {&TUNE, &DECAY, &LT_ATTACK, &DRIVE, &DISTR, &VEL},
        {"lt_c_tune", "lt_c_decay", "lt_c_attack", "lt_c_drive", "lt_c_dist_type", "vel_depth"}},
    {ER99_MT, "mt", "9w9/mid_tom", "Mid Tom", KP_TUNE_RATIO,
        {&TUNE, &DECAY, &MT_ATTACK, &DRIVE, &DISTR, &VEL},
        {"mt_c_tune", "mt_c_decay", "mt_c_attack", "mt_c_drive", "mt_c_dist_type", "vel_depth"}},
    {ER99_HT, "ht", "9w9/hi_tom", "Hi Tom", KP_TUNE_RATIO,
        {&TUNE, &DECAY, &HT_ATTACK, &DRIVE, &DISTR, &VEL},
        {"ht_c_tune", "ht_c_decay", "ht_c_attack", "ht_c_drive", "ht_c_dist_type", "vel_depth"}},
    {ER99_RS, "rs", "9w9/rimshot", "Rim Shot", KP_TUNE_RATIO,
        {&TUNE, &DRIVE, &DISTR, &VEL},
        {"rs_tune", "rs_saturation", "rs_dist_type", "vel_depth"}},
    {ER99_HC, "hc", "9w9/clap", "Hand Clap", KP_TUNE_RATIO,
        {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL},
        {"hc_tune", "hc_decay", "hc_drive", "hc_dist_type", "vel_depth"}},
    {ER99_CHH, "chh", "9w9/hat_closed", "Closed Hat", KP_TUNE_RATIO,
        {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL},
        {"chh_pitch", "chh_decay", "chh_drive", "chh_dist_type", "vel_depth"}},
    {ER99_OHH, "ohh", "9w9/hat_open", "Open Hat", KP_TUNE_RATIO,
        {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL},
        {"ohh_pitch", "ohh_decay", "ohh_drive", "ohh_dist_type", "vel_depth"}},
    {ER99_RC, "rc", "9w9/ride", "Ride", KP_TUNE_RATIO,
        {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL},
        {"rc_pitch", "rc_decay", "rc_drive", "rc_dist_type", "vel_depth"}},
    {ER99_CR, "cr", "9w9/crash", "Crash", KP_TUNE_RATIO,
        {&TUNE, &DECAY, &DRIVE, &DISTR, &VEL},
        {"cr_pitch", "cr_decay", "cr_drive", "cr_dist_type", "vel_depth"}},
};
#define NL ((int)(sizeof(LANES) / sizeof(LANES[0])))

static int n9_nrows(const n9_lane *l) {
    int n = 0;
    while (n < N9_ROWS && l->rows[n]) n++;
    return n;
}

/* ---- tables, filled at class init from a default machine ---------------- */

static dr32_eparam      g_params[NL][DR32_ENG_MAX_PARAMS];
static float            g_defaults[NL][DR32_ENG_MAX_PARAMS];
static char             g_names[NL][32];
static char             g_slugs[NL][24];
static dr32_engine_ops  g_ops[NL];
static dr32_model       g_models[NL];
static int              g_tables_done;

/* The shared cymbal PCM: ohh (also the closed hat's), ride, crash. */
static float   *g_pcm[3];
static uint32_t g_pcm_len[3];
static int      g_pcm_tried;

typedef struct {
    er99_engine_t e;        /* the whole machine; one lane of it sounds */
    int     lane;
    float   st;             /* the pad's transpose                       */
    float   tune_base;      /* Tune as the machine holds it, untransposed */
    kp_gate gate;
} n9_pad;

/* ⚠ The machine's samplers index ohh/ride/crash/chh; the chh borrows ohh. */
static void n9_borrow_pcm(er99_engine_t *e) {
    static const int src[ER99_NUM_SAMPLERS] = {0, 1, 2, 0};
    for (int i = 0; i < ER99_NUM_SAMPLERS; i++) {
        e->sampler[i].buffer = g_pcm[src[i]];
        e->sampler[i].length = g_pcm_len[src[i]];
    }
}

/* Tune goes to the machine as an engineering value with the pad's transpose
 * applied to the value the machine last held for it — its exact factory value
 * until the knob moves, so a transpose that returns to 0 returns exactly. */
static void n9_apply_tune(n9_pad *p) {
    const n9_lane *l = &LANES[p->lane];
    er99_engine_set_raw(&p->e, l->ekeys[0], kp_tune(p->tune_base, l->tune_kind, p->st));
}

static void *n9_create_lane(int lane, int sr) {
    n9_pad *p = (n9_pad *)calloc(1, sizeof(n9_pad));
    if (!p) return NULL;
    er99_engine_init(&p->e, (float)sr, NULL);
    n9_borrow_pcm(&p->e);
    p->lane = lane;
    er99_engine_get_raw(&p->e, LANES[lane].ekeys[0], &p->tune_base);
    kp_gate_init(&p->gate, sr);
    return p;
}

#define N9_CREATE(L) static void *n9_create_##L(int sr) { return n9_create_lane(L, sr); }
N9_CREATE(0) N9_CREATE(1) N9_CREATE(2) N9_CREATE(3) N9_CREATE(4) N9_CREATE(5)
N9_CREATE(6) N9_CREATE(7) N9_CREATE(8) N9_CREATE(9) N9_CREATE(10)
static void *(*const n9_create[])(int) = {
    n9_create_0, n9_create_1, n9_create_2, n9_create_3, n9_create_4, n9_create_5,
    n9_create_6, n9_create_7, n9_create_8, n9_create_9, n9_create_10,
};

static void n9_destroy(void *v) {
    n9_pad *p = (n9_pad *)v;
    if (!p) return;
    /* The PCM is borrowed: sample_data is NULL on every pad, so the machine's
     * free releases nothing of it. */
    er99_engine_free(&p->e);
    free(p);
}

static void n9_set(void *v, int idx, float display) {
    n9_pad *p = (n9_pad *)v;
    if (!p) return;
    const n9_lane *l = &LANES[p->lane];
    if (idx < 0 || idx >= n9_nrows(l)) return;
    /* ⚠ A WRITE THAT DOES NOT MOVE THE KNOB CHANGES NOTHING. The machine's
     * defaults are ENGINEERING values (a 13.0 ms kick sweep) and its pot
     * positions are derived from them, so writing a pot back re-quantises the
     * value to the pot's grid — a model load would nudge every default by up
     * to a step. Skipping the no-op write keeps a fresh pad the machine's own
     * factory voice, sample for sample (tests/test_kit_ports.c). */
    float cur = 0.0f;
    const float want = (float)lrintf(display);
    if (er99_engine_get_param(&p->e, l->ekeys[idx], &cur) && lrintf(cur) == (long)want) return;
    er99_engine_set_param(&p->e, l->ekeys[idx], want);
    if (l->rows[idx] == &TUNE) {
        er99_engine_get_raw(&p->e, l->ekeys[idx], &p->tune_base);
        n9_apply_tune(p);
    }
}

static void n9_note_on(void *v, float vel01, float tune_st) {
    n9_pad *p = (n9_pad *)v;
    if (!p) return;
    if (tune_st != p->st) { p->st = tune_st; n9_apply_tune(p); }
    int vel = (int)lrintf(vel01 * 127.0f);
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    er99_engine_trigger(&p->e, (er99_trigger_t)LANES[p->lane].trig, vel);
    kp_gate_hit(&p->gate);
}

/* 9W9 has no choke of its own beyond the hat pair; DR32's ramp is the choke,
 * and the gate stops the lane once it is quiet. */
static void n9_choke(void *v) { (void)v; }

static int n9_render(void *v, float *out, int n) {
    n9_pad *p = (n9_pad *)v;
    if (!p || !p->gate.active) {
        memset(out, 0, sizeof(float) * (size_t)n);
        return 0;
    }
    er99_engine_t *e = &p->e;
    const int t = LANES[p->lane].trig;
    const float vol = e->master.volume;
    for (int i = 0; i < n; i++) {
        /* The machine ticks its noise once per sample for every lane. */
        const float noise = wa_noise_tick(&e->noise);
        float x;
        switch (t) {
            case ER99_BD: case ER99_SD:
                x = er99_bt_render(&e->bt[t], noise); break;
            case ER99_LT: case ER99_MT: case ER99_HT:
                x = er99_tom909_render(&e->tom909[t - ER99_LT], &e->bt[t], noise); break;
            case ER99_RS:  x = er99_rim909_render(&e->rim909, noise); break;
            case ER99_HC:  x = er99_clap909_render(&e->clap909, noise); break;
            case ER99_OHH: x = render_sampler(&e->sampler[ER99_SAMP_OHH]); break;
            case ER99_CHH: x = render_sampler(&e->sampler[ER99_SAMP_CHH]); break;
            case ER99_RC:  x = render_sampler(&e->sampler[ER99_SAMP_RC]); break;
            case ER99_CR:  x = render_sampler(&e->sampler[ER99_SAMP_CR]); break;
            default:       x = 0.0f; break;
        }
        /* Master distortion is Off and the glue at 0 by default, so the
         * machine's master stage is its volume alone. */
        x *= vol;
        if (!(x > -8.0f && x < 8.0f)) x = 0.0f;
        out[i] = x;
        kp_gate_frame(&p->gate, x);
    }
    return kp_gate_end(&p->gate);
}

static void n9_tables(int sr) {
    if (g_tables_done) return;
    /* One default machine, asked for every row's factory value: its defaults
     * are set in engineering units and turned into pot positions at init. */
    er99_engine_t *ref = (er99_engine_t *)calloc(1, sizeof(er99_engine_t));
    if (ref) er99_engine_init(ref, (float)sr, NULL);
    for (int l = 0; l < NL; l++) {
        const n9_lane *ln = &LANES[l];
        const int nr = n9_nrows(ln);
        for (int i = 0; i < nr; i++) {
            const n9_row *r = ln->rows[i];
            float def = 0.0f;
            if (ref) er99_engine_get_param(ref, ln->ekeys[i], &def);
            int nopt = 0;
            if (r->options) {
                nopt = 1;
                for (const char *c = r->options; *c; c++) nopt += (*c == '|');
            }
            dr32_eparam ep = {r->key, r->name, r->short_name, 0.0f,
                              nopt ? (float)(nopt - 1) : 127.0f, (float)lrintf(def),
                              1.0f, NULL, "Voice", r->options};
            g_params[l][i] = ep;
            g_defaults[l][i] = ep.def;
        }
        snprintf(g_names[l], sizeof g_names[l], "9W9 %s", ln->name);
        snprintf(g_slugs[l], sizeof g_slugs[l], "9w9_%s", ln->id);
        dr32_engine_ops o = {DR32_ENG_9W9_BASE + l, g_slugs[l], g_names[l], "n9_", nr,
                             g_params[l], n9_create[l], n9_destroy, n9_set, n9_note_on,
                             n9_choke, n9_render, DR32_FAM_9W9};
        g_ops[l] = o;
        dr32_model m = {ln->slug, ln->name, DR32_ENG_9W9_BASE + l, g_defaults[l], 0.0f, 0.0f};
        g_models[l] = m;
    }
    if (ref) { er99_engine_free(ref); free(ref); }
    g_tables_done = 1;
}

/* The tables are needed to LIST the engines (the picker, the UI generator)
 * before any class init, so they are built on first ask — at the machine's
 * own rate, which is what its pot mapping assumes. */
#define N9_TABLE_SR 44100

const dr32_engine_ops *dr32_9w9_engine(int lane, int *count) {
    n9_tables(N9_TABLE_SR);
    if (count) *count = NL;
    return (lane >= 0 && lane < NL) ? &g_ops[lane] : NULL;
}

const dr32_model *dr32_9w9_models(int *count) {
    n9_tables(N9_TABLE_SR);
    if (count) *count = NL;
    return g_models;
}

void dr32_9w9_class_init(int sample_rate) {
    n9_tables(sample_rate);
    if (g_pcm_tried) return;
    /* Latched only once there is somewhere to look: an init that ran before
     * the module dir was known must not stop a later one finding the WAVs. */
    const char *dir = dr32_engines_module_dir();
    if (!dir) return;
    g_pcm_tried = 1;
    static const char *const files[3] = {"hh.wav", "ride.wav", "crash.wav"};
    for (int i = 0; i < 3; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/samples/9w9/%s", dir, files[i]);
        g_pcm[i] = load_wav_mono(path, &g_pcm_len[i]);
    }
}
