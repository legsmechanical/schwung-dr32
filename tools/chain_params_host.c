// chain_params_host.c — the HOST's own param parser, run over DR32's files.
//
// Built by tools/check_chain_params.mjs against a Schwung checkout's
// src/modules/chain/dsp/chain_params.c (SCHWUNG_SRC), never shipped.
//
//   chain_params_host <dir with module.json> <served chain_params.json>
//
// Prints JSON: how many params the host parses out of module.json (its
// metadata when a plugin serves no chain_params), how many it parses out of
// the served list (what the modulation refresh would replace them with), the
// indices where the two differ field for field, and the host's FALLBACK
// answer (what the page read before DR32 served its own).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain_internal.h"

void chain_log(const char *msg) { (void) msg; }

static chain_param_info_t A[MAX_CHAIN_PARAMS], B[MAX_CHAIN_PARAMS];

static void jstr(const char *s) {
    putchar('"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); }
    putchar('"');
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <dir> <served.json>\n", argv[0]); return 2; }
    int na = 0;
    if (parse_chain_params(argv[1], A, &na) != 0) { fprintf(stderr, "parse_chain_params failed\n"); return 1; }
    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t) n + 1);
    if (!s || fread(s, 1, (size_t) n, f) != (size_t) n) return 1;
    s[n] = 0; fclose(f);
    int nb = parse_chain_params_array_json(s, B, MAX_CHAIN_PARAMS);

    printf("{\"module_json\":%d,\"served\":%d,\"differ\":[", na, nb);
    int first = 1;
    for (int i = 0; i < na && i < nb; i++)
        if (memcmp(&A[i], &B[i], sizeof A[i])) { printf("%s", first ? "" : ","); jstr(A[i].key); first = 0; }
    printf("],\"fallback\":");

    /* The host's fallback answer, as chain_host.c emits it for a synth that
     * serves no chain_params (the "Fall back to parsed module.json data"
     * branch of get_param "chain_params"), field for field. */
    static char buf[262144];
    int off = 0, len = (int) sizeof buf;
    off += snprintf(buf + off, len - off, "[");
    for (int i = 0; i < na && off < len - 100; i++) {
        chain_param_info_t *p = &A[i];
        if (i > 0) off += snprintf(buf + off, len - off, ",");
        const char *type_str = (p->type == KNOB_TYPE_INT) ? "int" : (p->type == KNOB_TYPE_ENUM) ? "enum" : "float";
        off += snprintf(buf + off, len - off, "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g",
                        p->key, p->name[0] ? p->name : p->key, type_str, p->min_val, p->max_val);
        if (p->type == KNOB_TYPE_ENUM && p->option_count > 0) {
            off += snprintf(buf + off, len - off, ",\"options\":[");
            for (int j = 0; j < p->option_count && j < MAX_ENUM_OPTIONS; j++) {
                if (j > 0) off += snprintf(buf + off, len - off, ",");
                off += snprintf(buf + off, len - off, "\"%s\"", p->options[j]);
            }
            off += snprintf(buf + off, len - off, "]");
        }
        if (p->unit[0]) off += snprintf(buf + off, len - off, ",\"unit\":\"%s\"", p->unit);
        if (p->display_format[0]) off += snprintf(buf + off, len - off, ",\"display_format\":\"%s\"", p->display_format);
        off += snprintf(buf + off, len - off, "}");
    }
    off += snprintf(buf + off, len - off, "]");
    printf("%s}\n", buf);
    return 0;
}
