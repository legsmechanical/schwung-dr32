// set_cell_params.mjs — set drumCell parameters on every pad of a fixture.
//
// Usage: node tools/set_cell_params.mjs <song.abl> "Key=Value,Key2=Value2"
//
// Kept separate from make_fixture.mjs so a fixture can carry arbitrary effect
// parameters without make_fixture needing a flag per parameter.

import { readFileSync, writeFileSync } from 'node:fs';

const [path, spec] = process.argv.slice(2);
if (!path || !spec) {
    console.error('usage: node tools/set_cell_params.mjs <song.abl> "Key=Value,..."');
    process.exit(2);
}

const song = JSON.parse(readFileSync(path, 'utf8'));

function eachCell(node, fn) {
    if (Array.isArray(node)) { for (const v of node) eachCell(v, fn); return; }
    if (!node || typeof node !== 'object') return;
    if (node.kind === 'drumCell') fn(node);
    for (const k in node) eachCell(node[k], fn);
}

const pairs = spec.split(',').map((s) => s.split('='));
let n = 0;
eachCell(song, (cell) => {
    for (const [k, v] of pairs) {
        cell.parameters[k.trim()] = /^-?\d+(\.\d+)?$/.test(v.trim()) ? Number(v) : v.trim();
    }
    n++;
});

writeFileSync(path, JSON.stringify(song, null, 2) + '\n');
console.error(`${path}: set ${pairs.length} param(s) on ${n} cells`);
