// check_resample_page.mjs — drive src/resample.js the way the host does.
//
//   node tools/check_resample_page.mjs         (tests/run.sh runs it)
//
// The host hands an ENTERED canvas page the jog as CC 14 and the click as CC 3
// (onMidi), asks handleBack on Back, and draws it with drawPage(fctx, info)
// where info.values carries its extra_keys (page_controller.mjs, shadow_ui.js
// canvasPageHook / drawCanvasPageBody on the device). This fakes exactly that
// and a DSP that answers rs_status, and walks every path: what gets written,
// what cannot be clicked yet, and where Back goes.
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const file = path.resolve('src/resample.js');
await import(pathToFileURL(file).href);
const ov = globalThis.canvas_overlay;

let fail = 0;
const bad = (m) => { fail++; console.error('  FAIL ' + m); };
const eq = (a, b, what) => { if (JSON.stringify(a) !== JSON.stringify(b)) bad(`${what}: got ${JSON.stringify(a)}, want ${JSON.stringify(b)}`); };

for (const h of ['onMidi', 'handleBack', 'drawPage'])
    if (typeof ov?.[h] !== 'function') bad(`canvas_overlay.${h} is missing`);

/* A DSP: rs_status as DR32 serves it, and a log of what was written. */
const dsp = {
    st: { pad: 3, vel: 0, seq: 10, empty: 0, synths: 4, busy: 0, total: 0, done: 0, switched: 0, saved: 0, failed: 0, clamped: 0, name: 'FM Kick', last: '' },
    writes: [],
};
let closed = false;
const ctx = {
    width: 128, height: 64, state: {},
    getParam: (k) => (k === 'rs_status' ? JSON.stringify(dsp.st) : null),
    setParam: (k, v) => dsp.writes.push([k, String(v)]),
    close: () => { closed = true; return true; },
};
/* One CC per detent burst, as the host sends it: at most +-63 per message. */
const jog = (d) => {
    while (d !== 0) {
        const s = Math.max(-63, Math.min(63, d));
        ov.onMidi(ctx, { data: [0xB0, 14, s >= 0 ? s : 128 + s] });
        d -= s;
    }
};
const click = () => ov.onMidi(ctx, { data: [0xB0, 3, 127] });
const back = () => ov.handleBack(ctx, {});

/* A frame context that records what was printed, clipped like the host's. */
function draw(h = 46) {
    const out = { text: [], fills: 0 };
    const f = {
        width: 128, height: h,
        print: (x, y, t, c) => { if (y >= 0 && y < h) out.text.push({ y, t: String(t), inv: c === 0 }); },
        fillRect: () => { out.fills++; },
        setPixel: () => {},
    };
    ov.drawPage(f, { values: { rs_status: JSON.stringify(dsp.st) }, width: 128, height: h });
    return out;
}
const shown = (h) => draw(h).text.map((l) => l.t);
const cursor = () => (draw().text.find((l) => l.inv) || {}).t;

// ---- the menu
eq(shown(), ['Resample pad', 'Resample kit'], 'the menu');
eq(cursor(), 'Resample pad', 'the cursor starts on');
jog(1); eq(cursor(), 'Resample kit', 'jog down');
jog(5); eq(cursor(), 'Resample kit', 'jog past the end clamps');
jog(-1);
eq(back(), false, 'Back from the menu leaves the page');

