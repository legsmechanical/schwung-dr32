#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// WAVE format tags
#define WAVE_FMT_PCM        0x0001
#define WAVE_FMT_FLOAT      0x0003
#define WAVE_FMT_EXTENSIBLE 0xFFFE

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const char *dr32_wav_strerror(dr32_wav_err e) {
    switch (e) {
        case DR32_WAV_OK:              return "ok";
        case DR32_WAV_ERR_OPEN:        return "cannot open file";
        case DR32_WAV_ERR_FORMAT:      return "not a RIFF/WAVE file";
        case DR32_WAV_ERR_UNSUPPORTED: return "unsupported codec or bit depth";
        case DR32_WAV_ERR_MEMORY:      return "out of memory";
        case DR32_WAV_ERR_TOO_LARGE:   return "sample too long";
    }
    return "unknown";
}

void dr32_wav_free(dr32_wav *w) {
    if (!w) return;
    free(w->data);
    memset(w, 0, sizeof(*w));
}

/** Decode one sample of `bits` depth at `p` into [-1,1]. */
static inline float decode(const unsigned char *p, int bits, int is_float) {
    if (is_float) {
        float f;
        memcpy(&f, p, 4);
        return f;
    }
    switch (bits) {
        case 8:  // 8-bit PCM is UNSIGNED (offset binary) — the one asymmetry in RIFF
            return ((float)p[0] - 128.0f) / 128.0f;
        case 16: {
            int16_t v = (int16_t)rd16(p);
            return (float)v / 32768.0f;
        }
        case 24: {
            int32_t v = (int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) |
                                  ((uint32_t)p[2] << 24));
            return (float)(v >> 8) / 8388608.0f;   // arithmetic shift keeps the sign
        }
        case 32: {
            int32_t v = (int32_t)rd32(p);
            return (float)v / 2147483648.0f;
        }
        default: return 0.0f;
    }
}

dr32_wav_err dr32_wav_load(const char *path, dr32_wav *out) {
    if (!out) return DR32_WAV_ERR_FORMAT;
    memset(out, 0, sizeof(*out));
    if (!path) return DR32_WAV_ERR_OPEN;

    FILE *f = fopen(path, "rb");
    if (!f) return DR32_WAV_ERR_OPEN;

    dr32_wav_err err = DR32_WAV_ERR_FORMAT;
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12) goto fail;
    if (memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) goto fail;

    int have_fmt = 0;
    uint16_t fmt_tag = 0, channels = 0, bits = 0;
    uint32_t rate = 0;

    // Chunk walk. Chunks may appear in any order and there may be chunks we
    // don't care about (LIST/INFO, smpl, ...), so scan rather than assume.
    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8) break;          // clean EOF between chunks
        uint32_t id_size = rd32(ch + 4);

        if (!memcmp(ch, "fmt ", 4)) {
            unsigned char fmt[40];
            uint32_t want = id_size < sizeof(fmt) ? id_size : (uint32_t)sizeof(fmt);
            if (fread(fmt, 1, want, f) != want) goto fail;
            fmt_tag  = rd16(fmt);
            channels = rd16(fmt + 2);
            rate     = rd32(fmt + 4);
            bits     = rd16(fmt + 14);
            // WAVE_FORMAT_EXTENSIBLE hides the real tag in the GUID's first two
            // bytes; without this, 24-bit files written by some editors read as
            // "unsupported".
            if (fmt_tag == WAVE_FMT_EXTENSIBLE && want >= 26) fmt_tag = rd16(fmt + 24);
            have_fmt = 1;
            if (want < id_size) fseek(f, (long)(id_size - want), SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            if (!have_fmt) goto fail;

            int is_float = (fmt_tag == WAVE_FMT_FLOAT);
            if (fmt_tag != WAVE_FMT_PCM && !is_float) { err = DR32_WAV_ERR_UNSUPPORTED; goto fail; }
            if (channels < 1 || channels > 8)          { err = DR32_WAV_ERR_UNSUPPORTED; goto fail; }
            if (is_float ? (bits != 32) : (bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
                err = DR32_WAV_ERR_UNSUPPORTED; goto fail;
            }

            uint32_t bytes_per_sample = (uint32_t)bits / 8u;
            uint32_t frame_bytes = bytes_per_sample * channels;
            if (frame_bytes == 0) { err = DR32_WAV_ERR_UNSUPPORTED; goto fail; }

            size_t frames = id_size / frame_bytes;
            if (frames == 0) { err = DR32_WAV_ERR_FORMAT; goto fail; }
            if (frames > DR32_WAV_MAX_FRAMES) frames = DR32_WAV_MAX_FRAMES;  // clamp, don't fail

            // Keep 1 or 2 channels as-is; fold anything wider down to stereo.
            int out_ch = (channels >= 2) ? 2 : 1;

            unsigned char *raw = (unsigned char *)malloc(frames * frame_bytes);
            float *pcm = (float *)malloc(frames * (size_t)out_ch * sizeof(float));
            if (!raw || !pcm) { free(raw); free(pcm); err = DR32_WAV_ERR_MEMORY; goto fail; }

            if (fread(raw, 1, frames * frame_bytes, f) != frames * frame_bytes) {
                free(raw); free(pcm); err = DR32_WAV_ERR_FORMAT; goto fail;
            }

            for (size_t i = 0; i < frames; i++) {
                const unsigned char *p = raw + i * frame_bytes;
                if (out_ch == 1) {
                    pcm[i] = decode(p, bits, is_float);
                } else {
                    pcm[2 * i]     = decode(p, bits, is_float);
                    pcm[2 * i + 1] = decode(p + bytes_per_sample, bits, is_float);
                    // >2 channels: extra channels are summed into the pair so
                    // nothing is silently dropped.
                    for (int c = 2; c < channels; c++) {
                        float extra = decode(p + (size_t)c * bytes_per_sample, bits, is_float);
                        pcm[2 * i + (c & 1)] += extra;
                    }
                }
            }
            free(raw);

            out->data        = pcm;
            out->frames      = frames;
            out->sample_rate = (int)rate;
            out->channels    = out_ch;
            out->bits        = (int)bits;
            fclose(f);
            return DR32_WAV_OK;
        } else {
            fseek(f, (long)id_size, SEEK_CUR);
        }
        if (id_size & 1u) fseek(f, 1, SEEK_CUR);      // RIFF chunks are word-aligned
    }

fail:
    fclose(f);
    memset(out, 0, sizeof(*out));
    return err;
}
