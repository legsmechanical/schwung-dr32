#include "dr32_kit.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      /* strcasecmp */
#include <sys/stat.h>    /* the sample-decode memo — see dr32_kit_load_sample */

void dr32_kit_init(dr32_kit *k) {
    memset(k, 0, sizeof(*k));
    for (int i = 0; i < 128; i++) k->note_to_pad[i] = -1;
    for (int i = 0; i < DR32_PADS; i++) {
        dr32_pad_defaults(&k->pads[i].params);
        k->pads[i].model = -1;
        k->pads[i].note = DR32_FIRST_NOTE + i;
        k->note_to_pad[DR32_FIRST_NOTE + i] = (signed char)i;
    }
    k->master_gain = 1.0f;
    k->ui_current_pad = 0;
    k->ui_auto_select_pad = 1;      // playing a pad focuses it, as mrdrums does
    k->live_armed = 0;
    k->live_arm_block = 0;
    k->last_hit_pad = -1;
    k->last_hit_block = 0;
}

void dr32_kit_reset(dr32_kit *k) {
    dr32_kit_all_off(k);
    for (int i = 0; i < DR32_PADS; i++) {
        dr32_kit_load_sample(k, i, NULL);          /* retires a sample or an engine */
        dr32_pad_defaults(&k->pads[i].params);
        dr32_kit_set_note(k, i, DR32_FIRST_NOTE + i);
        memset(&k->pads[i].wide, 0, sizeof(k->pads[i].wide));
    }
    k->master_gain = 1.0f;
    k->link_all = 0;
    k->link_sub[0] = '\0';
    k->ui_current_pad = 0;
}

/* ---------- folder browse ------------------------------------------------
 *
 * The file browser hands DR32 a finished path and nothing else, so walking the
 * samples beside the current one means reading the directory here. Host thread
 * only: this opens directories and dr32_kit_browse_select() loads a WAV, both
 * of which dr32_kit_load_sample() already does from the same thread.
 *
 * One directory is cached at a time. Switching pads inside a kit almost always
 * stays in one folder, so the scan is rare; a scan on every knob step would be
 * pointless work, and scanning the whole user tree is out of the question at
 * ~3.8 GB. */

static void browse_free(dr32_kit *k) {
    for (int i = 0; i < k->browse_n; i++) free(k->browse[i]);
    free(k->browse);
    k->browse = NULL;
    k->browse_n = 0;
    k->browse_dir[0] = '\0';
}

static int is_audio_name(const char *n) {
    const char *d = strrchr(n, '.');
    if (!d) return 0;
    return !strcasecmp(d, ".wav") || !strcasecmp(d, ".aif") || !strcasecmp(d, ".aiff");
}

