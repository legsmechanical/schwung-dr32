/* check_pad_cell.mjs — the PAD cell's big number (src/canvas.js), rendered
 * for every pad through the DEVICE'S OWN framebuffer and cell context.
 *
 *   node tools/check_pad_cell.mjs [out.png]     (tests/run.sh runs it)
 *   SCHWUNG_SRC=/path/to/schwung ...
 *
 * Fails if any pad draws nothing, draws outside the 32x15 box (the device
 * clips silently), or draws the same picture as another pad. Writes a sheet
 * of all 32 (build/pad_cell.png) — 1-bit art is reviewed from its render,
 * never from its source. Skips, loudly, without a Schwung checkout.
 */
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const SRC = process.env.SCHWUNG_SRC || path.resolve('..', 'schwung-current');
const pp = path.join(SRC, 'src', 'shared', 'param_pages');
if (!fs.existsSync(path.join(pp, 'frame_ctx.mjs'))) {
    console.log(`check_pad_cell: no Schwung checkout at ${SRC} (set SCHWUNG_SRC) — SKIPPED`);
    process.exit(0);
}
const { frameCtx } = await import(pathToFileURL(path.join(pp, 'frame_ctx.mjs')).href);
const { createFramebuffer, drawContext } =
    await import(pathToFileURL(path.join(SRC, 'tools', 'param-pages', 'harness.mjs')).href);

await import(pathToFileURL(path.resolve('src/canvas.js')).href);
const ov = globalThis.canvas_overlay;
const errors = [];
if (!ov || ov.widgetKind !== 'custom:padnum' || typeof ov.drawCell !== 'function')
    errors.push('src/canvas.js does not define canvas_overlay { widgetKind: "custom:padnum", drawCell }');

/* The declaration the host reads is the chain_params dsp.so SERVES. */
const served = JSON.parse(fs.readFileSync('src/chain_params.json', 'utf8'));
const decl = served.find((p) => p.key === 'ui_current_pad');
if (!decl || !decl.viz || decl.viz.kind !== 'custom:padnum')
    errors.push('src/chain_params.json: ui_current_pad does not declare viz.kind "custom:padnum" — the host would never load canvas.js');

const CW = 32, CH = 15, COLS = 8, GAP = 2;
const W = COLS * (CW + GAP) + GAP, H = 4 * (CH + GAP) + GAP;
const sheet = createFramebuffer(W, H), sctx = drawContext(sheet);
const seen = new Map();
if (!errors.length) {
    for (let pad = 1; pad <= 32; pad++) {
        /* One cell on its own framebuffer: what it lit, and what fell outside. */
        const fb = createFramebuffer(CW, CH);
        ov.drawCell(frameCtx(drawContext(fb), { x: 0, y: 0, w: CW, h: CH }),
                    { values: { ui_current_pad: pad }, group: { keys: ['ui_current_pad'] } });
        const png = fb.toPng(1).toString('base64');
        if (fb.clipped()) errors.push(`pad ${pad}: ${fb.clipped()} pixels outside the 32x15 box`);
        if (seen.has(png)) errors.push(`pad ${pad} draws the same picture as pad ${seen.get(png)}`);
        seen.set(png, pad);
        const i = pad - 1;
        ov.drawCell(frameCtx(sctx, { x: GAP + (i % COLS) * (CW + GAP), y: GAP + Math.floor(i / COLS) * (CH + GAP), w: CW, h: CH }),
                    { values: { ui_current_pad: pad }, group: { keys: ['ui_current_pad'] } });
    }
    const blank = createFramebuffer(CW, CH).toPng(1).toString('base64');
    if (seen.has(blank)) errors.push(`pad ${seen.get(blank)} draws nothing`);
}
fs.mkdirSync('build', { recursive: true });
const out = process.argv[2] || 'build/pad_cell.png';
if (!errors.length) fs.writeFileSync(out, sheet.toPng(6));
if (errors.length) {
    console.error('check_pad_cell: FAILED');
    for (const e of errors) console.error('  - ' + e);
    process.exit(1);
}
console.log(`check_pad_cell: OK — 32 pads, each distinct, none clipped (${out})`);
