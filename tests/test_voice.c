// Voice engine tests. These encode the *manual's* semantics, so if a later
// refactor breaks Trigger-vs-Gate or the hold sentinel, it fails here rather
// than on the device.

#include "../dsp/dr32_voice.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define SR 44100
static float samp[SR];          // 1 s of DC = 1.0, so output == envelope * amp
static float out[2 * SR];

static void dc_sample(void) { for (int i = 0; i < SR; i++) samp[i] = 1.0f; }

/** Render n frames, return peak |L|. */
static float render(dr32_voice *v, int n) {
    memset(out, 0, sizeof(float) * 2 * (size_t)n);
    dr32_voice_render(v, out, n);
    float peak = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(out[2 * i]); if (a > peak) peak = a; }
    return peak;
}

/** Render n frames, return the LAST left-channel value. */
static float render_last(dr32_voice *v, int n) {
    memset(out, 0, sizeof(float) * 2 * (size_t)n);
    dr32_voice_render(v, out, n);
    return out[2 * (n - 1)];
}

int main(void) {
    dc_sample();
    printf("voice engine\n");

    // ---- envelope: attack reaches full scale in `attack` seconds
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.attack = 0.1f; p.hold = DR32_HOLD_INFINITE; p.filter_on = 0;
        p.vel_to_volume = 0.0f;             // isolate the envelope
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 127);
        float half = render_last(&v, SR / 20);          // 50 ms = half the attack
        CHECK(fabsf(half - 0.5f) < 0.02f, "attack midpoint %.3f, want ~0.5", half);
        float full = render_last(&v, SR / 20);          // another 50 ms
        CHECK(fabsf(full - 1.0f) < 0.02f, "attack end %.3f, want ~1.0", full);
    }

    // ---- A-H-D (Trigger) IGNORES note-off. This is the mode's whole point.
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.env_mode = DR32_ENV_AHD;
        p.attack = 0.001f; p.hold = 0.5f; p.decay = 0.5f;
        p.filter_on = 0; p.vel_to_volume = 0.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 127);
        render(&v, 1000);
        dr32_voice_release(&v, &p);                     // note-off immediately
        float after = render_last(&v, SR / 10);         // 100 ms later
        CHECK(after > 0.95f, "AHD decayed on note-off (%.3f) — Trigger must ignore it", after);
    }

    // ---- A-S-R (Gate) DOES release
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.env_mode = DR32_ENV_ASR;
        p.attack = 0.001f; p.decay = 0.05f;
        p.filter_on = 0; p.vel_to_volume = 0.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 127);
        render(&v, 1000);
        float held = render_last(&v, SR / 10);
        CHECK(held > 0.95f, "ASR should sustain while held, got %.3f", held);
        dr32_voice_release(&v, &p);
        float rel = render_last(&v, SR / 10);
        CHECK(rel < 0.01f, "ASR should have released, got %.3f", rel);
    }

    // ---- Hold = 60 s is the "infinite" sentinel: play the whole sample
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.hold = DR32_HOLD_INFINITE; p.attack = 0.001f;
        p.filter_on = 0; p.vel_to_volume = 0.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 127);
        float late = render_last(&v, SR - 100);         // nearly the whole second
        CHECK(late > 0.95f, "inf hold decayed early (%.3f)", late);
    }

    // ---- short hold decays after the hold time
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.hold = 0.05f; p.decay = 0.01f; p.attack = 0.001f;
        p.filter_on = 0; p.vel_to_volume = 0.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 127);
        float after = render_last(&v, SR / 5);          // 200 ms >> hold + decay
        CHECK(after < 0.01f, "short hold should have decayed, got %.3f", after);
    }

    // ---- velocity -> volume depth
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.attack = 0.001f; p.hold = DR32_HOLD_INFINITE; p.filter_on = 0;
        p.vel_to_volume = 1.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 64);
        float half_vel = render_last(&v, SR / 10);
        CHECK(fabsf(half_vel - 64.0f / 127.0f) < 0.02f,
              "full depth at vel 64 = %.3f, want ~0.504", half_vel);

        p.vel_to_volume = 0.0f;                          // depth 0 = velocity-independent
        dr32_voice_start(&v, &p, samp, SR, 1);
        float no_depth = render_last(&v, SR / 10);
        CHECK(fabsf(no_depth - 1.0f) < 0.02f, "zero depth at vel 1 = %.3f, want ~1.0", no_depth);
    }

    // ---- transpose sets the playback rate (+12 st = 2x, so a 1 s sample ends
    //      after 0.5 s)
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.transpose = 12.0f; p.hold = DR32_HOLD_INFINITE; p.attack = 0.001f;
        p.filter_on = 0; p.vel_to_volume = 0.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 127);
        CHECK(fabs(v.step - 2.0) < 1e-6, "+12 st step = %.6f, want 2.0", v.step);
        render(&v, SR / 2 - 200);
        CHECK(v.active, "voice ended too early at 2x");
        render(&v, 400);
        CHECK(!v.active, "voice should have consumed the sample by 0.5 s at 2x");

        p.transpose = -12.0f;
        dr32_voice_start(&v, &p, samp, SR, 127);
        CHECK(fabs(v.step - 0.5) < 1e-6, "-12 st step = %.6f, want 0.5", v.step);
    }

    // ---- detune is cents
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.detune = 100.0f;                    // 100 cents == 1 semitone
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 127);
        CHECK(fabs(v.step - pow(2.0, 1.0 / 12.0)) < 1e-6, "100 cents != 1 semitone");
    }

    // ---- playback start/length select a region
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.play_start = 0.25f; p.play_length = 0.5f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 127);
        CHECK(v.region_start == SR / 4, "region start %zu, want %d", v.region_start, SR / 4);
        CHECK(v.region_end == SR / 4 + SR / 2, "region end %zu", v.region_end);
    }

    // ---- empty pad (no sample) must not sound or crash
    {
        dr32_pad p; dr32_pad_defaults(&p);
        dr32_voice v;
        dr32_voice_start(&v, &p, NULL, 0, 127);
        CHECK(!v.active, "empty pad should not be active");
        CHECK(render(&v, 256) == 0.0f, "empty pad produced signal");
    }

    // ---- choke fades fast but does not click (no discontinuity to zero)
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.hold = DR32_HOLD_INFINITE; p.attack = 0.001f; p.decay = 10.0f;
        p.filter_on = 0; p.vel_to_volume = 0.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 127);
        render(&v, 1000);
        dr32_voice_choke(&v);
        float after_1ms = render_last(&v, SR / 1000);
        CHECK(after_1ms > 0.1f && after_1ms < 1.0f,
              "choke should fade, not jump: %.3f after 1 ms", after_1ms);
        render(&v, SR / 100);
        CHECK(!v.active, "choke should finish the voice within ~10 ms");
    }

    // ---- filter: LP at a low cutoff must attenuate a bright signal far more
    //      than LP wide open; 24 dB must cut harder than 12 dB.
    {
        // A 5 kHz sine against a 1 kHz cutoff: far enough above to be clearly
        // attenuated, close enough that 12 dB and 24 dB give different answers.
        // (A Nyquist tone floors BOTH slopes at zero and proves nothing.)
        for (int i = 0; i < SR; i++) samp[i] = sinf(2.0f * (float)M_PI * 5000.0f * (float)i / (float)SR);
        dr32_pad p; dr32_pad_defaults(&p);
        p.hold = DR32_HOLD_INFINITE; p.attack = 0.0001f; p.vel_to_volume = 0.0f;
        p.cutoff = 1000.0f;

        dr32_voice v;
        p.filter_type = DR32_FILT_LP12;
        dr32_voice_start(&v, &p, samp, SR, 127);
        render(&v, 2000);
        float lp12 = render(&v, 2000);

        p.filter_type = DR32_FILT_LP24;
        dr32_voice_start(&v, &p, samp, SR, 127);
        render(&v, 2000);
        float lp24 = render(&v, 2000);

        p.filter_type = DR32_FILT_HP24;
        dr32_voice_start(&v, &p, samp, SR, 127);
        render(&v, 2000);
        float hp24 = render(&v, 2000);

        // 5 kHz is ~2.3 octaves above a 1 kHz cutoff: 12 dB/oct predicts ~-28 dB
        // (0.04), 24 dB/oct ~-56 dB (0.0016). Assert the ORDERING and rough
        // magnitude rather than exact figures, which depend on the topology.
        CHECK(lp12 < 0.2f, "LP12 @1k barely touched a 5k tone: %.4f", lp12);
        CHECK(lp24 < lp12 * 0.5f, "LP24 (%.5f) should cut well past LP12 (%.5f)", lp24, lp12);
        CHECK(hp24 > 0.5f, "HP24 @1k should pass a 5k tone, got %.4f", hp24);

        // stability: no NaN/inf at high resonance
        p.filter_type = DR32_FILT_LP24; p.resonance = 0.99f; p.cutoff = 8000.0f;
        dr32_voice_start(&v, &p, samp, SR, 127);
        float peak = 0;
        for (int b = 0; b < 20; b++) { float q = render(&v, 1024); if (q > peak) peak = q; }
        CHECK(isfinite(peak) && peak < 100.0f, "high-resonance filter blew up: %.3f", peak);
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
