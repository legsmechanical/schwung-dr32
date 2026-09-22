/*
 * resample.js — the Resample page, drawn by the module.
 *
 * Josh (2026-09-22): "resample shouldn't be a page with widgets. it should be
 * a menu page. one of those that you have to click jog to enable navigation"
 * — "like you click into the categories page or the module or my presets
 * pages", and "it should go at the very end after master".
 *
 * ⭐ WHAT THIS IS TO THE HOST. A canvas declared `as_page` + `enterable`
 * (module.json level `resample`, and in chain_params, which is where the
 * planner looks for pages like this). The host treats it as a DOOR — the same
 * machinery as Category: the jog pages past it, a click ENTERS, and from then
 * the jog and the click are handed here as CC 14 / CC 3 until Back (or
 * Shift+jog) leaves. The host draws the header and footer; this draws the
 * body, in the band it is given. Needs a host with enterable canvas pages
 * (schwung PR #520); on one without, the page shows and cannot be entered.
 *
 *   RESAMPLE      Resample pad | Resample kit
 *     pad         "Tap a pad" -> "Pad 7  FM Kick / Velocity 96"; Resample | Cancel
 *     kit         "8 synth pads"; Velocity 100 | Resample | Cancel
 *     running     "Resampling 3/8" -> "Done", with what happened; OK
 *
 * ⚠ THE DRAW CANNOT READ. A page's draw gets values, not accessors, so the
 * DSP's `rs_status` (a JSON object) arrives through the host's read rotation
 * as one of this page's `extra_keys`; the hooks (onMidi) CAN read, and do, so
 * a click acts on what is true now rather than what was last drawn.
 *
 * The render itself is the DSP's (dsp/dr32_resample.c), on its own thread;
 * this only asks. Leaving the page does not stop it.
 */

const CC_JOG = 14;
const CC_CLICK = 3;

const ROW_H = 9;          /* the host list's line height (list_geometry) */
const LABEL_X = 4;
const VEL_DEFAULT = 100;  /* Josh: "yep" (kit velocity 100) */

/* One state for the page, kept here rather than in the hooks' ctx.state: the
 * draw and the hooks are the same object's methods (the host binds drawPage to
 * the overlay), and the draw is not handed ctx.state. */
