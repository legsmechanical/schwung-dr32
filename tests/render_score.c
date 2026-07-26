// render_score.c — offline renderer for the null test.
//
// Reads a score produced by tools/score.mjs, renders it through the SHIPPING
// engine (dr32_params -> dr32_kit -> dr32_voice), and writes a 32-bit float
// stereo WAV that can be diffed against the stock engine's own render.
//
// Usage: render_score <in.score> <out.wav> [frames]

#include "../dsp/dr32_params.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SR 44100
#define BLOCK 128          // the host's block size

typedef struct { int frame, on, note, vel; } event;

static void w16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void w32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }

static void write_float_wav(const char *path, const float *data, size_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    uint32_t bytes = (uint32_t)(frames * 2 * sizeof(float));
    fputs("RIFF", f); w32(f, 36 + bytes); fputs("WAVE", f);
    fputs("fmt ", f); w32(f, 16);
    w16(f, 3);                      // IEEE float
    w16(f, 2);
    w32(f, SR);
    w32(f, SR * 2 * sizeof(float));
    w16(f, 2 * sizeof(float));
    w16(f, 32);
    fputs("data", f); w32(f, bytes);
    fwrite(data, 1, bytes, f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: render_score <in.score> <out.wav> [frames]\n"); return 2; }
    size_t frames = (argc > 3) ? (size_t)atol(argv[3]) : 88200;

    FILE *in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }

    dr32_kit kit;
    dr32_kit_init(&kit);

    event *events = NULL;
    size_t n_events = 0, cap = 0;

    char line[1024];
    while (fgets(line, sizeof(line), in)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] == 'p' && line[1] == ' ') {
            char *key = line + 2;
            char *sp = strchr(key, ' ');
            if (!sp) continue;
            *sp = '\0';
            dr32_apply_param(&kit, key, sp + 1);
        } else if (line[0] == 'e' && line[1] == ' ') {
            if (n_events == cap) {
                cap = cap ? cap * 2 : 256;
                events = (event *)realloc(events, cap * sizeof(event));
            }
            event e;
            if (sscanf(line + 2, "%d %d %d %d", &e.frame, &e.on, &e.note, &e.vel) == 4)
                events[n_events++] = e;
        } else if (!strncmp(line, "master ", 7)) {
            dr32_apply_param(&kit, "master", line + 7);
        }
    }
    fclose(in);

    float *out = (float *)calloc(frames * 2, sizeof(float));
    float block[2 * BLOCK];
    size_t ei = 0;

    size_t pos = 0;
    while (pos < frames) {
        // Apply every event due at this exact frame first.
        while (ei < n_events && (size_t)events[ei].frame <= pos) {
            if (events[ei].on) dr32_kit_note_on(&kit, events[ei].note, events[ei].vel);
            else               dr32_kit_note_off(&kit, events[ei].note);
            ei++;
        }

        // Render up to the NEXT event, capped at one block. The stock engine
        // places events sample-accurately (it renders in chunks of at most 32
        // frames around timestamped events), so quantising note-ons to a 128
        // frame boundary would show up as a large transient residual and be
        // mistaken for an engine difference.
        //
        // NOTE: on the device we only get block-granular MIDI from the host, so
        // this precision exists for validation, not for playback.
        size_t next = frames;
        if (ei < n_events && (size_t)events[ei].frame < next) next = (size_t)events[ei].frame;
        size_t n = next - pos;
        if (n > BLOCK) n = BLOCK;
        if (n == 0) continue;

        dr32_kit_render(&kit, block, (int)n);
        memcpy(out + pos * 2, block, n * 2 * sizeof(float));
        pos += n;
    }

    write_float_wav(argv[2], out, frames);
    fprintf(stderr, "%s: %zu frames, %zu events\n", argv[2], frames, n_events);

    free(out);
    free(events);
    dr32_kit_free(&kit);
    return 0;
}
