// dr32_engine.c — the registry: which engines exist and which models the
// picker offers. The engines themselves are C++ (dsp/engines/*.cpp); this
// file is the C side's only way in.

#include "dr32_engine.h"

#include <stdio.h>
#include <string.h>

/* A kit port's lanes are a contiguous run of ids from its BASE. */
typedef const dr32_engine_ops *(*lane_fn)(int lane, int *count);
static const struct { int base; lane_fn lanes; } PORTS[] = {
    { DR32_ENG_9W9_BASE,  dr32_9w9_engine  },
    { DR32_ENG_6W6_BASE,  dr32_6w6_engine  },
    { DR32_ENG_8W8_BASE,  dr32_8w8_engine  },
    { DR32_ENG_CW78_BASE, dr32_cw78_engine },
};
#define N_PORTS ((int)(sizeof(PORTS) / sizeof(PORTS[0])))

const dr32_engine_ops *dr32_engine_get(int id) {
    switch (id) {
        case DR32_ENG_SIMIAN:        return &dr32_engine_simian;
        case DR32_ENG_URCHIN_DRUM:   return &dr32_engine_urchin_drum;
        case DR32_ENG_URCHIN_SNARE:  return &dr32_engine_urchin_snare;
        case DR32_ENG_URCHIN_CYMBAL: return &dr32_engine_urchin_cymbal;
        case DR32_ENG_CHOWKICK:      return &dr32_engine_chowkick;
        case DR32_ENG_FM_KICK:       return &dr32_engine_fm_kick;
        case DR32_ENG_FM_SNARE:      return &dr32_engine_fm_snare;
        case DR32_ENG_FM_METAL:      return &dr32_engine_fm_metal;
        case DR32_ENG_FM_PERC:       return &dr32_engine_fm_perc;
        default: break;
    }
    for (int p = 0; p < N_PORTS; p++) {
        int n = 0;
        PORTS[p].lanes(-1, &n);
        if (id >= PORTS[p].base && id < PORTS[p].base + n)
            return PORTS[p].lanes(id - PORTS[p].base, NULL);
    }
    return NULL;
}

static char g_module_dir[512];

void dr32_engines_set_module_dir(const char *dir) {
    snprintf(g_module_dir, sizeof(g_module_dir), "%s", dir ? dir : "");
}

const char *dr32_engines_module_dir(void) {
    return g_module_dir[0] ? g_module_dir : NULL;
}

void dr32_engines_init(int sample_rate) {
    dr32_simian_class_init(sample_rate);
    dr32_urchin_class_init(sample_rate);
    dr32_9w9_class_init(sample_rate);
}

/* Models in picker order: every engine family's own list, family by family.
 * The families are the picker's SECTIONS (src/browser.js reads the section
 * from the slug's prefix), so their order here is the order on screen. */
typedef const dr32_model *(*family_fn)(int *count);
static const family_fn FAMILIES[] = {
    dr32_simian_models, dr32_urchin_models,
    dr32_9w9_models, dr32_6w6_models, dr32_8w8_models, dr32_cw78_models,
    dr32_chowkick_models, dr32_fm_models,
};
#define N_FAMILIES ((int)(sizeof(FAMILIES) / sizeof(FAMILIES[0])))

int dr32_model_count(void) {
    int total = 0;
    for (int f = 0; f < N_FAMILIES; f++) {
        int n = 0;
        FAMILIES[f](&n);
        total += n;
    }
    return total;
}

const dr32_model *dr32_model_at(int i) {
    if (i < 0) return NULL;
    for (int f = 0; f < N_FAMILIES; f++) {
        int n = 0;
        const dr32_model *m = FAMILIES[f](&n);
        if (i < n) return &m[i];
        i -= n;
    }
    return NULL;
}

int dr32_model_find(const char *slug) {
    if (!slug || !slug[0]) return -1;
    int total = dr32_model_count();
    for (int i = 0; i < total; i++)
        if (!strcmp(dr32_model_at(i)->slug, slug)) return i;
    return -1;
}

int dr32_engine_param_index(const dr32_engine_ops *e, const char *key) {
    if (!e || !key) return -1;
    for (int i = 0; i < e->nparams; i++)
        if (!strcmp(e->params[i].key, key)) return i;
    return -1;
}
