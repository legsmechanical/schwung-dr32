// dr32_resample.c — RESAMPLE's render, naming and switch. See dr32_resample.h
// for what is baked and what stays live, and why.

/* pthreads and localtime_r are POSIX, and the build is -std=c11. */
#define _POSIX_C_SOURCE 200809L

#include "dr32_resample.h"
#include "wav.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define RS_BLOCK 128

static inline float db_lin(float db) { return powf(10.0f, db / 20.0f); }

int dr32_rs_is_synth(const dr32_kit *k, int pad) {
    return k && pad >= 0 && pad < DR32_PADS && k->pads[pad].engine && k->pads[pad].eops;
}

int dr32_rs_snapshot(const dr32_kit *k, int pad, int velocity, dr32_rs_src *out) {
    if (!k || pad < 0 || pad >= DR32_PADS || !out) return 0;
    const dr32_pad_slot *s = &k->pads[pad];
    if (!(s->engine ? s->eops != NULL : (s->sample && s->path[0]))) return 0;
    memset(out, 0, sizeof(*out));
    out->pad = pad;
    out->velocity = velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity);
    out->params = s->params;
    out->engine = s->engine;
    out->model = s->model;
    memcpy(out->eparam, s->eparam, sizeof(out->eparam));
    snprintf(out->path, sizeof(out->path), "%s", s->engine ? "" : s->path);
    return 1;
}

/* ---------- the render ---------------------------------------------------- */

typedef struct {
    float  *buf;
    size_t  n, alloc;          /* frames */
    int     ch;
    float   peak;
    size_t  quiet;             /* frames in a row at or under peak * floor */
    float   floor_lin;
} rs_acc;

static int acc_push(rs_acc *a, const float *x, int frames) {
    if (a->n + (size_t)frames > a->alloc) {
        size_t na = a->alloc ? a->alloc * 2 : (size_t)DR32_RS_SR;
        while (na < a->n + (size_t)frames) na *= 2;
        float *nb = (float *)realloc(a->buf, na * (size_t)a->ch * sizeof(float));
        if (!nb) return -1;
        a->buf = nb;
        a->alloc = na;
    }
    memcpy(a->buf + a->n * (size_t)a->ch, x, (size_t)frames * (size_t)a->ch * sizeof(float));
    for (int i = 0; i < frames; i++) {
        float v = fabsf(x[i * a->ch]);
        if (a->ch == 2 && fabsf(x[i * 2 + 1]) > v) v = fabsf(x[i * 2 + 1]);
        if (v > a->peak) a->peak = v;
        /* Silence only counts once the sound has STARTED: a sample that
         * opens with a second of digital zero is not finished (Fable). */
        if (a->peak > 0.0f && v <= a->peak * a->floor_lin) a->quiet++;
        else a->quiet = 0;
    }
    a->n += (size_t)frames;
    return 0;
}

void dr32_rs_take_free(dr32_rs_take *t) {
    if (!t) return;
    free(t->data);
    memset(t, 0, sizeof(*t));
}

int dr32_rs_render(const dr32_rs_src *src, dr32_rs_take *out) {
    return dr32_rs_render_abortable(src, out, NULL);
}

