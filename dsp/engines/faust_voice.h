/*
 * faust_voice.h — one Faust drum voice driven the way the OneTrick ports
 * drive theirs, minus everything that belonged to their kits.
 *
 * Shared by every engine TU (simian_engine.cpp, urchin_engine.cpp). It is
 * header-only and has no state of its own, so each TU compiles its own copy;
 * the one thing that must not be duplicated across TUs — a generated DSP class
 * — never passes through here by name.
 *
 * ⚠⚠ THE TRIGGER EDGE IS FORCED, PER VOICE. Faust edge-detects `Trigger`
 * (`Trigger > Trigger'`), and on Move a press and its release can both land
 * between two render calls — a write of the velocity followed by a write of 0
 * is then one value and no hit at all. So note_on drops the zone to 0 and
 * parks the velocity; render computes ONE frame with the zone at 0 and raises
 * it for the rest. The ports shrink the whole kit's block to one frame to do
 * this; here each pad renders alone, so only the voice being hit pays for it.
 *
 * Silence gate: upstream's SilenceTracker. A voice whose output has been
 * below 0.001 for 100 ms stops being computed until its next hit — a drum
 * machine is silent most of the time, and that is the whole CPU budget.
 */
#ifndef DR32_FAUST_VOICE_H
#define DR32_FAUST_VOICE_H

#include <cmath>
#include <cstring>

#include "faust_shim.h"

#define DR32_FV_BUF 256
#define DR32_FV_SILENCE_MS 100
#define DR32_FV_SILENCE_LEVEL 0.001f

struct Dr32FaustVoice {
    dsp        *d = nullptr;
    ZoneMap     zones;
    FAUSTFLOAT *z_trigger = nullptr;
    FAUSTFLOAT *z_wake = nullptr;

    float       pending = -1.0f;    /* velocity awaiting its forced edge, or -1 */
    int         silence = 0;        /* consecutive quiet frames */
    int         silence_max = 0;
    int         active = 0;

    float       buf[3][DR32_FV_BUF];  /* L, R, reverb send (unused) */
};

static inline float dr32_fv_clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Takes ownership of `d`. Builds the zone map and pins WakeUp on: the host
 * renders continuously, so there is no idle state for upstream's wake burn to
 * recover from. */
static inline void dr32_fv_setup(Dr32FaustVoice *v, dsp *d, int sr) {
    v->d = d;
    d->init(sr);
    d->buildUserInterface(&v->zones);
    v->z_trigger = v->zones.find("Trigger");
    v->z_wake = v->zones.find("WakeUp");
    if (v->z_wake) *v->z_wake = 1.0f;
    v->silence_max = sr * DR32_FV_SILENCE_MS / 1000;
    v->silence = v->silence_max;
    v->active = 0;
    v->pending = -1.0f;
}

static inline void dr32_fv_set_zone(FAUSTFLOAT *z, float x) {
    if (z) *z = (FAUSTFLOAT)x;
}

static inline void dr32_fv_note_on(Dr32FaustVoice *v, float vel01) {
    if (v->z_wake) *v->z_wake = 1.0f;
    v->silence = 0;
    v->active = 1;
    if (v->z_trigger) *v->z_trigger = 0.0f;
    v->pending = dr32_fv_clamp(vel01, 0.0f, 1.0f);
}

static inline int dr32_fv_render(Dr32FaustVoice *v, float *out, int n) {
    if (!v->active) {
        memset(out, 0, sizeof(float) * (size_t)n);
        return 0;
    }
    int done = 0;
    while (done < n) {
        int m = n - done;
        if (m > DR32_FV_BUF) m = DR32_FV_BUF;
        if (v->pending >= 0.0f) m = 1;          /* one frame at 0, then the edge */

        FAUSTFLOAT *outs[3] = {v->buf[0], v->buf[1], v->buf[2]};
        v->d->compute(m, nullptr, outs);

        /* Mono. Every engine here pans with a balance law whose centre is
         * unity on both sides (shared.lib stereoPanner, SIMIAN's drum.dsp),
         * and pan is pinned to centre, so L IS the voice. DR32 pans it. */
        for (int i = 0; i < m; i++) {
            float x = v->buf[0][i];
            out[done + i] = x;
            if (fabsf(x) > DR32_FV_SILENCE_LEVEL) v->silence = 0;
            else v->silence++;
        }
        done += m;

        if (v->pending >= 0.0f) {
            if (v->z_trigger) *v->z_trigger = (FAUSTFLOAT)v->pending;
            v->pending = -1.0f;
        }
    }
    if (v->silence >= v->silence_max) v->active = 0;
    return v->active;
}

#endif
