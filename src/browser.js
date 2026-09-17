/*
 * browser.js — DR32's sample browser, drawn by the module.
 *
 * ⭐ WHY THE MODULE DRAWS THIS. The browser was first built as an items page:
 * the DSP served the listing as a JSON parameter and the host drew it. Nothing
 * that went wrong was about browsing.
 *
 *   - the value channel is ~4 KB, so a folder of 1339 samples could not be
 *     listed at all and had to be truncated with a row saying so
 *   - `get_param` runs on the SPI callback with a ~900 us budget, so the
 *     directory scan sat in the audio thread
 *   - a click says only "row 3", and one click arrives as SEVERAL writes
 *     (measured on the device: four in ~32 ms) while the host is still drawing
 *     the previous listing — so clicks landed in the wrong listing and walked
 *     the user somewhere they had not asked for
 *
 * A canvas removes all of it. This file runs in the shadow UI's QuickJS, reads
 * the filesystem directly, owns its cursor, and speaks to the DSP exactly once
 * per load: `pad<N>_sample = <path>`. No listing crosses the wire, nothing
 * touches the audio thread, and a click is a click because we handle it.
 *
 * ⭐⭐ IT NEEDS `enterable: true`, AND THAT IS NOT A DETAIL. A canvas normally
 * gets the jog wheel and nothing else: the host spends the CLICK on closing it.
 * A browser is a tree, so the one gesture that means "enter" was the one that
 * meant "leave" — clicking `..` left the browser, clicking a folder left the
 * browser. `enterable` hands the click over and makes Back a contract this file
 * answers with `handleBack`. Without that flag, or on a host that predates it,
 * this screen can be scrolled and cannot be navigated.
 */

import * as os from 'os';

/* The two libraries, in display order. */
const ROOTS = [
    { label: 'Move Library', path: '/data/CoreLibrary/Samples' },
    { label: 'User Library', path: '/data/UserData/UserLibrary' },
];

const AUDIO = ['.wav', '.aif', '.aiff'];

/* Move's controls, as CC numbers rather than an import: this script is loaded
 * into the host's runtime BY PATH, and a bare relative import would resolve
 * against the wrong tree — the loader only rewrites /data/UserData/schwung/. */
const CC_JOG   = 14;
const CC_CLICK = 3;

/* Hardware pads arrive as notes 68..99 -- the PHYSICAL grid position, which is
 * not the kit's note map (36..67) and not the pad number either. We use it only
 * to know that a finger landed; see the note at the press handler. */
const PAD_NOTE_LO = 68;
const PAD_NOTE_HI = 99;

/* 128x64, 1-bit. Four rows fit under the title with the hint row beneath. */
const ROW_H = 10;
const TITLE_H = 12;
const VISIBLE = 4;

/*
 * The hint row we draw ourselves.
 *
 * ⭐ OURS, NOT THE HOST'S. `show_footer: false` turns the host's footer off,
 * because a canvas is a screen the MODULE owns and the host has no business
 * painting a vocabulary on it -- "up a level" means something here and nothing
 * on a scope or a meter. So the words are ours and they are about this browser.
 *
 * ⚠ NO measureText. The host's canvas ctx does not offer one (davebox's does,
 * which is exactly the sort of difference not to lean on), so text is laid out
 * from the device font's fixed 6px advance. Keep the hints short enough that a
 * pixel of drift cannot matter.
 */
const HINT_Y = 55;
const GLYPH_W = 6;
const HINT_PAD = 2;

function isAudio(name) {
    const dot = name.lastIndexOf('.');
    return dot >= 0 && AUDIO.indexOf(name.slice(dot).toLowerCase()) >= 0;
}

function baseName(p) {
    const s = p.lastIndexOf('/');
    return s < 0 ? p : p.slice(s + 1);
}

function dirName(p) {
    const s = p.lastIndexOf('/');
    return s <= 0 ? '' : p.slice(0, s);
}

/** Which library `p` is under, or null. The browser never walks outside them. */
function rootOf(p) {
    for (let i = 0; i < ROOTS.length; i++) {
        const r = ROOTS[i].path;
        if (p === r || p.indexOf(r + '/') === 0) return r;
    }
    return null;
}

function isRoot(p) {
    for (let i = 0; i < ROOTS.length; i++) if (ROOTS[i].path === p) return true;
    return false;
}

/*
 * One directory as rows: '..' first, then folders, then samples.
 *
 * ⚠ os.readdir answers [entries, errno] on some builds and a bare array on
 * others. The host's own FILEPATH_BROWSER_FS carries the same two-shape guard,
 * so this is the contract, not defensive coding.
 */
