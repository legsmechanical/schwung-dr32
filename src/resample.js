/*
 * resample.js — the Resample DIALOG, drawn by the module.
 *
 * Josh (2026-09-22): "<header> Resample Pad / Resample kit. clicking pad pulls
 * up a screen that displays the last tapped pad name and number on one line
 * and velocity level on the next (and updates as you hit pads) under that are
 * standard dialog boxes like we use in davebox sa - [Resample] [Cancel].
 * clicking kit pulls up a screen that says 'Resample all non-sample pads in
 * kit?' and under it [Resample] [Cancel]"
 *
 * ⭐ TWO PAGES, AS CATEGORY AND KIT ARE TWO. The list ("Resample Pad",
 * "Resample Kit") is the HOST's items page — level `resample` — so it looks
 * and behaves exactly like Category, brackets and all. Choosing a row writes
 * `rs_mode` and the host moves on to level `resample_dlg`, this page, a canvas
 * `as_page` + `enterable`: a DOOR, which a navigate_to arrives at already
 * entered, so the jog and the click are ours at once. Back, Cancel or the
 * last OK hands the door back.
 *
 * ⚠ THE DRAW CANNOT READ. A page's draw gets values, not accessors, so the
 * DSP's `rs_status` (a JSON object) arrives through the host's read rotation
 * as this page's `extra_keys` — which is also how the pad lines follow your
 * taps. The hooks CAN read, and do, so a click acts on what is true now.
 *
 * The render is the DSP's (dsp/dr32_resample.c), on its own thread; this only
 * asks. Leaving does not stop it.
 */

const CC_JOG = 14;
const CC_CLICK = 3;

/*
 * The host face's glyph widths, space (32) to '~' (126), from the host's own
 * font table (scripts/generate_font.py, trimmed the way load_font trims), with
 * the 1px spacing load_font("font.png", 1) adds. A page ctx has no
 * textWidth, and dAVEBOx's buttons centre by MEASURING — so do these.
 */
const GLYPH_W = '51355552335525255355555555224545555555555355555555555555555353553554555553443555555555555553135';
function textW(t) {
    const s = String(t);
    let w = 0;
    for (let i = 0; i < s.length; i++) {
        const c = s.charCodeAt(i);
        w += (c >= 32 && c <= 126 ? Number(GLYPH_W[c - 32]) : 5) + 1;
    }
    return w > 0 ? w - 1 : 0;
}
const FONT_H = 7;

/*
 * dAVEBOx SA's dialog buttons (dbxhost src/shared/menu_layout.mjs,
 * drawDialogButton / drawDialogButtonRow — "the NORMATIVE dialog convention"),
 * transcribed for a frame ctx: 6px label padding, 4px gaps, the row spread to
 * fill x0..x1, the selected button FILLED with the label knocked out, the
 * others OUTLINED; labels centred by measured width and by glyph height.
 * dAVEBOx places the row at y 46, h 13, x 6..122 on the full screen; this
 * page's band starts at screen row 9 and is 48 tall, so the same row is 33.
 */
const BTN_Y = 33, BTN_H = 13, BTN_X0 = 6, BTN_X1 = 122, BTN_PAD = 6, BTN_GAP = 4;

function drawButton(ctx, x, y, w, h, sel, label, off) {
    const lx = x + Math.round((w - textW(label)) / 2);
    const ly = y + Math.max(1, Math.round((h - FONT_H) / 2));
    if (sel && !off) {
        ctx.fillRect(x, y, w, h, 1);
        ctx.print(lx, ly, label, 0);
        return;
    }
    ctx.fillRect(x, y, w, 1, 1);
    ctx.fillRect(x, y + h - 1, w, 1, 1);
    ctx.fillRect(x, y, 1, h, 1);
    ctx.fillRect(x + w - 1, y, 1, h, 1);
    if (sel && off) {
        /* Selected but not possible yet (no pad tapped): a second, inner
         * outline says "you are here" without offering the click. */
        ctx.fillRect(x + 2, y + 2, w - 4, 1, 1);
        ctx.fillRect(x + 2, y + h - 3, w - 4, 1, 1);
        ctx.fillRect(x + 2, y + 2, 1, h - 4, 1);
        ctx.fillRect(x + w - 3, y + 2, 1, h - 4, 1);
    }
    ctx.print(lx, ly, label, 1);
}

function drawButtonRow(ctx, buttons) {
    const n = buttons.length;
    if (!n) return;
    const widths = buttons.map((b) => textW(b.label) + BTN_PAD * 2);
    const span = BTN_X1 - BTN_X0 - BTN_GAP * (n - 1);
    let used = widths.reduce((a, b) => a + b, 0);
    if (used < span) {
        const extra = Math.floor((span - used) / n);
        for (let i = 0; i < n; i++) widths[i] += extra;
    }
    let x = BTN_X0;
    for (let i = 0; i < n; i++) {
        drawButton(ctx, x, BTN_Y, widths[i], BTN_H, !!buttons[i].sel, buttons[i].label, !!buttons[i].off);
        x += widths[i] + BTN_GAP;
    }
}

/* Body lines, centred, one 10px pitch apart in the space above the buttons —
 * dAVEBOx's dlgLines, minus its capitals (this face has lowercase). Wrapped by
 * MEASURED width, never by a guessed break. */