int dr32_rs_render_abortable(const dr32_rs_src *src, dr32_rs_take *out, atomic_int *abort) {
    if (!src || !out) return -1;
    memset(out, 0, sizeof(*out));
    const size_t cap = (size_t)(DR32_RS_MAX_S * DR32_RS_SR);
    const size_t quiet_need = (size_t)(DR32_RS_SILENCE_S * DR32_RS_SR);
    rs_acc a = { 0 };
    a.floor_lin = db_lin(DR32_RS_SILENCE_DB);
    /* What the original hit had ON TOP of what the render captures — the level
     * the match must put back. */
    float level = 1.0f;
    int stopped = 0, err = 0;

    if (src->engine) {
        /* A SYNTH pad: a second instance of its engine, the pad's knob values,
         * one hit — what synth_start does, minus DR32's mix stage. */
        const dr32_engine_ops *e = dr32_engine_get(src->engine);
        if (!e) return -1;
        dr32_engines_init(DR32_RS_SR);          /* idempotent; the live pad already ran it */
        void *inst = e->create(DR32_RS_SR);
        if (!inst) return -1;
        for (int i = 0; i < e->nparams && i < DR32_ENG_MAX_PARAMS; i++) e->set(inst, i, src->eparam[i]);
        float vel01 = (float)src->velocity / 127.0f;
        e->note_on(inst, vel01 > 1.0f ? 1.0f : vel01,
                   src->params.transpose + src->params.detune / 100.0f);
        /* synth_start's amp: Volume and the Vel Vol law. Mute stays live, so
         * a muted pad is still captured at the level it would play. */
        level = db_lin(src->params.volume_db) * dr32_velocity_gain(src->velocity, src->params.vel_to_volume);
        a.ch = 1;
        float m[RS_BLOCK];
        while (a.n < cap) {
            int k = (int)((cap - a.n) < RS_BLOCK ? (cap - a.n) : RS_BLOCK);
            if (abort && atomic_load(abort)) { err = 1; break; }
            int alive = e->render(inst, m, k);
            if (acc_push(&a, m, k)) { err = 1; break; }
            if (!alive || a.quiet >= quiet_need) { stopped = 1; break; }
            if (a.peak <= 0.0f && a.n >= 5 * (size_t)DR32_RS_SR) break;    /* 5 s of nothing: give up */
        }
        e->destroy(inst);
    } else {
        /* A SAMPLE pad: its own decode of the file (the live buffer may be
         * retired by a load while we work), through the pad's own voice with
         * everything that stays live neutralised: Volume 0 dB, Pan centre, Vel
         * Vol off. Gain, the cell volume, the envelope, the filter and the
         * effects are all in the render — they are what gets baked. */
        dr32_wav w;
        if (dr32_wav_load(src->path, &w) != DR32_WAV_OK) return -1;
        dr32_pad p = src->params;
        p.volume_db = 0.0f;
        p.pan = 0.0f;
        p.vel_to_volume = 0.0f;
        p.speaker_on = 1;
        dr32_voice v;
        dr32_voice_start(&v, &p, w.data, w.frames, w.channels, w.sample_rate, src->velocity);
        /* Centre pan is unity to within a float's last bit; fold it in anyway,
         * so the match is exact rather than nearly. */
        float pl, pr;
        dr32_pan_gains(0.0f, &pl, &pr);
        level = db_lin(src->params.volume_db) / pl;
        a.ch = 2;
        float st[2 * RS_BLOCK];
        while (a.n < cap) {
            int k = (int)((cap - a.n) < RS_BLOCK ? (cap - a.n) : RS_BLOCK);
            if (abort && atomic_load(abort)) { err = 1; break; }
            memset(st, 0, sizeof(float) * 2 * (size_t)k);
            int alive = dr32_voice_render(&v, st, k);
            if (acc_push(&a, st, k)) { err = 1; break; }
            if (!alive || a.quiet >= quiet_need) { stopped = 1; break; }
            if (a.peak <= 0.0f && a.n >= 5 * (size_t)DR32_RS_SR) break;    /* 5 s of nothing: give up */
        }
        dr32_wav_free(&w);
    }
    if (err || a.peak <= 0.0f || !a.buf) { free(a.buf); return -1; }

    /* The END. Three cases, each so the take is the sound and nothing else:
     *   - it fell under the floor: keep 5 ms past the last frame above it and
     *     fade over those — they are all under -80 dB, so the fade touches
     *     nothing audible;
     *   - it stopped by itself first (an engine's own silence gate, a sample
     *     running out): keep it exactly — the original stops there too, and a
     *     fade would be a difference;
     *   - it hit the cap: fade the last 5 ms, which keeps it from clicking. */
    const float thr = a.peak * a.floor_lin;
    size_t end = 0;
    for (size_t i = a.n; i-- > 0; ) {
        float v = fabsf(a.buf[i * (size_t)a.ch]);
        if (a.ch == 2 && fabsf(a.buf[i * 2 + 1]) > v) v = fabsf(a.buf[i * 2 + 1]);
        if (v > thr) { end = i + 1; break; }
    }
    if (end == 0) end = 1;
    const size_t fade = (size_t)(DR32_RS_FADE_MS * 0.001f * DR32_RS_SR);
    size_t f0, f1;                     /* fade from f0 down to 0 at f0 + fade */
    if (!stopped) { end = a.n; f0 = end > fade ? end - fade : 0; }
    else { f0 = end; end = end + fade < a.n ? end + fade : a.n; }
    f1 = f0 + fade;
    for (size_t i = f0; i < end; i++) {
        const float g = (float)(f1 - 1 - i) / (float)fade;
        for (int c = 0; c < a.ch; c++) a.buf[i * (size_t)a.ch + (size_t)c] *= g;
    }

    /* A mono sample through a mono path comes out with L == R: keep it mono.
     * A stereo sample (or an effect that makes one) stays stereo. */
    int ch = a.ch;
    if (ch == 2) {
        int same = 1;
        for (size_t i = 0; i < end && same; i++) same = a.buf[2 * i] == a.buf[2 * i + 1];
        if (same) {
            for (size_t i = 0; i < end; i++) a.buf[i] = a.buf[2 * i];
            ch = 1;
        }
    }

    const float norm = db_lin(DR32_RS_PEAK_DBFS) / a.peak;
    for (size_t i = 0; i < end * (size_t)ch; i++) a.buf[i] = dr32_wav_q24(a.buf[i] * norm);

    float vol = 20.0f * log10f(level / norm);
    out->clamped = !(vol >= DR32_RS_VOL_MIN && vol <= DR32_RS_VOL_MAX);
    if (!(vol >= DR32_RS_VOL_MIN)) vol = DR32_RS_VOL_MIN;
    if (vol > DR32_RS_VOL_MAX) vol = DR32_RS_VOL_MAX;
    float *shrunk = (float *)realloc(a.buf, end * (size_t)ch * sizeof(float));
    out->data = shrunk ? shrunk : a.buf;
    out->frames = end;
    out->channels = ch;
    out->volume_db = vol;
    out->capped = !stopped;
    return 0;
}

