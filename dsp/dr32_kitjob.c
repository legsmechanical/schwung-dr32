// dr32_kitjob.c — see dr32_kitjob.h.

/* pthreads and clock_gettime are POSIX, and the build is -std=c11. */
#define _POSIX_C_SOURCE 200809L

#include "dr32_kitjob.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    dr32_kit_plan *plan;
    unsigned       gen;
    double         ms;
    char           path[DR32_MAX_PATH];
} kj_result;

struct dr32_kitjob {
    pthread_t       th;
    int             started;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int             quit;               /* under mu */

    /* The request, under mu. */
    unsigned        req_gen;
    char            req_path[DR32_MAX_PATH];
    dr32_pad_stamp  req_held[DR32_PADS];
    dr32_pad_stamp  work_held[DR32_PADS];   /* the worker's own copy */

    unsigned        gen;                /* audio thread's own counter */
    atomic_uint     want;               /* the generation the audio thread wants */
    _Atomic(kj_result *) done;          /* worker -> audio */
};

typedef struct { dr32_kitjob *j; unsigned gen; } kj_read;

static int superseded(void *ctx) {
    const kj_read *rd = (const kj_read *)ctx;
    return atomic_load(&rd->j->want) != rd->gen;
}

static void result_free(kj_result *r) {
    if (!r) return;
    dr32_kit_plan_free(r->plan);
    free(r);
}

static void *kj_worker(void *arg) {
    dr32_kitjob *j = (dr32_kitjob *)arg;
    unsigned seen = 0;
    char path[DR32_MAX_PATH];

    pthread_mutex_lock(&j->mu);
    for (;;) {
        while (!j->quit && j->req_gen == seen) pthread_cond_wait(&j->cv, &j->mu);
        if (j->quit) break;
        unsigned gen = seen = j->req_gen;
        memcpy(path, j->req_path, sizeof(path));
        memcpy(j->work_held, j->req_held, sizeof(j->work_held));
        pthread_mutex_unlock(&j->mu);

        if (atomic_load(&j->want) == gen) {
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            kj_read rd = { j, gen };
            dr32_kit_plan *plan = dr32_preset_prepare(path, j->work_held, superseded, &rd);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            kj_result *r = (kj_result *)calloc(1, sizeof(*r));
            if (r) {
                /* A NULL plan is published too: "that kit would not load" is
                 * an answer the audio thread reports, not a hang. */
                r->plan = plan;
                r->gen = gen;
                r->ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
                snprintf(r->path, sizeof(r->path), "%s", path);
                if (atomic_load(&j->want) == gen) {
                    /* An untaken older result is ours to free, here, off the
                     * audio thread. */
                    result_free(atomic_exchange(&j->done, r));
                } else {
                    result_free(r);
                }
            } else {
                dr32_kit_plan_free(plan);
            }
        }
        pthread_mutex_lock(&j->mu);
    }
    pthread_mutex_unlock(&j->mu);
    return NULL;
}

dr32_kitjob *dr32_kitjob_create(void) {
    dr32_kitjob *j = (dr32_kitjob *)calloc(1, sizeof(*j));
    if (!j) return NULL;
    pthread_mutex_init(&j->mu, NULL);
    pthread_cond_init(&j->cv, NULL);
    atomic_init(&j->want, 0);
    atomic_init(&j->done, NULL);
    return j;
}

void dr32_kitjob_destroy(dr32_kitjob *j) {
    if (!j) return;
    if (j->started) {
        atomic_store(&j->want, j->gen + 0x80000000u);   /* stops a read between WAVs */
        pthread_mutex_lock(&j->mu);
        j->quit = 1;
        pthread_cond_signal(&j->cv);
        pthread_mutex_unlock(&j->mu);
        pthread_join(j->th, NULL);
    }
    result_free(atomic_load(&j->done));
    pthread_cond_destroy(&j->cv);
    pthread_mutex_destroy(&j->mu);
    free(j);
}

int dr32_kitjob_post(dr32_kitjob *j, const char *path, const dr32_kit *kit) {
    if (!j || !path || !kit) return -1;
    if (!j->started) {
        /* Once per instance, on the first audition. If it cannot be made the
         * caller loads synchronously, as it always used to. */
        if (pthread_create(&j->th, NULL, kj_worker, j) != 0) return -2;
        j->started = 1;
    }
    /* NEVER block here — this is the audio callback. */
    if (pthread_mutex_trylock(&j->mu) != 0) return -1;
    unsigned gen = ++j->gen;
    j->req_gen = gen;
    snprintf(j->req_path, sizeof(j->req_path), "%s", path);
    dr32_preset_stamps(kit, j->req_held);
    atomic_store(&j->want, gen);
    pthread_cond_signal(&j->cv);
    pthread_mutex_unlock(&j->mu);
    return 0;
}

void dr32_kitjob_cancel(dr32_kitjob *j) {
    if (!j || !j->started) return;
    atomic_store(&j->want, ++j->gen);
}

int dr32_kitjob_take(dr32_kitjob *j, dr32_kit_plan **plan, char *path, int path_len,
                     double *ms) {
    if (!j || !j->started || !plan) return 0;
    if (!atomic_load_explicit(&j->done, memory_order_relaxed)) return 0;
    kj_result *r = atomic_exchange(&j->done, NULL);
    if (!r) return 0;
    if (r->gen != atomic_load(&j->want)) {
        /* Superseded after it was published — rare (a cancel in the block
         * between the two). Freed here rather than kept waiting. */
        result_free(r);
        return 0;
    }
    /* This generation is answered: not busy, and never taken twice. */
    atomic_store(&j->want, ++j->gen);
    *plan = r->plan;
    if (path && path_len > 0) snprintf(path, (size_t)path_len, "%s", r->path);
    if (ms) *ms = r->ms;
    free(r);
    return 1;
}

int dr32_kitjob_busy(const dr32_kitjob *j) {
    return j && j->started && atomic_load(&j->want) == j->gen &&
           j->req_gen == j->gen;
}