function listDir(dir) {
    const rows = [];
    if (!dir) {
        for (let i = 0; i < ROOTS.length; i++)
            rows.push({ label: ROOTS[i].label, path: ROOTS[i].path, dir: true });
        return rows;
    }
    rows.push({ label: '..', path: '', dir: true });

    let names = [];
    try {
        const r = os.readdir(dir);
        names = Array.isArray(r) && Array.isArray(r[0]) ? r[0] : (Array.isArray(r) ? r : []);
    } catch (e) {
        return rows;                       /* unreadable: '..' is still a way out */
    }

    const dirs = [], files = [];
    for (let i = 0; i < names.length; i++) {
        const n = names[i];
        if (!n || n.charAt(0) === '.') continue;
        const full = dir + '/' + n;
        let isDir = false;
        try {
            const st = os.stat(full);
            const m = Array.isArray(st) ? st[0] : st;      /* [obj, errno] or obj */
            isDir = !!m && (m.mode & 0o170000) === 0o040000;   /* S_IFDIR */
        } catch (e) { continue; }
        if (isDir) dirs.push({ label: n + '/', path: full, dir: true });
        else if (isAudio(n)) files.push({ label: n, path: full, dir: false });
    }
    const byName = (a, b) => {
        const x = a.label.toLowerCase(), y = b.label.toLowerCase();
        return x < y ? -1 : (x > y ? 1 : 0);
    };
    dirs.sort(byName);
    files.sort(byName);
    return rows.concat(dirs, files);
}

/*
 * The pad the browser is filling.
 *
 * ⚠ 1-BASED ON THE WIRE: split_pad_key does `idx -= 1` on the way in (verified
 * in dr32_params.c, not taken from a comment about the wire — reading the
 * comment instead of the conversion is exactly how an earlier cut filled the
 * pad BELOW the focused one).
 */
function focusedPad(ctx) {
    const n = parseInt(ctx.getParam('ui_current_pad'), 10);
    return isFinite(n) && n >= 1 ? n : 1;
}

/** Show `dir`, with the cursor on `cursorOn` if that row is there. */
function seat(st, dir, cursorOn) {
    st.dir = rootOf(dir || '') ? dir : '';
    st.rows = listDir(st.dir);
    st.cursor = 0;
    st.top = 0;
    if (cursorOn) {
        for (let i = 0; i < st.rows.length; i++) {
            if (st.rows[i].label === cursorOn) { st.cursor = i; break; }
        }
    }
    clampScroll(st);
}

function clampScroll(st) {
    if (st.cursor < st.top) st.top = st.cursor;
    if (st.cursor >= st.top + VISIBLE) st.top = st.cursor - VISIBLE + 1;
    if (st.top < 0) st.top = 0;
}

/*
 * Put the highlighted sample on the pad.
 *
 * ⚠ THIS NEVER SOUNDS BY ITSELF, deliberately. You hit the pad, so it plays at
 * the velocity you hit it with — which an auto-audition cannot reproduce, and
 * which is why the pads are left unblocked.
 * 🔴 There is no undo, as on the kit browser: scroll past a sample and the pad's
 * previous one is gone.
 */
function audition(ctx, st) {
    const row = st.rows[st.cursor];
    if (!row || row.dir) return;           /* a folder must not clear the pad */
    if (row.path === st.loaded) return;    /* already there; skip the write */
    ctx.setParam('pad' + st.pad + '_sample', row.path);
    st.loaded = row.path;
}

/*
 * Open the highlighted row: a folder, or take the sample and go.
 *
 * ⭑ CLICKING A SAMPLE LEAVES. The scroll already put it on the pad, so the
 * click is not a commit — it is "this one, I'm done", and that is the commonest
 * path through this screen. Making you press Back afterwards would be one
 * gesture too many every single time. Josh, from the device: "i'd like clicking
 * on a sample to exit the browser as well."
 *
 * ⚠ `ctx.close` only exists where the host offers it. Without it the click
 * still confirms and the browser stays up, which is what it did before.
 */
function enterRow(ctx, st) {
    const row = st.rows[st.cursor];
    if (!row) return;
    if (!row.dir) {
        audition(ctx, st);
        if (typeof ctx.close === 'function') ctx.close();
        return;
    }
    if (row.path) { seat(st, row.path, null); return; }
    goUp(st);                                      /* the '..' row */
}

/*
 * Up one level, or back to the two-library menu from a library root.
 *
 * The folder you came OUT of becomes the cursor, so stepping out lands you ON
 * it rather than at the top of a list — the whole of "minimize jumping in and
 * out of menus and folders".
 */
function goUp(st) {
    if (!st.dir) return false;                     /* already at the menu */
    const child = baseName(st.dir);
    if (isRoot(st.dir)) { seat(st, '', child); return true; }
    const parent = dirName(st.dir);
    seat(st, rootOf(parent) ? parent : '', child + '/');
    return true;
}

