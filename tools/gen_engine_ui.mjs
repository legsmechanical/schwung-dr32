/*
 * gen_engine_ui.mjs — build the synth engines' UI from the engines themselves.
 *
 *   node tools/gen_engine_ui.mjs            rewrite src/module.json + src/browser.js
 *   node tools/gen_engine_ui.mjs --check    exit 1 if either is stale (tests/run.sh)
 *
 * ⭐ ONE SOURCE. Every synth parameter — its key, label, range, unit and the
 * page it sits on — is declared once, in its engine's table (dsp/engines/*.cpp).
 * This compiles tools/dump_engines.c against those tables, asks, and writes:
 *
 *   module.json  one level per engine PAGE, gated `visible_if ui_engine == id`
 *                and carrying its params inline; a root entry for each; every
 *                engine key plus `model` in each pad level's child_copy_keys;
 *                the `ui_engine` gate in chain_params.
 *   browser.js   the picker's model list, between its GENERATED markers.
 *
 * Hand-edit neither of those parts: the next run overwrites them, and --check
 * fails the suite until it has.
 *
 * ⚠ FORMATTING IS PRESERVED, not re-flowed: the serializer below writes the
 * file's own style (2-space, a float-typed number keeps its ".0"), so a run
 * that changes nothing changes no bytes.
 */
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync, readdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const check = process.argv.includes('--check');
const ROOT = fileURLToPath(new URL('..', import.meta.url));
const MJ = join(ROOT, 'src/module.json');
const BR = join(ROOT, 'src/browser.js');

