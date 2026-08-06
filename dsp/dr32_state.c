// dr32_state.c — the `state` blob the host uses to persist a chain slot.
//
// WHY THIS EXISTS. Schwung persists a slot by asking the loaded module for
// get_param("state") and storing the answer in the set's slot_N.json, handing
// it back with set_param("state", ...) on reload. DR32 answered individual keys
// but never "state", so the host had nothing to save: `"config": {}` in every
// slot file, and NOTHING about a DR32 slot survived a reboot — kit, pad edits,
// sends, all gone. (Diagnosed 2026-08-05 while chasing an unrelated bug; the
// host's autosave was logging `chain=false` for exactly this reason.)
//
// SHAPE. State is the kit PATH plus the flat params, not a struct dump:
//
//   { "v":1, "kit":"<path>", "params": { "pad0_transpose":"-5", ... } }
//
// Loading the kit first reproduces the samples and every factory default, and
// the params then restore the user's edits on top. That keeps the blob small,
// makes it readable, and — the real reason — means this file never duplicates
// the parameter table. It round-trips through dr32_read_param /
// dr32_apply_param, the same pair the Shadow UI drives, so a field added there
// is persisted here by adding one name to the list below rather than by writing
// matching serialize and parse code that can silently drift apart.
//
// Restoring by PATH rather than by index is deliberate, mirroring how obxd
// restores its bank by name: an index breaks the moment the library changes
// underneath it.
//
// THREADING. Both directions run wherever the host serves params. The write
// side is pure snprintf into the caller's buffer — no allocation, no I/O. The
// read side parses JSON and may load a kit, exactly as set_param("kit") already
// does, so it is no worse than the path that has always existed.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "dr32_kit.h"
#include "dr32_params.h"
#include "dr32_json.h"

/* Per-pad fields worth persisting.
 *
 * Deliberately NOT the whole vocabulary: `browse*`, `frames`, `loaded`,
 * `waveform` and `voices` are derived or read-only, `play` and `panic` are
 * actions, and `sample_move` / `sample_user` are alternate SETTERS for the same
 * value `sample` reads back — persisting them would restore the same sample
 * twice. `ui_current_pad` / `ui_auto_select_pad` are editor focus, not sound;
 * restoring them would move the user's cursor on load.
 *
 * `sample` is first so a pad's audio is in place before anything shapes it. */
static const char *const PAD_FIELDS[] = {
    "sample",
    "note", "choke", "sending_note", "speaker_on",
    "transpose", "detune", "pitch_env",
    "gain", "volume", "cell_volume", "pan", "vel_vol",
    "env_mode", "attack", "hold", "decay",
    "filter_on", "filter_type", "cutoff", "resonance", "peak_gain",
    "mod_target", "mod_amount",
    "fx_type", "fx_p1", "fx_p2",
    "start", "length", "punch", "punch_time",
    "send1", "send2",
    NULL
};

/* Kit-level fields. Sends use the canonical pN aliases rather than the
 * per-type names (`size`/`time_l`/`feedback`/...): those all resolve to the
 * same slots, but the type-specific spelling only makes sense while that type
 * is armed, so a blob written as "feedback" would be unreadable after the user
 * switched the send to a reverb. pN is the type-agnostic form. */
static const char *const GLOBAL_FIELDS[] = {
    "master",
    "bus_compress", "bus_crunch", "bus_attack", "bus_sustain", "bus_mix",
    "send1_type", "send1_return", "send1_sync",
    "send1_p1", "send1_p2", "send1_p3", "send1_p4",
    "send1_p5", "send1_p6", "send1_p7", "send1_p8",
    "send2_type", "send2_return", "send2_sync",
    "send2_p1", "send2_p2", "send2_p3", "send2_p4",
    "send2_p5", "send2_p6", "send2_p7", "send2_p8",
    NULL
};

/* Minimal JSON string escaping. Sample paths are user-supplied and can contain
 * quotes or backslashes; without this one such path makes the whole blob
 * unparseable and the slot silently fails to restore. */
static int json_escape(char *dst, int cap, const char *src) {
    int n = 0;
    for (const char *p = src; *p && n < cap - 2; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') { dst[n++] = '\\'; dst[n++] = (char)c; }
        else if (c < 0x20)         { n += snprintf(dst + n, cap - n, "\\u%04x", c); }
        else                        dst[n++] = (char)c;
    }
    dst[n < cap ? n : cap - 1] = '\0';
    return n;
}

/* Append `"key":"value",` using the live value from dr32_read_param.
 * Unknown or empty values are skipped rather than written as "", so a blob
 * never asserts a value the engine did not actually report.
 *
 * `baseline` (may be NULL) is the parsed `params` object of a blob captured
 * right after the kit loaded. A live value equal to its baseline value is
 * NOT emitted — the kit path restores it — so the blob carries only edits. */
