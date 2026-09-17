/*
 * The filter graphic, resolved for EACH filter mode.
 *
 * ⚠⚠ WHY THIS EXISTS BESIDE pages_check. `pages_check` evaluates `visible_if`
 * statically, with no module to read from, and the host FAILS OPEN on a
 * condition it cannot answer. So it sees RES and GAIN on the page at once —
 * nine knobs on the Shape bank, and a `flt` group whose roles are no longer
 * adjacent — and reports "viz-declared-not-adjacent: no graphic is drawn".
 * On the device `filter_type` is served and answers, so exactly one of the two
 * is ever on the page.
 *
 * That gap matters more than it sounds: a viz group that breaks the row rule
 * draws as PLAIN DIALS with no error anywhere. The one check that would catch
 * it is the one the static tool cannot make, and "the device is fine, trust me"
 * is the shape of every bug this repo has a warning about. So this drives
 * upstream's OWN resolver over the key list each mode actually produces.
 *
 * Usage: SCHWUNG_SRC=<host checkout> node tools/check_viz_modes.mjs
 */
import { readFileSync } from 'node:fs';

const SRC = process.env.SCHWUNG_SRC;
if (!SRC) {
    console.log('check_viz_modes: SCHWUNG_SRC is not set');
    process.exit(2);
}

const { buildMetaIndex } = await import(`${SRC}/src/shared/param_pages/param_meta.mjs`);
const { resolveViz }     = await import(`${SRC}/src/shared/param_pages/viz.mjs`);

const d  = JSON.parse(readFileSync(new URL('../src/module.json', import.meta.url), 'utf8'));
const H  = d.capabilities.ui_hierarchy;
const ix = buildMetaIndex({ hierarchy: H, chainParams: d.capabilities.chain_params || [] });

const shape = H.levels.pad_shape;
const declared = (shape.knobs || []).filter((k) => typeof k === 'string' && k);
const metaOf = (k) => (shape.params || []).find((p) => p && p.key === k) || {};

/* The key list the host builds for one value of the gating param: a knob whose
 * `visible_if` names that param and disagrees with the value is dropped, which
 * is what page_plan does before it chunks or resolves any graphic. */
function keysFor(gateParam, value) {
    return declared.filter((k) => {
        const c = metaOf(k).visible_if;
        if (!c || c.param !== gateParam) return true;
        if (c.equals !== undefined)     return String(c.equals) === value;
        if (c.not_equals !== undefined) return String(c.not_equals) !== value;
        return true;
    });
}

const TYPES = (metaOf('filter_type').options || []);
let fail = 0;
for (const t of TYPES) {
    const keys = keysFor('filter_type', t);
    if (keys.length > 8) {
        console.log(`  FAIL filter type "${t}": ${keys.length} knobs on screen, over the 8 that make one bank`);
        fail++;
    }
    /* resolveViz answers { groups, invalid } — the invalid list is where a
     * group that could not be drawn goes, so an empty `groups` alone would not
     * say WHY nothing draws. */
    const res = resolveViz({ keys, metaIndex: ix }) || {};
    const flt = (res.groups || []).find((g) => g.group === 'flt');
    const why = (res.invalid || []).find((v) => v.group === 'flt');
    if (!flt) {
        console.log(`  FAIL filter type "${t}": no filter graphic — it would draw as plain dials, silently` +
                    (why ? ` (${why.reason})` : ''));
        fail++;
        continue;
    }
    /* Two cells is the shape Josh asked for: the curve over CUT and the one
     * value beside it, with TYPE released to its own cell by `span: false`. */
    /* ⚠ Each failure CONTINUES rather than falling through to the ok line.
     * The first version printed both, so a mutation that widened the graphic
     * to three cells reported the failure AND "TYPE on its own cell" directly
     * beneath it — a line that was false exactly when it mattered. */
    if (flt.slotSpan !== 2) {
        console.log(`  FAIL filter type "${t}": graphic spans ${flt.slotSpan} cells (${flt.keys.join(' + ')}), want 2 — TYPE needs "span": false to keep its own cell`);
        fail++;
        continue;
    }
    if (flt.roles.mode !== 'filter_type') {
        console.log(`  FAIL filter type "${t}": TYPE is not feeding the graphic (mode=${flt.roles.mode})`);
        fail++;
        continue;
    }
    const second = flt.keys[1];
    console.log(`  ok   ${t}: ${flt.keys.join(' + ')} at slot ${flt.slotStart}, span ${flt.slotSpan}` +
                `, TYPE on its own cell${second === 'peak_db' ? ' (GAIN in the RES seat)' : ''}`);
}

console.log(fail ? `check_viz_modes: ${fail} FAILURE(S)` : 'check_viz_modes: OK');
process.exit(fail ? 1 : 0);
