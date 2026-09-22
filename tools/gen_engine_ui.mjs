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
 *   engine_ui.json  one level per engine PAGE, gated `visible_if ui_engine
 *                == id` and carrying its params inline, plus the root nav
 *                entries for them. dsp/dr32.c MERGES this into the hierarchy
 *                it serves (after the Shape entry, at the end of `levels`).
 *   module.json  every engine key plus `model` in each pad level's
 *                child_copy_keys; the `ui_engine` gate in chain_params.
 *   browser.js   the picker's model list, between its GENERATED markers.
 *
 * ⭐ WHY THE ENGINE PAGES ARE NOT IN module.json. The host's C loader
 * (chain_params.c parse_chain_params) refuses a module.json over 64 KB, and
 * that loader is where the per-pad send ranges come from — over the line, the
 * sends go silent. A sound generator's hierarchy is SERVED by its DSP anyway,
 * so the engine pages live beside it and the file keeps its headroom for the
 * engines still to come. (Their params are therefore not in the host's C
 * metadata; nothing reads them there.)
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
const EU = join(ROOT, 'src/engine_ui.json');

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
        for (const f of readdirSync(join(ROOT, 'dsp/engines')).filter((f) => f.endsWith('.c'))) {
            const o = join(dir, f + '.o');
            execFileSync('cc', ['-std=c11', '-O1', '-I' + join(ROOT, 'dsp'), '-I' + join(ROOT, 'dsp/engines'),
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
/* DR32_FAM_* from which a family is a KIT PORT (dsp/dr32_engine.h). */
const FAMILY_PORTS = 3;
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
    if (p.options) {
        /* An enum, the way DR32 declares its own (link, filter_type): the
         * options by name, the default by name. The DSP reads and writes the
         * names. */
        const options = p.options.split('|');
        return { key: p.key, name: p.name, short_name: p.short_name, type: 'enum',
                 options, default: options[p.def] };
    }
    const e = { key: p.key, name: p.name, short_name: p.short_name, type: 'int',
                min: p.min, max: p.max, default: p.def };
    if (p.unit) e.unit = p.unit;
    if (VIZ_OFF.test(p.key)) e.viz = false;
    return e;
}

/*
 * Engines that share a key PREFIX are one instrument's LANES — the kit ports
 * (9W9, 6W6, 8W8, CW-78), one engine per lane of the machine. Their lanes have
 * the same panel (Tune, Decay, Drive, ...), so they share ONE page set, gated
 * on `ui_family`; a knob only some lanes have is gated on `ui_engine` inside
 * it. A page set per lane would be ~50 more levels, most of the served
 * hierarchy's 128 KB value channel (SHADOW_PARAM_VALUE_LEN, host 1.3.0+) for
 * one knob label per lane; one set per machine keeps the room for engines
 * still to come. (Josh, 2026-09-22: keep one page per machine.)
 *
 * ⚠ A condition tests ONE value. So a knob must be on every lane of its
 * instrument, on exactly one (`equals`), or on all but one (`not_equals`);
 * anything else cannot be expressed and is refused here — give each lane its
 * own key instead (9W9's toms each have `n9_<lane>_attack`).
 *
 * An engine with a prefix of its own (SIMIAN, URCHIN's three) is a group of
 * one and keeps the shape it had: its pages gated on `ui_engine == id`.
 */
const groups = [];
for (const e of engines) {
    let g = groups.find((x) => x.prefix === e.prefix);
    if (!g) groups.push(g = { prefix: e.prefix, engines: [] });
    g.engines.push(e);
}

/* The union of a group's params, each lane's own ORDER kept: a key new to the
 * union goes straight after the key that precedes it in its lane, so a lane's
 * extra lands next to the knob it belongs beside (the snare's Snappy after
 * Decay, not after Velocity). */
function unionParams(g) {
    const out = [];
    const lanes = new Map();
    for (const e of g.engines) {
        let prev = null;
        for (const p of e.params) {
            const at = out.findIndex((x) => x.key === p.key);
            if (at < 0) {
                const pi = prev === null ? -1 : out.findIndex((x) => x.key === prev);
                /* After the predecessor AND any keys already slotted after it
                 * for other lanes' extras — up to the next key this lane has. */
                let ins = pi + 1;
                const laneKeys = e.params.map((x) => x.key);
                while (ins < out.length && !laneKeys.includes(out[ins].key)) ins++;
                out.splice(ins, 0, p);
                lanes.set(p.key, [e.id]);
            } else {
                const q = out[at];
                for (const f of ['name', 'short_name', 'min', 'max', 'page', 'options', 'unit'])
                    if (q[f] !== p[f])
                        throw new Error(`${p.key}: lane ${e.slug} declares ${f}=${p[f]}, another lane ${q[f]} — a shared key is one knob`);
                lanes.get(p.key).push(e.id);
            }
            prev = p.key;
        }
    }
    return { params: out, lanes };
}

function laneGate(g, key, lanes) {
    const all = g.engines.map((e) => e.id);
    const on = lanes.get(key);
    if (on.length === all.length) return null;
    if (on.length === 1) return { param: 'ui_engine', equals: on[0] };
    if (on.length === all.length - 1)
        return { param: 'ui_engine', not_equals: all.find((id) => !on.includes(id)) };
    throw new Error(`${key} is on ${on.length} of ${all.length} ${g.prefix} lanes — a condition can ` +
                    `only name one lane; give each lane its own key`);
}

/* Rebuild the level table with the generated levels removed, then re-add them
 * right after pad_shape — the nav order is Kits, Pad, Shape | engine pages,
 * Mix, Master, and a gated level simply drops out of it. */
const newLevels = {};
const genLevels = [];
for (const g of groups) {
    const shared = g.engines.length > 1 && new Set(g.engines.map((e) => e.family)).size === 1
                   && g.engines[0].family >= FAMILY_PORTS;
    if (g.engines.length > 1 && !shared)
        throw new Error(`prefix ${g.prefix} is shared by engines that are not one kit port's lanes`);
    const { params, lanes } = shared ? unionParams(g) : { params: g.engines[0].params, lanes: null };
    const pages = [];
    for (const p of params) {
        let pg = pages.find((x) => x.name === p.page);
        if (!pg) pages.push(pg = { name: p.page, params: [] });
        pg.params.push(p);
    }
    for (const pg of pages) {
        /* A bank holds 8 knobs — counted per LANE, since a lane sees only its
         * own. */
        for (const e of g.engines) {
            const n = pg.params.filter((p) => !lanes || lanes.get(p.key).includes(e.id)).length;
            if (n > 8) throw new Error(`${e.slug} page "${pg.name}" has ${n} knobs; a bank holds 8`);
        }
        const key = GEN + g.prefix + pg.name.toLowerCase().replace(/\W+/g, '_');
        const lv = { name: pg.name,
                     visible_if: shared ? { param: 'ui_family', equals: g.engines[0].family }
                                        : { param: 'ui_engine', equals: g.engines[0].id } };
        for (const t of TEMPLATE) lv[t] = shape[t];
        /* Copy/Clear act on the level you are STANDING on, so every pad
         * level carries the same full list (filled in below). */
        lv.child_copy_keys = [];
        lv._keys = params.map((p) => p.key);   /* for the copy list below; not emitted */
        lv.params = pg.params.map((p) => {
            const ent = paramEntry(p);
            const gate = shared ? laneGate(g, p.key, lanes) : null;
            if (gate) ent.visible_if = gate;
            return ent;
        });
        lv.knobs = pg.params.map((p) => p.key);
        genLevels.push([key, lv]);
    }
}
for (const [k, v] of Object.entries(levels)) {
    if (k.startsWith(GEN)) continue;          /* they live in engine_ui.json now */
    newLevels[k] = v;
}
caps.ui_hierarchy.levels = newLevels;
newLevels.root.params = newLevels.root.params.filter((p) => !(p.level && p.level.startsWith(GEN)));
const NAV_AFTER = 'pad_shape';

/* Copy/Clear: a synth pad copies as its model first, then its values. The
 * host writes the list IN ORDER, so `model` goes right after `sample` — a
 * copied model has to exist on the target before its knobs can land. */
/* ⭑ An engine page only ever shows on a pad RUNNING that engine (it is gated),
 * so its list needs that engine's keys and no other's: whatever the target
 * was, the copied `model` makes it this engine. The base banks show on every
 * pad, so they carry every engine's keys. Keeping the engine pages' lists to
 * their own is what keeps the served hierarchy well inside the 128 KB value channel
 * as engines are added. */
const baseCopy = levels.pads.child_copy_keys.filter((k) => !isGenKey(k));
const withKeys = (keys) => {
    const base = baseCopy.slice();
    base.splice(base.indexOf('sample') + 1, 0, 'model', ...keys);
    return base;
};
const allKeys = [...new Set(engines.flatMap((e) => e.params.map((p) => p.key)))];
for (const lv of Object.values(newLevels))
    if (Array.isArray(lv.child_copy_keys)) lv.child_copy_keys = withKeys(allKeys);
for (const [, lv] of genLevels) {
    /* In the base banks' ORDER, not the page's: a kit port's page lays its
     * lanes' extras out beside the knobs they belong to, which is not the
     * order the base banks list them in, and two orders are two lists. */
    const own = new Set(lv._keys);
    lv.child_copy_keys = withKeys(allKeys.filter((k) => own.has(k)));
    delete lv._keys;
}

/* The gate, in chain_params: that is the table the grid's condition evaluator
 * reads through. It is on NO level, deliberately (docs/MODULES.md, "The gate
 * does not need a cell"; schwung-urchin's ui_engine note). */
const gate = { key: 'ui_engine', name: 'Engine', type: 'int', min: 0,
               max: Math.max(...engines.map((e) => e.id)), default: 0 };
const gi = caps.chain_params.findIndex((p) => p.key === 'ui_engine');
if (gi >= 0) caps.chain_params[gi] = gate; else caps.chain_params.push(gate);
/* The second gate: the kit ports' page sets follow the INSTRUMENT. */
const fgate = { key: 'ui_family', name: 'Instrument', type: 'int', min: 0,
                max: Math.max(...engines.map((e) => e.family)), default: 0 };
const fi = caps.chain_params.findIndex((p) => p.key === 'ui_family');
if (fi >= 0) caps.chain_params[fi] = fgate;
else caps.chain_params.splice(caps.chain_params.findIndex((p) => p.key === 'ui_engine') + 1, 0, fgate);

const outMj = ser(mj, 0, false) + '\n';

/* MINIFIED and in a fixed key order: dr32.c finds `"nav":[` and `"levels":{`
 * by exact text, as it finds everything else it splices. */
const outEu = JSON.stringify({
    nav_after: NAV_AFTER,
    nav: genLevels.map(([k, lv]) => ({ level: k, label: lv.name })),
    levels: Object.fromEntries(genLevels),
}) + '\n';

/* ---- browser.js model list -------------------------------------------- */
/* A section's label, where capitalising its slug prefix is not the name. */
const FAMILY_LABEL = { '9w9': '9W9', '6w6': '6W6', '8w8': '8W8', cw78: 'CW-78' };
const fams = [];
for (const m of models) {
    const id = m.slug.split('/')[0];
    let f = fams.find((x) => x.id === id);
    if (!f) fams.push(f = { id, label: FAMILY_LABEL[id] || id.charAt(0).toUpperCase() + id.slice(1), models: [] });
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
let euText = '';
try { euText = readFileSync(EU, 'utf8'); } catch { /* first run */ }
if (outEu !== euText) stale.push('src/engine_ui.json');
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
    writeFileSync(EU, outEu);
    console.log(`gen_engine_ui: wrote ${stale.length ? stale.join(', ') : 'nothing (up to date)'} — ` +
                `${engines.length} engines, ${genLevels.length} pages, ${models.length} models, ` +
                `module.json ${Buffer.byteLength(outMj)} bytes, engine_ui.json ${Buffer.byteLength(outEu)} bytes`);
}
