// check_resample_page.mjs — drive the Resample DIALOG (src/resample.js) the
// way the host does.
//
//   node tools/check_resample_page.mjs         (tests/run.sh runs it)
//
// The host hands an ENTERED canvas page the jog as CC 14 and the click as CC 3
// (onMidi), asks handleBack on Back, and draws it with drawPage(fctx, info),
// info.values carrying its extra_keys (page_controller.mjs, shadow_ui.js
// canvasPageHook / drawCanvasPageBody on the device). The list before it is
// the host's own items page, which writes rs_mode; here the DSP is faked with
// that mode already set, and every path is walked.
import path from 'node:path';
import { pathToFileURL } from 'node:url';

await import(pathToFileURL(path.resolve('src/resample.js')).href);
const ov = globalThis.canvas_overlay;

let fail = 0;
const bad = (m) => { fail++; console.error('  FAIL ' + m); };
const eq = (a, b, what) => { if (JSON.stringify(a) !== JSON.stringify(b)) bad(`${what}: got ${JSON.stringify(a)}, want ${JSON.stringify(b)}`); };

for (const h of ['onMidi', 'handleBack', 'drawPage'])
    if (typeof ov?.[h] !== 'function') bad(`canvas_overlay.${h} is missing`);

const base = () => ({ mode: 0, pad: 7, vel: 0, empty: 0, synths: 3, busy: 0, total: 0, done: 0,
                      switched: 0, saved: 0, failed: 0, clamped: 0, name: 'FM Kick', last: '' });
const dsp = { st: base(), writes: [] };
let closes = 0;
const ctx = {
    width: 128, height: 64, state: {},
    getParam: (k) => (k === 'rs_status' ? JSON.stringify(dsp.st) : null),
    setParam: (k, v) => {
        dsp.writes.push([k, String(v)]);
        if (k === 'rs_mode') dsp.st.mode = parseInt(v, 10);
    },
    close: () => { closes++; return true; },
};
const jog = (d) => ov.onMidi(ctx, { data: [0xB0, 14, d >= 0 ? d : 128 + d] });
const click = () => ov.onMidi(ctx, { data: [0xB0, 3, 127] });

/* A frame context the size of the host's band (screen rows 9..56). */
function draw() {
    const out = { text: [], fills: [] };
    const f = {
        width: 128, height: 48,
        print: (x, y, t, c) => out.text.push({ x, y, t: String(t), inv: c === 0 }),
        fillRect: (x, y, w, h, c) => out.fills.push({ x, y, w, h, c }),
        setPixel: () => {},
    };
    ov.drawPage(f, { values: { rs_status: JSON.stringify(dsp.st) }, width: 128, height: 48 });
    return out;
}
const texts = () => draw().text.map((l) => l.t);
/* The filled (selected, possible) button's label: printed knocked out. */
const selected = () => (draw().text.find((l) => l.inv) || {}).t;

// ---- Resample Pad: the pad, then its velocity once tapped
eq(texts(), ['Pad 7  FM Kick', 'Tap a pad', 'Resample', 'Cancel'], 'pad dialog before a tap');
eq(selected(), 'Cancel', 'Cancel is the default, as in dAVEBOx');
jog(-1);
eq(selected(), undefined, 'Resample cannot be taken before a tap (outlined, not filled)');
click();
eq(dsp.writes, [], 'a click on an impossible Resample writes nothing');
dsp.st.vel = 96;
eq(texts().slice(0, 2), ['Pad 7  FM Kick', 'Velocity 96'], 'after a tap: name and number, then velocity');
eq(selected(), 'Resample', 'and Resample can be taken');
dsp.st.pad = 3; dsp.st.name = 'Kick 707'; dsp.st.vel = 110;
eq(texts().slice(0, 2), ['Pad 3  Kick 707', 'Velocity 110'], 'it follows the next pad you hit');
dsp.st.empty = 1;
eq(texts()[0], 'Pad 3 is empty', 'an empty pad');
eq(selected(), undefined, 'an empty pad cannot be resampled');
dsp.st.empty = 0;
click();
eq(dsp.writes, [['rs_go', '1']], 'Resample writes rs_go');
dsp.st.busy = 1; dsp.st.total = 1;
eq(texts(), ['Resampling 0/1', 'OK'], 'running');
dsp.st.busy = 0; dsp.st.done = 1; dsp.st.switched = 1; dsp.st.last = 'Kick 707 v110 2026-09-22';
eq(texts(), ['Done', 'Kick 707 v110', '2026-09-22', 'OK'], 'done, with the file (wrapped by measured width)');
click();
eq(dsp.writes.slice(1), [['rs_mode', '-1']], 'OK closes the dialog in the DSP');
eq(closes, 1, 'and hands the door back');
eq(texts(), ['Choose Resample Pad', 'or Resample Kit'], 'paged to with no dialog open');

// ---- Cancel, and Back
dsp.st = base(); dsp.st.vel = 50; dsp.writes = []; closes = 0;
eq(selected(), 'Cancel', 'a new visit starts on Cancel');
jog(-1); jog(1);
click();
eq(dsp.writes, [['rs_mode', '-1']], 'Cancel writes only rs_mode');
eq(closes, 1, 'Cancel hands the door back');
dsp.st = base(); dsp.writes = []; closes = 0;
eq(ov.handleBack(ctx, {}), false, 'Back leaves (the host is already leaving)');
eq(dsp.writes, [['rs_mode', '-1']], 'Back closes the dialog in the DSP');
eq(closes, 0, 'Back does not also ask to close');

// ---- Resample Kit
dsp.st = base(); dsp.st.mode = 1; dsp.writes = [];
eq(texts(), ['Resample all', 'non-sample pads in', 'kit?', 'Resample', 'Cancel'], 'kit dialog (wrapped by measured width)');
jog(-1); click();
eq(dsp.writes, [['rs_go', '1']], 'Resample Kit writes rs_go');
ov.handleBack(ctx, {});                               /* that dialog is over */
dsp.st = base(); dsp.st.mode = 1; dsp.st.synths = 0; dsp.writes = [];
eq(texts().slice(0, 2), ['No non-sample pads in', 'kit'], 'a kit of samples only');
jog(-1); click();
eq(dsp.writes, [], 'nothing to resample, nothing written');

// ---- layout: inside the band, the button row where dAVEBOx puts it
dsp.st = base(); dsp.st.vel = 90;
const d = draw();
for (const l of d.text) if (l.y < 0 || l.y + 7 > 48 || l.x < 0) bad(`"${l.t}" drawn at ${l.x},${l.y}, outside the band`);
const btn = d.fills.filter((f) => f.h === 13 || f.y === 33);
const xs = btn.map((f) => f.x), xe = btn.map((f) => f.x + f.w);
/* dAVEBOx's row spreads by whole pixels, so its right edge lands within n-1 of x1. */
if (!btn.length || Math.min(...xs) !== 6 || Math.max(...xe) > 122 || Math.max(...xe) < 120) bad(`buttons span ${Math.min(...xs)}..${Math.max(...xe)}, want 6..~122`);
if (btn.some((f) => f.y < 33 || f.y + f.h > 46)) bad('a button leaves its 13px row at y 33');
const labels = d.text.filter((l) => l.t === 'Resample' || l.t === 'Cancel');
if (labels.some((l) => l.y !== 36)) bad('button labels are not vertically centred (y 36)');

console.log(fail ? `check_resample_page: FAILED (${fail})` : 'check_resample_page: OK — pad, kit, Cancel, Back, running, done');
process.exit(fail ? 1 : 0);
