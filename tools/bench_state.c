/* Where does get_param("state") actually spend its time?
 * Splits the two candidates: parsing the baseline, and the O(n^2) lookup. */
#include "../dsp/dr32_kit.h"
#include "../dsp/dr32_params.h"
#include "../dsp/dr32_state.h"
#include "../dsp/dr32_json.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static char blob[65536], base[65536];

static void occupy(dr32_kit *k, int pad, const char *path) {
    snprintf(k->pads[pad].path, sizeof(k->pads[pad].path), "%s", path);
}
static double ms_since(struct timespec t0) {
    struct timespec t1; clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec-t0.tv_sec)*1000.0 + (t1.tv_nsec-t0.tv_nsec)/1e6;
}

int main(void) {
    dr32_kit k; dr32_kit_init(&k);
    for (int p = 0; p < 16; p++) occupy(&k, p, "/data/CoreLibrary/x.wav");
    const char *KIT = "/data/CoreLibrary/Track Presets/Drums/Electronic/606 Kit.json";

    int bn = dr32_state_write(&k, KIT, base, sizeof(base), NULL);
    printf("baseline blob: %d bytes\n", bn);

    const int N = 200;
    struct timespec t0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; i++) dr32_state_write(&k, KIT, blob, sizeof(blob), base);
    double full = ms_since(t0) / N;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; i++) { dr32_json *r = dr32_json_parse(base); dr32_json_free(r); }
    double parse = ms_since(t0) / N;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; i++) dr32_state_write(&k, KIT, blob, sizeof(blob), NULL);
    double nobase = ms_since(t0) / N;

    printf("  full serialize (with baseline) : %.3f ms\n", full);
    printf("  baseline parse alone           : %.3f ms\n", parse);
    printf("  serialize with NO baseline     : %.3f ms   (no parse, no lookups)\n", nobase);
    printf("  => lookup cost                 : %.3f ms\n", full - parse - nobase);
    return 0;
}
