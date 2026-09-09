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
// ⚠⚠ THE SCAN IS INCREMENTAL, AND THAT IS NOT AN OPTIMISATION — IT IS THE ONLY
// LEGAL SHAPE. `get_param` runs on the SPI callback with a ~900us budget
// (docs/REALTIME_SAFETY.md), and REALTIME_SAFETY names "get_param that rescans
// a directory" as a known way modules blow it. Scanning ~442 files in one call
// would drop frames. So `dr32_kits_pump` does a BOUNDED slice per call and the
// host's own polling drives it to completion over a few dozen ticks: the preset
// page reads count -> index -> name round-robin, one read per tick, so the list
// fills in while the user is still looking at it.
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

typedef struct dr32_kits dr32_kits;

dr32_kits *dr32_kits_create(void);
void       dr32_kits_destroy(dr32_kits *c);

/** Do a BOUNDED slice of the scan. `budget` is how many directory entries may
 *  be examined, which is what costs — a file's 1 KB probe is the expensive
 *  part. Returns 1 once the catalogue is complete, 0 while still working.
 *  Cheap and safe to call every time a browser param is read. */
int  dr32_kits_pump(dr32_kits *c, int budget);

/** 1 once the whole tree has been walked. */
int  dr32_kits_ready(const dr32_kits *c);

/** Throw the catalogue away so the next pump rebuilds it — after a kit is
 *  saved or deleted, where the on-disk set has genuinely changed. */
void dr32_kits_invalidate(dr32_kits *c);

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
