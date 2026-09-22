/*
 * kit_port.h — what the kit-port engines (9W9, 6W6, 8W8, CW-78) share.
 *
 * Those four are whole drum MACHINES, not voices: one engine struct holding
 * every lane, a per-lane drive stage, send reverb and delay, and a master
 * stage. DR32 plays ONE LANE of one per pad. Each adapter keeps the machine's
 * own struct and its own trigger and setter code, untouched (vendored under
 * dsp/engines/<port>/ and #included into the adapter), and renders only the
 * lane its pad plays, through that lane's own drive stage. The machine's
 * reverb, delay, master distortion, glue and rhythm player are not run —
 * DR32's mix stage and sends are the signal path from there on.
 *
 * ⚠⚠ THE MACHINES NEVER STOP A LANE. Their lane guard is the choke gain, which
 * sits at 1.0 from the first hit onward, so a lane that has rung out keeps
 * being computed forever (their kits get away with it: one of each lane). With
 * up to 32 pads that is 32 circuits rendering silence, so the gate below is
 * DR32's, not theirs: the Faust voices' 100 ms below 0.001 (faust_voice.h).
 *
 * Plain C, so the C port (9W9) and the C++ ones share it.
 */
#ifndef DR32_KIT_PORT_H
#define DR32_KIT_PORT_H

#include <math.h>

#define KP_SILENCE_MS    100
#define KP_SILENCE_LEVEL 0.001f

typedef struct {
    int silence, silence_max, active;
} kp_gate;

static inline void kp_gate_init(kp_gate *g, int sr) {
    g->silence_max = sr * KP_SILENCE_MS / 1000;
    g->silence = g->silence_max;
    g->active = 0;
}

static inline void kp_gate_hit(kp_gate *g) {
    g->silence = 0;
    g->active = 1;
}

/* One output sample through the gate. */
static inline void kp_gate_frame(kp_gate *g, float x) {
    if (fabsf(x) > KP_SILENCE_LEVEL) g->silence = 0;
    else g->silence++;
}

/* End of a block: is the lane still worth computing? */
static inline int kp_gate_end(kp_gate *g) {
    if (g->silence >= g->silence_max) g->active = 0;
    return g->active;
}

/*
 * DR32's transpose on a lane's Tune. Every port stores Tune in one of two
 * engineering forms, and the pot table's curve says which:
 *   KP_TUNE_ST     semitones around the lane's own note (a LIN pot): add
 *   KP_TUNE_RATIO  a frequency or playback ratio (an EXP pot): multiply
 *   KP_TUNE_NONE   not a pitch (9W9's kick: Tune is its SWEEP time), so the
 *                  pad's transpose leaves it alone
 * The pot itself is never moved: the offset is applied to the value the
 * machine reads, so the knob still shows the pad's own Tune.
 */
enum { KP_TUNE_NONE = 0, KP_TUNE_ST = 1, KP_TUNE_RATIO = 2 };

static inline float kp_tune(float value, int kind, float st) {
    switch (kind) {
        case KP_TUNE_ST:    return value + st;
        case KP_TUNE_RATIO: return value * powf(2.0f, st / 12.0f);
        default:            return value;
    }
}

#endif
