/* check_chain_params.mjs — DR32's served chain_params changes NOTHING but the
 * PAD cell's viz, proven with the host's own parser.
 *
 *   node tools/check_chain_params.mjs         (tests/run.sh runs it)
 *   SCHWUNG_SRC=/path/to/schwung ...
 *
 * src/chain_params.json (tools/gen_engine_ui.mjs writes it; the DSP serves it)
 * replaces the fallback the host would otherwise build from module.json, and
 * it has two readers (see the generator):
 *   1. the modulation refresh PARSES it and replaces the slot's metadata —
 *      so its parse must equal the host's parse of module.json, struct for
 *      struct, or per-pad send ranges (and more) change underneath;
 *   2. the page merges each entry over the inline declaration — so the fields
 *      it carries must be the fallback's, plus only what the inline
 *      declaration already says (default/step/max_param) and the one viz.
 * Compiles tools/chain_params_host.c against the checkout's chain_params.c.
 * Skips, loudly, without a Schwung checkout.
 */
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.SCHWUNG_SRC || path.resolve('..', 'schwung-current');
const dspDir = path.join(SRC, 'src', 'modules', 'chain', 'dsp');
if (!fs.existsSync(path.join(dspDir, 'chain_params.c'))) {
    console.log(`check_chain_params: no Schwung checkout at ${SRC} (set SCHWUNG_SRC) — SKIPPED`);
    process.exit(0);
}
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'dr32-cp-'));
const exe = path.join(tmp, 'cp');
try {
    /* chain_params.c (and chain_json.c, for bounded_strstr) reach into the rest of the chain host; only the two
     * parsers are called, so the other references are left unresolved. */
    /* macOS has no <malloc.h>, which the host's header includes (glibc). */
    fs.writeFileSync(path.join(tmp, 'malloc.h'), '#include <stdlib.h>\n');
    const undef = process.platform === 'darwin' ? ['-Wl,-undefined,dynamic_lookup'] : ['-Wl,--unresolved-symbols=ignore-all'];
    execFileSync('cc', ['-std=gnu11', '-O1', '-w', '-idirafter', tmp, '-I' + dspDir, '-I' + path.join(SRC, 'src'),
        '-I' + path.join(SRC, 'src', 'host'), 'tools/chain_params_host.c', path.join(dspDir, 'chain_params.c'), path.join(dspDir, 'chain_json.c'),
        ...undef, '-lm', '-o', exe], { stdio: ['ignore', 'ignore', 'inherit'] });
    const r = JSON.parse(execFileSync(exe, ['src', 'src/chain_params.json'], { encoding: 'utf8' }));
    const errors = [];
    if (r.module_json !== r.served)
        errors.push(`the host parses ${r.module_json} params from module.json but ${r.served} from the served list`);
    if (r.differ.length)
        errors.push(`the served list re-parses DIFFERENTLY for: ${r.differ.join(', ')} — the modulation refresh would replace that metadata`);
    const served = JSON.parse(fs.readFileSync('src/chain_params.json', 'utf8'));
    const fb = r.fallback;
    const EXTRA = new Set(['default', 'step', 'max_param', 'viz']);
    served.forEach((e, i) => {
        const bare = Object.fromEntries(Object.entries(e).filter(([k]) => !EXTRA.has(k)));
        if (JSON.stringify(bare) !== JSON.stringify(fb[i]))
            errors.push(`entry ${i} (${e.key}) is ${JSON.stringify(bare)}; the host's fallback is ${JSON.stringify(fb[i])}`);
    });
    const viz = served.filter((e) => e.viz).map((e) => `${e.key}=${e.viz.kind}`);
    if (viz.join() !== 'ui_current_pad=custom:padnum') errors.push(`viz entries are [${viz}], want only ui_current_pad=custom:padnum`);
    if (errors.length) {
        console.error('check_chain_params: FAILED');
        for (const e of errors) console.error('  - ' + e);
        process.exit(1);
    }
    console.log(`check_chain_params: OK — ${r.served} params, the host's parse unchanged, the page's fields the fallback's plus ${viz}`);
} finally {
    fs.rmSync(tmp, { recursive: true, force: true });
}
