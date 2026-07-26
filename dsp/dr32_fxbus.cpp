// dr32_fxbus.cpp — implementation of DR32's send/insert buses.
//
// C++ because the vendored reverbs are C++ structs (see dsp/vendor/SOURCES.md);
// the interface is extern "C" so the rest of the C11 engine is unaffected.

#include "dr32_fxbus.h"

#include "vendor/airwin_verb.h"
#include "vendor/space_extra.h"

#include <cstring>
#include <cmath>
#include <cstdlib>
#include <new>

#define DR32_SEND_SLOTS   2
#define DR32_INSERT_SLOTS 2
#define DR32_MAX_BLOCK    1024

namespace {

/** Drum Buss — a drum-bus glue insert in the spirit of Ableton's Drum Buss,
 *  cut to three controls plus wet/dry.
 *
 *    Compress   — Airwindows *Pressure4* vari-mu compression (MIT, © Chris
 *                 Johnson) as ported in PALETTE's SQUASH.
 *    Crunch     — high-frequency grit: splits at ~1.2 kHz and hard-folds only
 *                 the upper band, so it adds bite without smearing the kick.
 *    Transients — bipolar attack/sustain shaping from the difference between a
 *                 fast and a slow envelope follower. 0.5 is neutral; below
 *                 softens the hit and lets the tail up, above sharpens it.
 *
 *  Dropped from the full Drum Buss set: Drive, Damp, Boom, Output. Three
 *  controls that each do something obvious beat eight that interact.
 *  Pressure4's release is fixed at a musical value rather than exposed, to hold
 *  that budget. */
struct DrumBuss {
    float fs = 44100.0f;
    // Pressure4 state (A/B ping-pong, as in the original)
    float spdA = 10000.0f, spdB = 10000.0f, cofA = 1.0f, cofB = 1.0f;
    int   flip = 0;
    // crunch band-split state
    float hpL = 0.0f, hpR = 0.0f;
    // transient envelope followers (fast + slow), per channel
    float envF[2] = { 0.0f, 0.0f }, envS[2] = { 0.0f, 0.0f };
    float aFast = 0.0f, aSlow = 0.0f, rel = 0.0f;

    float comp = 0.0f, crunch = 0.0f, trans = 0.5f;

    void setSampleRate(float sr) { fs = (sr > 1.0f) ? sr : 44100.0f; recalc(); }

    void setParams(float p1, float p2, float p3) {
        comp   = clamp01(p1);
        crunch = clamp01(p2);
        trans  = clamp01(p3);
        recalc();
    }

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    void recalc() {
        // Followers: ~1 ms vs ~40 ms attack; their difference IS the transient.
        aFast = 1.0f - std::exp(-1.0f / (0.001f * fs));
        aSlow = 1.0f - std::exp(-1.0f / (0.040f * fs));
        rel   = 1.0f - std::exp(-1.0f / (0.150f * fs));
    }

    void reset() {
        spdA = spdB = 10000.0f;
        cofA = cofB = 1.0f;
        hpL = hpR = 0.0f;
        envF[0] = envF[1] = envS[0] = envS[1] = 0.0f;
        flip = 0;
    }

