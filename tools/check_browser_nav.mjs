/*
 * The sample browser's NAVIGATION, driven over a fake tree.
 *
 * ⚠⚠ WHY THIS EXISTS. Every navigation bug this browser has had was reported
 * from the device, in a sentence, after a build-and-install round trip:
 * "'..' won't go back", "you can't get back out to the user library door",
 * "'..' doesn't go up a level, it just goes back to the sampler page". Not one
 * of them needed hardware to find. The browser is a pure function of a
 * directory listing and a stream of CCs, and this drives exactly that.
 *
 * The last one was not even in this file — the host was stealing the jog click
 * — but the FIX is here (knob 1 navigates on every host), and a fix nothing
 * exercises is the shape the rest of that list is made of.
 *
 * Usage: node tools/check_browser_nav.mjs
 */
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

/* ⚠ vm.SourceTextModule needs --experimental-vm-modules, so the script hands
 * itself the flag rather than making every caller remember it. Evaluating
 * browser.js in a vm is what lets its `import * as os` be a stub: the alternative
 * is a loader hook installed process-wide, which every other tool here would
 * then inherit. */
if (typeof vm.SourceTextModule !== 'function') {
    const { spawnSync } = await import('node:child_process');
    const { fileURLToPath } = await import('node:url');
    const r = spawnSync(process.execPath,
        ['--experimental-vm-modules', '--no-warnings', fileURLToPath(import.meta.url),
         ...process.argv.slice(2)],
        { stdio: 'inherit' });
    process.exit(r.status === null ? 1 : r.status);
}

/* A fake library tree: dir -> entries. */
const TREE = {
    '/data/CoreLibrary/Samples': ['Drums', 'Loops', 'stray.wav'],
    '/data/CoreLibrary/Samples/Drums': ['Kicks', 'clap.wav'],
    '/data/CoreLibrary/Samples/Drums/Kicks': ['kick1.wav', 'kick2.wav', 'notes.txt'],
    '/data/CoreLibrary/Samples/Loops': ['loop.aif'],
    '/data/UserData/UserLibrary': ['Mine'],
    '/data/UserData/UserLibrary/Mine': ['own.wav'],
};
const isDir = (p) => Object.prototype.hasOwnProperty.call(TREE, p);

const osStub = {
    readdir: (d) => (isDir(d) ? [TREE[d].slice(), 0] : [[], 2]),
    stat: (p) => [{ mode: isDir(p) ? 0o040755 : 0o100644 }, 0],
};

/* The params the browser reads and writes. */
let params = {};
const ctx = {
    width: 128, height: 64,
    state: {},
    clear() {}, print() {}, fillRect() {},
    getParam: (k) => (k in params ? params[k] : ''),
    setParam: (k, v) => { params[k] = String(v); return true; },
};

const src = readFileSync(new URL('../src/browser.js', import.meta.url), 'utf8');
const g = { globalThis: null };
const sandbox = vm.createContext(g);
g.globalThis = g;
const mod = new vm.SourceTextModule(src, { context: sandbox });
await mod.link((spec) => {
    if (spec !== 'os') throw new Error('unexpected import: ' + spec);
    return new vm.SyntheticModule(['readdir', 'stat'], function () {
        this.setExport('readdir', osStub.readdir);
        this.setExport('stat', osStub.stat);
    }, { context: sandbox });
});
await mod.evaluate();
const ov = g.canvas_overlay;

/* ── the gestures, as CC bytes ─────────────────────────────────────────── */
const cc = (n, v) => ov.onMidi(ctx, { data: [0xB0, n, v] });
const jog = (d) => cc(14, d > 0 ? d : 128 + d);
const click    = () => cc(3, 127);
/* Entering and leaving, as the host delivers them once `enterable` is set:
 * the click arrives at onMidi, and Back arrives as the handleBack hook. */
const navRight = () => click();
const navLeft  = () => ov.handleBack(ctx);
const pad      = (n) => ov.onMidi(ctx, { data: [0x90, 68 + n, 100] });  /* n = grid offset */