/* ---------- naming -------------------------------------------------------- */

/* The picker's family labels, by the model slug's prefix. */
static const struct { const char *id, *label; } FAMILY[] = {
    { "simian", "Simian" }, { "urchin", "Urchin" }, { "9w9", "9W9" }, { "6w6", "6W6" },
    { "8w8", "8W8" }, { "cw78", "CW-78" }, { "chowkick", "ChowKick" }, { "fm", "FM" },
};

static int all_digits(const char *s, size_t n) {
    if (!n) return 0;
    for (size_t i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

/* Length of `s` without a previous resample's " vNN YYYY-MM-DD" and an
 * optional " N" after it. */
static size_t strip_suffix(const char *s, size_t n) {
    size_t m = n;
    /* the duplicate number */
    size_t sp = m;
    while (sp > 0 && s[sp - 1] >= '0' && s[sp - 1] <= '9') sp--;
    if (sp < m && sp > 0 && s[sp - 1] == ' ' && m - sp <= 3) {
        size_t t = sp - 1;
        if (t >= 16 && s[t - 3] == '-' && s[t - 6] == '-') m = t;
    }
    /* " YYYY-MM-DD" */
    if (m < 11) return n;
    const char *d = s + m - 10;
    if (d[-1] != ' ' || !all_digits(d, 4) || d[4] != '-' || !all_digits(d + 5, 2) ||
        d[7] != '-' || !all_digits(d + 8, 2)) return n;
    size_t v = m - 11;                              /* end of " vNN" */
    size_t ds = v;
    while (ds > 0 && s[ds - 1] >= '0' && s[ds - 1] <= '9') ds--;
    if (ds == v || v - ds > 3 || ds < 2 || s[ds - 1] != 'v' || s[ds - 2] != ' ') return n;
    return ds - 2;
}

void dr32_rs_basename(const dr32_rs_src *src, const struct tm *date, char *out, size_t cap) {
    if (!out || !cap) return;
    char name[160] = "";
    if (src->engine) {
        const dr32_model *m = dr32_model_at(src->model);
        const char *label = "";
        if (m && m->slug) {
            const char *cut = strchr(m->slug, '/');
            size_t fl = cut ? (size_t)(cut - m->slug) : 0;
            for (size_t i = 0; i < sizeof(FAMILY) / sizeof(FAMILY[0]); i++)
                if (fl && strlen(FAMILY[i].id) == fl && !strncmp(FAMILY[i].id, m->slug, fl)) label = FAMILY[i].label;
        }
        snprintf(name, sizeof(name), "%s%s%s", label, label[0] ? " " : "", m ? m->name : "Synth");
    } else {
        const char *b = strrchr(src->path, '/');
        b = b ? b + 1 : src->path;
        const char *dot = strrchr(b, '.');
        size_t n = (dot && dot != b) ? (size_t)(dot - b) : strlen(b);
        n = strip_suffix(b, n);
        if (n >= sizeof(name)) n = sizeof(name) - 1;
        memcpy(name, b, n);
        name[n] = '\0';
    }
    /* Keep the name a name: no path separators or characters a FAT card or a
     * browser chokes on; bounded, so a long sample name leaves room. */
    size_t len = strlen(name);
    if (len > 80) {
        len = 80;                                   /* never mid-way through a UTF-8 character */
        while (len > 0 && ((unsigned char)name[len] & 0xC0) == 0x80) len--;
        name[len] = '\0';
    }
    while (len > 0 && name[len - 1] == ' ') name[--len] = '\0';
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || strchr("/\\:*?\"<>|", c)) name[i] = '-';
    }
    if (!name[0]) snprintf(name, sizeof(name), "Pad %d", src->pad + 1);
    snprintf(out, cap, "%s v%d %04d-%02d-%02d", name, src->velocity,
             date->tm_year + 1900, date->tm_mon + 1, date->tm_mday);
}

