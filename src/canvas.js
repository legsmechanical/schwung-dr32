/*
 * DR32's custom knob widget: the PAD cell draws the pad's number BIG — in the
 * HOST'S OWN big-number face, exactly as the host's built-in draws it.
 *
 * Josh, 2026-09-22: "i want the pad param to display as a big number", and
 * then, of a first version with DR32's own digits: "there is a non-custom big
 * number widget though". There is (render_page_movy.mjs WIDGET_BIGNUM), but a
 * module cannot ASK for it: the host picks it for a whole-number knob whose
 * range spans <= 24 (BIG_NUM_MAX_SPAN) or whose name says it is a count, and
 * PAD is 1..32. So this widget does what the built-in does, with the built-in's
 * own font and placement (drawBigNumber): tools/check_pad_cell.mjs requires
 * every pad to be pixel-identical to the host's drawBigNumber.
 *
 * ⭑ THE FONT IS IMPORTED, not copied: the widget script is evaluated as a
 * module named by its path (shadow_ui.c shadow_load_ui_module), so this
 * relative path resolves against the module's own directory,
 * <host root>/modules/sound_generators/dr32/, to that host's
 * shared/param_pages/. Under dAVEBOx the module is reached through the
 * dbx-host tree, so it resolves to dbx-host's copy. ⚠ If the import ever fails
 * to resolve, the WHOLE script fails to load, the kind goes unregistered and
 * the host draws its arc knob — logged in debug.log, never a hole.
 *
 * HOW IT IS REACHED. `ui_current_pad`'s entry in the chain_params dsp.so
 * SERVES (src/chain_params.json) declares viz.kind "custom:padnum", which is
 * also what makes the host load this file at all (shadow_ui.js: "declares a
 * custom viz kind; loading canvas.js"). ⚠ Not module.json's chain_params: the
 * host's fallback for a plugin that serves none carries no viz.
 *
 * ⚠ NOT browser.js. That is the ENGN knob's fullscreen canvas; this is the
 * per-cell overlay, and the host loads the two separately.
 *
 * THE VALUE. `ui_current_pad`, the cell's own key, 1..32 — the only read, so
 * it lands with the cell's value and never trails it. Unread: "--", as the
 * built-in does (bigNumberText).
 *
 * THE BOX IS 32 x 15 (render_page_movy: { w: cellW, h: lblY - rowY }).
 *
 * GPL-3.0-or-later.
 */
import { fontPrint, fontWidth, HEIGHT } from "../../../shared/param_pages/font_big_num.mjs";

globalThis.canvas_overlay = {
    widgetKind: "custom:padnum",

    drawCell(ctx, { values, group }) {
        const raw = values[group.keys[0]];
        const n = Math.round(Number(raw));
        const s = (raw === null || raw === undefined || raw === "" || !isFinite(n)) ? "--" : String(n);
        /* drawBigNumber: centred on the cell, (BOX_H - HEIGHT) / 2 down. */
        fontPrint(ctx, Math.floor(ctx.width / 2) - Math.floor(fontWidth(s) / 2),
                  Math.floor((ctx.height - HEIGHT) / 2), s, 1);
    },
};
