#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

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

/* ---------------------------------------------------------------- AIFF
 * Move's factory Core Library ships .aif alongside .wav, so a WAV-only loader
 * silently loses a large part of the stock content. AIFF is big-endian, its
 * sample rate is an 80-bit IEEE extended float, and SSND carries an offset. */

static uint16_t rd16be(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32be(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/** 80-bit IEEE 754 extended -> double (only the range we care about). */
static double rd_extended(const unsigned char *p) {
    int sign = (p[0] & 0x80) ? -1 : 1;
    int exp = ((p[0] & 0x7f) << 8) | p[1];
    uint64_t mant = 0;
    for (int i = 0; i < 8; i++) mant = (mant << 8) | p[2 + i];
    if (exp == 0 && mant == 0) return 0.0;
    return sign * (double)mant * pow(2.0, (double)(exp - 16383 - 63));
}

/** Decode one big-endian PCM sample. */
static inline float decode_be(const unsigned char *p, int bits) {
    switch (bits) {
        case 8:  return (float)(int8_t)p[0] / 128.0f;      /* AIFF 8-bit is SIGNED */
        case 16: return (float)(int16_t)rd16be(p) / 32768.0f;
        case 24: {
            int32_t v = (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8));
            return (float)(v >> 8) / 8388608.0f;
        }
        case 32: return (float)(int32_t)rd32be(p) / 2147483648.0f;
        default: return 0.0f;
    }
}

static dr32_wav_err load_aiff(FILE *f, dr32_wav *out) {
    dr32_wav_err err = DR32_WAV_ERR_FORMAT;
    uint16_t channels = 0, bits = 0;
    uint32_t nframes = 0;
    double rate = 44100.0;
    int have_comm = 0;
    int is_sowt = 0;          /* AIFC "sowt" = little-endian samples */

    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        uint32_t size = rd32be(ch + 4);

        if (!memcmp(ch, "COMM", 4)) {
            unsigned char c[40];
            uint32_t want = size < sizeof(c) ? size : (uint32_t)sizeof(c);
            if (fread(c, 1, want, f) != want) goto fail;
            channels = rd16be(c);
            nframes  = rd32be(c + 2);
            bits     = rd16be(c + 6);
            rate     = rd_extended(c + 8);
            if (want >= 22 && !memcmp(c + 18, "sowt", 4)) is_sowt = 1;
            have_comm = 1;
            if (want < size) fseek(f, (long)(size - want), SEEK_CUR);
        } else if (!memcmp(ch, "SSND", 4)) {
            if (!have_comm) goto fail;
            unsigned char s8[8];
            if (fread(s8, 1, 8, f) != 8) goto fail;
            uint32_t offset = rd32be(s8);
            if (offset) fseek(f, (long)offset, SEEK_CUR);

            if (channels < 1 || channels > 8) { err = DR32_WAV_ERR_UNSUPPORTED; goto fail; }
            if (bits != 8 && bits != 16 && bits != 24 && bits != 32) {
                err = DR32_WAV_ERR_UNSUPPORTED; goto fail;
            }
            uint32_t bps = bits / 8u;
            uint32_t frame_bytes = bps * channels;
            size_t frames = nframes;
            if (!frames || !frame_bytes) { err = DR32_WAV_ERR_FORMAT; goto fail; }
            if (frames > DR32_WAV_MAX_FRAMES) frames = DR32_WAV_MAX_FRAMES;

            int out_ch = (channels >= 2) ? 2 : 1;
            unsigned char *raw = (unsigned char *)malloc(frames * frame_bytes);
            float *pcm = (float *)malloc(frames * (size_t)out_ch * sizeof(float));
            if (!raw || !pcm) { free(raw); free(pcm); err = DR32_WAV_ERR_MEMORY; goto fail; }
            if (fread(raw, 1, frames * frame_bytes, f) != frames * frame_bytes) {
                free(raw); free(pcm); err = DR32_WAV_ERR_FORMAT; goto fail;
            }
            for (size_t i = 0; i < frames; i++) {
                const unsigned char *p = raw + i * frame_bytes;
                for (int c = 0; c < out_ch; c++) {
                    const unsigned char *sp = p + (size_t)c * bps;
                    pcm[i * out_ch + c] = is_sowt ? decode(sp, bits, 0) : decode_be(sp, bits);
                }
            }
            free(raw);
            out->data = pcm;
            out->frames = frames;
            out->sample_rate = (int)(rate + 0.5);
            out->channels = out_ch;
            out->bits = (int)bits;
            fclose(f);
            return DR32_WAV_OK;
        } else {
            fseek(f, (long)size, SEEK_CUR);
        }
        if (size & 1u) fseek(f, 1, SEEK_CUR);
    }
fail:
    fclose(f);
    memset(out, 0, sizeof(*out));
    return err;
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
    if (!memcmp(hdr, "FORM", 4) &&
        (!memcmp(hdr + 8, "AIFF", 4) || !memcmp(hdr + 8, "AIFC", 4))) {
        return load_aiff(f, out);
    }
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

/* ---------------------------------------------------------------- writer
 * 24-bit PCM, the Resample feature's output (dr32_resample.c). The encoding is
 * the exact inverse of decode() above, so what a pad plays from memory and
 * what it plays after the file is reloaded are the same numbers. */

static int32_t q24_int(float x) {
    float v = x * 8388608.0f;
    if (!(v > -8388608.0f)) return -8388608;      /* also catches NaN */
    if (v >= 8388607.0f) return 8388607;
    return (int32_t)lrintf(v);
}

float dr32_wav_q24(float x) { return (float)q24_int(x) / 8388608.0f; }

static void put16(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i)); }

