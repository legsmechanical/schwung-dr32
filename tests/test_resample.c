/* See test_wav.c: _GNU_SOURCE is the one macro that works on both libcs. */
#define _GNU_SOURCE

// RESAMPLE (dsp/dr32_resample.c). Build+run: tests/run.sh
//
// The laws, each checked against DR32's own render rather than a number:
//   1. a resampled pad, hit at the captured velocity, sounds as the original
//      did — synth pad and sample pad, panned, filtered, transposed, detuned
//      (this is the level match, and "what is baked" together)
//   2. a sample pad keeps its Vel Vol live: at ANOTHER velocity it still
//      matches; a synth pad's Vel Vol becomes 0
//   3. the baked knobs come back neutral; the live ones are untouched
//   4. the file: 24-bit, 44.1 kHz, peak -0.3 dBFS, trimmed, faded; mono for
//      a mono source, stereo for a stereo one; decodes to the pad's buffer
//   5. it stops on 1 s of silence, and at 20 s for a sound that never does
//   6. names: model or sample name, velocity, date; a previous resample's
//      suffix does not pile up; duplicates numbered; unsafe characters gone
//   7. a pad changed mid-render is not switched (its file is still written)
//   8. the job: the live pad is untouched until the switch, empty pads are
//      skipped, and a second job waits for the first

#include "../dsp/dr32_resample.h"
#include "../dsp/dr32_kit.h"
#include "../dsp/wav.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define FR 44100
#define BLK 128

/* Hit one pad and render the kit until it has been silent a while (or `max`
 * frames). Returns frames rendered into *out (interleaved stereo, malloc'd). */
static size_t hit_and_render(dr32_kit *k, int pad, int vel, size_t max, float **out) {
    float *buf = (float *)calloc(max * 2, sizeof(float));
    dr32_kit_note_on(k, k->pads[pad].note, vel);
    size_t n = 0;
    while (n + BLK <= max) {
        dr32_kit_render(k, buf + 2 * n, BLK);
        n += BLK;
        if (!dr32_pad_sounding(&k->pads[pad]) && k->pads[pad].wide.tail <= 0) break;
    }
    *out = buf;
    return n;
}

/* Worst difference between two renders, ignoring the first `skip` frames (the
 * resampled pad's own 0.1 ms attack ramp — see test_synth_pad), relative to
 * the original's peak. */
static float rel_err(const float *a, size_t na, const float *b, size_t nb, size_t skip) {
    float peak = 0.0f, err = 0.0f;
    size_t n = na > nb ? na : nb;
    for (size_t i = 0; i < 2 * na; i++) if (fabsf(a[i]) > peak) peak = fabsf(a[i]);
    for (size_t i = 2 * skip; i < 2 * n; i++) {
        float x = i < 2 * na ? a[i] : 0.0f, y = i < 2 * nb ? b[i] : 0.0f;
        if (fabsf(x - y) > err) err = fabsf(x - y);
    }
    return peak > 0 ? err / peak : 1.0f;
}

static void write_tone(const char *path, int ch, float secs, float hz, float decay_s) {
    size_t n = (size_t)(secs * FR);
    float *d = (float *)calloc(n * (size_t)ch, sizeof(float));
    for (size_t i = 0; i < n; i++) {
        float env = decay_s > 0 ? expf(-(float)i / (decay_s * FR)) : 1.0f;
        float t = (float)i / FR;
        d[i * (size_t)ch] = 0.5f * env * sinf(6.2831853f * hz * t);
        if (ch == 2) d[i * 2 + 1] = 0.4f * env * sinf(6.2831853f * hz * 1.5f * t);
    }
    dr32_wav_write24(path, d, n, ch, FR);
    free(d);
}

