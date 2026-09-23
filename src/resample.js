/*
 * resample.js — the Resample page: a list you click into, and its dialogs,
 * all on ONE page, drawn by the module.
 *
 * Josh (2026-09-22): "<header> Resample Pad / Resample kit. clicking pad pulls
 * up a screen that displays the last tapped pad name and number on one line
 * and velocity level on the next (and updates as you hit pads) under that are
 * standard dialog boxes like we use in davebox sa - [Resample] [Cancel].
 * clicking kit pulls up a screen that says 'Resample all non-sample pads in
 * kit?' and under it [Resample] [Cancel]" — one page, last after Master,
 * clicked into like Category, with Category's corner brackets, and "no host
 * changes".
 *
 * ⭐ WHAT THE HOST GIVES THIS PAGE, AND THE ONE THING IT DOES NOT (measured in
 * the device's page_controller.mjs / shadow_ui.js, and reviewed by Fable):
 *   - a canvas `as_page` + `enterable` is a DOOR: un-entered, the jog pages
 *     past it and a click ENTERS; entered, jog and click come here as CC 14 /
 *     CC 3, Back asks handleBack (true = stay inside), ctx.close() leaves.
 *   - the host draws NO brackets on a canvas page, so this draws them — the
 *     host's own geometry (list_geometry.mjs / page_controller.mjs), not a
 *     look-alike.
 *   - 🔴 the ENTERING click is the host's and nothing tells us it happened
 *     (no hook, no draw field). So "entered" is INFERRED: any gesture that
 *     reaches onMidi proves it (un-entered gestures never arrive). Until the
 *     first one, the page is drawn un-entered — the highlight lands on the
 *     first jog or click after the click that entered. Every gesture still
 *     means exactly what it means on the host's own list.
 *   - leaving is seen: Back via handleBack; Shift+jog leaves silently, but the
 *     page is not DRAWN while you are elsewhere (the host redraws the visible
 *     page every tick), so a gap in draws means you came back, and it resets.
 *     ⚠ Known and accepted (Fable): the host can also stop drawing a page it
 *     keeps ENTERED — the section picker, a trip to Help or the Modules list
 *     and back. The page then shows un-entered (brackets) while the host is
 *     still inside, so the next click opens "Resample Pad" rather than
 *     entering. Harmless (Cancel), and making the first click after an
 *     inferred un-entered state inert would cost every normal visit a click.
 *   - the draw cannot read params. The DSP's `rs_status` arrives on the host's
 *     read rotation as this page's `extra_keys` — which the host only runs
 *     for a page WITH a knob, so the level carries Master's knob (knob 1
 *     turns Master here as on the Master page; the planner folds the
 *     duplicate grid away). That is how the pad lines follow your taps.
 *
 * The render is the DSP's (dsp/dr32_resample.c), on its own thread; this
 * only asks. Leaving the page does not stop it.
 */

const CC_JOG = 14;
const CC_CLICK = 3;

/*
 * THE HOST'S LIST, in this page's band. The band starts at screen row 9
 * (render_page_movy: the first knob row), so Category's rows at y 10/19/...
 * with labels at x 9 (MENU_LIST_X/Y), a highlight one row above its text and
 * 9 tall across the screen (menu_layout drawMenuList), and the bracket frame
 * at x 4, rows 9..53, arms 4 long (page_controller MENU_FRAME_*,
 * render_page_movy drawBrackets) are here at y 1 / x 9 / y 0 / 4,0,120x45.
 */
const ROW_H = 9, LIST_Y = 1, LIST_X = 9, HI_OFF = 1;
const FRAME_X = 4, FRAME_Y = 0, FRAME_W = 120, FRAME_H = 45, ARM = 4;
const ROWS = ['Resample Pad', 'Resample Kit'];

/* A page that has not been drawn for this long was not on screen. */
const AWAY_MS = 250;

/*
 * The host face's glyph widths, space (32) to '~' (126), from its font table
 * (scripts/generate_font.py, trimmed as load_font trims), 1px spacing
 * (load_font("font.png", 1)). A page ctx has no textWidth, and dAVEBOx's
 * buttons centre by MEASURING — so do these.
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
 * others OUTLINED; labels centred by measured width and glyph height. dAVEBOx
 * puts the row at y 46, h 13, x 6..122 on the screen: y 33 in this band.
 */