    inline void tick(float &l, float &r) {
        float xl = l, xr = r;

        // --- transients: gain from the fast/slow envelope difference
        if (trans < 0.49f || trans > 0.51f) {
            const float depth = (trans - 0.5f) * 2.0f;      // -1..+1
            float *ch[2] = { &xl, &xr };
            for (int c = 0; c < 2; c++) {
                float mag = std::fabs(*ch[c]);
                envF[c] += (mag > envF[c] ? aFast : rel) * (mag - envF[c]);
                envS[c] += (mag > envS[c] ? aSlow : rel) * (mag - envS[c]);
                float diff = envF[c] - envS[c];             // >0 during an attack
                float g = 1.0f + depth * diff * 4.0f;
                if (g < 0.05f) g = 0.05f;
                if (g > 4.0f)  g = 4.0f;
                *ch[c] *= g;
            }
        }

        // --- crunch: fold only the HF band so the low end stays clean
        if (crunch > 0.0f) {
            const float aHi = 0.14f;                        // ~1.2 kHz split
            const float drv = 1.0f + crunch * 12.0f + crunch * crunch * 60.0f;
            float *lp[2] = { &hpL, &hpR };
            float *ch[2] = { &xl, &xr };
            for (int c = 0; c < 2; c++) {
                float x = *ch[c];
                *lp[c] += aHi * (x - *lp[c]);
                float low = *lp[c], high = x - *lp[c];
                float d = high * drv;
                float sh = std::tanh(d);                    // harder than the fold: grit
                *ch[c] = x + crunch * ((low + sh * 0.8f) - x);
            }
        }

        // --- compress: Pressure4 vari-mu
        if (comp > 0.0f) {
            const float A = comp;
            const float threshold = 1.0f - (A * 0.95f);
            const float muMakeup = 1.0f / threshold;
            const float release = std::pow(1.28f - 0.4f, 5.0f) * 32768.0f;
            const float fastest = std::sqrt(release);
            const float compensate = 0.5f + 0.5f * threshold;

            float cl = xl * muMakeup, cr = xr * muMakeup;
            float sense = std::fabs(cl) > std::fabs(cr) ? std::fabs(cl) : std::fabs(cr);
            float *spd = flip ? &spdA : &spdB;
            float *cof = flip ? &cofA : &cofB;
            if (sense > threshold) {
                float muVary = threshold / sense;
                float muAttack = std::sqrt(std::fabs(*spd));
                *cof = *cof * (muAttack - 1.0f);
                *cof += (muVary < threshold) ? threshold : muVary;
                *cof /= muAttack;
            } else {
                *cof = *cof * ((*spd) * (*spd) - 1.0f) + 1.0f;
                *cof /= (*spd) * (*spd);
            }
            float ns = (*spd) * ((*spd) - 1.0f) + std::fabs(sense * release) + fastest;
            *spd = ns / (*spd);
            float coeff = *cof;
            cl *= coeff; cr *= coeff;
            float br = std::fabs(cl); br = (br > 1.57079633f) ? 1.0f : std::sin(br);
            cl = (cl > 0) ? br : -br;
            br = std::fabs(cr); br = (br > 1.57079633f) ? 1.0f : std::sin(br);
            cr = (cr > 0) ? br : -br;
            xl += A * (cl * compensate - xl);
            xr += A * (cr * compensate - xr);
            flip ^= 1;
        }

        l = xl;
        r = xr;
    }
};

using efx::SpaceExtra;
using efx::Chamber;
using efx::InfinityVerb;

/** One effect instance. Only the algorithm the current type needs is run; all
 *  of them are resident because allocating on a type change would have to
 *  happen on the audio thread. */
struct Slot {
    dr32_efx_type type = DR32_EFX_NONE;
    float p1 = 0.5f, p2 = 0.5f, p3 = 0.5f, mix = 1.0f;

    SpaceExtra plate;      // Dattorro plate tank
    Chamber    room;       // Airwindows Chamber
    InfinityVerb hall;     // Airwindows InfinityVerb
    DrumBuss   buss;       // compress + drive + boom

    void setSampleRate(float fs) {
        plate.setSampleRate(fs);
        room.setSampleRate(fs);
        hall.setSampleRate(fs);
        plate.setType(SpaceExtra::Plate);
        buss.setSampleRate(fs);
        apply();
    }

    void apply() {
        // Reverbs take normalized controls; a send bus runs fully wet and gets
        // its level from the pad send amounts, so `mix` is applied by the
        // caller for sends and here for inserts.
        plate.setParams(p1, p2, p3, /*feed*/ p3, /*mix*/ 1.0f);
        room.setParams(p1, p2, p3, 1.0f);
        hall.setParams(p1, p2, p3, 1.0f);
        buss.setParams(p1, p2, p3);
    }

    void reset() {
        plate.reset();
        room.reset();
        hall.reset();
        buss.reset();
    }

    inline void tick(float &l, float &r) {
        switch (type) {
            case DR32_EFX_PLATE: plate.tick(l, r); break;
            case DR32_EFX_ROOM:  room.tick(l, r);  break;
            case DR32_EFX_HALL:  hall.tick(l, r);  break;
            case DR32_EFX_DRUMBUSS: buss.tick(l, r); break;
            default: break;
        }
    }

    bool active() const { return type != DR32_EFX_NONE; }
};

}  // namespace

struct dr32_fxbus {
    float sample_rate = 44100.0f;
    Slot  sends[DR32_SEND_SLOTS];
    Slot  inserts[DR32_INSERT_SLOTS];
    float send_return[DR32_SEND_SLOTS] = { 1.0f, 1.0f };
    // Per-block accumulation of what the pads sent to each bus.
    float send_buf[DR32_SEND_SLOTS][2 * DR32_MAX_BLOCK];
    int   send_pos[DR32_SEND_SLOTS] = { 0, 0 };
};

