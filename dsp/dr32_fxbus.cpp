// dr32_fxbus.cpp — implementation of DR32's send buses.
//
// C++ because the vendored reverbs are C++ structs (see dsp/vendor/SOURCES.md);
// the interface is extern "C" so the rest of the C11 engine is unaffected.

#include "dr32_fxbus.h"

#include "vendor/airwin_spaces.h"
#include "vendor/airwin_dyn.h"
#include "dr32_drumbus.h"
#include "dr32_sendfx.h"
#include "vendor/space_extra.h"
// Not vendored — ours, but a PORT rather than a design. Read its header before
// touching anything it does.
#include "dr32_supereco.h"

#include <cstring>
#include <cmath>
#include <cstdlib>
#include <new>

#define DR32_SEND_SLOTS   2
#define DR32_MAX_BLOCK    1024

namespace {

/** Drum Bus — a drum-bus glue insert in the spirit of Ableton's Drum Buss.
 *
 *    Compress   — Airwindows *Pop3*'s compressor section, driven by ONE knob
 *                 that sweeps a coordinated threshold + ratio + release
 *                 program, followed by our own makeup gain.
 *    Crunch     — full-band saturation: tanh soft knee, a cubic term for grit,
 *                 and a deliberate asymmetry so it produces EVEN harmonics too.
 *    Attack     — fast/smoothed envelope pair, shaping the onset only.
 *    Sustain    — dual-release envelope detector shaping the decay only.
 *
 *  Attack and Sustain are orthogonal AND symmetric by measurement: sweeping
 *  Sustain end to end moves the tail -8.0 to +11.9 dB with the attack at
 *  0.00 dB, and sweeping Attack moves the hit -14.5 to +14.6 dB with the tail
 *  at 0.00 dB. Attack is also program-dependent — a 40 ms swell gets about half
 *  the treatment of a real hit, and steady material moves 0.17 dB.
 *
 *  ── why not what was here before ────────────────────────────────────────
 *  The previous Compress was Airwindows Pressure4, a vari-mu LEVELLER. It was
 *  measured against a drum groove and failed on every count that matters here:
 *
 *    - no threshold at all. Its curve was ~2:1 across the WHOLE range, so a
 *      -48 dBFS signal came out at -30 dBFS: +17.7 dB of lift applied to
 *      sample noise floor, room bleed and reverb tails.
 *    - half the knob was dead — comp=0.50 produced 0.3 dB of gain reduction,
 *      and even at 1.00 only 4.8 dB.
 *    - ~20 ms to reach full gain reduction, so it never caught a kick
 *      (measured kick GR at full knob: 2.0 dB).
 *    - backwards on a groove: a hat landing after a kick came out 17.8 dB
 *      quieter than an isolated hat. It buried the hats instead of holding
 *      the kick.
 *
 *  Pop3 fixes this structurally, not by tuning. Its gain is
 *  (1-ratio) + (popComp*ratio) with popComp clamped to [0,1], so it can only
 *  ever ATTENUATE — measured 0.00 dB of lift on a -48 dBFS sine at every
 *  setting. Makeup is therefore ours to apply deliberately.
 *
 *  The old Transients stage was a single broadband gain from the ratio of two
 *  envelope followers, so the tail tracked the attack instead of opposing it:
 *  at knob 0 it took the attack down 18.1 dB and the tail down 3.0 dB. There
 *  was no sustain control anywhere on it, despite the comment claiming one.
 *  Both stages now use in-house detectors that are symmetric by construction
 *  and target disjoint parts of the hit.
 */
using dr32::DrumBuss;

using efx::SpaceExtra;

/** Input diffusion for the plate.
 *
 *  Measured against the Airwindows plates at a matched 1.20 s RT60, our tank's
 *  first 30 ms had a crest factor of 19.4 dB against their 12.0–14.5 — i.e. a
 *  handful of discrete echoes where they had a wash. Sparse-and-bright is
 *  exactly the recipe that reads as "metallic", and our tank also measured
 *  +7.3 dB of HF tilt.
 *
 *  Four cascaded allpasses per channel with mutually-prime delays smear the
 *  input into the tank without colouring it (an allpass is flat by
 *  construction). The two channels use different lengths so the plate keeps
 *  the decorrelation it already had. */
using dr32::Slot;

}  // namespace

