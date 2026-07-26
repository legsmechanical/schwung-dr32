// dr32_voice.h — one drumCell voice, reconstructed against the native engine.
//
// ⚠ THIS FILE TRACKS A BINARY RECONSTRUCTION, NOT A GUESS.
// The laws here (filter coefficients, velocity gain, pan, interpolation,
// clamps) come from `move original reconstruct/analysis/native-instruments/`
// — DRUM_FILTER_RECON.md, DRUM_SAMPLER_TRACE.md, DRUM_RACK_ARCHITECTURE.md.
// Do not "improve" them: any deviation is a fidelity bug, even when it sounds
// nicer. Notably the native engine uses LINEAR interpolation, and its
// velocity->volume law is in dB, not linear.
//
// Audio-thread safe: no allocation, no I/O. Sample buffers are BORROWED.

#ifndef DR32_VOICE_H
#define DR32_VOICE_H

#include <stddef.h>

#define DR32_SR 44100.0f

/** Voice_Envelope_Mode. A-H-D = manual's "Trigger" (ignores note-off);
 *  A-S-R = "Gate" (sustains while held). Matches the engine's 2-value
 *  envelope_mode dimension in the 320-kernel specialization index. */
typedef enum { DR32_ENV_AHD = 0, DR32_ENV_ASR = 1 } dr32_env_mode;

/** Filter type. NUMBERING MATCHES THE NATIVE ENGINE's filter_type field
 *  (DRUM_FILTER_RECON.md) — 0 LP12, 1 LP24, 2 HP24, 3 Peak. Note this is NOT
 *  the order the JSON strings suggest; "Lowpass" (the JSON default) is LP24. */
typedef enum {
    DR32_FILT_LP12 = 0,
    DR32_FILT_LP24 = 1,
    DR32_FILT_HP24 = 2,
    DR32_FILT_PEAK = 3,
} dr32_filter_type;

typedef enum {
    DR32_MOD_FILTER = 0, DR32_MOD_ATTACK, DR32_MOD_HOLD,
    DR32_MOD_DECAY,      DR32_MOD_FX1,    DR32_MOD_FX2,
} dr32_mod_target;

/** Hold sentinel: 60 s = the engine's hold clamp ceiling, which the UI shows
 *  as "inf" (play the whole sample). */
#define DR32_HOLD_INFINITE 60.0f

// Native runtime clamps (DRUM_RACK_ARCHITECTURE.md "Core runtime limits").
#define DR32_ATTACK_MIN 0.0001f
#define DR32_ATTACK_MAX 20.0f
#define DR32_DECAY_MIN  0.001f
#define DR32_DECAY_MAX  60.0f
#define DR32_HOLD_MIN   0.001f
#define DR32_HOLD_MAX   60.0f
#define DR32_CUTOFF_MIN 30.0f
#define DR32_CUTOFF_MAX 22000.0f
#define DR32_RESO_MAX   0.9f

/** Pad parameters, one-to-one with the drumCell JSON. */
typedef struct {
    float play_start;      // Voice_PlaybackStart, normalized fraction
    float play_length;     // Voice_PlaybackLength, normalized fraction
    float transpose;       // semitones
    float detune;          // CENTS as stored; converted internally
    int   pitch_to_env;
    float gain;            // Voice_Gain, linear
    float volume_db;       // chain mixer volume, dB
    float pan;             // chain mixer pan, SERIALIZED DOMAIN -50..+50
    float vel_to_volume;   // Voice_VelocityToVolume amount
    dr32_env_mode env_mode;
    float attack, hold, decay;   // seconds
    int   filter_on;
    dr32_filter_type filter_type;
    float cutoff;          // Hz
    float resonance;       // 0..0.9 after clamp
    float peak_gain;       // normalized; engine multiplies by 5/3
    dr32_mod_target mod_target;
    float mod_amount;
    int   choke_group;     // 0 = none, 1..16
    int   sending_note;    // note the rack feeds the cell (factory kits: 60)
    int   speaker_on;      // mixer speakerOn; 0 = sample-exact silence
} dr32_pad;

/** One TPT SVF stage's integrator state. */
typedef struct { float s1, s2; } dr32_svf;

typedef struct {
    const float *sample;   // borrowed, interleaved; may be NULL
    size_t sample_frames;
    int    channels;       // 1 or 2 — native keeps stereo samples stereo

    int    active;
    double pos;
    double step;
    size_t region_start, region_end;

    int    stage;          // 0 attack, 1 hold/sustain, 2 decay/release, 3 done
    float  env;
    float  atk_rate, dec_rate;
    float  hold_left;
    int    gate;
    int    hold_infinite;
    dr32_env_mode env_mode;

    float  amp;
    float  panl, panr;

    // Filter: independent state per channel, two stages for the 24 dB cascade.
    dr32_svf fL1, fL2, fR1, fR2;
    float  g, k1, k2, peak_coef;
    dr32_filter_type ftype;
    int    filter_on;      // effective (post-bypass-test)

    int    note;           // note this voice was started with (choke priority)
    unsigned block;        // render block it started in (simultaneity test)
} dr32_voice;

void dr32_voice_start(dr32_voice *v, const dr32_pad *p,
                      const float *sample, size_t frames, int channels,
                      int velocity);
void dr32_voice_release(dr32_voice *v, const dr32_pad *p);
void dr32_voice_choke(dr32_voice *v);
int  dr32_voice_render(dr32_voice *v, float *out, int n);
void dr32_pad_defaults(dr32_pad *p);

/** Exposed for tests: the engine's exact tan approximation and resonance split. */
float dr32_filter_g(float cutoff_hz);
void  dr32_filter_k(float resonance, float *k1, float *k2);
/** Exposed for tests: the engine's dB velocity law. */
float dr32_velocity_gain(int velocity, float amount);
/** Exposed for tests: equal-power pan over the -50..+50 domain. */
void  dr32_pan_gains(float pan, float *l, float *r);

#endif