/*
 * One hint: the key in a filled pill, the action beside it.
 *
 * The pill is what makes a row of hints parseable -- without it "BACK UP JOG
 * PAGES" reads as one run of words. Returns the width drawn, so the caller can
 * pack them right to left.
 */
function hintWidth(key, action) {
    return key.length * GLYPH_W + HINT_PAD * 2 + 1 + action.length * GLYPH_W;
}

function drawHint(ctx, x, key, action) {
    const kw = key.length * GLYPH_W + HINT_PAD * 2;
    ctx.fillRect(x, HINT_Y - 1, kw, 9, 1);
    ctx.print(x + HINT_PAD, HINT_Y, key, 0);
    ctx.print(x + kw + 1, HINT_Y, action, 1);
    return hintWidth(key, action);
}

/*
 * What Back will do, and -- while Shift is held -- that there is a way out.
 *
 * BACK is pinned to the right edge, which is where it sits on every other
 * screen on this device. The escape hatch is shown only while Shift is DOWN: a
 * permanent hint for "when navigation goes wrong" would be clutter on a screen
 * that works.
 */
function drawHints(ctx, st) {
    const back = st.dir ? 'UP' : 'EXIT';
    const bw = hintWidth('BACK', back);
    drawHint(ctx, ctx.width - bw, 'BACK', back);
    /* ⚠ ASK THE HOST, do not watch CC 49. The host reads Shift from the shim's
     * shared memory; the CC does not reliably reach a canvas. Watching the byte
     * worked under dAVEBOx, which forwards it, and did nothing at all on stock
     * -- reported from the device as the footer never changing. */
    const shift = typeof ctx.shiftHeld === 'function' && ctx.shiftHeld();
    if (shift) drawHint(ctx, 2, 'JOG', 'PAGES');
}