int dr32_wav_write24(const char *path, const float *data, size_t frames,
                     int channels, int sample_rate) {
    if (!path || !path[0] || (!data && frames) || channels < 1 || channels > 2 || sample_rate <= 0) return -1;
    const uint64_t bytes = (uint64_t)frames * (uint64_t)channels * 3u;
    if (bytes > 0xFFFFFFF0u - 36u) return -1;
    char part[4096];
    if (snprintf(part, sizeof(part), "%s.part", path) >= (int)sizeof(part)) return -1;
    FILE *f = fopen(part, "wb");
    if (!f) return -1;

    unsigned char h[44];
    memcpy(h, "RIFF", 4);  put32(h + 4, (uint32_t)(36u + bytes + (bytes & 1u)));
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4); put32(h + 16, 16);
    put16(h + 20, WAVE_FMT_PCM); put16(h + 22, (uint32_t)channels);
    put32(h + 24, (uint32_t)sample_rate);
    put32(h + 28, (uint32_t)sample_rate * (uint32_t)channels * 3u);
    put16(h + 32, (uint32_t)channels * 3u); put16(h + 34, 24);
    memcpy(h + 36, "data", 4); put32(h + 40, (uint32_t)bytes);
    int ok = fwrite(h, 1, sizeof(h), f) == sizeof(h);

    unsigned char buf[3 * 2 * 1024];
    const size_t n = frames * (size_t)channels;
    for (size_t i = 0; ok && i < n; ) {
        size_t m = 0;
        for (; i < n && m + 3 <= sizeof(buf); i++, m += 3) {
            const uint32_t v = (uint32_t)q24_int(data[i]);
            buf[m] = (unsigned char)v; buf[m + 1] = (unsigned char)(v >> 8); buf[m + 2] = (unsigned char)(v >> 16);
        }
        ok = fwrite(buf, 1, m, f) == m;
    }
    if (ok && (bytes & 1u)) ok = fputc(0, f) != EOF;        /* RIFF word alignment */
    if (fclose(f) != 0) ok = 0;
    if (!ok || rename(part, path) != 0) { remove(part); return -1; }
    return 0;
}