// ---- Resample pad: nothing to take until a pad is tapped after opening
click();
eq(shown().slice(0, 2), ['Tap a pad', 'to resample it'], 'pad screen before a tap');
eq(cursor(), undefined, 'Resample is not clickable before a tap (framed, not filled)');
click();
eq(dsp.writes, [], 'clicking a disabled Resample writes nothing');
dsp.st.vel = 96; dsp.st.seq = 11;                     /* a tap on pad 3 */
eq(shown().slice(0, 2), ['Pad 3  FM Kick', 'Velocity 96'], 'pad screen after a tap');
eq(cursor(), 'Resample', 'Resample is live after a tap');
dsp.st.empty = 1;
eq(shown()[0], 'Pad 3 is empty', 'an empty pad');
eq(cursor(), undefined, 'an empty pad cannot be resampled');
dsp.st.empty = 0;
click();
eq(dsp.writes, [['rs_pad', '3 96']], 'Resample pad writes the pad and the velocity shown');
dsp.st.busy = 1; dsp.st.total = 1;
eq(shown()[0], 'Resampling 0/1', 'running');
eq(cursor(), 'Hide', 'while running, the one row hides the screen');
dsp.st.busy = 0; dsp.st.done = 1; dsp.st.switched = 1; dsp.st.last = 'FM Kick v96 2026-09-22';
eq(shown(), ['Done', 'FM Kick v96 2026-09-22', 'OK'], 'done');
click();
eq(closed, true, 'OK leaves the page');
eq(shown(), ['Resample pad', 'Resample kit'], 'and it comes back to the menu');

// ---- a tap from BEFORE the screen opened does not count
closed = false; dsp.writes = [];
click();                                              /* Resample pad again; seq is 11 */
eq(shown().slice(0, 2), ['Tap a pad', 'to resample it'], 'an old tap is not a new one');
eq(cursor(), undefined, 'and Resample stays disabled on an old tap');
eq(back(), true, 'Back from the pad screen stays on the page');
eq(shown(), ['Resample pad', 'Resample kit'], 'Back goes to the menu');

// ---- Resample kit: velocity 100, editable, then the synth pads
jog(1); click();
eq(shown(), ['4 synth pads', 'Velocity 100', 'Resample', 'Cancel'], 'kit screen');
eq(cursor(), 'Resample', 'the kit screen lands on Resample');
jog(-1); click();
eq(cursor(), 'Velocity <100>', 'click on Velocity edits it');
jog(-3); eq(cursor(), 'Velocity <97>', 'jog turns it');
jog(-120); eq(cursor(), 'Velocity <1>', 'and clamps at 1');
jog(126); eq(cursor(), 'Velocity <127>', 'and at 127');
jog(-27);
eq(back(), true, 'Back ends the edit, on the page');
eq(cursor(), 'Velocity 100', 'Back leaves the value');
jog(1); click();
eq(dsp.writes, [['rs_kit', '100']], 'Resample kit writes the velocity');
dsp.st.busy = 1; dsp.st.total = 4; dsp.st.done = 1;
eq(shown()[0], 'Resampling 1/4', 'kit progress');
dsp.st.busy = 0; dsp.st.done = 4; dsp.st.switched = 3; dsp.st.saved = 1;
eq(shown()[1], '1 changed: file only', 'a pad changed mid-render is reported');
dsp.st.saved = 0; dsp.st.failed = 2;
eq(shown()[1], '2 failed', 'failures are reported');
dsp.st.failed = 0;
back();

// ---- no synth pads: Resample kit cannot start
dsp.st.synths = 0; dsp.writes = [];
jog(1); click();
eq(shown()[0], 'No synth pads', 'kit screen with none');
eq(cursor(), undefined, 'Resample kit is disabled with no synth pads');
click(); eq(dsp.writes, [], 'and writes nothing');
back();

// ---- a job still running: opening either screen shows it, never starts another
dsp.st.synths = 4; dsp.st.busy = 1; dsp.writes = [];
jog(-1); click();
eq(shown()[0], 'Resampling 4/4', 'a running job is shown instead of the pad screen');
click();
eq(dsp.writes, [], 'nothing is started over a running job');

// ---- the draw fits and never throws on a short band or no status
dsp.st = null;
try { draw(20); draw(8); draw(0); } catch (e) { bad('drawPage threw: ' + e); }
const tall = draw(46).text;
if (tall.some((l) => l.y + 7 > 46)) bad('a line is drawn past the band');

console.log(fail ? `check_resample_page: FAILED (${fail})` : 'check_resample_page: OK — menu, pad, kit, running, Back');
process.exit(fail ? 1 : 0);