static int cmp_name(const void *a, const void *b) {
    /* Case-insensitive so the order matches what a browser shows. */
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

/** Directory part of `path`. Returns 0 when there is none. */
static int split_dir(const char *path, char *out, size_t cap) {
    if (!path || !path[0]) return 0;
    const char *s = strrchr(path, '/');
    if (!s || s == path) return 0;
    size_t n = (size_t)(s - path);
    if (n >= cap) return 0;
    memcpy(out, path, n);
    out[n] = '\0';
    return 1;
}

static const char *base_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/** Point the cache at the folder holding `pad`'s sample. Returns entry count. */
static int browse_ensure(dr32_kit *k, int pad) {
    if (pad < 0 || pad >= DR32_PADS) return 0;
    char dir[DR32_MAX_PATH];
    if (!split_dir(k->pads[pad].path, dir, sizeof(dir))) { browse_free(k); return 0; }
    if (k->browse && !strcmp(k->browse_dir, dir)) return k->browse_n;   /* cache hit */

    browse_free(k);
    DIR *d = opendir(dir);
    if (!d) return 0;
    char **v = (char **)calloc(DR32_BROWSE_MAX, sizeof(char *));
    if (!v) { closedir(d); return 0; }
    int n = 0;
    struct dirent *e;
    while (n < DR32_BROWSE_MAX && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;          /* dotfiles and . / .. */
        if (!is_audio_name(e->d_name)) continue;
        char *dup = (char *)malloc(strlen(e->d_name) + 1);
        if (!dup) break;
        strcpy(dup, e->d_name);
        v[n++] = dup;
    }
    closedir(d);
    qsort(v, (size_t)n, sizeof(char *), cmp_name);
    k->browse = v;
    k->browse_n = n;
    snprintf(k->browse_dir, sizeof(k->browse_dir), "%s", dir);
    return n;
}

int dr32_kit_browse_count(dr32_kit *k, int pad) {
    return k ? browse_ensure(k, pad) : 0;
}

int dr32_kit_browse_index(dr32_kit *k, int pad) {
    if (!k) return -1;
    int n = browse_ensure(k, pad);
    if (n <= 0) return -1;
    const char *cur = base_name(k->pads[pad].path);
    for (int i = 0; i < n; i++) if (!strcmp(k->browse[i], cur)) return i;
    return -1;
}

int dr32_kit_browse_step(dr32_kit *k, int pad, int wire) {
    if (!k || pad < 0 || pad >= DR32_PADS) return -1;
    int delta = wire - k->browse_wire[pad];
    k->browse_wire[pad] = wire;                 /* echoed back verbatim; see the header */
    if (!delta) return dr32_kit_browse_index(k, pad);
    int cur = dr32_kit_browse_index(k, pad);
    return dr32_kit_browse_select(k, pad, (cur < 0 ? 0 : cur) + delta);
}

int dr32_kit_browse_select(dr32_kit *k, int pad, int idx) {
    if (!k) return -1;
    int n = browse_ensure(k, pad);
    if (n <= 0) return -1;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    if (idx == dr32_kit_browse_index(k, pad)) return idx;   /* already there */

    /* Build the full path against the CACHED directory, not the pad's current
     * one: load_sample is about to overwrite that path. */
    char full[DR32_MAX_PATH];
    int w = snprintf(full, sizeof(full), "%s/%s", k->browse_dir, k->browse[idx]);
    if (w <= 0 || (size_t)w >= sizeof(full)) return -1;

    /* The directory string survives the load (load_sample rewrites pad->path,
     * and browse_dir is a separate buffer), so the cache stays valid and the
     * next step does not rescan. */
    dr32_kit_load_sample(k, pad, full);
    return idx;
}

void dr32_kit_free(dr32_kit *k) {
    browse_free(k);
    for (int i = 0; i < DR32_PADS; i++) {
        free(k->pads[i].sample);
        free(k->pads[i].retired);
        k->pads[i].sample = k->pads[i].retired = NULL;
        k->pads[i].frames = 0;
        dr32_pad_slot *s = &k->pads[i];
        if (s->eops && s->eng) s->eops->destroy(s->eng);
        if (s->eops_retired && s->eng_retired) s->eops_retired->destroy(s->eng_retired);
        s->eng = s->eng_retired = NULL;
        s->eops = s->eops_retired = NULL;
        s->engine = 0;
        s->model = -1;
    }
}

/* ---------- synth engines ------------------------------------------------ */

/* Same retire discipline as a sample buffer: the audio thread checks
 * `synth.active` before touching `eng`, so stop the voice first, and destroy
 * the DISPLACED instance only on the next switch. */
static void retire_engine(dr32_pad_slot *s) {
    s->synth.active = 0;
    if (s->eops_retired && s->eng_retired) s->eops_retired->destroy(s->eng_retired);
    s->eng_retired = s->eng;
    s->eops_retired = s->eops;
    s->eng = NULL;
    s->eops = NULL;
    s->engine = 0;
    s->model = -1;
}

void dr32_kit_drop_engine(dr32_kit *k, int pad) {
    if (!k || pad < 0 || pad >= DR32_PADS) return;
    if (k->pads[pad].engine) retire_engine(&k->pads[pad]);
}

int dr32_pad_sounding(const dr32_pad_slot *s) {
    return s->engine ? s->synth.active : s->voice.active;
}

const char *dr32_pad_model_name(const dr32_pad_slot *s) {
    const dr32_model *m = s->engine ? dr32_model_at(s->model) : NULL;
    return m ? m->name : "";
}

int dr32_kit_set_model(dr32_kit *k, int pad, const char *slug) {
    if (!k || pad < 0 || pad >= DR32_PADS) return 0;
    int mi = dr32_model_find(slug);
    const dr32_model *m = dr32_model_at(mi);
    const dr32_engine_ops *e = m ? dr32_engine_get(m->engine) : NULL;
    if (!e) return 0;

    /* The shared tables (URCHIN's sine tables, SIMIAN's cymbal) are filled on
     * the FIRST model anyone picks, not at create_instance: that is on the SPI
     * callback too, and an instance that never plays a synth never pays it. */
    dr32_engines_init((int)DR32_SR);
    void *inst = e->create((int)DR32_SR);
    if (!inst) return 0;

    /* One kind of voice per pad: the sample goes. load_sample(NULL) also
     * retires any previous engine, which is what makes the swap below safe. */
    dr32_kit_load_sample(k, pad, NULL);
    dr32_pad_slot *s = &k->pads[pad];
    s->eops = e;
    s->eng = inst;
    s->engine = m->engine;
    s->model = mi;
    memset(&s->synth, 0, sizeof(s->synth));
    for (int i = 0; i < e->nparams; i++) {
        s->eparam[i] = m->values[i];
        e->set(inst, i, m->values[i]);
    }

    /* The model's own level and position become the pad's. Velocity is the
     * ENGINE's — SIMIAN's Vel Gain, URCHIN's strike energy — so DR32's Vel Vol
     * starts at 0 rather than stacking a second law on top; it is still there
     * to turn. The pad's note, choke group, sends and tune are left alone:
     * they are where the pad sits in the kit, not what it sounds like. So is Wide. */
    s->params.volume_db = m->volume_db;
    s->params.pan = m->pan;
    s->params.vel_to_volume = 0.0f;
    return 1;
}

/* Note-on for a synth pad: DR32's stage, laid down exactly as
 * dr32_voice_start lays down a sample's (dB volume, the dB velocity law,
 * speaker, equal-power pan), then the engine's own hit. */
static void synth_start(dr32_pad_slot *s, int velocity) {
    const dr32_pad *p = &s->params;
    dr32_synth *v = &s->synth;
    v->amp = powf(10.0f, p->volume_db / 20.0f) * dr32_velocity_gain(velocity, p->vel_to_volume);
    if (!p->speaker_on) v->amp = 0.0f;
    dr32_pan_gains(p->pan, &v->panl, &v->panr);
    v->choke_gain = 1.0f;
    v->choke_mul = 1.0f;
    float vel01 = (float)velocity / 127.0f;
    s->eops->note_on(s->eng, vel01 < 0 ? 0 : (vel01 > 1 ? 1 : vel01),
                     p->transpose + p->detune / 100.0f);
    v->active = 1;
}

/* The sample voice's choke, applied to a synth pad: the same 3 ms exponential
 * (dr32_voice.c CHOKE_SECONDS, DR32_DECAY_DB_PER_TIME), so a choke group cuts
 * a synth hat exactly as sharply as a sampled one. */
#define SYNTH_CHOKE_SECONDS 0.003f
#define SYNTH_CHOKE_FLOOR   1e-5f
static void synth_choke(dr32_pad_slot *s) {
    if (!s->synth.active) return;
    s->eops->choke(s->eng);
    s->synth.choke_mul = powf(10.0f, -DR32_DECAY_DB_PER_TIME / (20.0f * SYNTH_CHOKE_SECONDS * DR32_SR));
}

/* Engine -> mono -> DR32's stage -> ACCUMULATED into `out`, like
 * dr32_voice_render. A choked pad keeps computing, muted, until the engine's
 * own silence gate stops it: freezing it instead would leave a ringing model
 * to resume under the next hit. */
static void synth_render(dr32_kit *k, dr32_pad_slot *s, float *out, int frames) {
    float *m = k->eng_mono;
    dr32_synth *v = &s->synth;
    int alive = s->eops->render(s->eng, m, frames);

    /* ⭑ FILTER HOOK POINT — see dr32_synth in dr32_kit.h. Nothing runs here
     * today, by decision, not omission. */

    float gl = v->amp * v->panl, gr = v->amp * v->panr;
    if (v->choke_mul < 1.0f) {
        for (int i = 0; i < frames; i++) {
            float x = m[i] * v->choke_gain;
            out[2 * i]     += x * gl;
            out[2 * i + 1] += x * gr;
            v->choke_gain *= v->choke_mul;
        }
        if (v->choke_gain < SYNTH_CHOKE_FLOOR) { v->choke_gain = 0.0f; gl = gr = 0.0f; }
    } else if (v->amp > 0.0f) {
        for (int i = 0; i < frames; i++) {
            out[2 * i]     += m[i] * gl;
            out[2 * i + 1] += m[i] * gr;
        }
    }
    if (!alive) v->active = 0;
}

/* The pad's own source, sample or synth, ACCUMULATED into `out`. */
static void source_render(dr32_kit *k, dr32_pad_slot *s, float *out, int frames) {
    if (s->engine) synth_render(k, s, out, frames);
    else dr32_voice_render(&s->voice, out, frames);
}

/*
 * WIDE — a complementary-comb widener (Lauridsen's), one pad, in place.
 *
 *     side = g * HP(mid)(t - 8 ms)        mid = (L + R) / 2,  g = Wide / 100
 *
 * (WMODE Comb. The other mode, Haas, is haas_run below. A negative Wide
 * flips the side's sign: the same width, the comb teeth mirrored.)
 *     L += side        R -= side
 *
 * Why this and not the Haas delay it replaced (Josh, 2026-09-22, hearing the
 * Haas version: "the lean ... seems more intense now"): a one-sided delay
 * moves the image toward the side that arrives first, at every setting —
 * the precedence effect. Here both sides are treated alike and opposite, so
 * the image stays where Pan put it and only widens. And L + R is untouched —
 * the added copy cancels — so a mono sum is exactly the dry pad.
 *
 * The crossover: only the mid's HIGH band (above Wide Freq, 24 dB, two
 * Butterworth high-passes) is added, so nothing below it widens; the low end
 * needs no split at all, since nothing is added there. 20 Hz = full band.
 * Width is linear in g: the side is 20*log10(g) dB under the mid, so 1% is
 * -40 dB (a hint) and 100% is a full-depth comb.
 *
 * Andy Simper's trapezoidal SVF, Q = 1/sqrt(2).
 */
static inline float svf_hp(const dr32_wide *d, float st[2], float v0) {
    float v3 = v0 - st[1];
    float v1 = d->a1 * st[0] + d->a2 * v3;
    float v2 = st[1] + d->a2 * st[0] + d->a3 * v3;
    st[0] = 2.0f * v1 - st[0];
    st[1] = 2.0f * v2 - st[1];
    return v0 - d->k * v1 - v2;
}

static void haas_run(dr32_wide *d, const dr32_pad *p, float *x, int frames);
static void disperse_run(dr32_wide *d, const dr32_pad *p, float *x, int frames);
static void comb_run(dr32_wide *d, const dr32_pad *p, float pct, float *x, int frames);

static void wide_run(dr32_wide *d, const dr32_pad *p, float *x, int frames) {
    /* A mode change starts the stage clean — the two modes share `buf`. */
    if (d->mode != p->wide_mode) {
        memset(d->buf, 0, sizeof(d->buf));
        memset(d->s, 0, sizeof(d->s));
        memset(d->hs, 0, sizeof(d->hs));
        memset(d->dap, 0, sizeof(d->dap));
        memset(d->dhp, 0, sizeof(d->dhp));
        d->mode = p->wide_mode;
    }
    float pct = p->wide_pct;
    if (pct < -100.0f) pct = -100.0f;
    if (pct > 100.0f) pct = 100.0f;
    float side_g;                    /* the side's level: what COMP answers to */
    if (p->wide_mode == 1) { haas_run(d, p, x, frames); side_g = 0.0f; }
    else if (p->wide_mode == 2) {
        disperse_run(d, p, x, frames);
        const float W = 2.0f * fabsf(pct);
        side_g = W < 100.0f ? W * 0.01f : 1.0f;
    } else {
        comb_run(d, p, pct, x, frames);
        side_g = fabsf(pct) * 0.01f;
    }
    /*
     * COMP (Josh: "let's add comp"). A mid/side widener ADDS the side to
     * each ear, so each ear's energy grows by (1 + g^2): +3 dB at full. On,
     * the pad is trimmed by 1/sqrt(1 + g^2) so its stereo loudness holds as
     * it widens — at the price of the MONO sum, which drops by the same
     * amount (off, the mono sum is exactly the dry pad, as in Wider). Haas
     * adds no energy (one ear is only delayed), so it is never trimmed.
     */
    if (p->wide_comp && side_g > 0.0f) {
        const float c = 1.0f / sqrtf(1.0f + side_g * side_g);
        for (int i = 0; i < 2 * frames; i++) x[i] *= c;
    }
}

/* COMB: side = g * HP(mid) TIME late (Auto: 8 ms). */
static void comb_run(dr32_wide *d, const dr32_pad *p, float pct, float *x, int frames) {
    const float g = pct * 0.01f;   /* signed: - mirrors which side gets which comb teeth */
    const float ms = p->wide_time > 0.0f ? p->wide_time : DR32_WIDE_MS;
    const int dly = (int)(ms * 0.001f * DR32_SR + 0.5f);          /* <= 529 */
    const int hp = p->wide_hz > 20.5f;
    if (hp && d->hz != p->wide_hz) {
        const float t = tanf(3.14159265f * p->wide_hz / DR32_SR);
        d->k = 1.41421356f;
        d->a1 = 1.0f / (1.0f + t * (t + d->k));
        d->a2 = t * d->a1;
        d->a3 = t * d->a2;
        d->hz = p->wide_hz;
    }
    for (int i = 0; i < frames; i++) {
        float m = 0.5f * (x[2 * i] + x[2 * i + 1]);
        if (hp) m = svf_hp(d, d->s[1], svf_hp(d, d->s[0], m));
        d->buf[d->w] = m;
        const float side = g * d->buf[(d->w - dly) & (DR32_WIDE_BUF - 1)];
        d->w = (d->w + 1) & (DR32_WIDE_BUF - 1);
        x[2 * i]     += side;
        x[2 * i + 1] -= side;
    }
}

/*
 * HAAS — the other Wide mode (WMODE; Josh: "I like both, and can see the use
 * in each depending on context"). One side's high band is delayed by
 * 15 ms x (Wide/100)^2 — + the RIGHT side, - the LEFT — so the image LEANS
 * toward the side that arrives first (the precedence effect): that lean is
 * this mode's character, and the sign is how a kit's leans are balanced.
 * Above Wide Freq only, each channel split LR4 so the two low bands stay in
 * phase. The curve: width comes on between 0 and ~3 ms; the 15 ms cap is
 * where a drum transient stops fusing and reads as a flam. With TIME set, a
 * plugin's layout instead: TIME the delay, |WIDE| the delayed side's MIX —
 * less of it blends the dry back in on that side, easing lean and width
 * together. (True time-intensity trading, a LOUDER late side, is not here.)
 */
static inline void svf_split(const dr32_wide *d, float st[2], float v0, float *lp, float *hp) {
    float v3 = v0 - st[1];
    float v1 = d->a1 * st[0] + d->a2 * v3;
    float v2 = st[1] + d->a2 * st[0] + d->a3 * v3;
    st[0] = 2.0f * v1 - st[0];
    st[1] = 2.0f * v2 - st[1];
    *lp = v2;
    *hp = v0 - d->k * v1 - v2;
}

static void haas_run(dr32_wide *d, const dr32_pad *p, float *x, int frames) {
    float pct = p->wide_pct;
    if (pct < -100.0f) pct = -100.0f;
    if (pct > 100.0f) pct = 100.0f;
    const float a = pct * 0.01f;
    /* TIME set: a standard Haas plugin's layout (Josh: "let's do that for
     * haas") — TIME is the delay, |WIDE| the MIX on the delayed side (dry at
     * 0, fully delayed at 100). Auto (TIME 0): the curve, fully delayed. */
    const int manual = p->wide_time > 0.0f;
    const float ms = manual ? p->wide_time : DR32_WIDE_HAAS_MS_MAX * a * a;
    const float mix = manual ? fabsf(a) : 1.0f;
    /* LATE: the delayed side's level above the crossover (Josh: "No
     * delayed-side level ... let's try this"). Up counters the lean — the
     * precedence effect traded against intensity — down deepens it. */
    const float late_g = powf(10.0f, p->wide_late_db * 0.05f);
    const int dch = a < 0.0f ? 0 : 1;          /* + delays the RIGHT side, - the LEFT */
    if (dch != d->haas_side) {                 /* the other side now: start clean */
        memset(d->buf, 0, sizeof(d->buf));
        d->haas_side = dch;
    }
    const int dly = (int)(ms * 0.001f * DR32_SR + 0.5f);          /* <= 662 */
    const int split = p->wide_hz > 20.5f;
    if (split && d->hz != p->wide_hz) {
        const float t = tanf(3.14159265f * p->wide_hz / DR32_SR);
        d->k = 1.41421356f;
        d->a1 = 1.0f / (1.0f + t * (t + d->k));
        d->a2 = t * d->a1;
        d->a3 = t * d->a2;
        d->hz = p->wide_hz;
    }
    for (int i = 0; i < frames; i++) {
        float y[2];
        for (int c = 0; c < 2; c++) {
            const float v = x[2 * i + c];
            float lo = 0.0f, hi = v;
            if (split) {
                float l1, h1, l2, h2, dump;
                svf_split(d, d->hs[c][0], v, &l1, &h1);
                svf_split(d, d->hs[c][1], l1, &l2, &dump);
                svf_split(d, d->hs[c][2], h1, &dump, &h2);
                lo = l2; hi = h2;
            }
            if (c == dch) {
                d->buf[d->w] = hi;
                const float late = d->buf[(d->w - dly) & (DR32_WIDE_BUF - 1)];
                hi = (mix >= 1.0f ? late : hi + mix * (late - hi)) * late_g;
                d->w = (d->w + 1) & (DR32_WIDE_BUF - 1);
            }
            y[c] = lo + hi;
        }
        x[2 * i] = y[0];
        x[2 * i + 1] = y[1];
    }
}

/*
 * DISPERSE — the third Wide mode: Polyverse's Wider, MEASURED.
 *
 * Josh: "try to get as close to wider as we can". Not from its code: from
 * its OUTPUT. Josh rendered a single-sample click through Wider in Ableton at
 * Width 0-200%, with a left-only input and with Low Bypass at 200 Hz
 * (32-bit float, 44.1 kHz), and the responses were fitted. What came out,
 * every part to within a fraction of a percent:
 *
 *     L_out = L + F(L)        R_out = R - F(R)      (each channel on its own)
 *     F     = g * AP5( delay_D( HP(x) ) )
 *     g     = min(W / 100, 1)          -12 dB at 25%, -6 at 50, 0 dB from 100 on
 *     D     = 0.0300 ms * W            3 ms at 100%, 6 ms at 200%: the second
 *                                      half of the knob only LENGTHENS the delay
 *     AP5   = five first-order all-passes, FIXED whatever W is (the table)
 *     HP    = Low Bypass: a 24 dB Linkwitz-Riley high-pass (our WFREQ)
 *
 * For a mono pad that is the M/S shape Comb has (side = F(mid), mono sum
 * exact); for a stereo sample each side widens from itself, as Wider does.
 * The delay makes the comb and the all-passes smear it — the manual's "a
 * specialized array of all-pass and comb filters", exactly.
 *
 * DR32's WIDE (-100..+100) spans Wider's 0-200%: |WIDE| x 2. The sign mirrors
 * (which side gets +F). Not reproduced: a small treble droop that varies with
 * W (-0.3 dB at 8 kHz, a few dB at 17-20 kHz), which reads as Wider's own
 * fractional-delay interpolation; here the delay is cubic-interpolated.
 * The fit and its method: memory `widening-module-goal`, DR32 CLAUDE.md.
 */
static const float DISPERSE_A[DR32_WIDE_AP_STAGES] = {
    /* (a + z^-1) / (1 + a z^-1); corners 4.4, 41.5, 232, 1281 Hz and one near
     * Nyquist (a > 0), which is what makes the early taps alternate. */
    -0.999374f, -0.994100f, -0.967481f, -0.832310f, 0.816475f,
};
#define DISPERSE_MS_PER_PCT 0.0300f
#define DISPERSE_RING 1024          /* per channel; TIME's 12 ms is 529 frames */

static void disperse_run(dr32_wide *d, const dr32_pad *p, float *x, int frames) {
    float pct = p->wide_pct;
    if (pct < -100.0f) pct = -100.0f;
    if (pct > 100.0f) pct = 100.0f;
    const float W = 2.0f * fabsf(pct);                    /* Wider's 0..200 % */
    const float g = (W < 100.0f ? W * 0.01f : 1.0f) * (pct < 0.0f ? -1.0f : 1.0f);
    /* TIME overrides the delay (Auto, 0: Wider's own law, tied to W). */
    const float ms = p->wide_time > 0.0f ? p->wide_time : DISPERSE_MS_PER_PCT * W;
    const float D = ms * 0.001f * DR32_SR;                         /* frames, fractional */
    const int   Di = (int)D;
    const float Df = D - (float)Di;
    const int hp = p->wide_hz > 20.5f;
    if (hp && d->hz != p->wide_hz) {
        const float t = tanf(3.14159265f * p->wide_hz / DR32_SR);
        d->k = 1.41421356f;
        d->a1 = 1.0f / (1.0f + t * (t + d->k));
        d->a2 = t * d->a1;
        d->a3 = t * d->a2;
        d->hz = p->wide_hz;
    }
    for (int i = 0; i < frames; i++) {
        for (int c = 0; c < 2; c++) {
            float v = x[2 * i + c];
            if (hp) v = svf_hp(d, d->dhp[c][1], svf_hp(d, d->dhp[c][0], v));
            float *ring = d->buf + c * DISPERSE_RING;
            ring[d->w] = v;
            /* 4-point cubic (Catmull-Rom) between the frames either side of
             * the fractional delay: straight-line interpolation darkened the
             * top 1-3 dB more than Wider does. */
            const float ym = ring[(d->w - Di + 1) & (DISPERSE_RING - 1)];
            const float y0 = ring[(d->w - Di) & (DISPERSE_RING - 1)];
            const float y1 = ring[(d->w - Di - 1) & (DISPERSE_RING - 1)];
            const float y2 = ring[(d->w - Di - 2) & (DISPERSE_RING - 1)];
            const float c1 = 0.5f * (y1 - ym);
            const float c2 = ym - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym) + 1.5f * (y0 - y1);
            float y = ((c3 * Df + c2) * Df + c1) * Df + y0;
            for (int j = 0; j < DR32_WIDE_AP_STAGES; j++) {   /* first-order all-passes */
                const float a = DISPERSE_A[j];
                const float o = a * y + d->dap[c][j];
                d->dap[c][j] = y - a * o;
                y = o;
            }
            x[2 * i + c] += c ? -g * y : g * y;
        }
        d->w = (d->w + 1) & (DISPERSE_RING - 1);
    }
}

