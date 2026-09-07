// dr32_fx.cpp — DR32's Drum Buss stages, as ordinary chain effects.
//
// ONE BINARY, FOUR EFFECTS, selected by the `effect` param — the shape
// Airwindows already uses (one .so, 500+ effects, chosen by plugin_id).
// audio_fx_api_v2 is multi-instance, so putting Crunch, Attack, Sustain and
// Comp in one bus is four create_instance calls on this file.
//
// WHY THIS EXISTS. DR32's Drum Buss is four stages in series over the whole
// kit, and it lived inside the synth: not reorderable, not bypassable with the
// host's gesture, not an LFO target, not swappable for something else. Shipping
// it as ONE effect would keep all of that; shipping it as four is what lets you
// swap just the compressor, or put a drive between two stages.
//
// IT IS THE SAME DSP. dr32_drumbus.h is the struct the kit runs, shared rather
// than copied. Each stage is independently gated in there (atkOn, susOn,
// crunch > 0, the compressor's own amount), so an instance with ONE stage
// non-neutral is bit-identical to that stage inside the full bus. No new DSP
// was written for this module — it is that struct with three of four knobs at
// zero.
//
// The stages are NOT bit-identical to the old fixed chain when combined,
// because each instance now carries its own state and the order is the user's.
// That is the point of the split, and it is worth hearing before it replaces
// anything.

#include "host/audio_fx_api_v2.h"
#include "dsp/dr32_drumbus.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

static const host_api_v1_t *g_host = nullptr;

/* The four stages, in the order the kit runs them. The INDEX is the wire value
 * of `effect`, so it is also the enum order the host draws — see module.json,
 * which must not be reordered independently of this. */
enum Effect { FX_CRUNCH = 0, FX_ATTACK, FX_SUSTAIN, FX_COMP, FX_COUNT };
static const char *kEffectName[FX_COUNT] = { "Crunch", "Attack", "Sustain", "Comp" };

struct Instance {
    dr32::DrumBuss bus;
    int   effect = FX_CRUNCH;
    /* Bipolar for Attack and Sustain (-1..+1 about a neutral 0), unipolar for
     * Crunch and Comp. One stored value: the stage decides how to read it, and
     * only one stage is ever live in an instance. */
    float amount = 0.0f;
    float mix    = 1.0f;

    void apply() {
        /* EVERY OTHER STAGE NEUTRAL. This is the whole trick: DrumBuss gates
         * each stage on its own parameter, so the three that are zero cost
         * nothing and change nothing, and the one that is not behaves exactly
         * as it does inside the kit. */
        const float c  = (effect == FX_CRUNCH)  ? amount : 0.0f;
        const float a  = (effect == FX_ATTACK)  ? amount : 0.0f;
        const float s  = (effect == FX_SUSTAIN) ? amount : 0.0f;
        const float cp = (effect == FX_COMP)    ? amount : 0.0f;
        bus.setParams(cp, c, 0.5f + 0.5f * a, 0.5f + 0.5f * s);
    }
};

/* Scratch for the de-interleave DrumBuss wants. Per INSTANCE, not shared: two
 * instances in one bus run back to back on the same callback, and a shared
 * buffer would have the second read the first's samples. */
static const int kMaxFrames = 1024;

struct Inst : Instance {
    float sl[kMaxFrames];
    float sr[kMaxFrames];
    float io[2 * kMaxFrames];
    /* THE DRY COPY LIVES HERE, not on the stack. process_block runs on the SPI
     * audio callback, and 2 * kMaxFrames floats is 8 KB of stack frame per
     * insert -- four of them in a bus is 32 KB on a thread whose budget is
     * measured in microseconds and whose stack is not ours to grow. Same rule
     * the chain host learned when a 232 KB patch_info_t frame moved off it. */
    float dry[2 * kMaxFrames];
};

static void *v2_create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir; (void)config_json;
    Inst *in = new (std::nothrow) Inst();
    if (!in) return nullptr;
    in->bus.setSampleRate(44100.0f);
    in->apply();
    return in;
}

static void v2_destroy_instance(void *instance) { delete static_cast<Inst *>(instance); }

static void v2_process_block(void *instance, int16_t *audio, int frames) {
    Inst *in = static_cast<Inst *>(instance);
    if (!in || !audio || frames <= 0) return;
    if (frames > kMaxFrames) frames = kMaxFrames;   /* clamp, never overrun */

    for (int i = 0; i < 2 * frames; i++) in->io[i] = audio[i] * (1.0f / 32768.0f);

    /* Dry copy only when it is going to be blended back. At mix = 1 this is a
     * plain in-place run, exactly as the kit's bus does it. */
    const bool blend = in->mix < 0.999f;
    if (blend) memcpy(in->dry, in->io, sizeof(float) * 2 * (size_t)frames);

    in->bus.processBlock(in->io, frames, in->sl, in->sr);

    if (blend) {
        for (int i = 0; i < 2 * frames; i++)
            in->io[i] = in->dry[i] + (in->io[i] - in->dry[i]) * in->mix;
    }
    for (int i = 0; i < 2 * frames; i++) {
        float v = in->io[i] * 32768.0f;
        if (v >  32767.0f) v =  32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        audio[i] = (int16_t)v;
    }
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    Inst *in = static_cast<Inst *>(instance);
    if (!in || !key || !val) return;

    if (!strcmp(key, "effect")) {
        /* BY NAME OR BY INDEX. The host learns an enum's wire format from what
         * the plugin reports, and reports names here -- but a declaration in a
         * module.json may hand over either, so both are accepted rather than
         * having one of them silently select Crunch. */
        for (int i = 0; i < FX_COUNT; i++) {
            if (!strcmp(val, kEffectName[i])) { in->effect = i; in->apply(); return; }
        }
        int n = atoi(val);
        if (n >= 0 && n < FX_COUNT) { in->effect = n; in->apply(); }
        return;
    }
    if (!strcmp(key, "amount")) { in->amount = (float)atof(val); in->apply(); return; }
    if (!strcmp(key, "mix")) {
        float m = (float)atof(val);
        in->mix = m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
        return;
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    Inst *in = static_cast<Inst *>(instance);
    if (!in || !key || !buf || buf_len < 2) return -1;
    if (!strcmp(key, "effect"))
        return snprintf(buf, buf_len, "%s", kEffectName[in->effect]);
    if (!strcmp(key, "amount")) return snprintf(buf, buf_len, "%g", (double)in->amount);
    if (!strcmp(key, "mix"))    return snprintf(buf, buf_len, "%g", (double)in->mix);
    return -1;
}

static audio_fx_api_v2_t g_api;

extern "C" audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    memset(&g_api, 0, sizeof(g_api));
    g_api.api_version     = AUDIO_FX_API_VERSION_2;
    g_api.create_instance = v2_create_instance;
    g_api.destroy_instance = v2_destroy_instance;
    g_api.process_block   = v2_process_block;
    g_api.set_param       = v2_set_param;
    g_api.get_param       = v2_get_param;
    return &g_api;
}
