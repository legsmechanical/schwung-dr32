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
#include "dsp/dr32_sendfx.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

static const host_api_v1_t *g_host = nullptr;

/* The four stages, in the order the kit runs them.
 *
 * THEY ARE PRESETS, not a knob. Which stage this instance IS is a choice made
 * once, when the insert is placed; spending a knob on it costs a control every
 * time you touch the effect, forever, to change something you will change
 * approximately never. So the stage is the preset browser -- `preset` /
 * `preset_count` / `preset_name`, the contract the host already has a browser
 * for -- and the knobs are Amount and Dry/Wet, which is what you actually
 * reach for.
 *
 * `effect` remains a settable param, because a declaration is allowed to name
 * the stage directly and because the preset IS the effect here; it is simply
 * not on the knob row. */
/* ...and the seven SEND effects, which are the same `Slot` the kit's send
 * buses ran. They are here because retiring DR32's internal sends would
 * otherwise DELETE them: an effect that exists only inside a synth's own bus
 * disappears the moment that bus does. As inserts on a global send they are
 * the same thing they always were -- a wet return -- and as inserts on a slot
 * they are something the kit never offered.
 *
 * Bus stages and send types share one preset list on purpose. Which one an
 * instance is remains a choice made once, when the insert is placed, and the
 * knobs stay free for the controls you actually reach for. */
enum Effect {
    FX_CRUNCH = 0, FX_ATTACK, FX_SUSTAIN, FX_COMP,   /* the Drum Buss stages */
    FX_PLATE, FX_SPACES, FX_DELAY, FX_GATED,          /* the send types       */
    FX_DIGITAL, FX_HALL, FX_NONLIN, FX_NATIVE,
    FX_COUNT
};
static const char *kEffectName[FX_COUNT] = {
    "Crunch", "Attack", "Sustain", "Comp",
    "Plate", "Spaces", "Delay", "Gated", "Digital", "Hall", "NonLin", "Native"
};
/* The first send preset. Everything below it drives DrumBuss, everything from
 * it up drives Slot -- ONE comparison, so the two halves cannot disagree about
 * which engine an instance is running. */
#define FX_FIRST_SEND FX_PLATE
static const dr32_efx_type kSendType[FX_COUNT - FX_FIRST_SEND] = {
    DR32_EFX_PLATE, DR32_EFX_SPACES, DR32_EFX_DELAY, DR32_EFX_GATED,
    DR32_EFX_DIGITAL, DR32_EFX_HALL, DR32_EFX_NONLIN, DR32_EFX_NATIVE
};

struct Instance {
    dr32::DrumBuss bus;
    dr32::Slot     send;
    int   effect = FX_CRUNCH;
    /* Bipolar for Attack and Sustain (-1..+1 about a neutral 0), unipolar for
     * Crunch and Comp. One stored value: the stage decides how to read it, and
     * only one stage is ever live in an instance. */
    float amount = 0.0f;
    float mix    = 1.0f;
    /* Bit-transparent when the stage is doing nothing. DrumBuss gates each
     * stage internally, but the WRAPPER around it does not: int16->float,
     * de-interleave, re-interleave, float->int16 is four passes over the buffer
     * whatever the knobs say, and a Drum Bus is FOUR of these in series. The
     * kit's own bus has exactly this test (`bus_neutral`) and calls it "the
     * whole reason an always-on stage is acceptable"; an insert that seeds at
     * neutral needs it more, not less. */
    bool  neutral = true;

    bool isSend() const { return effect >= FX_FIRST_SEND; }

    /* The page's visibility hangs off this, exactly as it does on the kit's own
     * send page: the host's visible_if takes a SINGLE condition on a SINGLE
     * param, so "this is a reverb" and "this is a bus stage" each have to be
     * one equality. Deriving it here is what keeps the two rows -- Amount for a
     * stage, the eight send slots for a type -- from being drawn together. */
    const char *modeName() const {
        if (!isSend()) return "Bus";
        switch (kSendType[effect - FX_FIRST_SEND]) {
            case DR32_EFX_DELAY:  return "Delay";
            case DR32_EFX_GATED:  return "Gate";
            case DR32_EFX_NONLIN: return "NonLin";
            default:              return "Verb";
        }
    }

