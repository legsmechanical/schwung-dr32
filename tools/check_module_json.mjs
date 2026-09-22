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

/* ⭑ The synth engines' pages live in engine_ui.json beside module.json and
 * dsp/dr32.c merges them into the hierarchy it SERVES (nav entries after the
 * `nav_after` level, levels at the end). Check the merged document — the one
 * the host actually gets — by merging it the same way here. */
let engineUi = null;
{
    const euPath = path.replace(/module\.json$/, 'engine_ui.json');
    try { engineUi = JSON.parse(readFileSync(euPath, 'utf8')); } catch { /* none */ }
    if (engineUi) {
        Object.assign(levels, engineUi.levels || {});
        const rp = (levels.root && levels.root.params) || [];
        const at = rp.findIndex((p) => p && p.level === engineUi.nav_after);
        if (at < 0) console.error(`engine_ui.json nav_after "${engineUi.nav_after}" is not in root's params`);
        else rp.splice(at + 1, 0, ...(engineUi.nav || []));
    }
}

let errors = [];

/*
 * ⚠ THERE IS NO ALLOWLIST FOR A REPEATED KEY, and there must not be one.
 *
 * There was (`CLONED`, 2026-09-16), letting `volume` sit on both the Sample
 * and Mix banks. It was justified by `pages_check` passing — but that runs the
 * JS PAGE PLANNER, which tolerates a repeat. The C loader (chain_params.c
 * parse_hierarchy_params) does not: it returns count=-1 and the host keeps NONE
 * of the hierarchy's metadata. The casualty was the per-pad sends — the host
 * reads send_a/send_b's range from that metadata, found nothing, refused both
 * sends, and they were silent on stock 1.4.0 and dbxhost alike (device logs,
 * 2026-09-19: "Duplicate parameter key 'volume'" then "voice_send_params: no
 * range for a declared send; refused").
 *
 * A knob wanted on two banks gets a SECOND KEY that the DSP resolves to the
 * first — see canon_pad_sub() in dsp/dr32_params.c (`volume_clone`).
 */