/* Resample one pad synchronously, as the job would (minus the thread). */
static int resample_now(dr32_kit *k, int pad, int vel, const char *dir, char *path_out) {
    dr32_rs_src src;
    if (!dr32_rs_snapshot(k, pad, vel, &src)) return -1;
    dr32_rs_take t;
    if (dr32_rs_render(&src, &t) != 0) return -2;
    struct tm tmv = { .tm_year = 126, .tm_mon = 8, .tm_mday = 22 };
    char base[160];
    dr32_rs_basename(&src, &tmv, base, sizeof(base));
    if (dr32_rs_unique_path(dir, base, path_out, DR32_MAX_PATH) != 0) return -3;
    if (dr32_wav_write24(path_out, t.data, t.frames, t.channels, DR32_RS_SR) != 0) return -4;
    struct stat st;
    stat(path_out, &st);
    int ok = dr32_rs_apply(k, &src, &t, path_out, (long)st.st_size, (long)st.st_mtime);
    dr32_rs_take_free(&t);
    return ok ? 0 : -5;
}

static void test_wav_writer(const char *dir) {
    char p[512];
    snprintf(p, sizeof(p), "%s/w.wav", dir);
    float d[6] = { 0.5f, -0.25f, 1.5f, -1.0f, 0.1234567f, -0.0000001f };
    CHECK(dr32_wav_write24(p, d, 3, 2, 44100) == 0, "write24 failed");
    dr32_wav w;
    CHECK(dr32_wav_load(p, &w) == DR32_WAV_OK, "written file does not load");
    CHECK(w.frames == 3 && w.channels == 2 && w.sample_rate == 44100 && w.bits == 24,
          "shape %zu frames %d ch %d Hz %d bits", w.frames, w.channels, w.sample_rate, w.bits);
    for (int i = 0; i < 6 && w.data; i++)
        CHECK(w.data[i] == dr32_wav_q24(d[i]), "sample %d decodes %.9f, q24 says %.9f", i, w.data[i], dr32_wav_q24(d[i]));
    CHECK(w.data && w.data[2] < 1.0f, "a clipped sample must stay below full scale");
    dr32_wav_free(&w);
    struct stat st;
    char part[520];
    snprintf(part, sizeof(part), "%s.part", p);
    CHECK(stat(part, &st) != 0, "the .part file was left behind");
}

static void test_synth_pad(const char *dir) {
    dr32_kit k;
    dr32_kit_init(&k);
    CHECK(dr32_kit_set_model(&k, 0, "fm/kick"), "fm/kick did not load");
    dr32_pad *p = &k.pads[0].params;
    p->volume_db = -4.0f; p->vel_to_volume = 0.6f; p->pan = 15.0f;
    p->transpose = 3.0f; p->detune = -20.0f;
    /* Wide stays at 0 here only so the comparison below is sample-exact: the
     * resampled pad's 0.1 ms attack ramp (below) would otherwise smear through
     * Disperse's all-passes. The Stereo page is checked as LIVE by its mode
     * and crossover, which are kept whatever Wide is. */
    p->send_db[0] = -12.0f; p->choke_group = 3; p->wide_mode = 2; p->wide_hz = 300.0f;

    float *orig, *again;
    size_t no = hit_and_render(&k, 0, 96, 4 * FR, &orig);
    char path[DR32_MAX_PATH];
    CHECK(resample_now(&k, 0, 96, dir, path) == 0, "resample failed");
    CHECK(strstr(path, "/FM Kick v96 2026-09-22.wav") != NULL, "file named %s", path);
    CHECK(k.pads[0].engine == 0 && k.pads[0].sample && !strcmp(k.pads[0].path, path), "pad is not the sample now");
    CHECK(k.pads[0].channels == 1, "a synth pad resamples mono, got %d ch", k.pads[0].channels);

    /* 3: neutral and live */
    p = &k.pads[0].params;
    CHECK(p->transpose == 0 && p->detune == 0 && p->gain == 1 && p->cell_volume_db == 0, "tune/gain not neutral");
    CHECK(p->filter_on == 0 && p->fx_type == DR32_FX_STANDARD && p->hold >= DR32_HOLD_MAX && p->sending_note == 60,
          "shape/filter/fx not neutral");
    CHECK(p->play_start == 0 && p->play_length == 1, "start/end not the whole file");
    CHECK(p->vel_to_volume == 0, "a synth pad's Vel Vol must become 0, got %g", p->vel_to_volume);
    CHECK(p->pan == 15 && p->send_db[0] == -12 && p->choke_group == 3 && p->wide_hz == 300 && p->wide_mode == 2,
          "live params changed");

    /* 1: it sounds as it did — from the sixth frame on. A sample pad's
     * envelope rises from 0 over DR32_ATTACK_MIN (0.1 ms, 4.4 frames: the Move
     * sampler's own law, dr32_voice.c), so the take's first frames play
     * attenuated, as any sample's do. Measured: identical from frame 5. */
    size_t na = hit_and_render(&k, 0, 96, 4 * FR, &again);
    float e = rel_err(orig, no, again, na, 5);
    CHECK(e < 3e-4f, "resampled synth pad differs from the original by %.2e of peak", e);
    printf("  synth pad: %zu frames, worst difference %.1e of peak, Volume %.2f dB\n", na, e, p->volume_db);
    free(orig); free(again);

    /* 4: the file */
    dr32_wav w;
    CHECK(dr32_wav_load(path, &w) == DR32_WAV_OK && w.bits == 24 && w.sample_rate == 44100 && w.channels == 1,
          "file format");
    float peak = 0;
    for (size_t i = 0; i < w.frames; i++) if (fabsf(w.data[i]) > peak) peak = fabsf(w.data[i]);
    CHECK(fabsf(20 * log10f(peak) - DR32_RS_PEAK_DBFS) < 0.001f, "peak %.4f dBFS", 20 * log10f(peak));
    /* The FM engine stops itself (its silence gate, ~-75 dB): the take ends
     * there too, unfaded, because the original does. The cap's fade is
     * test_length's. */
    CHECK(fabsf(w.data[w.frames - 1]) < 1e-3f, "the take ends loud: %g", w.data[w.frames - 1]);
    CHECK(w.frames == k.pads[0].frames && !memcmp(w.data, k.pads[0].sample, w.frames * sizeof(float)),
          "the file does not decode to what the pad plays");
    dr32_wav_free(&w);
    dr32_kit_free(&k);
}