extern "C" {

dr32_fxbus *dr32_fxbus_create(float sample_rate) {
    dr32_fxbus *fx = new (std::nothrow) dr32_fxbus();
    if (!fx) return nullptr;
    fx->sample_rate = (sample_rate > 1.0f) ? sample_rate : 44100.0f;
    for (int i = 0; i < DR32_SEND_SLOTS; i++) fx->sends[i].setSampleRate(fx->sample_rate);
    for (int i = 0; i < DR32_INSERT_SLOTS; i++) fx->inserts[i].setSampleRate(fx->sample_rate);
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

void dr32_fxbus_set_insert_type(dr32_fxbus *fx, int slot, dr32_efx_type type) {
    if (!fx || slot < 0 || slot >= DR32_INSERT_SLOTS) return;
    if (fx->inserts[slot].type == type) return;
    fx->inserts[slot].type = type;
    fx->inserts[slot].reset();
}

void dr32_fxbus_set_send_params(dr32_fxbus *fx, int slot,
                                float p1, float p2, float p3) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    Slot &s = fx->sends[slot];
    s.p1 = p1; s.p2 = p2; s.p3 = p3;
    s.mix = 1.0f;                 // a send return is ALWAYS 100% wet
    s.apply();
}

void dr32_fxbus_set_insert_params(dr32_fxbus *fx, int slot,
                                  float p1, float p2, float p3, float mix) {
    if (!fx || slot < 0 || slot >= DR32_INSERT_SLOTS) return;
    Slot &s = fx->inserts[slot];
    s.p1 = p1; s.p2 = p2; s.p3 = p3; s.mix = mix;
    s.apply();
}

void dr32_fxbus_set_send_return(dr32_fxbus *fx, int slot, float gain) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    fx->send_return[slot] = gain;
}

void dr32_fxbus_send(dr32_fxbus *fx, int slot, float l, float r) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    int p = fx->send_pos[slot];
    if (p >= DR32_MAX_BLOCK) return;
    fx->send_buf[slot][2 * p]     += l;
    fx->send_buf[slot][2 * p + 1] += r;
}

void dr32_fxbus_process(dr32_fxbus *fx, float *out, int n) {
    if (!fx || !out || n <= 0) return;
    if (n > DR32_MAX_BLOCK) n = DR32_MAX_BLOCK;

    // --- send buses: run the effect fully wet, add the return into the mix
    for (int s = 0; s < DR32_SEND_SLOTS; s++) {
        Slot &slot = fx->sends[s];
        float *buf = fx->send_buf[s];
        if (slot.active()) {
            const float g = fx->send_return[s];   // return is fully wet by design
            for (int i = 0; i < n; i++) {
                float l = buf[2 * i], r = buf[2 * i + 1];
                slot.tick(l, r);
                out[2 * i]     += l * g;
                out[2 * i + 1] += r * g;
            }
        }
        std::memset(buf, 0, sizeof(float) * 2 * (size_t)n);
        fx->send_pos[s] = 0;
    }

    // --- inserts: serial, wet/dry per slot
    for (int s = 0; s < DR32_INSERT_SLOTS; s++) {
        Slot &slot = fx->inserts[s];
        if (!slot.active()) continue;
        const float wet = slot.mix, dry = 1.0f - slot.mix;
        for (int i = 0; i < n; i++) {
            float l = out[2 * i], r = out[2 * i + 1];
            float wl = l, wr = r;
            slot.tick(wl, wr);
            out[2 * i]     = l * dry + wl * wet;
            out[2 * i + 1] = r * dry + wr * wet;
        }
    }
}

void dr32_fxbus_reset(dr32_fxbus *fx) {
    if (!fx) return;
    for (int i = 0; i < DR32_SEND_SLOTS; i++) {
        fx->sends[i].reset();
        std::memset(fx->send_buf[i], 0, sizeof(fx->send_buf[i]));
        fx->send_pos[i] = 0;
    }
    for (int i = 0; i < DR32_INSERT_SLOTS; i++) fx->inserts[i].reset();
}

const char *dr32_efx_name(dr32_efx_type type) {
    switch (type) {
        case DR32_EFX_PLATE: return "Plate";
        case DR32_EFX_ROOM:  return "Room";
        case DR32_EFX_HALL:  return "Hall";
        case DR32_EFX_DRUMBUSS: return "Drum Buss";
        default:             return "Off";
    }
}

dr32_efx_type dr32_efx_from_name(const char *name) {
    if (!name || !*name) return DR32_EFX_NONE;
    if (!std::strcmp(name, "Plate")) return DR32_EFX_PLATE;
    if (!std::strcmp(name, "Room"))  return DR32_EFX_ROOM;
    if (!std::strcmp(name, "Hall"))  return DR32_EFX_HALL;
    if (!std::strcmp(name, "Drum Buss")) return DR32_EFX_DRUMBUSS;
    return DR32_EFX_NONE;
}

}  // extern "C"
