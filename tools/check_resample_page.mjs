// check_resample_page.mjs — drive the Resample page (src/resample.js) the way
// the host does.
//
//   node tools/check_resample_page.mjs         (tests/run.sh runs it)
//
// The host (device page_controller.mjs / shadow_ui.js) hands an ENTERED
// canvas page the jog as CC 14 and the click as CC 3 (onMidi), asks
// handleBack on Back, and redraws it every tick with drawPage(fctx, info),
// info.values carrying its extra_keys and info.nowMs the clock. The entering
// click itself never reaches the page. This fakes exactly that, plus a DSP
// answering rs_status, and walks every path.
import path from 'node:path';
import { pathToFileURL } from 'node:url';

await import(pathToFileURL(path.resolve('src/resample.js')).href);
const ov = globalThis.canvas_overlay;

let fail = 0;
const bad = (m) => { fail++; console.error('  FAIL ' + m); };
const eq = (a, b, what) => { if (JSON.stringify(a) !== JSON.stringify(b)) bad(`${what}: got ${JSON.stringify(a)}, want ${JSON.stringify(b)}`); };

for (const h of ['onMidi', 'handleBack', 'drawPage'])
    if (typeof ov?.[h] !== 'function') bad(`canvas_overlay.${h} is missing`);

const base = () => ({ pad: 0, vel: 0, empty: 1, synths: 3, jobs: 0, busy: 0, total: 0, done: 0,
                      switched: 0, saved: 0, failed: 0, clamped: 0, name: '', last: '' });
const dsp = { st: base(), writes: [], starts: true };
const ctx = {
    width: 128, height: 64, state: {},
    getParam: (k) => (k === 'rs_status' ? JSON.stringify(dsp.st) : null),
    setParam: (k, v) => {
        dsp.writes.push([k, String(v)]);
        if (k === 'rs_go' && dsp.starts) { dsp.st.jobs++; dsp.st.busy = 1; dsp.st.total = 1; dsp.st.done = 0; }
    },
    close: () => true,
};
const jog = (d) => ov.onMidi(ctx, { data: [0xB0, 14, d >= 0 ? d : 128 + d] });
const click = () => ov.onMidi(ctx, { data: [0xB0, 3, 127] });

/* A draw a tick after the last (16 ms) — or `gap` ms later. */
let clock = 1000;
function draw(gap = 16) {
    clock += gap;
    const out = { text: [], fills: [] };
    const f = {
        width: 128, height: 48,
        print: (x, y, t, c) => out.text.push({ x, y, t: String(t), inv: c === 0 }),
        fillRect: (x, y, w, h, c) => out.fills.push({ x, y, w, h, c }),
        setPixel: () => {},
    };
    ov.drawPage(f, { values: { rs_status: JSON.stringify(dsp.st) }, nowMs: clock, width: 128, height: 48 });
    return out;
}
const texts = (g) => draw(g).text.map((l) => l.t);
const inverted = () => (draw().text.find((l) => l.inv) || {}).t;
/* The bracket frame: single pixels at the four corners of 4,0,120x45. */
const brackets = (d) => ['4,0', '123,0', '4,44', '123,44'].every((c) =>
    d.fills.some((f) => `${f.x},${f.y}` === c && f.w === 1 && f.h === 1));

// ---- arriving: the list, un-entered — brackets, no highlight, rows at the top
let d = draw(1000);
eq(d.text.map((l) => [l.t, l.x, l.y]), [['Resample Pad', 9, 1], ['Resample Kit', 9, 10]], 'the list, at the host list\'s x 9 / rows 10,19');
if (!brackets(d)) bad('un-entered: the host\'s corner brackets are missing');
eq(inverted(), undefined, 'un-entered: nothing highlighted');

// ---- the host's entering click is invisible; the first jog after it is not
jog(1);
d = draw();
eq(inverted(), 'Resample Kit', 'first jog after entering moves from row 0, as the host list does');
if (brackets(d)) bad('entered: the brackets must go');
jog(-1);
eq(inverted(), 'Resample Pad', 'jog up');
jog(-5);
eq(inverted(), 'Resample Pad', 'the cursor clamps');

