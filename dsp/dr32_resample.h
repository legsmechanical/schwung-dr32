// dr32_resample.h — RESAMPLE: turn a pad into a sample of what it plays.
//
// Josh (2026-09-22): "resample a ... pad in place as a normalized sample of
// what the pad currently contains at a user defined velocity", into the user
// sample library, folder "DR32 Resample". The spec, with every decision and
// the words behind it: _worklogs/dr32-resample-spec.md (workspace).
//
// ⭐ THE SHAPE. A pad is SNAPSHOT on the audio thread (a struct copy — the
// params, the engine and its knob values, or the sample's path), RENDERED on a
// worker thread from that copy (its own engine instance, its own decode of the
// sample: the live pad is never touched and never cut off), written as a WAV,
// and only then SWITCHED, back on the audio thread, by handing the finished
// buffer to the pad (dr32_kit_adopt_sample) — no file is read to do it.
//
// ⭐ WHAT IS BAKED AND WHAT STAYS LIVE (Josh: "we'd want some sort of neutral
// setting on the knobs - otherwise params would be functionally doubling up";
// "agree with your suggestion re: what stays live - mixer params, etc"):
//   baked, then reset to neutral: the engine and its knobs, Start/End, the
//     Shape envelope, the filter, Punch and the playback effects, Transpose,
//     Detune, sample Gain and the cell's own volume;
//   live, untouched: Volume (level-matched, below), Pan, Send A/B, the Stereo
//     page, Choke, the pad's note, Mute.
// LEVEL MATCH (Josh: "Match"): the file is peak-normalised to -0.3 dBFS and the
// pad's Volume moved by the opposite amount, so a hit at the captured velocity
// is exactly as loud as before. VEL VOL (Josh: synth pads "fixed"): a sample
// pad keeps its Vel Vol, so the render leaves velocity-to-volume out and it is
// still applied live; a synth pad's velocity response is its engine's, baked
// at the captured velocity, and its Vel Vol becomes 0.
//
// Threading: dr32_rs_snapshot and dr32_rs_apply are audio-thread safe (no I/O,
// no allocation). Everything else allocates and does I/O — worker only.

#ifndef DR32_RESAMPLE_H
#define DR32_RESAMPLE_H

#include <stddef.h>
#include <time.h>
#include "dr32_kit.h"

/* Where the files go. The folder is created on first use. */
#define DR32_RS_DIR "/data/UserData/UserLibrary/Samples/DR32 Resample"

#define DR32_RS_SR          44100
/* Stop once the sound has stayed this far under its own peak ... */
#define DR32_RS_SILENCE_DB  (-80.0f)
/* ... for this long (Josh: "listen for silence and stop it automatically
 * after say 1 second of silence"). */
#define DR32_RS_SILENCE_S   1.0f
/* The safety cap, for a sound that never goes quiet (Josh: "20"). */
#define DR32_RS_MAX_S       20.0f
#define DR32_RS_FADE_MS     5.0f
#define DR32_RS_PEAK_DBFS   (-0.3f)
/* The pad Volume's own range; a match that would leave it is clamped. */
#define DR32_RS_VOL_MIN     (-36.0f)
#define DR32_RS_VOL_MAX     12.0f

/** Everything the render needs, copied from the pad. */
typedef struct {
    int      pad;                       /* 0-based */
    int      velocity;                  /* 1..127 */
    dr32_pad params;
    int      engine;                    /* DR32_ENG_*, 0 = a sample pad */
    int      model;
    float    eparam[DR32_ENG_MAX_PARAMS];
    char     path[DR32_MAX_PATH];       /* a sample pad's file */
} dr32_rs_src;

/** A finished render. `data` is owned (malloc'd, interleaved), already on the
 *  24-bit grid, so it equals what the written file decodes to. */
typedef struct {
    float  *data;
    size_t  frames;
    int     channels;                   /* 1, or 2 for a genuinely stereo pad */
    float   volume_db;                  /* the new pad's Volume */
    int     clamped;                    /* the match left the Volume range */
    int     capped;                     /* stopped by the cap, not by silence */
} dr32_rs_take;

/** Copy what the render needs. Returns 0 for an empty pad (nothing to take). */
int dr32_rs_snapshot(const dr32_kit *k, int pad, int velocity, dr32_rs_src *out);

/** Is this pad a synth pad — what "Resample kit" takes. */
int dr32_rs_is_synth(const dr32_kit *k, int pad);

/** Render, trim, fade, normalise. Returns 0 on success; -1 when the pad made
 *  no sound, the sample would not load, or memory ran out. Worker only. */
int dr32_rs_render(const dr32_rs_src *src, dr32_rs_take *out);
void dr32_rs_take_free(dr32_rs_take *t);

/** The file's name, without folder or extension: "<source> v<vel> <date>",
 *  source being the model ("FM Kick") or the sample's name, with a previous
 *  resample's " vNN YYYY-MM-DD" taken off first so names do not pile up. */
void dr32_rs_basename(const dr32_rs_src *src, const struct tm *date, char *out, size_t cap);

/** A free path in `dir` for `base`: "<dir>/<base>.wav", else " 2", " 3" ...
 *  (Josh: "append numbers to that where it results in duplicate names").
 *  Creates `dir`. Returns 0, or -1 if it cannot. Worker only. */
int dr32_rs_unique_path(const char *dir, const char *base, char *out, size_t cap);

/** Switch the pad to its take. Returns 1 if switched; 0 if the pad changed
 *  since the snapshot (it is left alone — the file is still written) — and
 *  then `take->data` is NOT consumed. Audio-thread safe. */
int dr32_rs_apply(dr32_kit *k, const dr32_rs_src *src, dr32_rs_take *take,
                  const char *path, long size, long mtime);

/* ---------- the job: one pad, or every synth pad, on a worker thread ------
 *
 * ⚠ set_param IS THE AUDIO (SPI) CALLBACK, so the request, the progress reads
 * and the switch happen there and nothing else does: _start copies and spawns,
 * _service switches finished pads, _status reads counters. The worker renders
 * and writes. A pad keeps its old sound until its file is on the card. */
typedef struct dr32_rs_job dr32_rs_job;

typedef struct {
    int  busy;          /* a job is rendering, or has pads still to switch */
    int  total, done;   /* pads in the last job; finished so far (any outcome) */
    int  switched;      /* now playing their take */
    int  saved_only;    /* written, but the pad had changed: not switched */
    int  failed;        /* nothing written */
    int  clamped;       /* switched, with Volume clamped to its range */
    char last[160];     /* the last file's name, no folder/extension */
} dr32_rs_status;

/** `dir` is where files go (DR32_RS_DIR on the device). Host thread. */
dr32_rs_job *dr32_rs_job_create(const char *dir);
/** Stops after the pad in hand, waits for the worker, frees. */
void dr32_rs_job_destroy(dr32_rs_job *j);
/** Snapshot `pads` (0-based) at `velocity` and start. Returns the number of
 *  pads taken (empty ones are skipped), 0 if none, -1 if a job is still busy
 *  or the thread would not start. Audio thread. */
int  dr32_rs_job_start(dr32_rs_job *j, const dr32_kit *k, const int *pads, int n, int velocity);
/** Switch every pad whose file is written. Returns how many switched. Audio
 *  thread — call it once per block from BOTH render paths. */
int  dr32_rs_job_service(dr32_rs_job *j, dr32_kit *k);
void dr32_rs_job_status(dr32_rs_job *j, dr32_rs_status *out);
/** Test hook: wait for the worker to finish rendering (not to be switched). */
void dr32_rs_job_wait(dr32_rs_job *j);

#endif
