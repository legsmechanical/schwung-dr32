// dr32_engine_kit.h — optional helpers for writing a DR32 engine plugin.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Josh Gaines / legsmechanical
// (The MIT permission notice is in dr32_engine_api.h, which this includes and
// travels with.)
//
// Nothing here is part of the contract (that is dr32_engine_api.h, which this
// includes). It is the code every adapter otherwise writes by hand: the export
// attribute, parameter rows, the knob mappings a linear knob needs, tune to a
// frequency ratio, stereo to mono. Header-only; copy it beside the API header.

#ifndef DR32_ENGINE_KIT_H
#define DR32_ENGINE_KIT_H

#include "dr32_engine_api.h"

#include <math.h>

/* Put this on dr32_engine_plugin and build with -fvisibility=hidden, so it is
 * the only symbol the plugin exports. */
#define DR32X_EXPORT __attribute__((visibility("default")))

/* ---- parameter rows ------------------------------------------------------
 * A row of a `dr32x_param` table: key, knob label, cell label, default, page.
 *
 *   static const dr32x_param PARAMS[] = {
 *       DR32X_HZ   ("pitch", "Pitch", "PITCH", 40, 400, 110, "Tone"),
 *       DR32X_PCT  ("drive", "Drive", "DRIVE", 20, "Tone"),
 *       DR32X_TIME ("decay", "Decay", "DECAY", 60, "Tone"),
 *       DR32X_ENUM ("wave",  "Wave",  "WAVE",  0, "Tone", "Sine|Saw|Square", 3),
 *   };
 */
/* 0..100 %. Scale to your own 0..1 with dr32x_unit(). */
#define DR32X_PCT(key, name, shrt, def, page)    { key, name, shrt, 0, 100, def, 1, "%", page, NULL }
/* -100..+100 %, for a bipolar amount. dr32x_unit() gives -1..1. */
#define DR32X_BIPCT(key, name, shrt, def, page)  { key, name, shrt, -100, 100, def, 1, "%", page, NULL }
/* A plain range in Hz, whole steps. Keep it narrow: the knob is linear. */
#define DR32X_HZ(key, name, shrt, lo, hi, def, page) { key, name, shrt, lo, hi, def, 1, "hz", page, NULL }
/* A TIME as a 0..100 position, to be mapped with dr32x_exp(): short times get
 * most of the knob. The cell shows the position, not the time. */
#define DR32X_TIME(key, name, shrt, def, page)   { key, name, shrt, 0, 100, def, 0.1f, NULL, page, NULL }
/* A choice. `count` is the number of options; the value is the index. */
#define DR32X_ENUM(key, name, shrt, def, page, options, count) \
    { key, name, shrt, 0, (count) - 1, def, 1, NULL, page, options }
/* Anything else: a whole-number or (step < 1) a float knob. */
#define DR32X_KNOB(key, name, shrt, lo, hi, def, step, unit, page) \
    { key, name, shrt, lo, hi, def, step, unit, page, NULL }

/* ---- display value -> what your DSP wants -------------------------------- */

/** A DR32X_PCT / DR32X_BIPCT value as 0..1 / -1..1. */
static inline float dr32x_unit(float display) { return display * 0.01f; }

/** A DR32X_ENUM value as its index. */
static inline int dr32x_index(float display) { return (int)(display + 0.5f); }

/** A 0..100 position on an exponential scale from `lo` to `hi` (both > 0):
 *  dr32x_exp(v, 0.005f, 4.0f) is 5 ms at 0, 4 s at 100, ~140 ms at 50. For a
 *  DR32X_TIME knob, or any range that spans decades (a cutoff in Hz). */
static inline float dr32x_exp(float display, float lo, float hi) {
    float t = display < 0.0f ? 0.0f : display > 100.0f ? 1.0f : display * 0.01f;
    return lo * powf(hi / lo, t);
}

/** The reverse: the 0..100 position that gives `value`. For turning one of
 *  your presets into a model's display values. */
static inline float dr32x_exp_inv(float value, float lo, float hi) {
    if (value < lo) value = lo;
    if (value > hi) value = hi;
    return 100.0f * logf(value / lo) / logf(hi / lo);
}

/** `v` as the knob can hold it: on the parameter's step, inside its range.
 *  Model values must be inside the range or the plugin is refused. */
static inline float dr32x_snap(const dr32x_param *p, float v) {
    v = roundf(v / p->step) * p->step;
    return v < p->min ? p->min : v > p->max ? p->max : v;
}

/* ---- note_on ------------------------------------------------------------- */

/** `tune_st` (the pad's transpose + detune, semitones) as a frequency ratio:
 *  multiply your voice's base frequency by it. */
static inline float dr32x_tune_ratio(float tune_st) { return powf(2.0f, tune_st / 12.0f); }

/** vel01 as a MIDI velocity 1..127, for a voice written against one. */
static inline int dr32x_velocity(float vel01) {
    int v = (int)(vel01 * 127.0f + 0.5f);
    return v < 1 ? 1 : v > 127 ? 127 : v;
}

/* ---- render -------------------------------------------------------------- */

/** A stereo voice into DR32's mono `out`: (l + r) * gain. Use 0.5 for a voice
 *  that writes its full level to both sides, 1.0 for one that already halved
 *  it to pan to the centre. */
static inline void dr32x_stereo_to_mono(const float *l, const float *r, float *out, int n, float gain) {
    for (int i = 0; i < n; i++) out[i] = (l[i] + r[i]) * gain;
}

#endif