/* ---- ask the engines ---------------------------------------------------- */
function dumpEngines() {
    const dir = mkdtempSync(join(tmpdir(), 'dr32-engines-'));
    try {
        const objs = [];
        for (const f of readdirSync(join(ROOT, 'dsp/engines')).filter((f) => f.endsWith('.cpp'))) {
            const o = join(dir, f + '.o');
            execFileSync('c++', ['-std=c++14', '-O1', '-Wno-comment', '-fno-exceptions', '-fno-rtti',
                '-I' + join(ROOT, 'dsp'), '-I' + join(ROOT, 'dsp/engines'),
                '-c', join(ROOT, 'dsp/engines', f), '-o', o]);
            objs.push(o);
        }
        for (const f of ['dsp/dr32_engine.c', 'tools/dump_engines.c']) {
            const o = join(dir, f.replace(/\W/g, '_') + '.o');
            execFileSync('cc', ['-std=c11', '-O1', '-I' + join(ROOT, 'dsp'), '-c', join(ROOT, f), '-o', o]);
            objs.push(o);
        }
        const exe = join(dir, 'dump');
        execFileSync('c++', ['-o', exe, ...objs, '-lm']);
        return JSON.parse(execFileSync(exe, { encoding: 'utf8' }));
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
}

/* ---- the file's own JSON style ----------------------------------------- */
const FLOATY = new Set(['float', 'wav_position']);
/* A list of plain keys (knobs, child_copy_keys) goes on ONE line: thirteen
 * pad levels each carry ~100 copy keys, and a line per key was a third of the
 * file — module.json has a 64 KB ceiling (check_module_json.mjs). */
function ser(v, ind, floaty) {
    const pad = '  '.repeat(ind);
    if (Array.isArray(v)) {
        if (!v.length) return '[]';
        if (v.every((x) => typeof x === 'string')) return '[' + v.map((x) => JSON.stringify(x)).join(', ') + ']';
        return '[\n' + v.map((x) => pad + '  ' + ser(x, ind + 1, false)).join(',\n') + '\n' + pad + ']';
    }
    if (v && typeof v === 'object') {
        const ks = Object.keys(v);
        if (!ks.length) return '{}';
        const f = FLOATY.has(v.type);
        return '{\n' + ks.map((k) => pad + '  ' + JSON.stringify(k) + ': ' + ser(v[k], ind + 1, f)).join(',\n') + '\n' + pad + '}';
    }
    if (typeof v === 'number' && floaty && Number.isInteger(v)) return v.toFixed(1);
    return JSON.stringify(v);
}

/* ---- build ------------------------------------------------------------- */
const { engines, models } = dumpEngines();
const text = readFileSync(MJ, 'utf8');
const mj = JSON.parse(text);
const caps = mj.capabilities;
const levels = caps.ui_hierarchy.levels;

const GEN = 'eng_';                    /* every generated level's key starts so */
const engineKeys = new Set(engines.flatMap((e) => e.params.map((p) => p.key)));
const isGenKey = (k) => k === 'model' || engineKeys.has(k) || engines.some((e) => k.startsWith(e.prefix));

/* The template every engine page shares with Shape and Mix: the same 32 pads,
 * the same focus. ⚠ NO note map and NO press params here — those belong to
 * `pads` alone (CLAUDE.md: three copies of the kit otherwise). */
const TEMPLATE = ['child_prefix', 'child_count', 'child_index_base', 'child_label', 'child_index_param'];
const shape = levels.pad_shape;

/* Pictures the host would guess wrong. A velocity AMOUNT named like a level
 * reads as a fader to the host's detector — the same lie DR32's own Vel Vol
 * turns off (CLAUDE.md, `vel_vol` carries `viz: false`). */
const VIZ_OFF = /_vel_gain$/;

function paramEntry(p) {
    const e = { key: p.key, name: p.name, short_name: p.short_name, type: 'int',
                min: p.min, max: p.max, default: p.def };
    if (p.unit) e.unit = p.unit;
    if (VIZ_OFF.test(p.key)) e.viz = false;
    return e;
}

/* Rebuild the level table with the generated levels removed, then re-add them
 * right after pad_shape — the nav order is Kits, Pad, Shape | engine pages,
 * Mix, Master, and a gated level simply drops out of it. */
const newLevels = {};
const genLevels = [];
for (const e of engines) {
    const pages = [];
    for (const p of e.params) {
        let pg = pages.find((x) => x.name === p.page);
        if (!pg) pages.push(pg = { name: p.page, params: [] });
        pg.params.push(p);
    }
    for (const pg of pages) {
        if (pg.params.length > 8)
            throw new Error(`${e.slug} page "${pg.name}" has ${pg.params.length} knobs; a bank holds 8`);
        const key = GEN + e.prefix + pg.name.toLowerCase().replace(/\W+/g, '_');
        const lv = { name: pg.name, visible_if: { param: 'ui_engine', equals: e.id } };
        for (const t of TEMPLATE) lv[t] = shape[t];
        /* Copy/Clear act on the level you are STANDING on, so every pad
         * level carries the same full list (filled in below). */
        lv.child_copy_keys = [];
        lv.params = pg.params.map(paramEntry);
        lv.knobs = pg.params.map((p) => p.key);
        genLevels.push([key, lv]);
    }
}
for (const [k, v] of Object.entries(levels)) {
    if (k.startsWith(GEN)) continue;
    newLevels[k] = v;
    if (k === 'pad_shape') for (const [gk, gv] of genLevels) newLevels[gk] = gv;
}
caps.ui_hierarchy.levels = newLevels;

/* Root navigation: same placement. */
const rootParams = newLevels.root.params.filter((p) => !(p.level && p.level.startsWith(GEN)));
const at = rootParams.findIndex((p) => p.level === 'pad_shape');
rootParams.splice(at + 1, 0, ...genLevels.map(([k, lv]) => ({ level: k, label: lv.name })));
newLevels.root.params = rootParams;

/* Copy/Clear: a synth pad copies as its model first, then its values. The
 * host writes the list IN ORDER, so `model` goes right after `sample` — a
 * copied model has to exist on the target before its knobs can land. */
const baseCopy = levels.pads.child_copy_keys.filter((k) => !isGenKey(k));
for (const lv of Object.values(newLevels)) {
    if (!Array.isArray(lv.child_copy_keys)) continue;
    const base = baseCopy.slice();
    const i = base.indexOf('sample');
    base.splice(i + 1, 0, 'model', ...engines.flatMap((e) => e.params.map((p) => p.key)));
    lv.child_copy_keys = base;
}

/* The gate, in chain_params: that is the table the grid's condition evaluator
 * reads through. It is on NO level, deliberately (docs/MODULES.md, "The gate
 * does not need a cell"; schwung-urchin's ui_engine note). */
const gate = { key: 'ui_engine', name: 'Engine', type: 'int', min: 0,
               max: Math.max(...engines.map((e) => e.id)), default: 0 };
const gi = caps.chain_params.findIndex((p) => p.key === 'ui_engine');
if (gi >= 0) caps.chain_params[gi] = gate; else caps.chain_params.push(gate);

const outMj = ser(mj, 0, false) + '\n';

/* ---- browser.js model list -------------------------------------------- */
const fams = [];
for (const m of models) {
    const id = m.slug.split('/')[0];
    let f = fams.find((x) => x.id === id);
    if (!f) fams.push(f = { id, label: id.charAt(0).toUpperCase() + id.slice(1), models: [] });
    f.models.push({ slug: m.slug, name: m.name });
}
const br = readFileSync(BR, 'utf8');
const BEGIN = '/* BEGIN GENERATED MODELS — tools/gen_engine_ui.mjs writes this; do not edit. */';
const END = '/* END GENERATED MODELS */';
const b0 = br.indexOf(BEGIN), b1 = br.indexOf(END);
if (b0 < 0 || b1 < b0) throw new Error('browser.js: GENERATED MODELS markers missing');
const famJs = 'const FAMILIES = [\n' + fams.map((f) =>
    `    { id: ${JSON.stringify(f.id)}, label: ${JSON.stringify(f.label)}, models: [\n` +
    f.models.map((m) => `        { slug: ${JSON.stringify(m.slug)}, name: ${JSON.stringify(m.name)} },`).join('\n') +
    '\n    ] },').join('\n') + '\n];\n';
const outBr = br.slice(0, b0) + BEGIN + '\n' + famJs + br.slice(b1);

/* ---- write or check ---------------------------------------------------- */
const stale = [];
if (outMj !== text) stale.push('src/module.json');
if (outBr !== br) stale.push('src/browser.js');
if (check) {
    if (stale.length) {
        console.log(`gen_engine_ui: STALE — ${stale.join(', ')}. Run: node tools/gen_engine_ui.mjs`);
        process.exit(1);
    }
    console.log(`gen_engine_ui: OK — ${engines.length} engines, ${genLevels.length} pages, ${models.length} models`);
} else {
    writeFileSync(MJ, outMj);
    writeFileSync(BR, outBr);
    console.log(`gen_engine_ui: wrote ${stale.length ? stale.join(', ') : 'nothing (up to date)'} — ` +
                `${engines.length} engines, ${genLevels.length} pages, ${models.length} models, ` +
                `module.json ${Buffer.byteLength(outMj)} bytes`);
}