/* One pad into `out`, whichever kind it is. The single dispatch both render
 * paths go through, so they cannot disagree about what a pad is.
 *
 * ⚠ Wide at 0 with no tail left takes the ORIGINAL path, untouched: a pad
 * that does not use Wide renders bit for bit as it did before Wide existed. */
static void pad_render(dr32_kit *k, dr32_pad_slot *s, float *out, int frames) {
    dr32_wide *d = &s->wide;
    const int sounding = dr32_pad_sounding(s);
    if (s->params.wide_pct == 0.0f && d->tail <= 0) {
        if (sounding) source_render(k, s, out, frames);
        return;
    }
    float *t = k->wide_tmp;
    memset(t, 0, sizeof(float) * 2 * (size_t)frames);
    if (sounding) {
        source_render(k, s, t, frames);
        d->tail = DR32_WIDE_BUF;           /* delay + the filters' settling */
    } else {
        d->tail -= frames;
    }
    wide_run(d, &s->params, t, frames);
    for (int i = 0; i < 2 * frames; i++) out[i] += t[i];
    if (d->tail <= 0) {                    /* flushed: the next hit starts clean */
        memset(d, 0, sizeof(*d));
    }
}

/* Does this pad need rendering this block? Sounding, or Wide still has the
 * delayed side's last few ms to play out. BOTH render paths ask this. */