// ---- Resample Pad, in place
click();
eq(texts(), ['Tap a pad', 'Resample', 'Cancel'], 'pad dialog before any tap');
eq(inverted(), 'Cancel', 'Cancel is the default, as in dAVEBOx');
jog(-1);
eq(inverted(), undefined, 'Resample cannot be taken before a tap');
click();
eq(dsp.writes, [], 'a click on an impossible Resample writes nothing');
Object.assign(dsp.st, { pad: 7, vel: 96, empty: 0, name: 'FM Kick' });
eq(texts().slice(0, 2), ['Pad 7  FM Kick', 'Velocity 96'], 'number and name, then velocity');
eq(inverted(), 'Resample', 'and Resample can be taken');
Object.assign(dsp.st, { pad: 3, vel: 110, name: 'Kick 707' });
eq(texts().slice(0, 2), ['Pad 3  Kick 707', 'Velocity 110'], 'it follows the next pad you hit');
dsp.st.empty = 1;
eq(texts()[0], 'Pad 3 is empty', 'an empty pad');
eq(inverted(), undefined, 'an empty pad cannot be resampled');
dsp.st.empty = 0;
click();
eq(dsp.writes, [['rs_go', 'pad']], 'Resample writes rs_go=pad');
/* The host's cached value is a few ticks old: it must not undo the start. */
{
    const stale = JSON.stringify(Object.assign({}, dsp.st, { jobs: dsp.st.jobs - 1, busy: 0 }));
    const out = [];
    ov.drawPage({ width: 128, height: 48, print: (x, y, t) => out.push(String(t)), fillRect: () => {}, setPixel: () => {} },
                { values: { rs_status: stale }, nowMs: (clock += 16), width: 128, height: 48 });
    eq(out, ['Resampling 0/1', 'OK'], 'a stale cached status does not flash "Could not start"');
}
eq(texts(), ['Resampling 0/1', 'OK'], 'running, in place');
Object.assign(dsp.st, { busy: 0, done: 1, switched: 1, last: 'Kick 707 v110 2026-09-22' });
eq(texts(), ['Done', 'Kick 707 v110', '2026-09-22', 'OK'], 'done, with the file');
click();
eq(texts(), ['Resample Pad', 'Resample Kit'], 'OK returns to the list');
eq(inverted(), 'Resample Pad', 'still entered, on the row you chose');

// ---- Cancel and Back inside a dialog return to the list and stay entered
click(); jog(1); click();
eq(texts(), ['Resample Pad', 'Resample Kit'], 'Cancel returns to the list');
click();
eq(ov.handleBack(ctx, {}), true, 'Back in a dialog stays on the page');
eq(inverted(), 'Resample Pad', 'Back returns to the entered list');
eq(ov.handleBack(ctx, {}), false, 'Back on the list leaves the door');
d = draw();
if (!brackets(d) || inverted() !== undefined) bad('after Back: brackets, no highlight');

// ---- Resample Kit
dsp.writes = [];
jog(1); click();
eq(texts(), ['Resample all', 'non-sample pads in', 'kit?', 'Resample', 'Cancel'], 'kit dialog, wrapped by measured width');
jog(-1); click();
eq(dsp.writes, [['rs_go', 'kit']], 'Resample writes rs_go=kit');
dsp.st.busy = 0; dsp.st.done = 3; dsp.st.total = 3; dsp.st.switched = 2; dsp.st.saved = 1;
eq(texts()[1], '1 file saved only', 'a pad changed mid-render is reported');
click();
dsp.st.synths = 0; dsp.writes = [];
click();
eq(texts().slice(0, 2), ['No non-sample pads in', 'kit'], 'a kit of samples only');
jog(-1); click();
eq(dsp.writes, [], 'nothing to resample, nothing written');
jog(1); click();

// ---- a start that does not happen is said, not shown as Done
dsp.st = base(); Object.assign(dsp.st, { pad: 2, vel: 80, empty: 0, name: 'Snare', jobs: 4 });
dsp.starts = false; dsp.writes = [];
jog(-1); click(); jog(-1); click();
eq(dsp.writes, [['rs_go', 'pad']], 'asked');
eq(texts().slice(0, 2), ['Could not start', 'Try again'], 'a refused start says so');
click();
dsp.starts = true;

// ---- a job still running from an earlier visit: shown, nothing started
dsp.st.busy = 1; dsp.st.total = 8; dsp.st.done = 5; dsp.writes = [];
click();
eq(texts(), ['Resampling 5/8', 'OK'], 'a running job is shown');
click();
eq(dsp.writes, [], 'nothing started over it');
dsp.st.busy = 0;

// ---- Shift+jog leaves without a word; coming back, the page starts fresh
click();                                            /* into a dialog */
eq(texts()[0], 'Pad 2  Snare', 'in the pad dialog');
d = draw(2000);                                     /* not drawn for 2 s: we were elsewhere */
eq(d.text.map((l) => l.t), ['Resample Pad', 'Resample Kit'], 'back after leaving: the list');
if (!brackets(d)) bad('back after leaving: un-entered, brackets');

// ---- layout
click(); jog(-1);
d = draw();
for (const l of d.text) if (l.y < 0 || l.y + 7 > 48 || l.x < 0) bad(`"${l.t}" drawn at ${l.x},${l.y}, outside the band`);
const btn = d.fills.filter((f) => f.y >= 33 && f.y + f.h <= 46);
const xs = btn.map((f) => f.x), xe = btn.map((f) => f.x + f.w);
/* dAVEBOx's row spreads by whole pixels, so its right edge lands within n-1 of x1. */
if (!btn.length || Math.min(...xs) !== 6 || Math.max(...xe) > 122 || Math.max(...xe) < 120) bad(`buttons span ${Math.min(...xs)}..${Math.max(...xe)}, want 6..~122`);
if (d.text.filter((l) => l.t === 'Resample' || l.t === 'Cancel').some((l) => l.y !== 36)) bad('button labels are not vertically centred (y 36)');

console.log(fail ? `check_resample_page: FAILED (${fail})` : 'check_resample_page: OK — list, enter, pad, kit, Cancel, Back, running, leaving');
process.exit(fail ? 1 : 0);
