// wav.h — minimal RIFF/WAVE loader for DR32 pad samples.
//
// Scope is deliberately narrow: what Move's own sample library actually
// contains, plus what a user can drop in. Measured on device 2026-07-25 —
// Preset Samples are PCM, mono, 44100 Hz, **24-bit** (not 16), so 24-bit
// support is mandatory, not optional.
//
// Output is always float32, deinterleaved to a single mono buffer (drum pads
// are mono voices; a stereo source is downmixed at load, once, rather than
// costing us a second read every note).
//
// NEVER call this from the audio thread — it does file I/O and allocates.

#ifndef DR32_WAV_H
#define DR32_WAV_H

#include <stddef.h>

typedef struct {
    float *data;        // mono float32, owned; NULL if load failed
    size_t frames;      // number of samples in `data`
    int    sample_rate; // source rate (44100 for everything in the Move library)
    int    channels;    // source channel count, before downmix
    int    bits;        // source bit depth
} dr32_wav;

typedef enum {
    DR32_WAV_OK = 0,
    DR32_WAV_ERR_OPEN,        // file missing / unreadable
    DR32_WAV_ERR_FORMAT,      // not RIFF/WAVE, or no fmt/data chunk
    DR32_WAV_ERR_UNSUPPORTED, // codec or bit depth we don't decode
    DR32_WAV_ERR_MEMORY,
    DR32_WAV_ERR_TOO_LARGE,   // exceeds the per-sample ceiling
} dr32_wav_err;

// Per-sample frame ceiling (~60 s at 44.1 kHz). A drum pad holding a 10-minute
// field recording is a user error we absorb gracefully, not an OOM.
#define DR32_WAV_MAX_FRAMES (44100u * 60u)

/** Load `path` into `out`. Returns DR32_WAV_OK on success; on failure `out` is
 *  zeroed (data == NULL) and the error code is returned. */
dr32_wav_err dr32_wav_load(const char *path, dr32_wav *out);

/** Free the buffer and zero the struct. Safe on an already-freed/zeroed wav. */
void dr32_wav_free(dr32_wav *w);

/** Human-readable error, for logging off the audio thread. */
const char *dr32_wav_strerror(dr32_wav_err e);

#endif