const goUpVia = (f) => f();
const rows = () => ctx.state.rows.map((r) => r.label);
const here = () => ctx.state.dir;
const at   = () => rows()[ctx.state.cursor];

let fail = 0;
function check(what, got, want) {
    if (got === want) { console.log(`  ok   ${what}: ${got}`); return; }
    console.log(`  FAIL ${what}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
    fail++;
}
/*
 * Move the cursor onto a row by label.
 *
 * ⚠⚠ FAILS LOUDLY when the row is not there. It used to return false and let
 * the script walk on, so a step that started from the wrong folder produced
 * four confusing failures three sections later instead of one clear one here.
 * A navigation helper that silently does nothing is a test that lies about
 * where it is.
 */
function cursorTo(label) {
    const i = rows().indexOf(label);
    if (i < 0) {
        console.log(`  FAIL cursorTo(${JSON.stringify(label)}): not in ${JSON.stringify(here())} — ${rows().join(',')}`);
        fail++;
        return false;
    }
    jog(i - ctx.state.cursor);
    return true;
}

/* ── 1. an empty pad opens on the two libraries ────────────────────────── */
params = { ui_current_pad: '3' };
ov.onOpen(ctx);
check('empty pad opens at the menu', here(), '');
check('the menu is the two libraries', rows().join(','), 'Move Library,User Library');

/* ── 2. the click walks in ─────────────────────────────────────────────── */
navRight();
check('click enters Move Library', here(), '/data/CoreLibrary/Samples');
check('.. is first, folders before files', rows().join(','), '..,Drums/,Loops/,stray.wav');
cursorTo('Drums/'); navRight();
check('click enters Drums', here(), '/data/CoreLibrary/Samples/Drums');

/* ── 3. Back goes UP, and lands ON the folder it came out of ──────────── */
navLeft();
check('Back goes up one', here(), '/data/CoreLibrary/Samples');
check('the cursor is on the folder we left', at(), 'Drums/');

/* ── 4. …and from a library ROOT it returns to the picker, on the door
 *      it came out of. This is Josh's "you can't get back out to the user
 *      library door", and it must hold for the knob as well as the click. */
navLeft();
check('up from the root returns to the picker', here(), '');
check('the cursor is on the library we left', at(), 'Move Library');
cursorTo('User Library'); navRight();
check('the other door still opens', here(), '/data/UserData/UserLibrary');
navLeft();
check('and returns to the picker too', here(), '');

/* ── 5. the '..' ROW and Back mean the same thing ────────────────────── */
cursorTo('Move Library'); navRight();
cursorTo('Drums/'); navRight();
check('in Drums via the row', here(), '/data/CoreLibrary/Samples/Drums');
cursorTo('..'); click();
check('clicking .. goes up', here(), '/data/CoreLibrary/Samples');
cursorTo('Drums/'); navRight(); cursorTo('..'); click();
check('clicking .. goes up, same as Back', here(), '/data/CoreLibrary/Samples');

/* ── 6. scrolling onto a sample loads it; a folder never clears the pad ─ */
cursorTo('Drums/'); navRight(); cursorTo('Kicks/'); navRight();
check('non-audio is not listed', rows().join(','), '..,kick1.wav,kick2.wav');
params.pad3_sample = '';
cursorTo('kick1.wav');
check('the scroll loaded the pad', params.pad3_sample, '/data/CoreLibrary/Samples/Drums/Kicks/kick1.wav');
cursorTo('..');
check('landing on a folder leaves the pad alone', params.pad3_sample, '/data/CoreLibrary/Samples/Drums/Kicks/kick1.wav');

/* ── 7. a pad press with a sample moves the browser; an empty one does not
 *
 * ⚠⚠ THE NOTE IS A DOORBELL. 68..99 is the physical GRID POSITION, in a
 * different order from the kit's pads, so the browser must NOT derive a pad
 * from it -- it asks the module, which owns the note->pad map. The note numbers
 * below are deliberately UNRELATED to the pads: a browser that went back to
 * arithmetic fails here.
 *
 * ⚠⚠ AND IT MUST VOUCH -- BUT ONLY WHERE VOUCHING IS WANTED. Two host
 * behaviours, and the browser has to work on both:
 *
 *   stock    nothing moves focus on a bare note once `host_vouches` latches, so
 *            the browser must say "a finger did that" (`ui_live_press`) and the
 *            DSP matches it to the note it just played.
 *   dAVEBOx  it emits the pad notes itself, so it NAMES the note and focus has
 *            already moved by the time we see the press. A vouch there finds the
 *            note consumed, ARMS FORWARD, and is claimed by the next note to
 *            arrive -- a sequenced one, moving focus to a pad nobody touched.
 *
 * So: read first, vouch only if focus did not move. Both halves are pinned. */
let vouched = 0;
const dspFocus = { pad: '1' };
ctx.setParam = (k, v) => {
    params[k] = String(v);
    /* The DSP's own rule: a vouch is matched against the note just played. */
    if (k === 'ui_live_press') { vouched++; params.ui_current_pad = dspFocus.pad; }
    return true;
};

/* --- a stock-shaped host: focus does NOT move until we vouch --- */
params.pad7_sample = '/data/UserData/UserLibrary/Mine/own.wav';
params.ui_current_pad = String(ctx.state.pad);   /* focus is where we think it is */
dspFocus.pad = '7';                              /* the note the DSP just played */
pad(0);                                          /* note 68 -- not pad 68, not pad 1 */
check('a stock-shaped host gets a vouch', vouched, 1);
check('a loaded pad takes the browser with it', here(), '/data/UserData/UserLibrary/Mine');
check('…and onto its own sample', at(), 'own.wav');
check('…and the browser is filling the pad the MODULE named', ctx.state.pad, 7);

/* --- an empty pad: follow the focus, leave the listing alone --- */
params.ui_current_pad = String(ctx.state.pad);
dspFocus.pad = '9';
pad(3);
check('an empty pad does NOT move the listing', here(), '/data/UserData/UserLibrary/Mine');
check('…but is the pad being filled', ctx.state.pad, 9);

/* --- a dAVEBOx-shaped host: focus has ALREADY moved, so do not vouch ---
 * 🔴 THE ONE THAT MATTERS. A redundant vouch here arms a claim that the next
 * note takes, so a sequencer playing under the browser would drag focus to a
 * pad nobody touched. */
const before = vouched;
params.ui_current_pad = '7';           /* the host named the note; focus moved */
pad(5);
check('a host that already moved focus gets NO vouch', vouched - before, 0);
check('…and the browser follows it anyway', ctx.state.pad, 7);
check('…back onto that pad\'s folder', here(), '/data/UserData/UserLibrary/Mine');

/* ── 8. Back walks up the tree, and only leaves at the top ────────────── */
/* The exit contract on a host that knows `enterable`: true means "I went up",
 * anything else means "I am done, close me". A browser that answered true
 * forever would hold the button on its own screen; one that answered false too
 * early would drop the user out of a folder they were still in. */
/* Back to the picker first -- section 7 left us inside the user library. */
while (here()) goUpVia(navLeft);
cursorTo('Move Library'); navRight(); cursorTo('Drums/'); navRight(); cursorTo('Kicks/'); navRight();
check('three deep', here(), '/data/CoreLibrary/Samples/Drums/Kicks');
check('Back climbs one', ov.handleBack(ctx), true);
check('...to Drums', here(), '/data/CoreLibrary/Samples/Drums');
check('Back climbs again', ov.handleBack(ctx), true);
check('...to the library root', here(), '/data/CoreLibrary/Samples');
check('Back leaves the root', ov.handleBack(ctx), true);
check('...to the picker', here(), '');
check('Back at the picker declines', ov.handleBack(ctx), false);
check('...and stays put, for the host to close', here(), '');

/* ── 9. clicking a sample takes it AND leaves ─────────────────────────── */
/* The scroll already put it on the pad, so the click is "this one, I'm done".
 * Pinned because the close is easy to lose in a refactor and its absence is
 * invisible: the browser simply stays up, which is what it used to do. */
{
    let closed = 0;
    ctx.close = () => { closed++; };
    while (here()) navLeft();
    cursorTo('Move Library'); click(); cursorTo('Drums/'); click();
    cursorTo('Kicks/'); click(); cursorTo('kick2.wav');
    /* Whichever pad the browser is filling -- section 7 leaves it wherever the
     * host's focus last pointed, and hardcoding one here made this fail for a
     * reason that had nothing to do with clicking. */
    const padKey = 'pad' + ctx.state.pad + '_sample';
    check('the scroll loaded it', params[padKey], '/data/CoreLibrary/Samples/Drums/Kicks/kick2.wav');
    click();
    check('clicking a sample closes the browser', closed, 1);
    cursorTo('..'); click();
    check('clicking a FOLDER does not close it', closed, 1);
    check('...it navigates', here(), '/data/CoreLibrary/Samples/Drums');
    delete ctx.close;
    cursorTo('Kicks/'); click(); cursorTo('kick1.wav'); click();
    check('a host without ctx.close still confirms without throwing',
          params[padKey], '/data/CoreLibrary/Samples/Drums/Kicks/kick1.wav');
}

/* ── 10. the browser draws its OWN hint row ───────────────────────────── */
/*
 * ⭐ THE MODULE OWNS THE SCREEN. A first cut had the HOST draw these hints,
 * fed by a `canGoUp` hook -- and "up a level" / "exit" are a file browser's
 * vocabulary, meaningless to a scope or a meter. Josh: "baking ui elements into
 * the canvas feature was a mistake and misses the whole point of the canvas."
 * So `show_footer: false` turns the host's footer off and we draw our own.
 *
 * Pinned on what is DRAWN, because that is the only place the words exist now.
 */
{
    const drawn = [];
    const dctx = Object.assign({}, ctx, {
        clear() {}, fillRect() {}, drawLine() {},
        print: (x, y, t) => { if (y >= 50) drawn.push(String(t)); },
    });
    while (here()) navLeft();
    ov.draw(dctx);
    check('at the picker, the hint says EXIT', drawn.join(' ').includes('EXIT'), true);

    drawn.length = 0;
    cursorTo('Move Library'); click();
    ov.draw(dctx);
    check('inside a library, it says UP', drawn.join(' ').includes('UP'), true);
    check('...and not EXIT', drawn.join(' ').includes('EXIT'), false);

    /* The escape hatch is advertised only while Shift is DOWN -- a permanent
     * hint for "when navigation goes wrong" is clutter on a screen that works.
     * The gesture itself is the HOST's; we only watch the key. */
    let shift = false;
    dctx.shiftHeld = () => shift;
    drawn.length = 0;
    ov.draw(dctx);
    check('Shift up: no jog hint', drawn.join(' ').includes('PAGES'), false);
    shift = true;
    drawn.length = 0;
    ov.draw(dctx);
    check('Shift down: the escape hatch is advertised', drawn.join(' ').includes('PAGES'), true);
    shift = false;
    drawn.length = 0;
    ov.draw(dctx);
    check('Shift up again: it goes away', drawn.join(' ').includes('PAGES'), false);

    /* A host too old to offer it must not throw -- the hint simply never shows. */
    delete dctx.shiftHeld;
    drawn.length = 0;
    ov.draw(dctx);
    check('a host without ctx.shiftHeld still draws', drawn.join(' ').includes('BACK'), true);
}

console.log(fail ? `check_browser_nav: ${fail} FAILURE(S)` : 'check_browser_nav: OK');
process.exit(fail ? 1 : 0);