struct dr32_fxbus {
    float sample_rate = 44100.0f;
    Slot  sends[DR32_SEND_SLOTS];
    float send_return[DR32_SEND_SLOTS] = { 1.0f, 1.0f };
    // The always-on Drum Bus over the summed mix. Neutral by default, and
    // BYPASSED while neutral — see bus_neutral.
    DrumBuss bus;
    bool  bus_neutral = true;
    float bus_mix = 1.0f;                  // dry/wet blend = parallel compression
    float bus_dry[2 * DR32_MAX_BLOCK];     // pre-bus copy, only filled when mix < 1
    // Per-block accumulation of what the pads sent to each bus.
    float send_buf[DR32_SEND_SLOTS][2 * DR32_MAX_BLOCK];
    // Blocks since anything was fed to each bus. A loaded-but-unused reverb
    // should cost nothing, but its tail must still ring out first.
    int   idle_blocks[DR32_SEND_SLOTS] = { 0, 0 };
    // De-interleave scratch, shared by every slot: the vendored algorithms are
    // block-based with separate L/R pointers. One copy, not one per slot.
    float scratch_l[DR32_MAX_BLOCK];
    float scratch_r[DR32_MAX_BLOCK];
};

extern "C" {

int dr32_efx_is_tank(dr32_efx_type t) {
    return Slot::spaceType(t) >= 0 ? 1 : 0;
}

dr32_fxbus *dr32_fxbus_create(float sample_rate) {
    dr32_fxbus *fx = new (std::nothrow) dr32_fxbus();
    if (!fx) return nullptr;
    fx->sample_rate = (sample_rate > 1.0f) ? sample_rate : 44100.0f;
    for (int i = 0; i < DR32_SEND_SLOTS; i++) fx->sends[i].setSampleRate(fx->sample_rate);
    fx->bus.setSampleRate(fx->sample_rate);
    // Neutral: Attack and Sustain are bipolar about 0 on this side of the API,
    // 0.5 inside DrumBuss. Fully wet, so Mix only ever takes the stage AWAY.
    dr32_fxbus_set_bus_params(fx, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    std::memset(fx->send_buf, 0, sizeof(fx->send_buf));
    return fx;
}

void dr32_fxbus_destroy(dr32_fxbus *fx) { delete fx; }

void dr32_fxbus_set_send_type(dr32_fxbus *fx, int slot, dr32_efx_type type) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    if (fx->sends[slot].type == type) return;
    fx->sends[slot].type = type;
    fx->sends[slot].reset();
}

void dr32_fxbus_set_send_params(dr32_fxbus *fx, int slot, const float *p, int n) {
    if (!fx || !p || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    Slot &s = fx->sends[slot];
    if (n > DR32_SEND_PARAMS) n = DR32_SEND_PARAMS;
    for (int i = 0; i < n; i++) s.p[i] = p[i];
    s.apply();                    // a send return is ALWAYS 100% wet
}

void dr32_fxbus_set_bpm(dr32_fxbus *fx, float bpm) {
    if (!fx) return;
    // setBpm early-outs on an unchanged value, so this is a float compare per
    // block in the common case — cheap enough to call from render_block.
    for (int i = 0; i < DR32_SEND_SLOTS; i++) fx->sends[i].delay.setBpm(bpm);
}

void dr32_fxbus_set_bus_params(dr32_fxbus *fx, float compress, float crunch,
                               float attack, float sustain, float mix) {
    if (!fx) return;
    // Bipolar -1..+1 on the way in, 0..1 about 0.5 inside DrumBuss. This is the
    // ONE place that conversion lives.
    fx->bus.setParams(compress, crunch, 0.5f + 0.5f * attack, 0.5f + 0.5f * sustain);
    fx->bus_mix = mix < 0.0f ? 0.0f : (mix > 1.0f ? 1.0f : mix);
    // The bypass test, and the whole reason an always-on stage is acceptable.
    // The tolerances match DrumBuss's own atkOn/susOn gates (which are ±0.005 in
    // the 0..1 domain, so ±0.01 here) — the two must not disagree about what
    // neutral means. Deliberately NOT keyed on mix: at neutral the stage passes
    // its input through, so blending it against the dry is still the dry.
    fx->bus_neutral = (compress <= 0.001f) && (crunch <= 0.001f) &&
                      (fabsf(attack) <= 0.01f) && (fabsf(sustain) <= 0.01f);
}

void dr32_fxbus_set_send_return(dr32_fxbus *fx, int slot, float gain) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    fx->send_return[slot] = gain;
}

void dr32_fxbus_send(dr32_fxbus *fx, int slot, int frame, float l, float r) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    if (frame < 0 || frame >= DR32_MAX_BLOCK) return;
    fx->send_buf[slot][2 * frame]     += l;
    fx->send_buf[slot][2 * frame + 1] += r;
}

