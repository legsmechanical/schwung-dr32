// dr32_voice.h — one drumCell voice: sample playback + AHD/ASR envelope +
// filter + velocity modulation. The playback *effects* (Stretch, Loop, FM, …)
// are M2; the hooks they modulate (FX1/FX2 mod targets) are already here.
//
// Everything in this header is audio-thread safe: no allocation, no I/O. The
// sample buffer is BORROWED — the kit owns it and swaps it atomically off-thread.

#ifndef DR32_VOICE_H
#define DR32_VOICE_H

#include <stddef.h>

#define DR32_SR 44100.0f

/** Voice_Envelope_Mode. A-H-D is the manual's "Trigger" (plays through on its
 *  own schedule, ignoring note-off); A-S-R is "Gate" (sustains while held,
 *  then fades over Decay). */
typedef enum { DR32_ENV_AHD = 0, DR32_ENV_ASR = 1 } dr32_env_mode;

/** Voice_Filter_Type, in the JSON's own terms. Measured on device — note that
 *  24 dB is the unsuffixed default for both LP and HP. */
typedef enum {
    DR32_FILT_LP24 = 0,   // "Lowpass"
    DR32_FILT_LP12 = 1,   // "Lowpass 12dB"
    DR32_FILT_HP24 = 2,   // "Highpass"
    DR32_FILT_PEAK = 3,   // "Peak" (uses peak_gain)
} dr32_filter_type;

/** Voice_ModulationTarget. FX1/FX2 are accepted and stored now, applied in M2. */
typedef enum {
    DR32_MOD_FILTER = 0, DR32_MOD_ATTACK, DR32_MOD_HOLD,
    DR32_MOD_DECAY,      DR32_MOD_FX1,    DR32_MOD_FX2,
} dr32_mod_target;

/** Hold sentinel: 60.0 s means "play the whole sample" (measured, see spec §2.2). */
#define DR32_HOLD_INFINITE 60.0f

/** Pad parameters, one-to-one with the drumCell JSON. Plain data: the UI writes
 *  it, the audio thread reads it. */
typedef struct {
    // sample region
    float play_start;      // Voice_PlaybackStart, 0..1 of the sample
    float play_length;     // Voice_PlaybackLength, 0..1
    // pitch
    float transpose;       // semitones
    float detune;          // cents
    int   pitch_to_env;    // Voice_PitchToEnvelopeModulation
    // level
    float gain;            // Voice_Gain, linear
    float volume_db;       // Volume, dB (cell-level, distinct from Voice_Gain)
    float pan;             // -1..1
    float vel_to_volume;   // Voice_VelocityToVolume, 0..1
    // envelope
    dr32_env_mode env_mode;
    float attack, hold, decay;   // seconds
    // filter
    int   filter_on;
    dr32_filter_type filter_type;
    float cutoff;          // Hz
    float resonance;       // 0..1
    float peak_gain;       // Voice_Filter_PeakGain
    // modulation (single slot)
    dr32_mod_target mod_target;
    float mod_amount;      // bipolar
    // routing
    int   choke_group;     // 0 = none
} dr32_pad;

/** A 2-pole TPT state-variable filter section. Two are cascaded for 24 dB. */
typedef struct { float ic1, ic2; } dr32_svf;

typedef struct {
    const float *sample;   // borrowed, may be NULL (empty pad)
    size_t sample_frames;

    int    active;
    double pos;            // fractional read position, frames
    double step;           // frames per output frame (pitch ratio)
    size_t region_start, region_end;

    // envelope
    int    stage;          // 0 attack, 1 hold/sustain, 2 decay/release, 3 done
    float  env;            // 0..1
    float  atk_rate, dec_rate;
    float  hold_left;      // seconds remaining (AHD only)
    int    gate;           // note held (ASR)
    int    hold_infinite;
    dr32_env_mode env_mode;

    float  amp;            // velocity-derived gain
    float  panl, panr;
    dr32_svf f1, f2;
    float  g, k, a_peak;   // filter coefficients
    dr32_filter_type ftype;
    int    filter_on;
} dr32_voice;

/** Start a voice. `sample` may be NULL (silent pad). `velocity` is 1..127. */
void dr32_voice_start(dr32_voice *v, const dr32_pad *p,
                      const float *sample, size_t frames, int velocity);

/** Note-off. Only meaningful in A-S-R (Gate); A-H-D ignores it, as on Move. */
void dr32_voice_release(dr32_voice *v, const dr32_pad *p);

/** Cut immediately (choke group). Uses a short fade, not a hard stop, so a
 *  choked pad doesn't click. */
void dr32_voice_choke(dr32_voice *v);

/** Render `n` frames additively into interleaved stereo `out`.
 *  Returns 1 while the voice is still sounding, 0 once finished. */
int dr32_voice_render(dr32_voice *v, float *out, int n);

/** Fill a pad struct with the drumCell defaults (what an untouched cell means). */
void dr32_pad_defaults(dr32_pad *p);

#endif