static int emit_param(const dr32_kit *kit, const char *key,
                      const dr32_json *baseline,
                      char *buf, int cap, int n, int *first) {
    char val[DR32_MAX_PATH + 8];
    int len = dr32_read_param(kit, key, val, (int)sizeof(val));
    if (len <= 0 || !val[0]) return n;

    if (baseline) {
        const char *base = dr32_json_str(baseline, key, NULL);
        if (base && strcmp(base, val) == 0) return n;   /* unedited — kit restores it */
    }

    char esc[DR32_MAX_PATH * 2 + 8];
    json_escape(esc, (int)sizeof(esc), val);
    n += snprintf(buf + n, cap - n, "%s\"%s\":\"%s\"", *first ? "" : ",", key, esc);
    *first = 0;
    return n;
}

/** Serialize the kit into `buf`. Returns bytes written (0 on failure).
 *  `kit_path` may be NULL/empty — a hand-built kit with no preset file still
 *  persists its per-pad state, it simply has no baseline to reload. */
int dr32_state_write(const dr32_kit *kit, const char *kit_path,
                     char *buf, int buf_len, const char *baseline_json) {
    if (!kit || !buf || buf_len < 64) return 0;

    /* Parse the baseline once; its `params` object is the compare table.
     * A baseline that fails to parse degrades to the full dump, never to an
     * error — the full dump is always a correct (if fat) answer. Only honour
     * a baseline written for the SAME kit path: comparing against another
     * kit's values would silently drop real edits. */
    dr32_json *base_root = NULL;
    const dr32_json *base_params = NULL;
    if (baseline_json && baseline_json[0]) {
        base_root = dr32_json_parse(baseline_json);
        if (base_root) {
            const char *base_kit = dr32_json_str(base_root, "kit", "");
            if (base_kit && kit_path && strcmp(base_kit, kit_path) == 0)
                base_params = dr32_json_get(base_root, "params");
        }
    }

    int n = 0, first = 1;
    char esc[DR32_MAX_PATH * 2 + 8];
    json_escape(esc, (int)sizeof(esc), kit_path ? kit_path : "");
    n += snprintf(buf + n, buf_len - n, "{\"v\":1,\"kit\":\"%s\",\"params\":{", esc);

    for (int i = 0; GLOBAL_FIELDS[i]; i++)
        n = emit_param(kit, GLOBAL_FIELDS[i], base_params, buf, buf_len, n, &first);

    for (int pad = 0; pad < DR32_PADS; pad++) {
        /* Skip pads with nothing loaded. A silent pad carries no sound the user
         * could have shaped, and skipping them keeps a typical 16-pad kit well
         * under half the blob a full 32 would produce. */
        if (!kit->pads[pad].sample && !kit->pads[pad].path[0]) continue;
        for (int f = 0; PAD_FIELDS[f]; f++) {
            char key[64];
            snprintf(key, sizeof(key), "pad%d_%s", pad, PAD_FIELDS[f]);
            n = emit_param(kit, key, base_params, buf, buf_len, n, &first);
            if (n >= buf_len - 8) { dr32_json_free(base_root); return 0; }   /* truncated — better none than half */
        }
    }

    dr32_json_free(base_root);
    n += snprintf(buf + n, buf_len - n, "}}");
    return (n < buf_len) ? n : 0;
}

/** Restore from a blob written by dr32_state_write.
 *  `load_kit` is invoked for the stored path before params are applied, so the
 *  caller keeps ownership of preset loading (and of its own kit_path bookkeeping).
 *  Returns 1 if the blob parsed. */
int dr32_state_read(dr32_kit *kit, const char *json,
                    void (*load_kit)(void *ctx, const char *path), void *ctx) {
    if (!kit || !json || !json[0]) return 0;

    dr32_json *root = dr32_json_parse(json);
    if (!root) return 0;

    /* Kit FIRST: it replaces every pad wholesale, so applying params before it
     * would throw them away. */
    const char *path = dr32_json_str(root, "kit", "");
    if (path && path[0] && load_kit) load_kit(ctx, path);

    const dr32_json *params = dr32_json_get(root, "params");
    if (params) {
        for (const dr32_json *m = params->first; m; m = m->next) {
            if (!m->key || m->type != DR32_JSON_STRING || !m->str) continue;
            /* Unknown keys are ignored, not an error: a blob written by a newer
             * build must still load on an older one rather than failing whole. */
            dr32_apply_param(kit, m->key, m->str);
        }
    }

    dr32_json_free(root);
    return 1;
}
