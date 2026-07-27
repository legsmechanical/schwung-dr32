// check_module_json.mjs — validate module.json against the host's constraints
// BEFORE it reaches the device.
//
// The host (chain_params.c parse_hierarchy_params) rejects the ENTIRE
// ui_hierarchy with count=-1 if any parameter key appears twice, which
// registers no params at all — the module loads, the menu is there, and
// nothing works. That failure cost a debugging round; this makes it a build
// error instead.

import { readFileSync } from 'node:fs';

const path = process.argv[2] || 'src/module.json';
const d = JSON.parse(readFileSync(path, 'utf8'));
const caps = d.capabilities || {};
const levels = ((caps.ui_hierarchy || {}).levels) || {};

let errors = [];

// 1. duplicate keys across the whole hierarchy
const seen = new Map();
for (const [lname, level] of Object.entries(levels)) {
    for (const p of level.params || []) {
        if (!p || typeof p !== 'object' || !p.key) continue;
        if (seen.has(p.key)) {
            errors.push(`duplicate key "${p.key}" (in levels "${seen.get(p.key)}" and "${lname}") — the host rejects the WHOLE hierarchy for this`);
        } else {
            seen.set(p.key, lname);
        }
    }
}

// 2. editable params need `name`, not `label`
for (const [lname, level] of Object.entries(levels)) {
    for (const p of level.params || []) {
        if (!p || typeof p !== 'object' || !p.key) continue;
        if (!p.name) errors.push(`param "${p.key}" in "${lname}" has no \`name\` (label is only for level links)`);
    }
}

// 3. knobs must reference params that exist in the same level
for (const [lname, level] of Object.entries(levels)) {
    const keys = new Set((level.params || []).filter(p => p && p.key).map(p => p.key));
    for (const k of level.knobs || []) {
        if (!keys.has(k)) errors.push(`level "${lname}" maps knob "${k}" which is not one of its params`);
    }
}

// 4. level links must point at real levels
for (const [lname, level] of Object.entries(levels)) {
    for (const p of level.params || []) {
        if (p && p.level && !levels[p.level]) errors.push(`level "${lname}" links to missing level "${p.level}"`);
    }
}

// 5. the host refuses module.json larger than 64 KB
const size = readFileSync(path).length;
if (size > 65536) errors.push(`module.json is ${size} bytes; the host rejects > 65536`);

console.log(`${path}: ${seen.size} params, ${Object.keys(levels).length} levels, ${size} bytes`);
if (errors.length) {
    console.error('FAILED:');
    for (const e of errors) console.error('  - ' + e);
    process.exit(1);
}
console.log('OK');
