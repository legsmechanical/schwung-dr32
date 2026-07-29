// verb_score.mjs — pull a Reverb device's raw parameters out of a .abl and emit
// them as a flat key/value list the C renderer can execute.
//
// The JS-parses / C-renders split is the same one score.mjs uses, for the same
// reason: the .abl is JSON and Node already reads it, while the thing under
// test is C.
//
// Usage:  node tools/verb_score.mjs <song.abl> <out.verb> [--index=0]
//
// Output is one `Key Value` pair per line, plus a `# ` comment header. Booleans
// become 1/0 and enums become their ordinal, because the C side takes floats —
// RoomType in particular MUST survive as an ordinal, since anything other than
// SuperEco means the render is not comparable at all.

import { readFileSync, writeFileSync } from 'node:fs';

const [songPath, outPath, ...flags] = process.argv.slice(2);
if (!songPath || !outPath) {
    console.error('usage: node tools/verb_score.mjs <song.abl> <out.verb> [--index=N]');
    process.exit(2);
}
const opt = Object.fromEntries(flags.map((f) => {
    const i = f.indexOf('=');
    return i < 0 ? [f.replace(/^--/, ''), '1']
                 : [f.slice(2, i), f.slice(i + 1)];
}));
const want = Number(opt.index ?? 0);

// Enum orderings, as the binary declares them. RoomType is ReverbQualityMode.
const ENUMS = {
    RoomType:       ['SuperEco', 'Eco', 'Mid', 'High'],
    HighFilterType: ['Shelf', 'Lowpass'],
    SizeSmoothing:  ['Fast', 'Slow'],
};

function findReverbs(node, out = []) {
    if (Array.isArray(node)) for (const v of node) findReverbs(v, out);
    else if (node && typeof node === 'object') {
        if (node.kind === 'reverb' && node.parameters) out.push(node.parameters);
        for (const k of Object.keys(node)) findReverbs(node[k], out);
    }
    return out;
}

const song = JSON.parse(readFileSync(songPath, 'utf8'));
const found = findReverbs(song);
if (!found.length) {
    console.error(`${songPath}: no reverb device found`);
    process.exit(1);
}
if (want >= found.length) {
    console.error(`${songPath}: only ${found.length} reverb device(s), asked for #${want}`);
    process.exit(1);
}
const params = found[want];

const lines = [`# ${songPath} reverb #${want} of ${found.length}`];
const skipped = [];
for (const [key, raw] of Object.entries(params)) {
    let v;
    if (typeof raw === 'number') v = raw;
    else if (typeof raw === 'boolean') v = raw ? 1 : 0;
    else if (typeof raw === 'string' && ENUMS[key]) {
        const i = ENUMS[key].indexOf(raw);
        if (i < 0) { skipped.push(`${key}=${raw} (unknown enum member)`); continue; }
        v = i;
    } else if (raw && typeof raw === 'object') {
        // Automated/modulated parameters serialise as an object with a value.
        if (typeof raw.value === 'number') v = raw.value;
        else if (typeof raw.value === 'boolean') v = raw.value ? 1 : 0;
        else { skipped.push(`${key} (unhandled object shape)`); continue; }
    } else { skipped.push(`${key}=${raw} (unhandled type)`); continue; }
    lines.push(`${key} ${v}`);
}

// ⚠ Never drop a parameter quietly. A key this script cannot represent is a key
// the renderer never sees, which shows up later as a bad null number with
// nothing to explain it.
if (skipped.length) {
    console.error(`${songPath}: ${skipped.length} parameter(s) NOT emitted:`);
    for (const s of skipped) console.error(`    ${s}`);
    process.exit(1);
}

writeFileSync(outPath, lines.join('\n') + '\n');
console.log(`${outPath}: ${lines.length - 1} parameters`);