static int pad_live(const dr32_pad_slot *s) {
    return dr32_pad_sounding(s) || s->wide.tail > 0;
}

void dr32_kit_set_note(dr32_kit *k, int pad, int note) {
    if (pad < 0 || pad >= DR32_PADS || note < 0 || note > 127) return;
    int old = k->pads[pad].note;
    if (old >= 0 && old < 128 && k->note_to_pad[old] == pad) k->note_to_pad[old] = -1;
    k->pads[pad].note = note;
    k->note_to_pad[note] = (signed char)pad;
}

/* Is `path` byte-for-byte what this pad already holds?
 *
 * A kit recall re-reads all 16 pads, and the WAV decode was ~3.5 ms of it —
 * ON THE SPI CALLBACK, against a 2.9 ms block period (measured on device
 * 2026-09-08 via param-slow). Recalling the same kit decodes 16 identical
 * files every time.
 *
 * ⚠ THIS IS DELIBERATELY NOT "skip the reload when the kit path matches".
 * dr32_state_read loads the kit FIRST precisely because it replaces every pad
 * wholesale, and the saved blob carries only the user's DELTAS from that
 * baseline; skipping the reload would apply those deltas to whatever the user
 * had edited since, turning a restore into a merge. The memo is at the DECODE,
 * which no semantics depend on: pad params still reset from the kit JSON, the
 * pad is still "replaced", the bytes are simply not read twice.
 *
 * Size+mtime, not path alone, so editing a sample in place still reloads it —
 * otherwise "reload the kit" would be the one gesture that could not pick up
 * an edited file. One stat per pad is microseconds against a full decode. */