static int mkdir_p(const char *dir) {
    char tmp[DR32_MAX_PATH];
    if (snprintf(tmp, sizeof(tmp), "%s", dir) >= (int)sizeof(tmp)) return -1;
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

int dr32_rs_unique_path(const char *dir, const char *base, char *out, size_t cap) {
    if (mkdir_p(dir) != 0) return -1;
    struct stat st;
    for (int i = 1; i < 1000; i++) {
        int w = i == 1 ? snprintf(out, cap, "%s/%s.wav", dir, base)
                       : snprintf(out, cap, "%s/%s %d.wav", dir, base, i);
        if (w <= 0 || (size_t)w >= cap) return -1;
        if (stat(out, &st) != 0) return 0;
    }
    return -1;
}

/* ---------- the switch ---------------------------------------------------- */

int dr32_rs_apply(dr32_kit *k, const dr32_rs_src *src, dr32_rs_take *take,
                  const char *path, long size, long mtime) {
    if (!k || !src || !take || !take->data || src->pad < 0 || src->pad >= DR32_PADS) return 0;
    dr32_pad_slot *s = &k->pads[src->pad];
    /* "If the pad changed mid-render, write the file, do not switch": any
     * difference at all — another sound, or a knob moved — leaves it alone. */
    if (s->engine != src->engine || s->model != src->model ||
        memcmp(&s->params, &src->params, sizeof(s->params)) != 0 ||
        memcmp(s->eparam, src->eparam, sizeof(s->eparam)) != 0 ||
        strcmp(src->engine ? "" : s->path, src->path) != 0)
        return 0;

    /* Neutral everything, then put back what stays live. */
    const dr32_pad old = s->params;
    dr32_pad np;
    dr32_pad_defaults(&np);
    np.hold = DR32_HOLD_MAX;           /* play the whole file */
    np.filter_on = 0;
    np.volume_db = take->volume_db;
    np.vel_to_volume = src->engine ? 0.0f : old.vel_to_volume;
    np.pan = old.pan;
    np.send_db[0] = old.send_db[0];
    np.send_db[1] = old.send_db[1];
    np.choke_group = old.choke_group;
    np.speaker_on = old.speaker_on;
    np.wide_pct = old.wide_pct;
    np.wide_hz = old.wide_hz;
    np.wide_mode = old.wide_mode;
    np.wide_time = old.wide_time;
    np.wide_comp = old.wide_comp;
    np.wide_late_db = old.wide_late_db;

    dr32_kit_adopt_sample(k, src->pad, take->data, take->frames, take->channels,
                          DR32_RS_SR, path, size, mtime);
    s->params = np;
    take->data = NULL;                 /* the pad owns it now */
    return 1;
}

/* ---------- the job ------------------------------------------------------- */

enum { RS_WAIT = 0, RS_READY, RS_SWITCHED, RS_SAVED_ONLY, RS_FAILED };

/*
 * ONE worker thread per instance, started on the first Resample and then
 * PARKED on a condition variable (Fable's review: creating and joining a
 * thread per job put a clone, an 8 MB stack map and a join on the audio
 * callback). The audio thread only posts; the worker renders, writes, and
 * frees what the audio thread hands back (`trash`), so no free() of a take
 * ever runs on the callback either.
 */
struct dr32_rs_job {
    char           dir[DR32_MAX_PATH];
    int            n;
    dr32_rs_src    src[DR32_PADS];
    dr32_rs_take   take[DR32_PADS];
    char           path[DR32_PADS][DR32_MAX_PATH];
    long           size[DR32_PADS], mtime[DR32_PADS];
    char           base[DR32_PADS][160];
    float         *trash[DR32_PADS];   /* takes the pad did not adopt; the worker frees */
    atomic_int     state[DR32_PADS];
    atomic_int     busy;            /* a job is posted or rendering */
    atomic_int     abort;
    atomic_int     last;            /* index of the last file written, -1 = none */
    atomic_int     jobs;            /* jobs started, ever: the page's "did it start" */
    int            clamped;         /* audio thread only */
    int            started;         /* the thread exists */
    int            posted;          /* under `mu`: a job is waiting for the worker */
    int            quit;            /* under `mu` */
    pthread_t      th;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
};

static void rs_take_trash(dr32_rs_job *j) {
    for (int i = 0; i < DR32_PADS; i++) { free(j->trash[i]); j->trash[i] = NULL; }
}

static void rs_run(dr32_rs_job *j) {
    rs_take_trash(j);
    for (int i = 0; i < j->n; i++) {
        if (atomic_load(&j->abort)) { atomic_store(&j->state[i], RS_FAILED); continue; }
        dr32_rs_take *t = &j->take[i];
        if (dr32_rs_render_abortable(&j->src[i], t, &j->abort) != 0) {
            atomic_store(&j->state[i], RS_FAILED);
            continue;
        }
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        dr32_rs_basename(&j->src[i], &tmv, j->base[i], sizeof(j->base[i]));
        struct stat st;
        if (atomic_load(&j->abort) ||
            dr32_rs_unique_path(j->dir, j->base[i], j->path[i], sizeof(j->path[i])) != 0 ||
            dr32_wav_write24(j->path[i], t->data, t->frames, t->channels, DR32_RS_SR) != 0 ||
            stat(j->path[i], &st) != 0) {
            dr32_rs_take_free(t);
            atomic_store(&j->state[i], RS_FAILED);
            continue;
        }
        /* The stem the file actually got, " 2" and all, for the status line. */
        const char *b = strrchr(j->path[i], '/');
        snprintf(j->base[i], sizeof(j->base[i]), "%.*s", (int)strlen(b + 1) - 4, b + 1);
        j->size[i] = (long)st.st_size;
        j->mtime[i] = (long)st.st_mtime;
        atomic_store(&j->last, i);
        atomic_store_explicit(&j->state[i], RS_READY, memory_order_release);
    }
}

static void *rs_worker(void *arg) {
    dr32_rs_job *j = (dr32_rs_job *)arg;
    pthread_mutex_lock(&j->mu);
    for (;;) {
        while (!j->posted && !j->quit) pthread_cond_wait(&j->cv, &j->mu);
        if (j->quit) break;
        j->posted = 0;
        pthread_mutex_unlock(&j->mu);
        rs_run(j);
        atomic_store(&j->busy, 0);
        pthread_mutex_lock(&j->mu);
    }
    pthread_mutex_unlock(&j->mu);
    return NULL;
}

dr32_rs_job *dr32_rs_job_create(const char *dir) {
    dr32_rs_job *j = (dr32_rs_job *)calloc(1, sizeof(*j));
    if (!j) return NULL;
    snprintf(j->dir, sizeof(j->dir), "%s", dir ? dir : DR32_RS_DIR);
    atomic_store(&j->last, -1);
    pthread_mutex_init(&j->mu, NULL);
    pthread_cond_init(&j->cv, NULL);
    return j;
}

void dr32_rs_job_destroy(dr32_rs_job *j) {
    if (!j) return;
    atomic_store(&j->abort, 1);             /* the render checks it every block */
    if (j->started) {
        pthread_mutex_lock(&j->mu);
        j->quit = 1;
        pthread_cond_signal(&j->cv);
        pthread_mutex_unlock(&j->mu);
        pthread_join(j->th, NULL);
    }
    for (int i = 0; i < DR32_PADS; i++) dr32_rs_take_free(&j->take[i]);
    rs_take_trash(j);
    pthread_cond_destroy(&j->cv);
    pthread_mutex_destroy(&j->mu);
    free(j);
}

int dr32_rs_job_start(dr32_rs_job *j, const dr32_kit *k, const int *pads, int n, int velocity) {
    if (!j || !k) return -1;
    if (atomic_load(&j->busy)) return -1;
    for (int i = 0; i < j->n; i++)
        if (atomic_load(&j->state[i]) == RS_READY) return -1;     /* not switched yet */
    /* Anything a finished job still holds goes to the worker to free. */
    for (int i = 0; i < DR32_PADS; i++) {
        if (j->take[i].data) {
            if (!j->trash[i]) { j->trash[i] = j->take[i].data; j->take[i].data = NULL; }
            else return -1;                                  /* cannot happen; never leak */
        }
    }
    int m = 0;
    for (int i = 0; i < n && m < DR32_PADS; i++) {
        if (!dr32_rs_snapshot(k, pads[i], velocity, &j->src[m])) continue;
        memset(&j->take[m], 0, sizeof(j->take[m]));
        atomic_store(&j->state[m], RS_WAIT);
        m++;
    }
    if (!m) return 0;
    if (!j->started) {
        /* Once per instance: the only thread creation Resample ever does. */
        if (pthread_create(&j->th, NULL, rs_worker, j) != 0) return -1;
        j->started = 1;
    }
    j->n = m;
    j->clamped = 0;
    atomic_store(&j->last, -1);
    atomic_store(&j->abort, 0);
    atomic_store(&j->busy, 1);
    atomic_fetch_add(&j->jobs, 1);
    pthread_mutex_lock(&j->mu);             /* the worker holds it only to wait */
    j->posted = 1;
    pthread_cond_signal(&j->cv);
    pthread_mutex_unlock(&j->mu);
    return m;
}

int dr32_rs_job_service(dr32_rs_job *j, dr32_kit *k) {
    if (!j) return 0;
    int switched = 0;
    for (int i = 0; i < j->n; i++) {
        if (atomic_load_explicit(&j->state[i], memory_order_acquire) != RS_READY) continue;
        if (dr32_rs_apply(k, &j->src[i], &j->take[i], j->path[i], j->size[i], j->mtime[i])) {
            if (j->take[i].clamped) j->clamped++;
            atomic_store(&j->state[i], RS_SWITCHED);
            switched++;
        } else {
            /* Not adopted: the next job hands it to the worker to free. */
            atomic_store(&j->state[i], RS_SAVED_ONLY);
        }
    }
    return switched;
}

void dr32_rs_job_status(dr32_rs_job *j, dr32_rs_status *o) {
    memset(o, 0, sizeof(*o));
    if (!j) return;
    o->jobs = atomic_load(&j->jobs);
    if (!o->jobs) return;
    o->total = j->n;
    o->busy = atomic_load(&j->busy);
    for (int i = 0; i < j->n; i++) {
        switch (atomic_load(&j->state[i])) {
            case RS_READY:      o->busy = 1; break;
            case RS_SWITCHED:   o->done++; o->switched++; break;
            case RS_SAVED_ONLY: o->done++; o->saved_only++; break;
            case RS_FAILED:     o->done++; o->failed++; break;
            default: break;
        }
    }
    o->clamped = j->clamped;
    int last = atomic_load(&j->last);
    if (last >= 0 && last < j->n) snprintf(o->last, sizeof(o->last), "%s", j->base[last]);
}

void dr32_rs_job_wait(dr32_rs_job *j) {
    while (j && atomic_load(&j->busy)) {
        struct timespec ts = { 0, 1000000 };
        nanosleep(&ts, NULL);
    }
}
