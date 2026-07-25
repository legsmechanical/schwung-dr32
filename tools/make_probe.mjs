// Generate the M0 write-back probe: take a real kit, extend it to 32 pads and
// edit params *through the library API*, then serialize. If native Move loads
// the result, native write-back is viable and M3 can save real .ablpreset files.
//
// Usage: node tools/make_probe.mjs <source.ablpreset> <out.ablpreset>

import { readFileSync, writeFileSync } from 'node:fs';
import { parseKit, serializeKit, HOLD_INFINITE } from '../lib/ablpreset.mjs';

const [src, out] = process.argv.slice(2);
const kit = parseKit(readFileSync(src, 'utf8'));

kit.name = 'DR32 WRITEBACK';
kit.pads.sort((a, b) => a.note - b.note);

// Bank B: 16 more pads at notes 52-67, cloned from bank A, an octave up.
const bankB = kit.pads.slice(0, 16).map((p, i) => ({
  ...p,
  index: 16 + i,
  note: 52 + i,
  color: (p.color + 6) % 26,
  params: { ...p.params, Voice_Transpose: 12 },
  mixer: { ...p.mixer },
  _chain: null,          // force a fresh cloned chain
  _cell: null,
  _origKeys: p._origKeys,
}));
kit.pads = kit.pads.concat(bankB);

// Edits that exercise the writer on every value shape we care about:
kit.pads[0].params.Voice_Envelope_Hold = HOLD_INFINITE;   // float sentinel
kit.pads[0].params.Voice_Transpose = -5;                  // int
kit.pads[1].chokeGroup = 1;                               // was null
kit.pads[2].chokeGroup = 1;
kit.pads[3].mixer.pan = -0.5;                             // fresh float
kit.pads[4].params.Effect_Type = 'Sub Osc';               // enum switch

writeFileSync(out, serializeKit(kit, { indent: 2 }) + '\n');
console.log(`${out}: ${kit.pads.length} pads, notes ${kit.pads[0].note}-${kit.pads.at(-1).note}`);