void dr32_fxbus_process(dr32_fxbus *fx, float *out, int n) {
    if (!fx || !out || n <= 0) return;
    if (n > DR32_MAX_BLOCK) n = DR32_MAX_BLOCK;

    // --- send buses: run the effect fully wet, add the return into the mix
    for (int s = 0; s < DR32_SEND_SLOTS; s++) {
        Slot &slot = fx->sends[s];
        float *buf = fx->send_buf[s];

        int fed = 0;
        for (int i = 0; i < 2 * n; i++) { if (buf[i] != 0.0f) { fed = 1; break; } }
        fx->idle_blocks[s] = fed ? 0 : (fx->idle_blocks[s] + 1);
        // ~4 s of silence is well past any tail these reverbs produce.
        const int idle_limit = (int)(4.0f * 44100.0f / (float)(n > 0 ? n : 128));

        // ⚠ The idle skip is disabled in null-test mode: its output gate cuts
        // at 1e-6 (-120 dB), which would otherwise put a floor under the null
        // depth that has nothing to do with the model.
        if (slot.active() &&
            (slot.nativeRawMode || fx->idle_blocks[s] <= idle_limit)) {
            const float g = fx->send_return[s];   // return is fully wet by design
            slot.processBlock(buf, n, fx->scratch_l, fx->scratch_r);
            // The idle counter is armed by the INPUT, but held open by the
            // OUTPUT. A reverb dies well inside four seconds, so input alone was
            // enough; a Delay at 16 sixteenths with high feedback rings far
            // longer than that and would have been cut off mid-repeat — silence
            // between hits is exactly the state a delay is FOR. Anything still
            // making sound keeps its slot alive, whatever the algorithm.
            float outPeak = 0.0f;
            for (int i = 0; i < n; i++) {
                float l = buf[2 * i], r = buf[2 * i + 1];
                float a = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
                if (a > outPeak) outPeak = a;
                out[2 * i]     += l * g;
                out[2 * i + 1] += r * g;
            }
            if (outPeak > 1e-6f) fx->idle_blocks[s] = 0;
        }
        std::memset(buf, 0, sizeof(float) * 2 * (size_t)n);
    }

    // --- the always-on Drum Bus, over the summed mix (dry + both returns).
    // Bypassed entirely while neutral: not "runs and does nothing", actually
    // skipped, so the stage is bit-transparent and costs one bool test per
    // block for anyone who never opens the page.
    if (!fx->bus_neutral) {
        const float mix = fx->bus_mix;
        // Parallel path: keep the unprocessed mix only when it is actually
        // going to be blended back in. At mix = 1 this is a plain in-place run
        // and the copy never happens.
        if (mix < 0.999f) std::memcpy(fx->bus_dry, out, sizeof(float) * 2 * (size_t)n);
        fx->bus.processBlock(out, n, fx->scratch_l, fx->scratch_r);
        if (mix < 0.999f) {
            for (int i = 0; i < 2 * n; i++)
                out[i] = fx->bus_dry[i] + (out[i] - fx->bus_dry[i]) * mix;
        }
    }
}

void dr32_fxbus_reset(dr32_fxbus *fx) {
    if (!fx) return;
    for (int i = 0; i < DR32_SEND_SLOTS; i++) {
        fx->sends[i].reset();
        std::memset(fx->send_buf[i], 0, sizeof(fx->send_buf[i]));
    }
    // The bus holds envelope followers, a compressor and a DC blocker; a kit
    // change must not leave any of that pointing at the previous kit's level.
    fx->bus.reset();
}


int dr32_fxbus_native_set_raw(dr32_fxbus *fx, int slot, const char *key, float value) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return -1;
    return (int)fx->sends[slot].native.setRaw(key, value);
}

void dr32_fxbus_native_raw_commit(dr32_fxbus *fx, int slot) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    fx->sends[slot].native.build();
    fx->sends[slot].native.reset();
    fx->sends[slot].nativeRawMode = true;
    // The generic pre-delay line must be out of the way: the device's PreDelay
    // is one of the raw parameters and it is modelled inside the port.
    fx->sends[slot].preLen = 0;
}



}  // extern "C"