static void test_sample_pad(const char *dir, int ch) {
    char src[512];
    snprintf(src, sizeof(src), "%s/tone%d.wav", dir, ch);
    write_tone(src, ch, 1.5f, 220.0f, 0.25f);
    dr32_kit k;
    dr32_kit_init(&k);
    CHECK(dr32_kit_load_sample(&k, 4, src) == DR32_WAV_OK, "tone did not load");
    dr32_pad *p = &k.pads[4].params;
    p->transpose = 5; p->detune = 12; p->gain = 0.7f; p->cell_volume_db = -3; p->volume_db = -6;
    p->vel_to_volume = 0.5f; p->pan = -10; p->filter_on = 1; p->filter_type = DR32_FILT_LP24;
    p->cutoff = 2000; p->resonance = 0.3f; p->decay = 0.4f; p->hold = 0.2f;

    float *o80, *o120, *r80, *r120;
    size_t n80 = hit_and_render(&k, 4, 80, 4 * FR, &o80);
    size_t n120 = hit_and_render(&k, 4, 120, 4 * FR, &o120);
    char path[DR32_MAX_PATH];
    CHECK(resample_now(&k, 4, 80, dir, path) == 0, "resample failed (%d ch)", ch);
    char want[64];
    snprintf(want, sizeof(want), "/tone%d v80 2026-09-22.wav", ch);
    CHECK(strstr(path, want) != NULL, "file named %s", path);
    CHECK(k.pads[4].channels == ch, "a %d-channel sample resampled to %d", ch, k.pads[4].channels);
    CHECK(k.pads[4].params.vel_to_volume == 0.5f, "a sample pad keeps its Vel Vol");
    size_t m80 = hit_and_render(&k, 4, 80, 4 * FR, &r80);
    size_t m120 = hit_and_render(&k, 4, 120, 4 * FR, &r120);
    float e80 = rel_err(o80, n80, r80, m80, 5), e120 = rel_err(o120, n120, r120, m120, 5);
    CHECK(e80 < 3e-4f, "%d-ch sample pad at vel 80 differs by %.2e of peak", ch, e80);
    CHECK(e120 < 3e-4f, "%d-ch sample pad at vel 120 (Vel Vol live) differs by %.2e of peak", ch, e120);
    printf("  %s sample pad: worst difference %.1e (vel 80), %.1e (vel 120)\n", ch == 2 ? "stereo" : "mono", e80, e120);
    free(o80); free(o120); free(r80); free(r120);
    dr32_kit_free(&k);
}