const BTN_Y = 33, BTN_H = 13, BTN_X0 = 6, BTN_X1 = 122, BTN_PAD = 6, BTN_GAP = 4;

function outline(ctx, x, y, w, h) {
    ctx.fillRect(x, y, w, 1, 1);
    ctx.fillRect(x, y + h - 1, w, 1, 1);
    ctx.fillRect(x, y, 1, h, 1);
    ctx.fillRect(x + w - 1, y, 1, h, 1);
}

function drawButton(ctx, x, y, w, h, sel, label, off) {
    const lx = x + Math.round((w - textW(label)) / 2);
    const ly = y + Math.max(1, Math.round((h - FONT_H) / 2));
    if (sel && !off) {
        ctx.fillRect(x, y, w, h, 1);
        ctx.print(lx, ly, label, 0);
        return;
    }
    outline(ctx, x, y, w, h);
    /* Selected but not possible yet (no pad tapped): a second, inner outline
     * says "you are here" without offering the click. */
    if (sel && off) outline(ctx, x + 2, y + 2, w - 4, h - 4);
    ctx.print(lx, ly, label, 1);
}

function drawButtonRow(ctx, buttons) {
    const n = buttons.length;
    if (!n) return;
    const widths = buttons.map((b) => textW(b.label) + BTN_PAD * 2);
    const span = BTN_X1 - BTN_X0 - BTN_GAP * (n - 1);
    const used = widths.reduce((a, b) => a + b, 0);
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

/* Body lines, centred, 10px apart, in the space above the buttons —
 * dAVEBOx's dlgLines (wrapped by MEASURED width), minus its capitals: this
 * face has lowercase. */
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

/* The host's frame corners (drawBrackets): an arm along each edge. */
function drawBrackets(ctx) {
    const x = FRAME_X, y = FRAME_Y, w = FRAME_W, h = FRAME_H;
    for (let i = 0; i < ARM; i++) {
        ctx.fillRect(x + i, y, 1, 1, 1);
        ctx.fillRect(x + w - 1 - i, y, 1, 1, 1);
        ctx.fillRect(x + i, y + h - 1, 1, 1, 1);
        ctx.fillRect(x + w - 1 - i, y + h - 1, 1, 1, 1);
    }
    for (let i = 0; i < ARM - 1; i++) {
        ctx.fillRect(x, y + i, 1, 1, 1);
        ctx.fillRect(x + w - 1, y + i, 1, 1, 1);
        ctx.fillRect(x, y + h - 1 - i, 1, 1, 1);
        ctx.fillRect(x + w - 1, y + h - 1 - i, 1, 1, 1);
    }
}

/* ------------------------------------------------------------ state */

const S = {
    entered: false,   /* inferred — see the header */
    view: 'list',     /* list | pad | kit | run */
    cursor: 0,        /* the list row */
    sel: 1,           /* dialog button: 0 Resample, 1 Cancel (dAVEBOx's destructive default) */
    jobs0: -1,        /* the DSP's job count when Resample was pressed */
    status: null,
    lastDraw: -1,
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

function toList() {
    S.view = 'list';
    S.sel = 1;
}

function reset() {
    S.entered = false;
    S.view = 'list';
    S.cursor = 0;
    S.sel = 1;
}

function canGo(st) {
    if (!st || st.busy) return false;
    if (S.view === 'pad') return st.pad > 0 && st.vel > 0 && !st.empty;
    if (S.view === 'kit') return st.synths > 0;
    return false;
}

/* A dialog: { lines, buttons }. */
function dialog(st) {
    if (!st) return { lines: ['...'], buttons: [] };
    const ok = [{ label: 'OK', sel: true }];
    if (S.view === 'run') {
        if (!(st.jobs > S.jobs0)) return { lines: ['Could not start', 'Try again'], buttons: ok };
        if (st.busy) return { lines: ['Resampling ' + st.done + '/' + st.total], buttons: ok };
        const l = ['Done'];
        if (st.failed) l.push(st.failed + ' failed');
        else if (st.saved) l.push(st.saved + ' file saved only');
        else if (st.clamped) l.push('level clamped');
        else if (st.last) l.push(st.last);
        return { lines: l, buttons: ok };
    }
    /* A job from an earlier visit still running: show it, start nothing. */
    if (st.busy) return { lines: ['Resampling ' + st.done + '/' + st.total], buttons: ok };
    const buttons = [{ label: 'Resample', sel: S.sel === 0, off: !canGo(st) },
                     { label: 'Cancel', sel: S.sel === 1 }];
    if (S.view === 'pad') {
        if (!(st.pad > 0)) return { lines: ['Tap a pad'], buttons };
        if (st.empty) return { lines: ['Pad ' + st.pad + ' is empty', 'Tap another pad'], buttons };
        return { lines: ['Pad ' + st.pad + '  ' + (st.name || ''), 'Velocity ' + st.vel], buttons };
    }
    if (!st.synths) return { lines: ['No non-sample pads in kit'], buttons };
    return { lines: ['Resample all non-sample pads in kit?'], buttons };
}

globalThis.canvas_overlay = {
    onMidi(ctx, msg) {
        const d = msg && msg.data;
        if (!d || d.length < 3 || (d[0] & 0xF0) !== 0xB0) return;
        /* Only an ENTERED door is handed gestures: this one proves it. */
        S.entered = true;
        if (d[1] === CC_JOG) {
            const v = d[2];
            const delta = v === 0 ? 0 : (v <= 63 ? v : -(128 - v));
            if (!delta) return;
            if (S.view === 'list') {
                /* From row 0, exactly as the host's list after its entering
                 * click — the highlight simply becomes visible now. */
                S.cursor = Math.max(0, Math.min(ROWS.length - 1, S.cursor + (delta > 0 ? 1 : -1)));
                return;
            }
            const sc = dialog(readStatus(ctx));
            if (sc.buttons.length >= 2) S.sel = delta > 0 ? 1 : 0;
            return;
        }
        if (d[1] !== CC_CLICK || d[2] === 0) return;
        if (S.view === 'list') {
            /* The row under the cursor, as the host's entered list's click. */
            S.view = S.cursor === 0 ? 'pad' : 'kit';
            S.sel = 1;
            readStatus(ctx);
            return;
        }
        const st = readStatus(ctx);
        const sc = dialog(st);
        if (sc.buttons.length < 2 || S.sel === 1) { toList(); return; }   /* OK, Cancel */
        if (!canGo(st)) return;                                          /* Resample, not yet */
        S.jobs0 = st.jobs;
        ctx.setParam('rs_go', S.view === 'pad' ? 'pad' : 'kit');
        readStatus(ctx);                   /* synchronous: it started, or it did not */
        S.view = 'run';
    },

    /* true = "I went up a level, keep me here"; anything else = the host
     * leaves the door (and the brackets come back). */
    handleBack(ctx) {
        if (S.view !== 'list') { toList(); return true; }
        S.entered = false;
        return false;
    },

    drawPage(ctx, info) {
        const now = info && typeof info.nowMs === 'number' ? info.nowMs : Date.now();
        if (S.lastDraw < 0 || now - S.lastDraw > AWAY_MS) reset();       /* arrived */
        S.lastDraw = now;
        let st = parseStatus(info && info.values ? info.values.rs_status : null) || S.status;
        /* Never go BACKWARDS: the host's cached value can predate a
         * synchronous read made by a click (it is re-read every few ticks),
         * and `jobs` only ever grows — an older answer would flash "Could not
         * start" right after a start that worked (Fable). */
        if (st && S.status && st.jobs < S.status.jobs) st = S.status;
        if (st) S.status = st;

        if (S.view !== 'list') {
            const sc = dialog(st);
            drawLines(ctx, sc.lines);
            drawButtonRow(ctx, sc.buttons);
            return;
        }
        for (let i = 0; i < ROWS.length; i++) {
            const y = LIST_Y + i * ROW_H;
            const on = S.entered && i === S.cursor;
            if (on) ctx.fillRect(0, y - HI_OFF, ctx.width, ROW_H, 1);
            ctx.print(LIST_X, y, ROWS[i], on ? 0 : 1);
        }
        if (!S.entered) drawBrackets(ctx);
    },
};