static int sample_is_current(const dr32_pad_slot *s, const char *path) {
    if (!s->sample || !s->path[0]) return 0;
    if (strcmp(s->path, path) != 0) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;        /* gone or unreadable → reload */
    return (long)st.st_size == s->src_size && (long)st.st_mtime == s->src_mtime;
}

int dr32_kit_load_sample(dr32_kit *k, int pad, const char *path) {
    if (pad < 0 || pad >= DR32_PADS) return DR32_WAV_ERR_OPEN;
    dr32_pad_slot *s = &k->pads[pad];

    /* Already holding exactly this file — keep the decoded buffer. Returns
     * before the retire below, so the audio thread's pointer is untouched and
     * no voice is silenced: re-decoding identical audio was the only thing
     * being skipped.
     *
     * ⚠⚠ AND `set_param` IS THE HOST'S SPI AUDIO CALLBACK, not a control
     * thread — the comment at the `kit` case in dr32.c claimed otherwise for
     * months. A WAV read here blocks the audio budget directly, and a state
     * restore re-asserts every pad's path: measured by Charles at 18.9 ms warm
     * and 111 ms cold against a ~2.9 ms block, an audible click on every
     * Shift+Delete (PR #2).
     *
     * ⭑ This guard compares size+mtime through sample_is_current, not the path
     * alone, so a sample EDITED in place still reloads. PR #2 proposed the
     * path-only form; this one subsumes it. */
    if (path && path[0] && sample_is_current(s, path)) return DR32_WAV_OK;

    // Silence the pad first: the audio thread checks `active` before touching
    // `sample`, so stopping the voice before the swap means it cannot be mid-read
    // on the buffer we're about to replace.
    s->voice.active = 0;
    // A pad is a sample pad OR a synth pad. Anything that loads (or clears) a
    // sample makes it a sample pad again.
    if (s->engine) retire_engine(s);

    // One-deep retire. The buffer we displace now is freed on the NEXT load of
    // this pad — by which time many audio blocks have passed. Freeing it here
    // would race with a render that had already loaded the old pointer.
    free(s->retired);
    s->retired = s->sample;
    s->sample = NULL;
    s->frames = 0;
    s->path[0] = '\0';
    /* Clear the decode stamp with the buffer it describes. sample_is_current
     * already requires a non-NULL sample, so this is belt-and-braces — but a
     * stamp outliving its buffer is exactly the kind of stale pair that starts
     * matching again by accident after a struct reuse. */
    s->src_size = 0;
    s->src_mtime = 0;

    if (!path || !path[0]) return DR32_WAV_OK;     // clearing the pad

    dr32_wav w;
    int err = dr32_wav_load(path, &w);
    if (err != DR32_WAV_OK) return err;

    s->sample = w.data;
    s->frames = w.frames;
    s->channels = w.channels;
    s->sample_rate = w.sample_rate;
    snprintf(s->path, sizeof(s->path), "%s", path);
    /* Stamp what we just decoded, for sample_is_current above. Stat AFTER the
     * read: a file rewritten between the two then looks stale next time and
     * reloads, which is the safe direction to be wrong in. */
    {
        struct stat st;
        if (stat(path, &st) == 0) { s->src_size = (long)st.st_size; s->src_mtime = (long)st.st_mtime; }
        else                      { s->src_size = 0; s->src_mtime = 0; }
    }
    return DR32_WAV_OK;
}