static void test_length(const char *dir) {
    /* A 2 s tone that stops dead, played whole: the take ends where it does. */
    char src[512];
    snprintf(src, sizeof(src), "%s/two.wav", dir);
    write_tone(src, 1, 2.0f, 330.0f, 0.0f);
    dr32_kit k;
    dr32_kit_init(&k);
    dr32_kit_load_sample(&k, 0, src);
    k.pads[0].params.hold = DR32_HOLD_MAX;
    dr32_rs_src s;
    dr32_rs_snapshot(&k, 0, 100, &s);
    dr32_rs_take t;
    CHECK(dr32_rs_render(&s, &t) == 0, "render failed");
    CHECK(!t.capped && t.frames > (size_t)(1.99f * FR) && t.frames <= (size_t)(2.005f * FR) + 1,
          "2 s tone gave %zu frames, capped %d", t.frames, t.capped);
    dr32_rs_take_free(&t);

    /* A 2 s tone and then 25 s of SILENCE in the same file: the sample never
     * runs out inside the cap, so only the silence stop ends it — 1 s into
     * the quiet, not at 20 s. */
    snprintf(src, sizeof(src), "%s/gap.wav", dir);
    {
        size_t n = (size_t)(27 * FR);
        float *d = (float *)calloc(n, sizeof(float));
        for (size_t i = 0; i < (size_t)(2 * FR); i++) d[i] = 0.5f * sinf(6.2831853f * 330.0f * (float)i / FR);
        dr32_wav_write24(src, d, n, 1, FR);
        free(d);
    }
    dr32_kit_load_sample(&k, 0, src);
    k.pads[0].params.hold = DR32_HOLD_MAX;
    dr32_rs_snapshot(&k, 0, 100, &s);
    CHECK(dr32_rs_render(&s, &t) == 0, "render failed");
    CHECK(!t.capped && t.frames <= (size_t)(2.005f * FR) + 1,
          "tone + silence: %zu frames, capped %d — the silence stop did not end it", t.frames, t.capped);
    dr32_rs_take_free(&t);

    /* A tone that never stops (a 25 s file): the 20 s cap, faded. */
    snprintf(src, sizeof(src), "%s/long.wav", dir);
    write_tone(src, 1, 25.0f, 330.0f, 0.0f);
    dr32_kit_load_sample(&k, 0, src);
    k.pads[0].params.hold = DR32_HOLD_MAX;
    dr32_rs_snapshot(&k, 0, 100, &s);
    CHECK(dr32_rs_render(&s, &t) == 0, "render failed");
    CHECK(t.capped && t.frames == (size_t)(DR32_RS_MAX_S * FR), "25 s tone gave %zu frames, capped %d", t.frames, t.capped);
    CHECK(t.data && t.data[t.frames - 1] == 0.0f && fabsf(t.data[t.frames - 100]) < 0.5f, "the cap is not faded");
    dr32_rs_take_free(&t);

    /* Silence: nothing to take. */
    snprintf(src, sizeof(src), "%s/quiet.wav", dir);
    float z[4410] = { 0 };
    dr32_wav_write24(src, z, 4410, 1, FR);
    dr32_kit_load_sample(&k, 0, src);
    dr32_rs_snapshot(&k, 0, 100, &s);
    CHECK(dr32_rs_render(&s, &t) != 0, "a silent pad must not make a file");

    /* An empty pad is not a source. */
    CHECK(!dr32_rs_snapshot(&k, 9, 100, &s), "an empty pad was snapshot");
    dr32_kit_free(&k);
}

