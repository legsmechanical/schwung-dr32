// dump_engines.c — print the engine and model tables as JSON.
//
// The engine descriptors in dsp/engines/*.cpp are the ONE source for every
// synth parameter: key, label, range, page. tools/gen_engine_ui.mjs compiles
// this against them and builds the engine pages in src/module.json and the
// picker's model list in src/browser.js from what it prints, so the UI cannot
// drift from the DSP. Off-device only; nothing here ships.
#include <stdio.h>

#include "../dsp/dr32_engine.h"

static void str(const char *s) {
    putchar('"');
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

int main(void) {
    printf("{\"engines\":[");
    for (int id = 1; id < DR32_ENG_COUNT; id++) {
        const dr32_engine_ops *e = dr32_engine_get(id);
        if (!e) continue;
        printf("%s{\"id\":%d,\"slug\":", id > 1 ? "," : "", e->id);
        str(e->slug); printf(",\"name\":"); str(e->name);
        printf(",\"prefix\":"); str(e->prefix); printf(",\"params\":[");
        for (int i = 0; i < e->nparams; i++) {
            const dr32_eparam *p = &e->params[i];
            printf("%s{\"key\":", i ? "," : ""); str(p->key);
            printf(",\"name\":"); str(p->name);
            printf(",\"short_name\":"); str(p->short_name);
            printf(",\"min\":%g,\"max\":%g,\"def\":%g,\"step\":%g",
                   (double)p->min, (double)p->max, (double)p->def, (double)p->step);
            printf(",\"unit\":"); if (p->unit) str(p->unit); else printf("null");
            printf(",\"page\":"); str(p->page);
            printf(",\"options\":"); if (p->options) str(p->options); else printf("null");
            printf("}");
        }
        printf("]}");
    }
    printf("],\"models\":[");
    for (int i = 0; i < dr32_model_count(); i++) {
        const dr32_model *m = dr32_model_at(i);
        printf("%s{\"slug\":", i ? "," : ""); str(m->slug);
        printf(",\"name\":"); str(m->name); printf(",\"engine\":%d}", m->engine);
    }
    printf("]}\n");
    return 0;
}
