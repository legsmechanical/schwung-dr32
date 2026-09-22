/* check_pad_cell.mjs — the PAD cell's big number (src/canvas.js) is the
 * host's OWN big number, pixel for pixel, for every pad.
 *
 *   node tools/check_pad_cell.mjs [out.png]     (tests/run.sh runs it)
 *   SCHWUNG_SRC=/path/to/schwung ...
 *
 * canvas.js imports the host's font by a path RELATIVE to where the module is
 * installed (<root>/modules/sound_generators/dr32/), so this lays out that tree
 * in a temp dir — the checkout's shared/param_pages beside a copy of
 * canvas.js — and loads it from there: a wrong relative path fails here, not
 * silently on the device. Then, for pads 1..32 and an unread value, the
 * widget's cell must equal the host's drawBigNumber (render_page_movy.mjs) in
 * the same cell, and nothing may fall outside the 32x15 box. Writes a sheet of
 * all 32 (build/pad_cell.png). Skips, loudly, without a Schwung checkout.
 */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const SRC = process.env.SCHWUNG_SRC || path.resolve('..', 'schwung-current');
const pp = path.join(SRC, 'src', 'shared', 'param_pages');
if (!fs.existsSync(path.join(pp, 'font_big_num.mjs'))) {
    console.log(`check_pad_cell: no Schwung checkout at ${SRC} (set SCHWUNG_SRC) — SKIPPED`);
    process.exit(0);
}
const url = (f) => pathToFileURL(f).href;
const { frameCtx } = await import(url(path.join(pp, 'frame_ctx.mjs')));
const { drawBigNumber, BOX_H } = await import(url(path.join(pp, 'render_page_movy.mjs')));
const { createFramebuffer, drawContext } = await import(url(path.join(SRC, 'tools', 'param-pages', 'harness.mjs')));

/* The device layout: <root>/shared/param_pages/ and <root>/modules/sound_generators/dr32/canvas.js */
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'dr32-cell-'));
const errors = [];
try {
    fs.cpSync(path.join(SRC, 'src', 'shared'), path.join(root, 'shared'), { recursive: true });
    const mod = path.join(root, 'modules', 'sound_generators', 'dr32');
    fs.mkdirSync(mod, { recursive: true });
    fs.copyFileSync('src/canvas.js', path.join(mod, 'canvas.js'));
    await import(url(path.join(mod, 'canvas.js')));
} catch (e) {
    errors.push(`src/canvas.js does not load from the device layout: ${e.message}`);
} finally {
    fs.rmSync(root, { recursive: true, force: true });
}
const ov = globalThis.canvas_overlay;
if (!errors.length && (!ov || ov.widgetKind !== 'custom:padnum' || typeof ov.drawCell !== 'function'))
    errors.push('src/canvas.js does not define canvas_overlay { widgetKind: "custom:padnum", drawCell }');

/* The declaration the host reads is the chain_params dsp.so SERVES. */
const served = JSON.parse(fs.readFileSync('src/chain_params.json', 'utf8'));
const decl = served.find((p) => p.key === 'ui_current_pad');
if (!decl || !decl.viz || decl.viz.kind !== 'custom:padnum')
    errors.push('src/chain_params.json: ui_current_pad does not declare viz.kind "custom:padnum" — the host would never load canvas.js');

const CW = 32, CH = BOX_H, COLS = 8, GAP = 2;
const W = COLS * (CW + GAP) + GAP, H = 4 * (CH + GAP) + GAP;
const sheet = createFramebuffer(W, H), sctx = drawContext(sheet);
const cell = (v) => ({ values: { ui_current_pad: v }, group: { keys: ['ui_current_pad'] } });
if (!errors.length) {
    for (const v of [...Array.from({ length: 32 }, (_, i) => i + 1), '']) {
        const mine = createFramebuffer(CW, CH);
        ov.drawCell(frameCtx(drawContext(mine), { x: 0, y: 0, w: CW, h: CH }), cell(v));
        const host = createFramebuffer(CW, CH);
        drawBigNumber(drawContext(host), Math.floor(CW / 2), 0, v === '' ? '--' : String(v));
        if (mine.clipped()) errors.push(`pad ${v}: ${mine.clipped()} pixels outside the 32x15 box`);
        if (mine.toPng(1).toString('base64') !== host.toPng(1).toString('base64'))
            errors.push(`${v === '' ? 'unread' : 'pad ' + v}: not the host's drawBigNumber, pixel for pixel`);
        if (v === '') continue;
        const i = v - 1;
        ov.drawCell(frameCtx(sctx, { x: GAP + (i % COLS) * (CW + GAP), y: GAP + Math.floor(i / COLS) * (CH + GAP), w: CW, h: CH }), cell(v));
    }
}
fs.mkdirSync('build', { recursive: true });
const out = process.argv[2] || 'build/pad_cell.png';
if (errors.length) {
    console.error('check_pad_cell: FAILED');
    for (const e of errors) console.error('  - ' + e);
    process.exit(1);
}
fs.writeFileSync(out, sheet.toPng(6));
console.log(`check_pad_cell: OK — 32 pads and "--", each the host's own big number pixel for pixel, none clipped (${out})`);
