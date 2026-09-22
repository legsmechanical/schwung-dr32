/*
 * DR32's custom knob widget: the PAD cell draws the pad's number, BIG.
 *
 * Josh, 2026-09-22: "i want the pad param to display as a big number". The
 * host's own cell draws 1..32 in its small value font; this draws the same
 * number in 5x7 digits at 2x (10x14 each), which fills the cell.
 *
 * HOW IT IS REACHED. `ui_current_pad`'s entry in the chain_params dsp.so
 * SERVES (src/chain_params.json) declares viz.kind "custom:padnum", which is
 * also what makes the host load this file at all (shadow_ui.js: "declares a
 * custom viz kind; loading canvas.js"). ⚠ Not module.json's chain_params: the
 * host's fallback for a plugin that serves none carries no viz. If
 * it fails to load, or a host has never heard of the kind, the kind does not
 * claim the cell and the host's plain number is drawn instead: the page stays
 * correct, and the failure is named in the device's debug.log.
 *
 * ⚠ NOT browser.js. That is the ENGN knob's fullscreen canvas (a canvas
 * param's canvas_script); this file is the per-cell overlay, and the host
 * loads the two separately. Both assign globalThis.canvas_overlay, each in
 * its own load.
 *
 * THE VALUE. `ui_current_pad`, the cell's own key, 1..32 — the only read, so
 * it lands with the cell's value and never trails it.
 *
 * THE BOX IS 32 x 15 (render_page_movy: { w: cellW, h: lblY - rowY }), and the
 * context CLIPS to it silently. Two digits: 10 + 2 + 10 = 22 wide, 14 tall.
 *
 * DRAWING BUDGET. Glyphs are compiled to horizontal RUNS once at load, so a
 * two-digit number is ~25 fillRects, not ~280 pixels.
 *
 * GPL-3.0-or-later.
 */

const GLYPHS = {
    "0": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": ["#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."],
};
const SCALE = 2;
const GLYPH_W = 5 * SCALE, GLYPH_H = 7 * SCALE, GAP = 2;

/* Each glyph as runs [x, y, w] in glyph pixels. */
const RUNS = {};
for (const [ch, rows] of Object.entries(GLYPHS)) {
    const runs = [];
    rows.forEach((row, y) => {
        let x = 0;
        while (x < row.length) {
            if (row[x] !== "#") { x++; continue; }
            let e = x;
            while (e < row.length && row[e] === "#") e++;
            runs.push([x, y, e - x]);
            x = e;
        }
    });
    RUNS[ch] = runs;
}

function drawNumber(ctx, s, ox, oy) {
    for (let i = 0; i < s.length; i++) {
        const runs = RUNS[s[i]];
        if (!runs) continue;
        const gx = ox + i * (GLYPH_W + GAP);
        for (const [x, y, w] of runs) ctx.fillRect(gx + x * SCALE, oy + y * SCALE, w * SCALE, SCALE, 1);
    }
}

globalThis.canvas_overlay = {
    widgetKind: "custom:padnum",

    /* Exported for tools/check_pad_cell.mjs. */
    _glyphs: GLYPHS,

    drawCell(ctx, { values, group }) {
        let n = Math.round(Number(values[group.keys[0]]));
        if (!Number.isFinite(n)) return;
        n = Math.min(32, Math.max(1, n));
        const s = String(n);
        const w = s.length * GLYPH_W + (s.length - 1) * GAP;
        drawNumber(ctx, s, (ctx.width - w) >> 1, Math.max(0, (ctx.height - GLYPH_H) >> 1));
    },
};