// 1. duplicate keys across the whole hierarchy
const seen = new Map();
for (const [lname, level] of Object.entries(levels)) {
    for (const p of level.params || []) {
        if (!p || typeof p !== 'object' || !p.key) continue;
        if (seen.has(p.key)) {
            errors.push(`duplicate key "${p.key}" (in levels "${seen.get(p.key)}" and "${lname}") — the host's C loader rejects the WHOLE hierarchy for this (per-pad sends go silent); give the second cell its own key and alias it in canon_pad_sub()`);
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
//
// ⭑ EXCEPT "": an EMPTY knob entry is a DELIBERATE GAP, and it is the only way
// to leave a slot blank. A level's pages are chunked flat 8 at a time, so only
// its LAST page may be short — any other page needs a filler to hold position.
//
// It works because keyOf() returns "" unchanged (it only filters null), so the
// entry survives into page.keys, and every consumer downstream guards falsy
// keys: onKnobTurn and onClick bail on `!key`, and all three value-read loops
// `continue` on `!k`. So the slot draws as a gap, is not turnable, is not
// clickable, and is NEVER read from the DSP — it costs nothing on the SPI
// callback, which matters here.
//
// ⚠ Do not "tidy" an empty entry out of a knobs array. It is load-bearing
// layout, and removing one silently pulls every knob after it one slot earlier.
for (const [lname, level] of Object.entries(levels)) {
    const keys = new Set((level.params || []).filter(p => p && p.key).map(p => p.key));
    for (const k of level.knobs || []) {
        if (k === "") continue;                      // a deliberate gap — see above
        if (!keys.has(k)) errors.push(`level "${lname}" maps knob "${k}" which is not one of its params`);
    }
}

// 4. level links must point at real levels
for (const [lname, level] of Object.entries(levels)) {
    for (const p of level.params || []) {
        if (p && p.level && !levels[p.level]) errors.push(`level "${lname}" links to missing level "${p.level}"`);
    }
}

// 5. every pad level must keep the drum-surface contract AND the splice anchor.
//    dsp/dr32.c inserts the loaded kit's pad names into the served hierarchy
//    immediately before EACH `"child_index_param"`; lose that key and the DSP
//    silently serves module.json verbatim — pages still plan, focus just
//    stops following and every voice is "Pad N" again.
//
//    ⭑ THE PAD KNOBS ARE THREE SIBLING LEVELS (Sample / Shape / Mix), one per
//    bank, sharing one focus. They must agree about the children they address,
//    or two banks are editing two different pads while showing one pad number.
const PAD_LEVELS = ['pads', 'pad_shape', 'pad_mix'];
/* ⭑ The synth engines' pages (generated by tools/gen_engine_ui.mjs, keys
 * `eng_*`) are pad levels too: the same 32 children, the same focus, and a
 * splice anchor each, so they carry the same contract. They are NOT part of
 * the three-bank partition below — each belongs to one engine and is gated
 * on `ui_engine`, so a pad only ever shows one engine's set. */
const ENGINE_LEVELS = Object.keys(levels).filter((k) => k.startsWith('eng_'));
const ALL_PAD_LEVELS = [...PAD_LEVELS, ...ENGINE_LEVELS];
{
    if (caps.ui_hierarchy && caps.ui_hierarchy.pad_layout !== 'drums')
        errors.push('ui_hierarchy.pad_layout must be "drums" (upstream 1.2 drum-surface contract)');
    for (const name of ALL_PAD_LEVELS) {
        const lvl = levels[name];
        if (!lvl) { errors.push(`levels.${name} missing — it is one of the three pad banks`); continue; }
        if (lvl.child_index_param !== 'ui_current_pad')
            errors.push(`levels.${name}.child_index_param must be "ui_current_pad" — one shared focus, and also the child_names splice anchor in dsp/dr32.c`);
        if (lvl.child_count !== 32 || lvl.child_prefix !== 'pad')
            errors.push(`levels.${name} must declare child_prefix "pad" and child_count 32 — dr32_params.c speaks pad<N>_<key>`);
        if (lvl.child_index_base !== 1)
            errors.push(`levels.${name}.child_index_base must be 1 — the pad selector reads 1-32`);
    }
    // ⭑⭑ THE NOTE MAP IS DECLARED ONCE, ON `pads`, AND THAT IS WHAT MAKES
    //    THREE BANKS ONE RACK. voicesOf (upstream voices.mjs) emits a voice
    //    per child of every level that declares child_note_base — so all three
    //    declaring it published 96 voices at notes 36..131, and the drum
    //    surface seated three copies of the kit. Shape and Mix are further
    //    EDITING VIEWS of the same 32 pads, not 32 more pads.
    {
        const noted = ALL_PAD_LEVELS.filter((n) => levels[n] && levels[n].child_note_base !== undefined);
        if (noted.length !== 1 || noted[0] !== 'pads')
            errors.push(`child_note_base is declared on [${noted.join(', ')}]; exactly one pad level ("pads") may declare it, or voicesOf publishes ${32 * Math.max(noted.length, 1)} voices instead of 32`);
        if ((levels.pads || {}).child_note_base !== 36)
            errors.push('levels.pads.child_note_base must be 36 (DR32_FIRST_NOTE)');
    }

    // ⚠ The press params stay on ONE level. dAVEBOx takes "the FIRST child
    //   level declaring child_press_param" (davebox/ui/ui_discover.mjs), so a
    //   second declaration is a second answer to a question with one answer.
    for (const k of ['child_press_param', 'child_press_note_param']) {
        if (!(levels.pads || {})[k])
            errors.push(`levels.pads.${k} missing — dAVEBOx sound mode reads it (see CLAUDE.md)`);
        const on = ALL_PAD_LEVELS.filter((n) => levels[n] && levels[n][k]);
        if (on.length !== 1)
            errors.push(`${k} is declared on ${on.length} pad levels (${on.join(', ')}); dAVEBOx takes the FIRST, so exactly one must declare it`);
    }
    const raw = JSON.stringify(d);                 /* the MERGED document */
    const n = (raw.match(/"child_index_param"/g) || []).length;
    if (n !== ALL_PAD_LEVELS.length)
        errors.push(`"child_index_param" appears ${n} times; dsp/dr32.c splices at each one, so it must appear exactly once per pad level (${ALL_PAD_LEVELS.length})`);
}

// 5a. the engine gate — ui_engine — and the pages that hang off it.
{
    const GATE = 'ui_engine';
    /* In chain_params: the table the grid's condition evaluator reads
     * through. */
    if (!(caps.chain_params || []).some((p) => p && p.key === GATE))
        errors.push(`chain_params must declare "${GATE}" — the condition evaluator reads the gate through it`);
    /* 🔴 On NO level. A gate listed in a level's knobs or params is expanded
     * through the child template (hierChildKeyFor), asked for as pad1_ui_engine,
     * which nothing serves — and the evaluator FAILS OPEN, showing every
     * engine's pages at once. */
    for (const [lname, lvl] of Object.entries(levels)) {
        if ((lvl.params || []).some((p) => p && p.key === GATE) || (lvl.knobs || []).includes(GATE))
            errors.push(`level "${lname}" lists "${GATE}" — the gate must be on no level (docs/MODULES.md, "The gate does not need a cell")`);
    }
    const onGate = (g, v) => g && g.param === GATE && g.equals === v;
    /* The kit ports' second gate: their lanes share ONE page set per
     * instrument, gated on ui_family (DR32_FAM_*, 3 and up), with a lane's
     * own knob gated on ui_engine inside it. Same rules as ui_engine: in
     * chain_params, on no level. */
    const FGATE = 'ui_family';
    const famPages = ENGINE_LEVELS.filter((n) => (levels[n].visible_if || {}).param === FGATE);
    if (famPages.length) {
        if (!(caps.chain_params || []).some((p) => p && p.key === FGATE))
            errors.push(`chain_params must declare "${FGATE}" — the kit ports' pages gate on it`);
        for (const [lname, lvl] of Object.entries(levels))
            if ((lvl.params || []).some((p) => p && p.key === FGATE) || (lvl.knobs || []).includes(FGATE))
                errors.push(`level "${lname}" lists "${FGATE}" — a gate must be on no level`);
    }
    /* Every engine page is gated to exactly one engine, or to one kit port... */
    for (const name of ENGINE_LEVELS) {
        const lvl = levels[name];
        const g = lvl.visible_if;
        const fam = g && g.param === FGATE;
        if (!g || (g.param !== GATE && !fam) || !Number.isInteger(g.equals) || g.equals < (fam ? 3 : 1))
            errors.push(`levels.${name} must carry visible_if {param:"${GATE}", equals:<engine id>} or {param:"${FGATE}", equals:<kit port>}; ungated, it shows on every pad`);
        const knobs = (lvl.knobs || []).filter(Boolean);
        /* One bank per LANE: on a kit port's page a knob gated on ui_engine
         * shows on one lane (equals) or all but one (not_equals), so count
         * what each lane actually sees. A lane named by no gate sees only the
         * ungated knobs. */
        const kg = (k) => ((lvl.params || []).find((p) => p && p.key === k) || {}).visible_if || null;
        for (const k of knobs) {
            const gk = kg(k);
            if (gk && (gk.param !== GATE || (gk.equals === undefined) === (gk.not_equals === undefined)))
                errors.push(`levels.${name}.${k}: a knob on an engine page may only be gated on ${GATE} (equals or not_equals)`);
            if (gk && !fam)
                errors.push(`levels.${name}.${k} is gated, but its page belongs to one engine already`);
        }
        const ids = new Set([-1]);
        for (const k of knobs) { const gk = kg(k); if (gk) ids.add(gk.equals !== undefined ? gk.equals : gk.not_equals); }
        for (const id of ids) {
            const seen = knobs.filter((k) => {
                const gk = kg(k);
                return !gk || (gk.equals !== undefined ? gk.equals === id : gk.not_equals !== id);
            }).length;
            if (seen > 8)
                errors.push(`levels.${name} shows ${seen} knobs${id >= 0 ? ` on engine ${id}` : ''}; an engine page is one bank`);
        }
        for (const k of knobs)
            if (!(lvl.params || []).some((p) => p && p.key === k))
                errors.push(`levels.${name} puts "${k}" on a knob but declares it nowhere — engine params must be declared inline (a key only some pads have cannot borrow metadata)`);
        if (!(lvl.child_copy_keys || []).length)
            errors.push(`levels.${name} has no child_copy_keys; Copy on this page would copy only this page`);
    }
    /* ...and the sample-only parts of the base banks are gated to the sample
     * voice (engine 0), so a synth pad does not wear Start/End, the Shape
     * bank or Punch. Failing open would only clutter; pinned anyway, because
     * a knob that turns and does nothing is exactly the confusion this avoids. */
    if (!onGate(levels.pad_shape && levels.pad_shape.visible_if, 0))
        errors.push(`levels.pad_shape must be gated visible_if ${GATE} == 0 — Shape is the sample voice's`);
    for (const [lname, keys] of [['pads', ['start', 'end']], ['pad_mix', ['punch', 'punch_time']]]) {
        for (const k of keys) {
            const p = ((levels[lname] || {}).params || []).find((x) => x && x.key === k);
            if (!p || !onGate(p.visible_if, 0))
                errors.push(`${lname}.${k} must be gated visible_if ${GATE} == 0 — it is sample-only`);
        }
    }
    /* Copy/Clear act on the level you stand on. The base banks show on every
     * pad, so they carry the SAME full list, with `model` right behind
     * `sample` (written in order: the model must exist on the target before
     * its knobs can land). An engine page shows only on a pad running THAT
     * engine, so its list is the same one with every OTHER engine's keys taken
     * out — anything else would copy a different pad depending on the page. */
    const full = (levels.pads || {}).child_copy_keys || [];
    const prefixes = [...new Set(ENGINE_LEVELS.map((n) => n.slice(4).split('_')[0] + '_'))];
    for (const name of ALL_PAD_LEVELS) {
        const own = name.startsWith('eng_') ? name.slice(4).split('_')[0] + '_' : null;
        const want = own ? full.filter((k) => !prefixes.some((p) => p !== own && k.startsWith(p))) : full;
        const ck = (levels[name] || {}).child_copy_keys || [];
        if (JSON.stringify(ck) !== JSON.stringify(want))
            errors.push(`levels.${name}.child_copy_keys is not ${own ? `the full list scoped to ${own}*` : "pads' list"} — Copy would copy a different pad depending on the page`);
    }
    const ck = (levels.pads || {}).child_copy_keys || [];
    if (ck.indexOf('model') !== ck.indexOf('sample') + 1)
        errors.push('child_copy_keys must list "model" immediately after "sample"');
}

// 5a'. the host evaluates at most FOUR distinct gate params per module (the
//      #533 condition-read budget). Counted on the MERGED document.
{
    const gates = new Set();
    const walk = (o) => {
        if (!o || typeof o !== 'object') return;
        if (o.visible_if && o.visible_if.param) gates.add(o.visible_if.param);
        for (const v of Object.values(o)) walk(v);
    };
    walk(caps.ui_hierarchy);
    if (gates.size > 4)
        errors.push(`${gates.size} distinct visible_if params (${[...gates].join(', ')}); the host evaluates at most 4`);
}

// 5b. the three banks must partition the pad params — no key on two banks
//     (two cells for one value) and none dropped on the way (a param the DSP
//     answers that no page can reach).
{
    const seen = new Map();
    for (const name of PAD_LEVELS) {
        const lvl = levels[name] || {};
        const declared = (lvl.params || []).map((p) => p && p.key).filter(Boolean);
        for (const k of declared) {
            if (seen.has(k))
                errors.push(`pad param "${k}" is declared on both ${seen.get(k)} and ${name} — the banks PARTITION the pad params`);
            seen.set(k, name);
        }
        const knobs = (lvl.knobs || []).filter((k) => typeof k === 'string' && k);
        for (const k of knobs)
            if (!declared.includes(k) && !(lvl.child_key_overrides || {})[k])
                errors.push(`levels.${name} puts "${k}" on a knob but declares it nowhere`);
        /*
         * Count what can be on screen AT ONCE, not what is declared.
         *
         * Two knobs gating on the same param with complementary conditions are
         * one SLOT: the Shape bank's RES and GAIN swap places with the filter
         * type, the way the Move's own Peak filter turns Resonance into Gain.
         * The planner filters hidden keys before it chunks, so nine declared
         * knobs with one such pair is eight cells and one page.
         *
         * ⚠ pages_check will still say "level-over-eight" for the same level,
         * and that is not a disagreement: it evaluates visible_if statically
         * with nothing to read, and the host FAILS OPEN on an unanswerable
         * condition. On the device filter_type is served and answers.
         */
        const gate = (k) => ((lvl.params || []).find((p) => p && p.key === k) || {}).visible_if || null;
        const paired = new Set();
        for (const a of knobs) {
            const ga = gate(a);
            if (!ga || paired.has(a)) continue;
            for (const b of knobs) {
                if (b === a || paired.has(b)) continue;
                const gb = gate(b);
                if (!gb || gb.param !== ga.param) continue;
                const complementary =
                    (ga.equals !== undefined && gb.not_equals !== undefined && ga.equals === gb.not_equals) ||
                    (gb.equals !== undefined && ga.not_equals !== undefined && gb.equals === ga.not_equals);
                if (complementary) { paired.add(a); paired.add(b); break; }
            }
        }
        const atOnce = knobs.length - paired.size / 2;
        if (atOnce > 8)
            errors.push(`levels.${name} has ${atOnce} knobs on screen at once; a bank that overflows 8 becomes two pages and stops being one bank`);
    }
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
