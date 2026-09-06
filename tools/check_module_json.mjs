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

// 5. the pads level must keep the drum-surface contract AND the splice anchor.
//    dsp/dr32.c inserts the loaded kit's pad names into the served hierarchy
//    immediately before `"child_index_param"`; lose that key and the DSP
//    silently serves module.json verbatim — pages still plan, focus just
//    stops following and every voice is "Pad N" again.
{
    const pads = levels.pads || {};
    if (caps.ui_hierarchy && caps.ui_hierarchy.pad_layout !== 'drums')
        errors.push('ui_hierarchy.pad_layout must be "drums" (upstream 1.2 drum-surface contract)');
    if (pads.child_index_param !== 'ui_current_pad')
        errors.push('levels.pads.child_index_param must be "ui_current_pad" — it is also the child_names splice anchor in dsp/dr32.c');
    if (pads.child_note_base !== 36)
        errors.push('levels.pads.child_note_base must be 36 (DR32_FIRST_NOTE)');
    if (pads.child_count !== 32 || pads.child_prefix !== 'pad')
        errors.push('levels.pads must declare child_prefix "pad" and child_count 32 — dr32_params.c speaks pad<N>_<key>');
    for (const k of ['child_press_param', 'child_press_note_param'])
        if (!pads[k]) errors.push(`levels.pads.${k} missing — dAVEBOx sound mode reads it (see CLAUDE.md)`);
    const raw = readFileSync(path, 'utf8');
    const n = (raw.match(/"child_index_param"/g) || []).length;
    if (n !== 1) errors.push(`"child_index_param" appears ${n} times; the splice anchor must be unique`);
}

// 6. the host refuses module.json larger than 64 KB
const size = readFileSync(path).length;
if (size > 65536) errors.push(`module.json is ${size} bytes; the host rejects > 65536`);

console.log(`${path}: ${seen.size} params, ${Object.keys(levels).length} levels, ${size} bytes`);
if (errors.length) {
    console.error('FAILED:');
    for (const e of errors) console.error('  - ' + e);
    process.exit(1);
}
console.log('OK');
