/*
 * The Attack / Hold / Decay knobs' card: the TIME, while the knob is turned.
 *
 * Josh, 2026-09-28: "i want the knob movement to feel the same, i just want
 * more precision before about 12 oclock". The host steps every float knob by
 * the same fraction of its range per detent, so the knobs (`atk_knob`,
 * `hold_knob`, `dcy_knob`) are 0..1 POSITIONS and the DSP maps a position to
 * seconds on a curve (dsp/dr32_params.c, knob_to_sec). The host would print
 * the position; this card prints the time.
 *
 * ⚠ THE CURVE IS DUPLICATED from dsp/dr32_params.c — a card cannot ask the DSP
 * (no getParam on the card path). tools/check_env_knobs.mjs holds this copy to
 * the table tests/test_state.c writes from the C one; change both or neither.
 *
 * It draws the host's own card band (overlay_card.mjs drawCardBand, inlined:
 * a card script cannot rely on an import resolving): name left, value right,
 * inverted, 9 rows.
 *
 * GPL-3.0-or-later.
 */
const KNOBS = {
    atk_knob:  { lo: 0.0001, hi: 20, noon: 0.1 },
    hold_knob: { lo: 0.001,  hi: 60, noon: 1.0, inf: true },
    dcy_knob:  { lo: 0.001,  hi: 60, noon: 1.0 },
};

function knobToSec(k, pos) {
    if (!(pos > 0)) return k.lo;
    if (pos >= 1) return k.hi;
    const s = (k.hi - k.lo) / (k.noon - k.lo) - 1;
    const r = s * s;
    return k.lo + (k.hi - k.lo) * (Math.pow(r, pos) - 1) / (r - 1);
}

/* Three significant figures. The band is chosen on the ROUNDED value, so
 * 9.996 ms prints "10.0 ms", not "10.00 ms". */
function fmtTime(t) {
    const ms = t * 1000;
    if (Number(ms.toFixed(2)) < 10)  return ms.toFixed(2) + " ms";
    if (Number(ms.toFixed(1)) < 100) return ms.toFixed(1) + " ms";
    if (Math.round(ms) < 1000)       return Math.round(ms) + " ms";
    if (Number(t.toFixed(2)) < 10)   return t.toFixed(2) + " s";
    return t.toFixed(1) + " s";
}

/* The text for one knob's raw position, or "" when there is no answer. */
function envCardText(key, raw) {
    const k = KNOBS[key];
    const pos = Number(raw);
    if (!k || raw === null || raw === undefined || raw === "" || !isFinite(pos)) return "";
    if (k.inf && pos >= 1) return "Inf";
    return fmtTime(knobToSec(k, pos));
}

function band(ctx, name, text) {
    const w = ctx.width;
    ctx.fillRect(0, 0, w, 9, 1);
    const vw = text ? ctx.textWidth(text) : 0;
    let nm = String(name || "");
    while (nm.length > 1 && ctx.textWidth(nm) > w - 6 - vw) nm = nm.slice(0, -1);
    ctx.print(2, 1, nm, 0);
    if (text) ctx.print(w - 2 - vw, 1, text, 0);
}

/* One drawer per knob: the card is handed its own value only, not its key. */
globalThis.dr32_atk_card  = (ctx, o) => band(ctx, o.name, envCardText("atk_knob",  o.raw));
globalThis.dr32_hold_card = (ctx, o) => band(ctx, o.name, envCardText("hold_knob", o.raw));
globalThis.dr32_dcy_card  = (ctx, o) => band(ctx, o.name, envCardText("dcy_knob",  o.raw));

/* For tools/check_env_knobs.mjs, which evaluates this file in node. */
globalThis.dr32_env_card_text = envCardText;
