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
    float crunchNorm = 1.0f;   // output normalisation so drive does not change level
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
        // Two followers with the SAME release and different attacks. The shared
        // release matters: with different releases the fast one sits above the
        // slow one for the whole decay, so the "transient" reading never
        // returns to zero and the stage becomes a flat gain boost — which is
        // exactly what the first version did (measured: attack AND tail both
        // +6x, ratio unchanged).
        aFast = 1.0f - std::exp(-1.0f / (0.001f * fs));   // 1 ms
        aSlow = 1.0f - std::exp(-1.0f / (0.025f * fs));   // 25 ms
        rel   = 1.0f - std::exp(-1.0f / (0.060f * fs));   // shared

        // Saturator makeup, MEASURED rather than assumed: integrate the shaper
        // over a reference sine and normalise so RMS in ~= RMS out. A fixed
        // reference point got this wrong by 8 dB.
        const int    K = 32;
        const float  amp = 0.3f;
        double in2 = 0.0, out2 = 0.0;
        for (int i = 0; i < K; i++) {
            float x = amp * std::sin(2.0f * 3.14159265f * (float)i / (float)K);
            float y = shape(x);
            in2 += (double)x * x;
            out2 += (double)y * y;
        }
        crunchNorm = (out2 > 1e-12) ? (float)std::sqrt(in2 / out2) : 1.0f;
    }

    /** The saturation curve itself, before makeup. */
    inline float shape(float x) const {
        float d = x * (1.0f + crunch * 24.0f);
        float sh = std::tanh(d);
        return sh - 0.15f * sh * sh * sh;
    }

    void reset() {
        spdA = spdB = 10000.0f;
        cofA = cofB = 1.0f;
        envF[0] = envF[1] = envS[0] = envS[1] = 0.0f;
        flip = 0;
    }

    inline void tick(float &l, float &r) {
        float xl = l, xr = r;

        // --- transients: gain from the RATIO of a fast and a slow follower.
        //
        // The previous version used their raw difference, which is proportional
        // to signal level — so it did almost nothing on quiet material and was
        // inconsistent across kits. A ratio is level-independent: it says "how
        // much faster is this moment than the local average", which is what a
        // transient actually is.
        if (trans < 0.49f || trans > 0.51f) {
            const float depth = (trans - 0.5f) * 2.0f;      // -1..+1
            float *ch[2] = { &xl, &xr };
            for (int c = 0; c < 2; c++) {
                float mag = std::fabs(*ch[c]);
                envF[c] += (mag > envF[c] ? aFast : rel) * (mag - envF[c]);
                envS[c] += (mag > envS[c] ? aSlow : rel) * (mag - envS[c]);
                // How far the fast follower is ABOVE the slow one, relative to
                // the slow one: ~0 in steady state, large only at an onset.
                float t = (envF[c] - envS[c]) / (envS[c] + 1e-4f);
                if (t < 0.0f) t = 0.0f;
                if (t > 1.5f) t = 1.5f;
                float g = std::pow(2.0f, depth * t * 2.0f);   // up to ~+/-9 dB
                if (g < 0.1f) g = 0.1f;
                if (g > 4.0f) g = 4.0f;
                *ch[c] *= g;
            }
        }

        // --- crunch: FULL-BAND saturation.
        //
        // This used to split at ~1.2 kHz and only fold the upper band, which
        // audibly thinned the low end — that is EQ shaping, not saturation.
        // A crunchy saturator should add harmonics across the spectrum and
        // leave the tonal balance alone, so it now drives the whole signal and
        // normalises the output so pushing Crunch changes character, not level.
        if (crunch > 0.0f) {
            float *ch[2] = { &xl, &xr };
            for (int c = 0; c < 2; c++) {
                // tanh gives the soft knee; the cubic term adds the odd-harmonic
                // grit that reads as "crunch" rather than "warm".
                *ch[c] += crunch * (shape(*ch[c]) * crunchNorm - *ch[c]);
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
#define DR32_PREDELAY_MAX_MS 200
#define DR32_PREDELAY_MAX ((int)(DR32_PREDELAY_MAX_MS * 48))   // headroom to 48 kHz

struct Slot {
    dr32_efx_type type = DR32_EFX_NONE;
    float p1 = 0.5f, p2 = 0.5f, p3 = 0.5f, mix = 1.0f;
    float pd = 0.0f;                       // pre-delay 0..1 -> 0..200 ms

    // Pre-delay line, applied before the reverb. The plate has its own internal
    // pre-delay (its third parameter!) but Chamber and InfinityVerb have none,
    // so one line here keeps the control identical across all three — and the
    // plate's internal one is held at zero so they cannot stack.
    float pre[2 * DR32_PREDELAY_MAX];
    int   preW = 0, preLen = 0;

    // Plate AND Room are both the Dattorro tank, at different scales — Room is
    // simply a small, short, damped one. Hall is Airwindows Chamber.
    //
    // InfinityVerb was dropped: it is Airwindows' *infinite* reverb, built to
    // sustain forever without blowing up, and it measured a 20 s RT60 at EVERY
    // decay setting. Wrong tool for a hall.
    SpaceExtra tank;       // Dattorro figure-eight plate tank (Plate + Room)
    Chamber    chamber;    // Airwindows Chamber (Hall)
    DrumBuss   buss;

    void setSampleRate(float fs) {
        fs_ = fs;
        tank.setSampleRate(fs);
        chamber.setSampleRate(fs);
        tank.setType(SpaceExtra::Plate);
        buss.setSampleRate(fs);
        apply();
    }

    void apply() {
        // p3 is DECAY for every reverb. The plate's own third parameter is its
        // internal pre-delay, so it is pinned to 0 and decay goes to `feed`
        // instead — previously p3 was passed as BOTH, so one knob was
        // simultaneously pre-delay and decay.
        // Decay is mapped per type so the knob spans a musical range instead of
        // saturating. Measured targets: ~0.3 s at 0, ~1.2 s at 0.5, ~4 s at 1.
        tank.setParams(p1, p2, /*internal predelay*/ 0.0f, /*feed = decay*/ p3 * 0.75f, 1.0f);

        // Room and Hall are the SAME algorithm at different scales, which is
        // what they physically are: two rooms of different size. Chamber is an
        // actual chamber/room model, so it suits both far better than a shrunk
        // plate tank (which just sounds like a small plate).
        //
        // Chamber's B_ ("bigness") is really regeneration, and its
        // regen = (1-(1-B)^6)*0.123 curve saturates by about B = 0.3 — so the
        // whole useful range lives in the bottom third, with a floor offset so
        // even a minimum-decay hall still sounds like a hall.
        if (type == DR32_EFX_ROOM) {
            chamber.setParams(0.10f + 0.30f * p1, p2, 0.02f + p3 * 0.13f, 1.0f);
        } else {
            chamber.setParams(0.45f + 0.55f * p1, p2, 0.06f + p3 * 0.26f, 1.0f);
        }
        buss.setParams(p1, p2, p3);
        int n = (int)(pd * (DR32_PREDELAY_MAX_MS * 0.001f) * fs_);
        if (n < 0) n = 0;
        if (n > DR32_PREDELAY_MAX - 1) n = DR32_PREDELAY_MAX - 1;
        preLen = n;
    }

    float fs_ = 44100.0f;

    void reset() {
        std::memset(pre, 0, sizeof(pre));
        preW = 0;
        tank.reset();
        chamber.reset();
        buss.reset();
    }

    inline void tick(float &l, float &r) {
        // Pre-delay ahead of the reverb (no effect on Drum Buss).
        if (preLen > 0 && type != DR32_EFX_DRUMBUSS) {
            int w = preW % DR32_PREDELAY_MAX;
            int rd = (preW - preLen) % DR32_PREDELAY_MAX;
            if (rd < 0) rd += DR32_PREDELAY_MAX;
            pre[2 * w] = l;
            pre[2 * w + 1] = r;
            l = pre[2 * rd];
            r = pre[2 * rd + 1];
            preW = (preW + 1) % DR32_PREDELAY_MAX;
        }
        switch (type) {
            case DR32_EFX_PLATE: tank.tick(l, r); break;
            case DR32_EFX_ROOM:
            case DR32_EFX_HALL:  chamber.tick(l, r); break;
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
    // Blocks since anything was fed to each bus. A loaded-but-unused reverb
    // should cost nothing, but its tail must still ring out first.
    int   idle_blocks[DR32_SEND_SLOTS] = { 0, 0 };
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
                                float p1, float p2, float p3, float predelay) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    Slot &s = fx->sends[slot];
    s.p1 = p1; s.p2 = p2; s.p3 = p3; s.pd = predelay;
    s.mix = 1.0f;                 // a send return is ALWAYS 100% wet
    s.apply();
}

void dr32_fxbus_set_insert_params(dr32_fxbus *fx, int slot,
                                  float p1, float p2, float p3, float predelay, float mix) {
    if (!fx || slot < 0 || slot >= DR32_INSERT_SLOTS) return;
    Slot &s = fx->inserts[slot];
    s.p1 = p1; s.p2 = p2; s.p3 = p3; s.pd = predelay; s.mix = mix;
    s.apply();
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

        if (slot.active() && fx->idle_blocks[s] <= idle_limit) {
            const float g = fx->send_return[s];   // return is fully wet by design
            for (int i = 0; i < n; i++) {
                float l = buf[2 * i], r = buf[2 * i + 1];
                slot.tick(l, r);
                out[2 * i]     += l * g;
                out[2 * i + 1] += r * g;
            }
        }
        std::memset(buf, 0, sizeof(float) * 2 * (size_t)n);
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
    }
    for (int i = 0; i < DR32_INSERT_SLOTS; i++) fx->inserts[i].reset();
}

void dr32_efx_defaults(dr32_efx_type type, float *o) {
    if (!o) return;
    // [size, damp, decay, predelay, mix]. Pre-delay is in 0..200 ms.
    switch (type) {
        case DR32_EFX_PLATE:
            // Snare/clap plate: medium tank, a little damping so it is not
            // brittle, short-ish tail, ~10 ms pre-delay to keep the hit clear.
            o[0] = 0.45f; o[1] = 0.35f; o[2] = 0.45f; o[3] = 0.05f; o[4] = 1.0f; break;
        case DR32_EFX_ROOM:
            // Small, dry-ish, minimal pre-delay — ambience rather than effect.
            o[0] = 0.30f; o[1] = 0.40f; o[2] = 0.30f; o[3] = 0.02f; o[4] = 1.0f; break;
        case DR32_EFX_HALL:
            // Big and damped, longer tail, ~20 ms pre-delay for the sense of
            // distance a hall needs.
            o[0] = 0.75f; o[1] = 0.55f; o[2] = 0.70f; o[3] = 0.10f; o[4] = 1.0f; break;
        case DR32_EFX_DRUMBUSS:
            // Gentle glue with a hint of grit and a little extra attack —
            // audible but not a statement.
            o[0] = 0.30f; o[1] = 0.15f; o[2] = 0.60f; o[3] = 0.0f; o[4] = 1.0f; break;
        default:
            o[0] = 0.5f; o[1] = 0.3f; o[2] = 0.5f; o[3] = 0.0f; o[4] = 1.0f; break;
    }
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
