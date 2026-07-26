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
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
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
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
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
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
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
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        float late = render_last(&v, SR - 100);         // nearly the whole second
        CHECK(late > 0.95f, "inf hold decayed early (%.3f)", late);
    }

    // ---- short hold decays after the hold time
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.hold = 0.05f; p.decay = 0.01f; p.attack = 0.001f;
        p.filter_on = 0; p.vel_to_volume = 0.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        float after = render_last(&v, SR / 5);          // 200 ms >> hold + decay
        CHECK(after < 0.01f, "short hold should have decayed, got %.3f", after);
    }

    // ---- velocity -> volume is the engine's dB law, NOT a linear blend:
    //      gain = 10^(((vel - 70) * amount * 0.4) / 20). Velocity 70 is unity
    //      for ANY amount, and velocities above it BOOST past unity.
    {
        CHECK(fabsf(dr32_velocity_gain(70, 1.0f) - 1.0f) < 1e-6f,
              "vel 70 must be unity, got %.6f", dr32_velocity_gain(70, 1.0f));
        CHECK(fabsf(dr32_velocity_gain(1, 0.0f) - 1.0f) < 1e-6f, "amount 0 must be unity");
        float want127 = powf(10.0f, ((127.0f - 70.0f) * 0.35f * 0.4f) / 20.0f);
        CHECK(fabsf(dr32_velocity_gain(127, 0.35f) - want127) < 1e-5f,
              "vel 127 @0.35 = %.5f, want %.5f", dr32_velocity_gain(127, 0.35f), want127);
        CHECK(dr32_velocity_gain(127, 0.35f) > 1.0f, "high velocity should exceed unity");
        CHECK(dr32_velocity_gain(20, 0.35f) < 1.0f, "low velocity should fall below unity");

        // and it reaches the voice
        dr32_pad p; dr32_pad_defaults(&p);
        p.attack = 0.001f; p.hold = DR32_HOLD_INFINITE; p.filter_on = 0;
        p.vel_to_volume = 0.35f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 1, 70);
        CHECK(fabsf(render_last(&v, SR / 10) - 1.0f) < 0.02f, "vel 70 through the voice != unity");
    }

    // ---- pan: equal-power over the SERIALIZED -50..+50 domain, sqrt(2)-scaled
    //      so centre is unity in both channels (measured: p=-25 -> 1.30656087
    //      and 0.541196287).
    {
        float l, r;
        dr32_pan_gains(0.0f, &l, &r);
        CHECK(fabsf(l - 1.0f) < 1e-5f && fabsf(r - 1.0f) < 1e-5f,
              "centre pan should be unity both channels, got %.6f/%.6f", l, r);
        dr32_pan_gains(-25.0f, &l, &r);
        CHECK(fabsf(l - 1.30656087f) < 1e-4f, "pan -25 L = %.8f, want 1.30656087", l);
        CHECK(fabsf(r - 0.541196287f) < 1e-4f, "pan -25 R = %.8f, want 0.541196287", r);
        dr32_pan_gains(25.0f, &l, &r);
        CHECK(fabsf(l - 0.541196287f) < 1e-4f, "pan +25 should mirror -25");
        dr32_pan_gains(-50.0f, &l, &r);
        CHECK(fabsf(l - 1.41421356f) < 1e-4f, "hard left L should be ~sqrt(2), got %.6f", l);
        CHECK(fabsf(r - 1e-5f) < 1e-6f, "silent endpoint floors to 1e-5, got %.8f", r);
    }

    // ---- filter coefficients match the engine's own approximations
    {
        // g is a rational approximation of tan(pi*f/fs) — close to tanf but not
        // identical, and the difference is part of the sound.
        for (float f = 50.0f; f < 20000.0f; f *= 2.0f) {
            float g = dr32_filter_g(f);
            float ref = tanf((float)M_PI * f / 44100.0f);
            CHECK(fabsf(g - ref) / ref < 0.01f, "g(%.0f) = %.6f vs tan %.6f", f, g, ref);
        }
        CHECK(dr32_filter_g(10.0f) == dr32_filter_g(30.0f), "cutoff must clamp at 30 Hz");
        CHECK(dr32_filter_g(30000.0f) == dr32_filter_g(22000.0f), "cutoff must clamp at 22 kHz");

        float k1, k2;
        dr32_filter_k(0.0f, &k1, &k2);
        CHECK(fabsf(k1 - 2.0f) < 1e-6f && fabsf(k2 - 2.0f) < 1e-6f,
              "zero resonance should be k=2 both stages, got %.4f/%.4f", k1, k2);
        dr32_filter_k(0.5f, &k1, &k2);
        CHECK(fabsf(k1 - 2.0f * 0.5f / 2.5f) < 1e-6f, "k1 law wrong: %.6f", k1);
        CHECK(fabsf(k2 - 2.0f * 1.5f / 2.5f) < 1e-6f, "k2 law wrong: %.6f", k2);
        CHECK(k1 < k2, "resonance should be distributed, k1 < k2");
        dr32_filter_k(5.0f, &k1, &k2);       // clamps at 0.9
        float c1, c2; dr32_filter_k(0.9f, &c1, &c2);
        CHECK(fabsf(k1 - c1) < 1e-6f, "resonance must clamp at 0.9");
    }

    // ---- transpose sets the playback rate (+12 st = 2x, so a 1 s sample ends
    //      after 0.5 s)
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.transpose = 12.0f; p.hold = DR32_HOLD_INFINITE; p.attack = 0.001f;
        p.filter_on = 0; p.vel_to_volume = 0.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        CHECK(fabs(v.step - 2.0) < 1e-6, "+12 st step = %.6f, want 2.0", v.step);
        render(&v, SR / 2 - 200);
        CHECK(v.active, "voice ended too early at 2x");
        render(&v, 400);
        CHECK(!v.active, "voice should have consumed the sample by 0.5 s at 2x");

        p.transpose = -12.0f;
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        CHECK(fabs(v.step - 0.5) < 1e-6, "-12 st step = %.6f, want 0.5", v.step);
    }

    // ---- detune is cents
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.detune = 100.0f;                    // 100 cents == 1 semitone
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        CHECK(fabs(v.step - pow(2.0, 1.0 / 12.0)) < 1e-6, "100 cents != 1 semitone");
    }

    // ---- playback start/length select a region
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.play_start = 0.25f; p.play_length = 0.5f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        // The engine maps normalized start over N-1, not N (note start path,
        // "convert normalized playback start to a frame offset within N - 1").
        size_t want_start = (size_t)(0.25 * (double)(SR - 1));
        CHECK(v.region_start == want_start, "region start %zu, want %zu", v.region_start, want_start);
        CHECK(v.region_end == want_start + SR / 2, "region end %zu", v.region_end);
    }

    // ---- empty pad (no sample) must not sound or crash
    {
        dr32_pad p; dr32_pad_defaults(&p);
        dr32_voice v;
        dr32_voice_start(&v, &p, NULL, 0, 1, 127);
        CHECK(!v.active, "empty pad should not be active");
        CHECK(render(&v, 256) == 0.0f, "empty pad produced signal");
    }

    // ---- choke fades fast but does not click (no discontinuity to zero)
    {
        dr32_pad p; dr32_pad_defaults(&p);
        p.hold = DR32_HOLD_INFINITE; p.attack = 0.001f; p.decay = 10.0f;
        p.filter_on = 0; p.vel_to_volume = 0.0f;
        dr32_voice v;
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
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
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        render(&v, 2000);
        float lp12 = render(&v, 2000);

        p.filter_type = DR32_FILT_LP24;
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        render(&v, 2000);
        float lp24 = render(&v, 2000);

        p.filter_type = DR32_FILT_HP24;
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        render(&v, 2000);
        float hp24 = render(&v, 2000);

        // 5 kHz is ~2.3 octaves above a 1 kHz cutoff: 12 dB/oct predicts ~-28 dB
        // (0.04), 24 dB/oct ~-56 dB (0.0016). Assert the ORDERING and rough
        // magnitude rather than exact figures, which depend on the topology.
        CHECK(lp12 < 0.2f, "LP12 @1k barely touched a 5k tone: %.4f", lp12);
        // Peak at normalized gain 1.0 is EXACTLY flat: the band term cancels
        // because peak_coef == k == 5/3.
        {
            dr32_pad q; dr32_pad_defaults(&q);
            q.hold = DR32_HOLD_INFINITE; q.attack = 0.0001f; q.vel_to_volume = 0.0f;
            q.filter_type = DR32_FILT_PEAK; q.peak_gain = 1.0f; q.cutoff = 1000.0f;
            dr32_voice pv;
            dr32_voice_start(&pv, &q, samp, SR, 1, 70);
            render(&pv, 2000);
            float flat = render(&pv, 2000);
            q.filter_on = 0;
            dr32_voice bv;
            dr32_voice_start(&bv, &q, samp, SR, 1, 70);
            render(&bv, 2000);
            float bypass = render(&bv, 2000);
            CHECK(fabsf(flat - bypass) < 1e-3f,
                  "Peak @gain 1.0 must be flat: %.5f vs bypass %.5f", flat, bypass);
        }
        CHECK(lp24 < lp12 * 0.5f, "LP24 (%.5f) should cut well past LP12 (%.5f)", lp24, lp12);
        CHECK(hp24 > 0.5f, "HP24 @1k should pass a 5k tone, got %.4f", hp24);

        // stability: no NaN/inf at high resonance
        p.filter_type = DR32_FILT_LP24; p.resonance = 0.99f; p.cutoff = 8000.0f;
        dr32_voice_start(&v, &p, samp, SR, 1, 127);
        float peak = 0;
        for (int b = 0; b < 20; b++) { float q = render(&v, 1024); if (q > peak) peak = q; }
        CHECK(isfinite(peak) && peak < 100.0f, "high-resonance filter blew up: %.3f", peak);
    }

    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
