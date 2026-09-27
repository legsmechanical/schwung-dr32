// dr32_kitjob.h — the kit browser's load, read on a worker thread.
//
// ⚠⚠ WHY: the browser auditions a kit once its cursor settles, and a kit load
// reads a preset and decodes up to 32 WAVs. On the SPI callback that measured
// 8-83 ms a load (device, 2026-09-27; one block stalled 72 ms) against a
// 2.9 ms block — a dropout across EVERY track, once per kit you stop on, and on
// dAVEBOx enough to make its render pool give up and render serially.
//
// So the audition POSTS the path here; the worker runs dr32_preset_prepare;
// the audio thread TAKES the finished plan between blocks and applies it
// (dr32_preset_apply: params, notes, buffer handover — no file read).
//
// Only the browser goes through here. A state restore or a set load still
// loads synchronously: nothing is playing then, and the host expects the kit
// in place when set_param returns.
//
// ⭑ Newest wins. Each post (and each cancel) moves a generation; a result for
// an older generation is never applied — the worker stops reading it early if
// it can, and discards it if it cannot. So a synchronous load that lands while
// the browser's load is still reading cannot be overwritten by it.
//
// Every call below is the AUDIO thread's, and none of them blocks: the worker
// holds the lock only to copy a request in, and a post that loses the race
// returns -1 to be retried next block.

#ifndef DR32_KITJOB_H
#define DR32_KITJOB_H

#include "dr32_preset.h"

typedef struct dr32_kitjob dr32_kitjob;

dr32_kitjob *dr32_kitjob_create(void);
/** Stops the worker (between WAVs) and joins it. */
void dr32_kitjob_destroy(dr32_kitjob *j);

/** Ask for `path` to be read, against what `kit` holds now (the decode memo).
 *  Supersedes anything still in flight. 0 = posted, -1 = try again next
 *  block, -2 = no worker thread could be made (load it synchronously). */
int  dr32_kitjob_post(dr32_kitjob *j, const char *path, const dr32_kit *kit);

/** Whatever is in flight will never be taken. */
void dr32_kitjob_cancel(dr32_kitjob *j);

/** 1 once the latest post has an answer: `*plan` is the finished plan, or
 *  NULL if that kit would not load. Its path is copied to `path`, and `ms` gets
 *  how long the read took on the worker. The caller applies the plan and frees
 *  it with dr32_kit_plan_free. 0 = nothing yet. */
int  dr32_kitjob_take(dr32_kitjob *j, dr32_kit_plan **plan, char *path, int path_len,
                      double *ms);

/** 1 while a post has not yet been taken or superseded. */
int  dr32_kitjob_busy(const dr32_kitjob *j);

#endif
