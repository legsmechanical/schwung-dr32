/*
 * The Attack / Hold / Decay card prints the time from the knob's POSITION with
 * its own copy of the DSP's curve (a card cannot ask the DSP). This holds that
 * copy to the C one: tests/test_state.c writes dist/tests/env_knobs.json —
 * position -> seconds as dr32_params.c computes it — and every row must print
 * as the card would print the C value.
 *
 * Usage: node tools/check_env_knobs.mjs   (after tests/run.sh)
 */
import { readFileSync, existsSync } from 'node:fs';

const TABLE = 'dist/tests/env_knobs.json';
if (!existsSync(TABLE)) { console.log(`check_env_knobs: ${TABLE} missing — run tests/run.sh`); process.exit(1); }
await import(new URL('../src/env_card.js', import.meta.url));
const text = globalThis.dr32_env_card_text;
const table = JSON.parse(readFileSync(TABLE, 'utf8'));

let fail = 0, rows = 0;
for (const [key, pts] of Object.entries(table)) {
    for (const [pos, sec] of pts) {
        rows++;
        /* Compare the TIME, not the text: the C value is a float, so the two
         * may straddle a rounding edge. 0.1% or 1 us, whichever is looser. */
        const card = text(key, pos);
        const want = (key === 'hold_knob' && pos >= 1) ? 'Inf' : null;
        if (want) { if (card !== want) { console.log(`  FAIL ${key} ${pos}: card "${card}", want Inf`); fail++; } continue; }
        const m = /^([\d.]+) (ms|s)$/.exec(card);
        const t = m ? Number(m[1]) * (m[2] === 'ms' ? 1e-3 : 1) : NaN;
        const tol = Math.max(sec * 5e-3, 1e-5) + (m && m[2] === 'ms' ? 0.5e-5 : 0.005);
        if (!(Math.abs(t - sec) <= tol)) { console.log(`  FAIL ${key} ${pos}: card "${card}", DSP ${sec} s`); fail++; }
    }
}
if (text('hold_knob', null) !== '' || text('hold_knob', '') !== '') { console.log('  FAIL an unanswered read must draw no time'); fail++; }
console.log(fail ? `check_env_knobs: ${fail} FAIL` : `check_env_knobs: OK — ${rows} positions, the card prints the DSP's time`);
process.exit(fail ? 1 : 0);