void dr32_kit_adopt_sample(dr32_kit *k, int pad, float *data, size_t frames,
                           int channels, int sample_rate, const char *path,
                           long size, long mtime) {
    if (!k || pad < 0 || pad >= DR32_PADS) { free(data); return; }
    dr32_pad_slot *s = &k->pads[pad];
    /* The same order as dr32_kit_load_sample: stop the voice before the
     * pointer moves, retire rather than free. */
    s->voice.active = 0;
    if (s->engine) retire_engine(s);
    free(s->retired);
    s->retired = s->sample;
    s->sample = data;
    s->frames = data ? frames : 0;
    s->channels = channels == 2 ? 2 : 1;
    s->sample_rate = sample_rate;
    snprintf(s->path, sizeof(s->path), "%s", data && path ? path : "");
    s->src_size = data ? size : 0;
    s->src_mtime = data ? mtime : 0;
}

void dr32_kit_note_on(dr32_kit *k, int note, int velocity) {
    if (note < 0 || note > 127) return;
    int pad = k->note_to_pad[note];
    if (pad < 0) return;
    /* Focus follows this note if nothing is sequencing (see transport_running),
     * or if a host vouched that a finger caused it. A note alone, with a
     * transport running, cannot: a live hit and a sequenced one are identical
     * here (measured on device), so following every note let playback drag the
     * editor around. The vouch (ui_live_press) supplies the missing bit.
     *
     * Record the hit either way — the press signal may still be in flight, and
     * set_param("ui_live_press") looks back at this. (An earlier attempt to
     * have the host tag the note MOVE_MIDI_SOURCE_PAD reached nothing even with
     * the gate removed; that plumbing was reverted. Don't re-tread it.) */
    k->last_hit_pad = pad;
    k->last_hit_block = k->block;
    k->pad_vel[pad] = velocity;
    k->hit_seq++;
    if (k->ui_auto_select_pad && !k->transport_running && !k->host_vouches) {
        /* Nothing is sequencing, so this note came from a hand. Follow it
         * outright — no vouch needed, on any host. (See transport_running in
         * dr32_kit.h.) A vouch that is still in flight for this same press is
         * consumed too, so it cannot re-arm for the next sequenced note. */
        k->ui_current_pad = pad;
        k->live_armed = 0;
        k->last_hit_pad = -1;
    } else if (k->live_armed && k->ui_auto_select_pad &&
               (k->block - k->live_arm_block) <= DR32_LIVE_MATCH_BLOCKS) {
        k->ui_current_pad = pad;
        k->live_armed = 0;
        k->last_hit_pad = -1;   /* consumed — see the note in dr32_params.c */
    }

    dr32_pad_slot *s = &k->pads[pad];

    // Choke arbitration, per the native DrumChainMidiNode: among note-ons that
    // arrive at the SAME time in the same nonzero group, the HIGHEST incoming
    // MIDI note wins and the lower ones are killed. Sequential hits behave the
    // usual way (the newer note chokes the older).
    //
    // "Same time" is approximated as "same render block", which is the finest
    // grain available to us: the host hands us a block's MIDI before rendering.
    int grp = s->params.choke_group;
    if (grp > 0) {
        for (int i = 0; i < DR32_PADS; i++) {
            if (i == pad) continue;
            dr32_pad_slot *o = &k->pads[i];
            if (o->params.choke_group != grp || !dr32_pad_sounding(o)) continue;
            unsigned ob = o->engine ? o->synth.block : o->voice.block;
            int      on = o->engine ? o->synth.note  : o->voice.note;
            if (ob == k->block && on > note) {
                // A higher note already won this block: the incoming note loses.
                return;
            }
            if (o->engine) synth_choke(o);
            else dr32_voice_choke(&o->voice);
        }
    }

    if (s->engine) {
        synth_start(s, velocity);
        s->synth.note = note;          // arbitration uses the INCOMING note
        s->synth.block = k->block;
        return;
    }
    dr32_voice_start(&s->voice, &s->params, s->sample, s->frames, s->channels,
                     s->sample_rate, velocity);
    s->voice.note = note;              // arbitration uses the INCOMING note
    s->voice.block = k->block;
}