const S = {
    mode: 'menu',         /* menu | pad | kit | run */
    cur: 0,               /* the row under the cursor */
    vel: VEL_DEFAULT,     /* Resample kit's velocity */
    editVel: false,       /* the jog is turning Velocity */
    seq0: -1,             /* the DSP's hit counter when "Resample pad" opened */
    status: null,         /* the last rs_status we parsed */
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

/* A tap made since "Resample pad" opened, on a pad that holds something. */
function padReady(st) {
    return !!(st && S.seq0 >= 0 && st.seq > S.seq0 && st.vel > 0 && !st.empty && !st.busy);
}

/* The screen's rows: [{label, act, off}]. */
function rows(st) {
    switch (S.mode) {
        case 'pad': return [
            { label: 'Resample', act: 'go-pad', off: !padReady(st) },
            { label: 'Cancel', act: 'menu' },
        ];
        case 'kit': return [
            { label: 'Velocity ' + (S.editVel ? '<' + S.vel + '>' : S.vel), act: 'vel' },
            { label: 'Resample', act: 'go-kit', off: !(st && st.synths > 0 && !st.busy) },
            { label: 'Cancel', act: 'menu' },
        ];
        case 'run': return [{ label: st && st.busy ? 'Hide' : 'OK', act: 'close' }];
        default: return [
            { label: 'Resample pad', act: 'pad' },
            { label: 'Resample kit', act: 'kit' },
        ];
    }
}

/* The text above the rows. */
function lines(st) {
    if (S.mode === 'pad') {
        if (!st || S.seq0 < 0 || st.seq <= S.seq0) return ['Tap a pad', 'to resample it'];
        if (st.empty) return ['Pad ' + st.pad + ' is empty', 'tap another'];
        return ['Pad ' + st.pad + '  ' + (st.name || ''), 'Velocity ' + st.vel];
    }
    if (S.mode === 'kit') {
        if (!st) return ['...'];
        return [st.synths ? st.synths + ' synth pad' + (st.synths === 1 ? '' : 's') : 'No synth pads'];
    }
    if (S.mode === 'run') {
        if (!st) return ['...'];
        const out = [st.busy ? 'Resampling ' + st.done + '/' + st.total : 'Done'];
        if (st.failed) out.push(st.failed + ' failed');
        else if (st.saved) out.push(st.saved + ' changed: file only');
        else if (st.clamped) out.push(st.clamped + ' level clamped');
        else if (!st.busy && st.last) out.push(st.last);
        return out;
    }
    return [];
}

function toMenu() {
    S.mode = 'menu';
    S.cur = 0;
    S.editVel = false;
}

function open(ctx, mode) {
    const st = readStatus(ctx);
    S.editVel = false;
    /* A job still running: show it rather than start a second. */
    if (st && st.busy) { S.mode = 'run'; S.cur = 0; return; }
    S.mode = mode;
    S.cur = mode === 'pad' ? 0 : 1;           /* kit: land on Resample, Velocity above */
    if (mode === 'pad') S.seq0 = st ? st.seq : 0;
}

function act(ctx, row) {
    if (!row || row.off) return undefined;
    switch (row.act) {
        case 'pad': open(ctx, 'pad'); return undefined;
        case 'kit': open(ctx, 'kit'); return undefined;
        case 'menu': toMenu(); return undefined;
        case 'vel': S.editVel = !S.editVel; return undefined;
        case 'go-pad': {
            const st = readStatus(ctx);                   /* what is true NOW */
            if (!padReady(st)) return undefined;
            ctx.setParam('rs_pad', st.pad + ' ' + st.vel);
            S.mode = 'run'; S.cur = 0;
            return undefined;
        }
        case 'go-kit':
            ctx.setParam('rs_kit', String(S.vel));
            S.mode = 'run'; S.cur = 0;
            return undefined;
        case 'close':
            toMenu();
            if (typeof ctx.close === 'function') ctx.close();
            return undefined;
    }
    return undefined;
}

globalThis.canvas_overlay = {
    onMidi(ctx, msg) {
        const d = msg && msg.data;
        if (!d || d.length < 3 || (d[0] & 0xF0) !== 0xB0) return;
        const st = readStatus(ctx);
        const rs = rows(st);
        if (d[1] === CC_JOG) {
            const v = d[2];
            const delta = v === 0 ? 0 : (v <= 63 ? v : -(128 - v));
            if (!delta) return;
            if (S.editVel) {
                S.vel = Math.max(1, Math.min(127, S.vel + delta));
                return;
            }
            S.cur = Math.max(0, Math.min(rs.length - 1, S.cur + (delta > 0 ? 1 : -1)));
            return;
        }
        if (d[1] === CC_CLICK && d[2] > 0) return act(ctx, rs[S.cur]);
    },

    /* The host's contract: true = "I went up a level, keep me"; anything
     * else = "I am at my top" and the host leaves the page. */
    handleBack(ctx) {
        if (S.editVel) { S.editVel = false; return true; }
        if (S.mode !== 'menu') { toMenu(); return true; }
        S.cur = 0;
        return false;
    },

    drawPage(ctx, info) {
        const st = parseStatus(info && info.values ? info.values.rs_status : null) || S.status;
        if (st) S.status = st;
        const rs = rows(st);
        if (S.cur >= rs.length) S.cur = rs.length - 1;
        const h = ctx.height, w = ctx.width;
        const top = Math.max(0, h - rs.length * ROW_H);
        const ls = lines(st);
        for (let i = 0; i < ls.length && 1 + (i + 1) * ROW_H <= top + 1; i++)
            ctx.print(LABEL_X, 1 + i * ROW_H, ls[i], 1);
        for (let i = 0; i < rs.length; i++) {
            const y = top + i * ROW_H;
            const on = i === S.cur;
            if (on && !rs[i].off) {
                ctx.fillRect(0, y, w, ROW_H, 1);
            } else if (on) {
                /* The cursor on something that cannot be done yet: framed,
                 * not filled — you can see where you are, and that a click
                 * will do nothing. */
                ctx.fillRect(0, y, w, 1, 1);
                ctx.fillRect(0, y + ROW_H - 1, w, 1, 1);
                ctx.fillRect(0, y, 1, ROW_H, 1);
                ctx.fillRect(w - 1, y, 1, ROW_H, 1);
            }
            ctx.print(LABEL_X, y + 1, rs[i].label, on && !rs[i].off ? 0 : 1);
        }
    },
};