static void test_names(const char *dir) {
    struct tm d = { .tm_year = 126, .tm_mon = 0, .tm_mday = 5 };
    dr32_rs_src s;
    memset(&s, 0, sizeof(s));
    s.velocity = 100;
    char b[160];
    const char *cases[][2] = {
        { "/x/Kick 707.wav",                      "Kick 707 v100 2026-01-05" },
        { "/x/Kick 707 v96 2026-09-22.wav",       "Kick 707 v100 2026-01-05" },
        { "/x/Kick 707 v96 2026-09-22 3.wav",     "Kick 707 v100 2026-01-05" },
        { "/x/Snare 2.wav",                       "Snare 2 v100 2026-01-05" },
        { "/x/a:b|c?.aif",                        "a-b-c- v100 2026-01-05" },
        { "/x/v96 2026-09-22.wav",                "v96 2026-09-22 v100 2026-01-05" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        snprintf(s.path, sizeof(s.path), "%s", cases[i][0]);
        dr32_rs_basename(&s, &d, b, sizeof(b));
        CHECK(!strcmp(b, cases[i][1]), "%s -> \"%s\", want \"%s\"", cases[i][0], b, cases[i][1]);
    }
    s.path[0] = '\0';
    s.engine = 1;
    s.model = dr32_model_find("9w9/kick");
    s.velocity = 7;
    dr32_rs_basename(&s, &d, b, sizeof(b));
    CHECK(!strcmp(b, "9W9 Bass Drum v7 2026-01-05"), "model name \"%s\"", b);

    char sub[512], p[DR32_MAX_PATH];
    snprintf(sub, sizeof(sub), "%s/deep/DR32 Resample", dir);
    for (int i = 1; i <= 3; i++) {
        CHECK(dr32_rs_unique_path(sub, "Kick v100 2026-01-05", p, sizeof(p)) == 0, "unique_path failed");
        char want[600];
        if (i == 1) snprintf(want, sizeof(want), "%s/Kick v100 2026-01-05.wav", sub);
        else snprintf(want, sizeof(want), "%s/Kick v100 2026-01-05 %d.wav", sub, i);
        CHECK(!strcmp(p, want), "duplicate %d named %s", i, p);
        FILE *f = fopen(p, "wb");
        if (f) fclose(f);
    }
}

static void test_changed_pad(const char *dir) {
    (void)dir;
    dr32_kit k;
    dr32_kit_init(&k);
    dr32_kit_set_model(&k, 2, "fm/snare");
    dr32_rs_src s;
    dr32_rs_snapshot(&k, 2, 100, &s);
    dr32_rs_take t;
    CHECK(dr32_rs_render(&s, &t) == 0, "render failed");
    k.pads[2].params.transpose = 1.0f;                     /* a knob moved mid-render */
    CHECK(!dr32_rs_apply(&k, &s, &t, "/x.wav", 1, 1), "a changed pad was switched");
    CHECK(k.pads[2].engine && t.data, "the changed pad was touched, or its take consumed");
    dr32_rs_take_free(&t);
    dr32_kit_free(&k);
}

static void test_job(const char *dir) {
    char out[512];
    snprintf(out, sizeof(out), "%s/job", dir);
    dr32_kit k;
    dr32_kit_init(&k);
    dr32_kit_set_model(&k, 0, "fm/kick");
    dr32_kit_set_model(&k, 1, "simian/snare");
    char src[512];
    snprintf(src, sizeof(src), "%s/tone1.wav", dir);
    dr32_kit_load_sample(&k, 2, src);
    /* "Resample kit": the synth pads only. */
    int pads[DR32_PADS], n = 0;
    for (int i = 0; i < DR32_PADS; i++) if (dr32_rs_is_synth(&k, i)) pads[n++] = i;
    CHECK(n == 2 && pads[0] == 0 && pads[1] == 1, "synth pads: %d", n);

    dr32_rs_job *j = dr32_rs_job_create(out);
    int with_empty[3] = { pads[0], pads[1], 7 };
    CHECK(dr32_rs_job_start(j, &k, with_empty, 3, 100) == 2, "the empty pad was not skipped");
    CHECK(dr32_rs_job_start(j, &k, pads, 1, 100) == -1, "a second job started over the first");
    dr32_rs_job_wait(j);
    CHECK(k.pads[0].engine && k.pads[1].engine, "a pad switched before the service ran");
    dr32_rs_status st;
    dr32_rs_job_status(j, &st);
    CHECK(st.busy && st.done == 0 && st.total == 2, "before the switch: busy %d done %d total %d", st.busy, st.done, st.total);
    CHECK(dr32_rs_job_service(j, &k) == 2, "service did not switch both");
    CHECK(!k.pads[0].engine && !k.pads[1].engine && k.pads[2].sample, "pads after the switch");
    dr32_rs_job_status(j, &st);
    CHECK(!st.busy && st.done == 2 && st.switched == 2 && !st.failed,
          "after: busy %d done %d switched %d failed %d", st.busy, st.done, st.switched, st.failed);
    CHECK(strstr(st.last, "Simian Snare v100 ") == st.last, "last file \"%s\"", st.last);
    int files = 0;
    DIR *dd = opendir(out);
    for (struct dirent *e; dd && (e = readdir(dd)); ) if (strstr(e->d_name, ".wav")) files++;
    if (dd) closedir(dd);
    CHECK(files == 2, "%d files in the folder", files);
    /* A job that fails to write (a folder that cannot be made) fails soft. */
    dr32_rs_job_destroy(j);
    j = dr32_rs_job_create("/dev/null/nope");
    dr32_kit_set_model(&k, 5, "fm/perc");
    int five = 5;
    CHECK(dr32_rs_job_start(j, &k, &five, 1, 100) == 1, "start");
    dr32_rs_job_wait(j);
    CHECK(dr32_rs_job_service(j, &k) == 0 && k.pads[5].engine, "a failed write switched the pad");
    dr32_rs_job_status(j, &st);
    CHECK(st.failed == 1 && !st.busy, "failed %d busy %d", st.failed, st.busy);
    dr32_rs_job_destroy(j);
    dr32_kit_free(&k);
}

/* 1, for EVERY model: each engine resamples to itself. A second instance with
 * the same knob values is the whole premise, so an engine whose sound hangs
 * on anything else (a free-running oscillator, a noise seed shared across
 * instances, a table filled by the first instance) shows up here. Measured
 * 2026-09-22: all 87 within 1e-4 of peak but ChowKick "Wonky Synth", which
 * never goes quiet and is taken to the 20 s cap (its fade is the difference).
 * The energy bound allows the sampler's 0.1 ms attack ramp, which costs the
 * shortest clicks (a rimshot) up to a quarter of a dB. */
static void test_all_models(const char *dir) {
    char out[512];
    snprintf(out, sizeof(out), "%s/models", dir);
    dr32_engines_set_module_dir("src");
    int bad = 0;
    for (int mi = 0; mi < dr32_model_count(); mi++) {
        const dr32_model *m = dr32_model_at(mi);
        dr32_kit k;
        dr32_kit_init(&k);
        dr32_kit_set_model(&k, 0, m->slug);
        float *o, *r;
        size_t no = hit_and_render(&k, 0, 100, 21 * FR, &o);
        char path[DR32_MAX_PATH];
        int rc = resample_now(&k, 0, 100, out, path);
        size_t nr = hit_and_render(&k, 0, 100, 21 * FR, &r);
        double eo = 0, er = 0;
        for (size_t i = 0; i < 2 * no; i++) eo += (double)o[i] * o[i];
        for (size_t i = 0; i < 2 * nr; i++) er += (double)r[i] * r[i];
        const float e = rel_err(o, no, r, nr, 5);
        const double edb = 10 * log10((er + 1e-20) / (eo + 1e-20));
        const int capped = no > (size_t)(DR32_RS_MAX_S * FR);
        if (rc != 0 || fabs(edb) > 0.3 || (!capped && e > 2e-4f)) {
            bad++;
            printf("  FAIL %s: rc %d, energy %+.2f dB, worst %.1e of peak\n", m->slug, rc, edb, e);
        }
        free(o); free(r);
        if (rc == 0) unlink(path);
        dr32_kit_free(&k);
    }
    CHECK(bad == 0, "%d of %d models do not resample to themselves", bad, dr32_model_count());
    printf("  all %d models resample to themselves\n", dr32_model_count());
}

int main(void) {
    printf("test_resample\n");
    char tmpl[] = "/tmp/dr32rsXXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL, "mkdtemp");
    if (!dir) return 1;
    test_wav_writer(dir);
    test_synth_pad(dir);
    test_sample_pad(dir, 1);
    test_sample_pad(dir, 2);
    test_length(dir);
    test_names(dir);
    test_changed_pad(dir);
    test_job(dir);
    test_all_models(dir);
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) printf("  (could not remove %s)\n", dir);
    printf("test_resample: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
