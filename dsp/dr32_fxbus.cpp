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
 *  cut down to three controls plus wet/dry.
 *
 *    Compress — Airwindows *Pressure4* vari-mu compression (MIT, © Chris
 *               Johnson) as ported in PALETTE's SQUASH.
 *    Drive    — tube/Spiral soft-fold saturation from PALETTE's DRIVE, which
 *               splits at ~200 Hz and drives only the upper band so the low end
 *               stays intact. That split is what makes it usable on a kit.
 *    Boom     — resonant low shelf around 60-90 Hz for kick weight.
 *
 *  Deliberately omitted from Drum Buss's full control set: Crunch, Damp,
 *  Transients and Output. Three controls that each do something obvious beat
 *  eight that interact. */
struct DrumBuss {
    float fs = 44100.0f;
    // Pressure4 state (A/B ping-pong, as in the original)
    float spdA = 10000.0f, spdB = 10000.0f, cofA = 1.0f, cofB = 1.0f;
    int   flip = 0;
    // drive band-split state
    float lpL = 0.0f, lpR = 0.0f;
    // boom resonator state
    float b1L = 0.0f, b2L = 0.0f, b1R = 0.0f, b2R = 0.0f;
    float boomG = 0.0f, boomF = 0.0f;

    float comp = 0.0f, drive = 0.0f, boom = 0.0f;

    void setSampleRate(float sr) { fs = (sr > 1.0f) ? sr : 44100.0f; recalc(); }

    void setParams(float p1, float p2, float p3) {
        comp  = (p1 < 0.0f) ? 0.0f : (p1 > 1.0f ? 1.0f : p1);
        drive = (p2 < 0.0f) ? 0.0f : (p2 > 1.0f ? 1.0f : p2);
        boom  = (p3 < 0.0f) ? 0.0f : (p3 > 1.0f ? 1.0f : p3);
        recalc();
    }

    void recalc() {
        // Boom: a gentle resonant lift, tuned lower as it is pushed.
        float f = 90.0f - 30.0f * boom;
        boomF = 2.0f * 3.14159265358979f * f / fs;
        boomG = boom * 1.8f;
    }

    void reset() {
        spdA = spdB = 10000.0f;
        cofA = cofB = 1.0f;
        lpL = lpR = b1L = b2L = b1R = b2R = 0.0f;
        flip = 0;
    }

    inline void tick(float &l, float &r) {
        float xl = l, xr = r;

        // --- drive: split at ~200 Hz, fold only the upper band
        if (drive > 0.0f) {
            const float aBass = 0.030f;
            const float drv = 1.0f + drive * 6.0f + drive * drive * 30.0f;
            const float himk = 0.7f + drive * 0.5f;
            float *lp[2] = { &lpL, &lpR };
            float *ch[2] = { &xl, &xr };
            for (int c = 0; c < 2; c++) {
                float x = *ch[c];
                *lp[c] += aBass * (x - *lp[c]);
                float bass = *lp[c], high = x - *lp[c];
                float d = high * drv, ad = d < 0 ? -d : d;
                float sh = (ad > 1e-6f) ? std::sin(d * ad) / ad : d;
                *ch[c] = x + drive * ((bass + sh * himk) - x);
            }
        }

        // --- boom: resonant low lift (state-variable, low output)
        if (boomG > 0.0f) {
            float g = boomF;
            float *b1[2] = { &b1L, &b1R };
            float *b2[2] = { &b2L, &b2R };
            float *ch[2] = { &xl, &xr };
            for (int c = 0; c < 2; c++) {
                float hp = (*ch[c] - (1.4f + g) * *b1[c] - *b2[c]) / (1.0f + g * (1.4f + g));
                float bp = *b1[c] + g * hp;
                float lo = *b2[c] + g * bp;
                *b1[c] = 2.0f * bp - *b1[c];
                *b2[c] = 2.0f * lo - *b2[c];
                *ch[c] += lo * boomG;
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
            // Pressure4's second-stage sin() overdrive
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
