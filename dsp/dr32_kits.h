// dr32_kits.h — the kit CATALOGUE behind the two-level kit browser.
//
// The browser is Kits (a category list) -> that category's kits (a preset
// list). Both pages are the host's own kinds; this file is what feeds them.
//
// ⚠⚠ WHY THIS EXISTS AT ALL, rather than a `filepath` browser: the host's file
// browser filters by EXTENSION, and `/data/UserData/UserLibrary/Track Presets`
// holds every instrument's presets, not just drum racks. Measured on the
// device: 365 files there, of which only 75 are drum racks. The old User Kits
// cell listed all 365 and 290 of them could not load. A catalogue can filter by
// CONTENT, which the browser structurally cannot.
//
// ⚠⚠ THE SCAN RUNS ON ITS OWN THREAD, AND THAT IS NOT AN OPTIMISATION — IT IS
// THE ONLY LEGAL SHAPE. `get_param` runs on the SPI callback with a ~900us
// budget (docs/REALTIME_SAFETY.md), which names "get_param that rescans a
// directory" as a known way modules blow it. This was first an INCREMENTAL scan
// pumped from those reads, 24 entries a call, on the premise that 24 entries
// fit the budget. On the device it did not: the first browser open logged
// `param-slow: get ... synth:kit_count took 10.597 ms on the SPI callback`
// (2026-09-27) and dropped audio. Cold storage makes one fopen cost
// milliseconds, so NO per-call slice is safe — a time-bounded one included.
// So the first read starts a worker that walks the whole tree and publishes the
// finished catalogue once; every read only looks at what is already published.
// Until then the list is Init alone, and `dr32_kits_ready` is 0.
//
// ⭑ Identifying a drum rack costs ONE 1 KB READ. Measured across all 75 user
// drum racks: the `drumZoneSettings` / `drumRack` marker sits between byte 452
// and byte 581, median 530 — always inside the first kilobyte. A preset without
// it in that window is not a drum rack. (`kind` does NOT work: a drum kit's
// kind is "instrumentRack", same as everything else.)

#ifndef DR32_KITS_H
#define DR32_KITS_H

#include "dr32_kit.h"   /* DR32_MAX_PATH */

#ifdef __cplusplus
extern "C" {
#endif

#define DR32_KITS_MAX      768   /* entries; the device has ~152 today */
#define DR32_KIT_CATS      8
#define DR32_KIT_NAME_LEN  64
#define DR32_KITS_WALK_MAX 12    /* directory depth; the user tree reaches 8 */
/* The Init kit (Josh, 2026-09-22: "an 'Init' category that has one preset --
 * 'Init' basically puts the module in the state it's in when you first load
 * it"). Not a file: this PATH is a marker the "kit" param recognises
 * (dr32.c, load_kit_any), so a saved set that holds it, a cancelled preview
 * that returns to it, and the browser all load it like any kit. */
#define DR32_KIT_INIT_PATH "dr32:init"
#define DR32_KIT_INIT_CAT  "Init"
#define DR32_KIT_INIT_NAME "Init"

typedef struct dr32_kits dr32_kits;

dr32_kits *dr32_kits_create(void);
void       dr32_kits_destroy(dr32_kits *c);

/** Start the scan if it has not started, adopt its catalogue if it has
 *  finished. Returns dr32_kits_ready(). Never touches disk: cheap and safe to
 *  call on every browser read (the audio thread). Every read below must be on
 *  that SAME thread — the adopt is not locked against them. */
int  dr32_kits_poll(dr32_kits *c);

/** 1 once the whole tree has been walked and adopted. Before that the
 *  catalogue is Init alone, and "not found" may mean "not scanned yet". */
int  dr32_kits_ready(const dr32_kits *c);

/** Categories that actually contain kits, in display order. `cat` is an index
 *  into THAT list, not into the fixed table — an empty category is never shown,
 *  so a device with no user kits does not offer an empty "My Kits". */
int         dr32_kits_cat_count(const dr32_kits *c);
const char *dr32_kits_cat_name(const dr32_kits *c, int cat);

/** Kits within a category. `idx` is 0-based within that category. */
int         dr32_kits_count(const dr32_kits *c, int cat);
const char *dr32_kits_name(const dr32_kits *c, int cat, int idx);
const char *dr32_kits_path(const dr32_kits *c, int cat, int idx);

/** Where `path` sits in the catalogue, so the browser can open on the kit that
 *  is actually loaded rather than at entry 0. Returns 0 on success. */
int  dr32_kits_locate(const dr32_kits *c, const char *path, int *cat, int *idx);

#ifdef __cplusplus
}
#endif
#endif
