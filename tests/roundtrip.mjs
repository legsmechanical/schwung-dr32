// Round-trip probe: parse -> serialize must be byte-identical for every drum kit
// in the device corpus. This is the software half of the M0 write-back probe —
// if we can't reproduce a file we didn't write, we have no business saving over
// the user's presets.
//
// Usage: node tests/roundtrip.mjs <corpus-dir>

import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { parseKit, serializeKit, isDrumKit, resolveUri, effectParamsFor } from '../lib/ablpreset.mjs';

const root = process.argv[2];
if (!root) { console.error('usage: node tests/roundtrip.mjs <corpus-dir>'); process.exit(2); }

function walk(dir, out = []) {
  for (const e of readdirSync(dir)) {
    const p = join(dir, e);
    const st = statSync(p);
    if (st.isDirectory()) walk(p, out);
    else if (e.endsWith('.ablpreset') || e.endsWith('.json')) out.push(p);
  }
  return out;
}

let kits = 0, exact = 0, differing = [], errors = [], pads = 0, missingSamples = 0, emptyPads = 0;
const indents = new Map();

for (const f of walk(root)) {
  let text;
  try { text = readFileSync(f, 'utf8'); } catch { continue; }
  if (!isDrumKit(text)) continue;
  kits++;
  try {
    const kit = parseKit(text);
    pads += kit.pads.length;
    for (const p of kit.pads) {
      if (!p.sampleUri) { emptyPads++; continue; }
      if (!p.samplePath) missingSamples++;
      // every pad must resolve to a known effect param pair
      if (effectParamsFor(p).length !== 2) {
        throw new Error(`unknown Effect_Type ${JSON.stringify(p.params.Effect_Type)}`);
      }
    }
    // Detect the file's own indentation so we compare like with like.
    const m = text.match(/\n(\s+)"/);
    const ind = m ? m[1].length : 1;
    indents.set(ind, (indents.get(ind) || 0) + 1);
    const out = serializeKit(kit, { indent: ind });
    if (out.trim() === text.trim()) exact++;
    else differing.push([f, firstDiff(text.trim(), out.trim())]);
  } catch (e) {
    errors.push([f, e.message]);
  }
}

function firstDiff(a, b) {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) {
    if (a[i] !== b[i]) {
      return `@${i}: orig …${a.slice(Math.max(0, i - 40), i + 40)}…\n         new  …${b.slice(Math.max(0, i - 40), i + 40)}…`;
    }
  }
  return `length ${a.length} vs ${b.length}`;
}

console.log(`kits parsed      : ${kits}`);
console.log(`pads             : ${pads} (${emptyPads} empty, ${missingSamples} unresolvable URI)`);
console.log(`byte-exact       : ${exact}/${kits}`);
console.log(`indent widths    : ${[...indents].map(([k, v]) => `${k}sp×${v}`).join(', ')}`);
if (errors.length) {
  console.log(`\nERRORS (${errors.length}):`);
  for (const [f, m] of errors.slice(0, 10)) console.log(`  ${f}\n    ${m}`);
}
if (differing.length) {
  console.log(`\nDIFFERING (${differing.length}):`);
  for (const [f, d] of differing.slice(0, 3)) console.log(`  ${f}\n    ${d}`);
}
process.exit(errors.length || differing.length ? 1 : 0);