globalThis.canvas_overlay = {
    /*
     * ⭐ ASK FOR THE PADS. Opening a canvas leaves the knob grid, and the grid
     * is what normally reconciles `pad_observe` — so without this a browser
     * cannot tell which pad you pressed, on the one screen where filling a pad
     * is the entire job. Measured on the device: knob touches reached this
     * script and pad presses did not.
     *
     * PASSIVE. The pad still plays the kit; the screen is merely told as well,
     * which is the whole point — you hit the pad to hear what you just put on
     * it, at the velocity you hit it with.
     *
     * Ignored by a host that does not know the flag, which is exactly the
     * behaviour we had: the pads play and the browser keeps filling the pad
     * that was focused when it opened.
     */
    wantsPads: true,

    onOpen(ctx) {
        const st = ctx.state;
        st.pad = focusedPad(ctx);
        st.loaded = '';

        /*
         * A pad that already HAS a sample opens on that sample's folder, with
         * the cursor on it. An empty pad has nowhere of its own, so it opens on
         * the two libraries.
         */
        const cur = ctx.getParam('pad' + st.pad + '_sample') || '';
        if (cur && rootOf(dirName(cur))) {
            st.loaded = cur;
            seat(st, dirName(cur), baseName(cur));
        } else {
            seat(st, '', null);
        }
    },

    /*
     * Back climbs the tree, and leaves only when there is nothing left to climb.
     *
     * ⭐ THE HOST'S CONTRACT: true means "I went up a level, keep me here";
     * anything else means "I am at my top". So this implements a way UP and
     * never a way OUT — the host already owns leaving, and one press past the
     * library picker is exactly when it should happen.
     */
    handleBack(ctx) {
        const st = ctx.state;
        return !!(st && goUp(st));
    },

    onMidi(ctx, msg) {
        const d = msg && msg.data;
        if (!d || d.length < 3) return;
        const st = ctx.state;
        if (!st.rows) return;
        const status = d[0] & 0xF0;

        /*
         * A PAD PRESS moves the browser to that pad — and BOTH halves matter:
         * it follows a pad that already HAS a sample, and stays exactly where it
         * is when the pad is empty, so you can walk one folder and fill empty
         * pad after empty pad without the listing moving under you.
         *
         * ⚠ WHETHER PAD NOTES REACH A CANVAS AT ALL IS A HOST QUESTION and the
         * answer differs by host. Nothing here depends on it: unforwarded, the
         * pads simply play and the browser keeps filling the pad that was
         * focused when it opened.
         */
        if (status === 0x90 && d[2] > 0 && d[1] >= PAD_NOTE_LO && d[1] <= PAD_NOTE_HI) {
            /*
             * ⚠⚠ THE NOTE IS A DOORBELL, NOT AN ADDRESS. 68..99 is the PHYSICAL
             * grid position, and it is NOT the pad number: the kit is laid out
             * in drum-rack order, so deriving `note - 68 + 1` names a different
             * pad from the one under your finger. That is what the first cut
             * did, and it showed the grid position as the pad.
             *
             * ⭐ WE DO NOT NEED THE MAP, AND MUST NOT OWN A COPY OF IT. The DSP
             * already moved its own focus: dr32_kit.c sets `ui_current_pad`
             * from the note it RECEIVED the instant a hand hits a pad with
             * nothing sequencing (`ui_auto_select_pad`, no vouch required). So
             * the note only tells us that a finger landed; the module tells us
             * where. The host says the same thing in as many words -- "the
             * pad-to-note map is Move's, not ours" (page_controller) -- and a
             * second copy here would be a copy that can disagree, and would go
             * wrong under a pad layout or a transpose besides.
             *
             * ⚠⚠⚠ AND WE MUST VOUCH, OR THIS STOPS WORKING AFTER A WHILE.
             *
             * `host_vouches` is a ONE-WAY LATCH (dr32_kit.h): the moment any
             * host vouches once, "a bare note-on never moves focus again,
             * whatever the transport says". Until then the DSP follows live
             * hits on its own, which is why a fresh session looks perfect --
             * and why it then quietly stops. Josh, from the device: "was
             * working great. and then after loading a bunch of samples, the
             * browser stopped following the pads." Every trip out to the knob
             * grid vouches, and after the first one our reads went stale.
             *
             * `ui_live_press` is the vouch, and dr32_params.c names US as its
             * writer in as many words: "the canvas is the obvious one". It
             * carries NO pad -- "a grid position is not a pad, so the note
             * decides, and this only vouches that a finger was involved". The
             * DSP looks back at the note it just played (within 20 blocks,
             * 58 ms) and moves focus, or arms forward if we beat the note here.
             *
             * ⓘ Order matters and is reliable: setParam and getParam are both
             * synchronous round trips, so the vouch has landed and focus has
             * moved by the time we ask. Both are legal here -- onMidi is not a
             * draw-path hook, so the accessors are present.
             */
            /*
             * ⭐ ASK BEFORE VOUCHING, because on some hosts focus has ALREADY
             * moved and a second claim is actively harmful.
             *
             * dAVEBOx emits the pad notes itself, so it knows exactly which
             * note it sent and NAMES it (`ui_live_note`) -- deterministic,
             * where a vouch is a race it measured itself losing 2 presses in
             * 16. By the time we see the press, focus is already correct. A
             * vouch on top of that finds the note consumed, ARMS FORWARD, and
             * is then claimed by the next note to arrive -- a sequenced one,
             * moving focus to a pad nobody touched.
             *
             * So: read first. If focus moved, follow it and say nothing.
             */
            let pad = focusedPad(ctx);
            if (pad === st.pad) {
                /* Focus did NOT move, so this host is leaving it to us. Vouch,
                 * and read back -- setParam and getParam are both synchronous
                 * round trips, so the vouch has landed and the DSP has matched
                 * it against the note it just played before we ask again. */
                ctx.setParam('ui_live_press', '1');
                pad = focusedPad(ctx);
            }
            if (pad === st.pad) return;
            st.pad = pad;
            const cur = ctx.getParam('pad' + pad + '_sample') || '';
            st.loaded = cur;
            if (cur && rootOf(dirName(cur))) seat(st, dirName(cur), baseName(cur));
            return;
        }

        if (status !== 0xB0) return;

        if (d[1] === CC_JOG) {
            const v = d[2];
            const delta = v === 0 ? 0 : (v <= 63 ? v : -(128 - v));
            if (!delta) return;
            st.cursor += delta;
            if (st.cursor < 0) st.cursor = 0;
            if (st.cursor >= st.rows.length) st.cursor = st.rows.length - 1;
            clampScroll(st);
            audition(ctx, st);
            return;
        }

        if (d[1] === CC_CLICK && d[2] > 0) enterRow(ctx, st);
    },

    draw(ctx) {
        const st = ctx.state;
        ctx.clear();
        if (!st.rows) return;

        /* Which pad is being filled, and where we are. */
        const where = st.dir ? baseName(st.dir) : 'Libraries';
        ctx.print(2, 2, ('Pad ' + st.pad + '  ' + where).slice(0, 21), 1);
        ctx.drawLine(0, TITLE_H - 2, ctx.width, TITLE_H - 2, 1);

        for (let i = 0; i < VISIBLE; i++) {
            const idx = st.top + i;
            if (idx >= st.rows.length) break;
            const y = TITLE_H + i * ROW_H;
            const on = idx === st.cursor;
            if (on) ctx.fillRect(0, y - 1, ctx.width, ROW_H, 1);
            ctx.print(3, y, st.rows[idx].label.slice(0, 20), on ? 0 : 1);
        }

        drawHints(ctx, st);
    },
};