void dr32_kit_note_off(dr32_kit *k, int note) {
    if (note < 0 || note > 127) return;
    int pad = k->note_to_pad[note];
    if (pad < 0) return;
    /* A synth pad ignores note-off: every engine here is percussive, and its
     * hit runs to the end of its own envelope. */
    if (k->pads[pad].engine) return;
    dr32_voice_release(&k->pads[pad].voice, &k->pads[pad].params);
}

void dr32_kit_all_off(dr32_kit *k) {
    for (int i = 0; i < DR32_PADS; i++) {
        dr32_pad_slot *s = &k->pads[i];
        s->voice.active = 0;
        /* ⚠ A synth pad is MUTED, not stopped. Stopping it would freeze a
         * ringing model mid-tail, and that frozen state would resume under the
         * next hit — measured as a quieter, wrong second hit on an URCHIN tom
         * (its energy tracker still full). Muted, it rings out unheard and the
         * engine's own silence gate ends it. */
        if (s->engine && s->synth.active) {
            s->eops->choke(s->eng);
            s->synth.choke_gain = 0.0f;
            s->synth.choke_mul = 0.0f;
        }
    }
}

void dr32_kit_render(dr32_kit *k, float *out, int frames) {
    k->block++;
    if (frames > DR32_KIT_MAX_BLOCK) frames = DR32_KIT_MAX_BLOCK;
    memset(out, 0, sizeof(float) * 2 * (size_t)frames);

    /* Every voice straight into the mix. There is no send detour left: a pad's
     * `send_db[]` is read by the HOST now (voice_send_params, see dr32.c) and
     * applied on its side into its own return buses, so nothing here has to
     * render a pad twice. */
    for (int i = 0; i < DR32_PADS; i++) {
        dr32_pad_slot *s = &k->pads[i];
        if (pad_live(s)) pad_render(k, s, out, frames);
    }

    if (k->master_gain != 1.0f) {
        for (int i = 0; i < 2 * frames; i++) out[i] *= k->master_gain;
    }
}