function drawLines(ctx, lines) {
    const out = [];
    for (const raw of lines) {
        if (!raw) continue;
        let cur = '';
        for (const word of String(raw).split(' ')) {
            const next = cur ? cur + ' ' + word : word;
            if (cur && textW(next) > 124) { out.push(cur); cur = word; }
            else cur = next;
        }
        if (cur) out.push(cur);
    }
    const pitch = 10;
    const block = out.length * pitch - (pitch - FONT_H);
    const top = Math.max(0, Math.floor((BTN_Y - 2 - block) / 2));
    for (let i = 0; i < out.length; i++)
        ctx.print(Math.max(0, Math.floor((128 - textW(out[i])) / 2)), top + i * pitch, out[i], 1);
}

/* ------------------------------------------------------------ state */

const S = {
    sel: 1,          /* 0 Resample, 1 Cancel: dAVEBOx's destructive default */
    started: false,  /* Resample was clicked on this visit */
    mode: -1,        /* the mode this visit is for */
    status: null,
};

function parseStatus(raw) {
    if (raw && typeof raw === 'object') return raw;
    if (typeof raw !== 'string' || raw.charAt(0) !== '{') return null;
    try { return JSON.parse(raw); } catch (e) { return null; }
}

function readStatus(ctx) {
    const st = parseStatus(ctx.getParam('rs_status'));
    if (st) S.status = st;
    return S.status;
}

/* A new visit (the mode changed under us): start from the default button. */
function sync(st) {
    const mode = st ? st.mode : -1;
    if (mode !== S.mode) {
        S.mode = mode;
        S.sel = 1;
        S.started = false;
    }
}

function canGo(st) {
    if (!st || st.busy) return false;
    if (st.mode === 0) return st.vel > 0 && !st.empty;
    if (st.mode === 1) return st.synths > 0;
    return false;
}

/* What the dialog shows: { lines, buttons } */
function screen(st) {
    if (!st) return { lines: ['...'], buttons: [] };
    if (S.started || (st.busy && st.mode >= 0)) {
        if (st.busy) return { lines: ['Resampling ' + st.done + '/' + st.total], buttons: [{ label: 'OK', sel: true }] };
        const l = ['Done'];
        if (st.failed) l.push(st.failed + ' failed');
        else if (st.saved) l.push(st.saved + ' changed, file saved only');
        else if (st.clamped) l.push('level clamped');
        else if (st.last) l.push(st.last);
        return { lines: l, buttons: [{ label: 'OK', sel: true }] };
    }
    const go = canGo(st);
    const buttons = [{ label: 'Resample', sel: S.sel === 0, off: !go }, { label: 'Cancel', sel: S.sel === 1 }];
    if (st.mode === 0) {
        if (st.empty) return { lines: ['Pad ' + st.pad + ' is empty', 'Tap another pad'], buttons };
        if (!(st.vel > 0)) return { lines: ['Pad ' + st.pad + '  ' + (st.name || ''), 'Tap a pad'], buttons };
        return { lines: ['Pad ' + st.pad + '  ' + (st.name || ''), 'Velocity ' + st.vel], buttons };
    }
    if (st.mode === 1) {
        if (!st.synths) return { lines: ['No non-sample pads in kit'], buttons: [{ label: 'Resample', sel: S.sel === 0, off: true }, { label: 'Cancel', sel: S.sel === 1 }] };
        return { lines: ['Resample all non-sample pads in kit?'], buttons };
    }
    /* No dialog open — you paged here rather than chose a row. */
    if (st.busy) return { lines: ['Resampling ' + st.done + '/' + st.total], buttons: [] };
    return { lines: ['Choose Resample Pad', 'or Resample Kit'], buttons: [] };
}

/* Tell the DSP no dialog is open, and (from a click) hand the door back. On
 * Back the host is already leaving, so only the first half. */
function finish(ctx, close) {
    ctx.setParam('rs_mode', '-1');
    S.mode = -1;
    S.sel = 1;
    S.started = false;
    if (close && typeof ctx.close === 'function') ctx.close();
}

globalThis.canvas_overlay = {
    onMidi(ctx, msg) {
        const d = msg && msg.data;
        if (!d || d.length < 3 || (d[0] & 0xF0) !== 0xB0) return;
        const st = readStatus(ctx);
        sync(st);
        const sc = screen(st);
        if (d[1] === CC_JOG) {
            if (sc.buttons.length < 2) return;
            const v = d[2];
            const delta = v === 0 ? 0 : (v <= 63 ? v : -(128 - v));
            if (delta) S.sel = delta > 0 ? 1 : 0;
            return;
        }
        if (d[1] !== CC_CLICK || d[2] === 0) return;
        if (sc.buttons.length < 2 || S.sel === 1) { finish(ctx, true); return; }  /* OK, Cancel */
        if (!canGo(st)) return;                                  /* Resample, not yet */
        ctx.setParam('rs_go', '1');
        S.started = true;
    },

    /* Back is Cancel (or OK), and leaves: the dialog has no level to go up. */
    handleBack(ctx) {
        finish(ctx, false);
        return false;
    },

    drawPage(ctx, info) {
        const st = parseStatus(info && info.values ? info.values.rs_status : null) || S.status;
        if (st) S.status = st;
        sync(st);
        const sc = screen(st);
        drawLines(ctx, sc.lines);
        drawButtonRow(ctx, sc.buttons);
    },
};
