// dr32_fxbus.cpp — implementation of DR32's send/insert buses.
//
// C++ because the vendored reverbs are C++ structs (see dsp/vendor/SOURCES.md);
// the interface is extern "C" so the rest of the C11 engine is unaffected.

#include "dr32_fxbus.h"

#include "vendor/airwin_verb.h"
#include "vendor/space_extra.h"

#include <cstring>
#include <cstdlib>
#include <new>

#define DR32_SEND_SLOTS   2
#define DR32_INSERT_SLOTS 2
#define DR32_MAX_BLOCK    1024

namespace {

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

    void setSampleRate(float fs) {
        plate.setSampleRate(fs);
        room.setSampleRate(fs);
        hall.setSampleRate(fs);
        plate.setType(SpaceExtra::Plate);
        apply();
    }

    void apply() {
        // Reverbs take normalized controls; a send bus runs fully wet and gets
        // its level from the pad send amounts, so `mix` is applied by the
        // caller for sends and here for inserts.
        plate.setParams(p1, p2, p3, /*feed*/ p3, /*mix*/ 1.0f);
        room.setParams(p1, p2, p3, 1.0f);
        hall.setParams(p1, p2, p3, 1.0f);
    }

    void reset() {
        plate.reset();
        room.reset();
        hall.reset();
    }

    inline void tick(float &l, float &r) {
        switch (type) {
            case DR32_EFX_PLATE: plate.tick(l, r); break;
            case DR32_EFX_ROOM:  room.tick(l, r);  break;
            case DR32_EFX_HALL:  hall.tick(l, r);  break;
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
                                float p1, float p2, float p3, float mix) {
    if (!fx || slot < 0 || slot >= DR32_SEND_SLOTS) return;
    Slot &s = fx->sends[slot];
    s.p1 = p1; s.p2 = p2; s.p3 = p3; s.mix = mix;
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
            const float g = fx->send_return[s] * slot.mix;
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
        default:             return "Off";
    }
}

dr32_efx_type dr32_efx_from_name(const char *name) {
    if (!name || !*name) return DR32_EFX_NONE;
    if (!std::strcmp(name, "Plate")) return DR32_EFX_PLATE;
    if (!std::strcmp(name, "Room"))  return DR32_EFX_ROOM;
    if (!std::strcmp(name, "Hall"))  return DR32_EFX_HALL;
    return DR32_EFX_NONE;
}

}  // extern "C"