/* One float sample pair -> the host's int16 destination, ACCUMULATING and
 * saturating. Accumulate because the destinations alias: two pads on one host
 * bus share a pointer and their sum is supposed to happen here. */
static inline void mix_f32_to_i16(int16_t *dst, const float *src, int n) {
    for (int i = 0; i < n; i++) {
        float v = src[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        int32_t sum = (int32_t)dst[i] + (int32_t)(v * 32767.0f);
        if (sum > 32767) sum = 32767;
        if (sum < -32768) sum = -32768;
        dst[i] = (int16_t)sum;
    }
}

void dr32_kit_render_split(dr32_kit *k, int16_t *const *voice_out, int n_voices,
                           int16_t *main_out, int frames) {
    if (!k || !voice_out) return;
    k->block++;
    if (frames > DR32_KIT_MAX_BLOCK) frames = DR32_KIT_MAX_BLOCK;

    /* ⚠ NOTHING THE HOST OWNS IS CLEARED HERE. It cleared the distinct
     * destinations before calling, and clearing one ourselves would wipe
     * another pad's audio out of a buffer they share. `split_dry` is OURS and
     * is zeroed per block exactly as dr32_kit_render zeroes `out`. */
    memset(k->split_dry, 0, sizeof(float) * 2 * (size_t)frames);

    for (int i = 0; i < DR32_PADS; i++) {
        dr32_pad_slot *s = &k->pads[i];
        if (!pad_live(s)) continue;

        int16_t *dst = (i < n_voices && voice_out[i]) ? voice_out[i] : NULL;
        if (dst && dst != main_out) {
            /*
             * ROUTED OUT. Rendered alone into the scratch, because this pad's
             * audio has to leave as int16 into a buffer it may SHARE with
             * another pad — so it is converted and accumulated, never written.
             * Master gain is applied here because this pad will not reach the
             * summing below.
             */
            memset(k->scratch, 0, sizeof(float) * 2 * (size_t)frames);
            pad_render(k, s, k->scratch, frames);
            for (int n = 0; n < 2 * frames; n++) k->scratch[n] *= k->master_gain;
            mix_f32_to_i16(dst, k->scratch, 2 * frames);
        } else {
            /* Stays in the kit: straight into the mix, exactly as
             * dr32_kit_render does it — dr32_voice_render accumulates, and
             * `split_dry` was zeroed once above just like that path's `out`. */
            pad_render(k, s, k->split_dry, frames);
        }
    }

    /* The same single application of master_gain, in the same place, that
     * dr32_kit_render uses. */
    if (k->master_gain != 1.0f) {
        for (int n = 0; n < 2 * frames; n++) k->split_dry[n] *= k->master_gain;
    }
    if (main_out) mix_f32_to_i16(main_out, k->split_dry, 2 * frames);
}

int dr32_kit_active_voices(const dr32_kit *k) {
    int n = 0;
    for (int i = 0; i < DR32_PADS; i++) if (dr32_pad_sounding(&k->pads[i])) n++;
    return n;
}