    /* Choosing a type LOADS that type's musical starting point, which is what
     * the kit's send page does and for the same reason: selecting an effect
     * should sound like something immediately rather than inherit the previous
     * effect's knob positions. Only on an actual CHANGE -- re-selecting the
     * preset you are already on must not throw away your edits. */
    void selectEffect(int n) {
        const bool changed = (n != effect);
        effect = n;
        if (changed && isSend()) dr32_efx_defaults(kSendType[effect - FX_FIRST_SEND], send.p);
        apply();
    }

    void apply() {
        if (isSend()) {
            /* The bus stays at its zeroed defaults, which DrumBuss gates off
             * entirely, so an instance is one engine or the other and never
             * pays for both. */
            send.type = kSendType[effect - FX_FIRST_SEND];
            send.apply();
            /* NEVER neutral: a reverb with a wet mix is doing work by
             * definition, and `mix` at zero is the honest way to switch it off.
             * Saying otherwise here would silence a tail mid-decay. */
            neutral = (mix <= 0.0f);
            return;
        }
        /* EVERY OTHER STAGE NEUTRAL. This is the whole trick: DrumBuss gates
         * each stage on its own parameter, so the three that are zero cost
         * nothing and change nothing, and the one that is not behaves exactly
         * as it does inside the kit. */
        const float c  = (effect == FX_CRUNCH)  ? amount : 0.0f;
        const float a  = (effect == FX_ATTACK)  ? amount : 0.0f;
        const float s  = (effect == FX_SUSTAIN) ? amount : 0.0f;
        const float cp = (effect == FX_COMP)    ? amount : 0.0f;
        bus.setParams(cp, c, 0.5f + 0.5f * a, 0.5f + 0.5f * s);
        /* The SAME tolerances DrumBuss gates its own stages on -- compOn is
         * c > 0.001, and the shapers are +/-0.005 in the 0..1 domain, so
         * +/-0.01 here. The two must not disagree about what neutral means, or
         * this returns early on a stage that would have done something. */
        neutral = (cp <= 0.001f) && (c <= 0.001f) &&
                  (fabsf(a) <= 0.01f) && (fabsf(s) <= 0.01f);
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
    in->send.setSampleRate(44100.0f);
    in->apply();
    return in;
}

static void v2_destroy_instance(void *instance) { delete static_cast<Inst *>(instance); }

static void v2_process_block(void *instance, int16_t *audio, int frames) {
    Inst *in = static_cast<Inst *>(instance);
    if (!in || !audio || frames <= 0) return;
    /* One bool test and the buffer is untouched -- not "runs and does nothing".
     * Four of these seed with a Drum Bus, and at rest they must cost what the
     * baked-in stage cost, which was nothing. */
    if (in->neutral) return;
    if (frames > kMaxFrames) frames = kMaxFrames;   /* clamp, never overrun */

    for (int i = 0; i < 2 * frames; i++) in->io[i] = audio[i] * (1.0f / 32768.0f);

    /* Dry copy only when it is going to be blended back. At mix = 1 this is a
     * plain in-place run, exactly as the kit's bus does it. */
    const bool blend = in->mix < 0.999f;
    if (blend) memcpy(in->dry, in->io, sizeof(float) * 2 * (size_t)frames);

    if (in->isSend()) in->send.processBlock(in->io, frames, in->sl, in->sr);
    else              in->bus.processBlock(in->io, frames, in->sl, in->sr);

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

    /* PRESET AND EFFECT ARE THE SAME CHOICE, by two names. `preset` is an
     * index (what the browser sends), `preset_name` a name (what a default_fx
     * or default_buses declaration sends), `effect` the original spelling.
     * Routing all three to one place is what stops the browser and a
     * declaration from disagreeing about which stage this is. */
    if (!strcmp(key, "preset")) {
        int n = atoi(val);
        if (n >= 0 && n < FX_COUNT) in->selectEffect(n);
        return;
    }
    if (!strcmp(key, "preset_name") || !strcmp(key, "effect")) {
        /* BY NAME OR BY INDEX. The host learns an enum's wire format from what
         * the plugin reports, and reports names here -- but a declaration in a
         * module.json may hand over either, so both are accepted rather than
         * having one of them silently select Crunch. */
        for (int i = 0; i < FX_COUNT; i++) {
            if (!strcmp(val, kEffectName[i])) { in->selectEffect(i); return; }
        }
        int n = atoi(val);
        if (n >= 0 && n < FX_COUNT) in->selectEffect(n);
        return;
    }
    if (!strcmp(key, "amount")) { in->amount = (float)atof(val); in->apply(); return; }
    /* `sync` is a WORD on the wire (the enum reports names) and a number in a
     * restored state or a script, exactly as it is on the kit. */
    if (!strcmp(key, "sync")) {
        in->send.p[5] = !strcmp(val, "Sync") ? 1.0f
                  : !strcmp(val, "Free") ? 0.0f
                  : ((float)atof(val) >= 0.5f ? 1.0f : 0.0f);
        in->apply();
        return;
    }
    {
        /* One table, shared with the kit -- see dr32_send_slot_index. */
        int idx = dr32_send_slot_index(key);
        if (idx >= 0) { in->send.p[idx] = (float)atof(val); in->apply(); return; }
    }
    if (!strcmp(key, "mix")) {
        float m = (float)atof(val);
        in->mix = m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
        /* A send instance's neutrality IS its mix, so the gate has to be
         * recomputed here; for a bus stage apply() is a no-op on this path. */
        in->apply();
        return;
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    Inst *in = static_cast<Inst *>(instance);
    if (!in || !key || !buf || buf_len < 2) return -1;
    /* The browser contract: a count, an index, and a name. */
    if (!strcmp(key, "preset_count")) return snprintf(buf, buf_len, "%d", FX_COUNT);
    if (!strcmp(key, "preset"))       return snprintf(buf, buf_len, "%d", in->effect);
    if (!strcmp(key, "preset_name") || !strcmp(key, "effect"))
        return snprintf(buf, buf_len, "%s", kEffectName[in->effect]);
    if (!strcmp(key, "amount")) return snprintf(buf, buf_len, "%g", (double)in->amount);
    if (!strcmp(key, "mode"))   return snprintf(buf, buf_len, "%s", in->modeName());
    /* Does this type have an envelope with a release? Gated and NonLin do; the
     * reverbs and the delay do not. One more derived value, for `mode`'s
     * reason: visible_if has no OR. */
    if (!strcmp(key, "env")) {
        const bool e = in->isSend() &&
                       (kSendType[in->effect - FX_FIRST_SEND] == DR32_EFX_GATED ||
                        kSendType[in->effect - FX_FIRST_SEND] == DR32_EFX_NONLIN);
        return snprintf(buf, buf_len, "%s", e ? "Env" : "-");
    }
    /* "-" when the type is not a delay at all. That sentinel is what lets the
     * two time pages hang off a SINGLE equality -- without it a reverb would
     * draw the delay's time knobs, since `sync` would still read "Sync". */
    if (!strcmp(key, "sync")) {
        if (!in->isSend() || kSendType[in->effect - FX_FIRST_SEND] != DR32_EFX_DELAY)
            return snprintf(buf, buf_len, "%s", "-");
        return snprintf(buf, buf_len, "%s", in->send.p[5] >= 0.5f ? "Sync" : "Free");
    }
    {
        int idx = dr32_send_slot_index(key);
        if (idx >= 0) return snprintf(buf, buf_len, "%g", (double)in->send.p[idx]);
    }
    if (!strcmp(key, "mix"))    return snprintf(buf, buf_len, "%g", (double)in->mix);
    /* WHAT THE BOX SAYS. Four instances of one binary would otherwise all wear
     * the module's abbreviation and be indistinguishable in the chain diagram --
     * three boxes reading the same three letters, which is what "FX 1, FX 2,
     * FX 3" looks like once you cannot tell them apart. The host polls this for
     * the handful of effects that report a live identity. */
    if (!strcmp(key, "display_name"))
        return snprintf(buf, buf_len, "%s", kEffectName[in->effect]);
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
