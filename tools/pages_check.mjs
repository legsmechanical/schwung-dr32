// pages_check.mjs — run upstream Schwung's OWN contract validator and voice
// resolver over the hierarchy DR32 actually serves, off-device.
//
// The host plans every page from get_param("ui_hierarchy"), not from
// module.json, so the input here is the served text (tests/test_state.c writes
// it to dist/tests/served_hierarchy.json after loading two samples). The
// library is imported from a Schwung checkout — the upstream tree, not a fork —
// because the rules that matter (viz groups must sit contiguously on one row,
// child levels must resolve, voices must number 32 at notes 36..67) are the
// host's, and a re-implementation here would be a second opinion.
//
//   node tools/pages_check.mjs                 # after tests/run.sh
//   SCHWUNG_SRC=/path/to/schwung node tools/pages_check.mjs
//
// Side effect: writes dist/tests/dr32-fixture.json, a one-module fixture in
// the shape upstream's preview tools read:
//   node ../schwung-current/tools/param-pages/preview.mjs dr32 --all \
//        --layout movy --fixture dist/tests/dr32-fixture.json --png /tmp/dr32
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const SRC = process.env.SCHWUNG_SRC || path.resolve("..", "schwung-current");
const lib = (f) => pathToFileURL(path.join(SRC, "src", "shared", "param_pages", f)).href;
if (!fs.existsSync(path.join(SRC, "src", "shared", "param_pages", "voices.mjs"))) {
    console.error(`pages_check: no Schwung checkout at ${SRC} (set SCHWUNG_SRC) — skipped`);
    process.exit(0);
}
const { validateContract } = await import(lib("validate_contract.mjs"));
const { voicesOf, padLayoutOf } = await import(lib("voices.mjs"));
const { planPages, PAGE_KNOBS } = await import(lib("page_plan.mjs"));

const served = "dist/tests/served_hierarchy.json";
if (!fs.existsSync(served)) {
    console.error(`pages_check: ${served} missing — run tests/run.sh first`);
    process.exit(1);
}
let hierarchy;
try {
    hierarchy = JSON.parse(fs.readFileSync(served, "utf8"));
} catch (e) {
    console.error(`pages_check: the SERVED hierarchy is not valid JSON (${e.message}) — the host would plan no pages at all`);
    process.exit(1);
}
const mod = JSON.parse(fs.readFileSync("src/module.json", "utf8"));
const chainParams = mod.capabilities.chain_params;

let fail = 0;
const bad = (m) => { fail++; console.error("  FAIL " + m); };

// ---- what the host would resolve
if (padLayoutOf(hierarchy) !== "drums") bad(`pad_layout resolves to ${padLayoutOf(hierarchy)}, want drums`);
const voices = voicesOf(hierarchy);
if (voices.length !== 32) bad(`${voices.length} voices, want 32`);
voices.forEach((v, i) => {
    if (v.note !== 36 + i) bad(`voice ${i} plays note ${v.note}, want ${36 + i}`);
});
const named = voices.filter((v) => !/^Pad \d+$/.test(v.name)).map((v) => v.name);
console.log(`  voices: ${voices.length}, notes ${voices[0]?.note}..${voices[voices.length - 1]?.note}, named from the kit: ${JSON.stringify(named)}`);
if (named.length !== 2) bad("expected exactly the two loaded samples to carry names");

// ---- contract validation, the host's rules
const { findings } = validateContract({ id: "dr32", hierarchy, chainParams, capabilities: mod.capabilities });
for (const f of findings) {
    const line = `[${f.level}] ${f.rule}: ${f.message}`;
    if (f.level === "error") bad(line); else console.log("  " + line);
}

// ---- the pages, as planned
const { pages } = planPages({ hierarchy, chainParams });
const knobPages = pages.filter((p) => p.kind === PAGE_KNOBS);
console.log(`  ${pages.length} pages planned, ${knobPages.length} knob pages`);
for (const p of knobPages) console.log(`    ${String(p.level).padEnd(8)} ${(p.keys || []).map((k) => k || "-").join(" ")}`);

// ---- the send pages, as the host sees them with visible_if applied
//
// The planner above is fail-open (everything visible). On the device each
// send level collapses to the cells of the ARMED type, and the design bar is
// that every type fits ONE page of eight — a type spilling onto a second page
// would hide its Return behind a jog for no reason. Values are the DSP's own
// derived params (dr32_params.c: send1_mode / _env / _sync), which is what the
// host's visible_if reads.
const SEND_STATES = {
    Plate:  { mode: "Verb",   env: "-",   sync: "-",    type: "Plate" },
    Native: { mode: "Verb",   env: "-",   sync: "-",    type: "Native" },
    Gated:  { mode: "Gate",   env: "Env", sync: "-",    type: "Gated" },
    NonLin: { mode: "NonLin", env: "Env", sync: "-",    type: "NonLin" },
    "Delay/Sync": { mode: "Delay", env: "-", sync: "Sync", type: "Delay" },
    "Delay/Free": { mode: "Delay", env: "-", sync: "Free", type: "Delay" },
};
for (const [label, st] of Object.entries(SEND_STATES)) {
    const vals = { send1_mode: st.mode, send1_env: st.env, send1_sync: st.sync, send1_type: st.type };
    const visible = (cond) => {
        const v = vals[cond.param];
        if (v === undefined) return true;
        if ("equals" in cond) return v === cond.equals;
        if ("not_equals" in cond) return v !== cond.not_equals;
        return true;
    };
    const r = planPages({ hierarchy, chainParams, visible });
    const sp = r.pages.filter((p) => p.kind === PAGE_KNOBS && p.level === "send1");
    const keys = sp.flatMap((p) => (p.keys || []).filter(Boolean)).map((k) => k.replace(/^send1_/, ""));
    console.log(`    send1 as ${label.padEnd(10)} ${sp.length} page(s): ${keys.join(" ")}`);
    if (sp.length !== 1) bad(`send1 as ${label} spans ${sp.length} pages, want 1`);
}

// ---- a fixture for upstream's preview tools
fs.mkdirSync("dist/tests", { recursive: true });
fs.writeFileSync("dist/tests/dr32-fixture.json", JSON.stringify({
    _source: "schwung-dr32 tools/pages_check.mjs — served hierarchy + module.json chain_params",
    generated_at: new Date().toISOString(),
    module_count: 1,
    not_captured: [],
    modules: [{
        id: "dr32", category: "sound_generator", component_key: "synth", status: "ok",
        name: mod.name, version: mod.version,
        ui_hierarchy: hierarchy, chain_params: chainParams, presets: null,
    }],
}, null, 1));

console.log(fail ? `pages_check: FAILED (${fail})` : "pages_check: OK");
process.exit(fail ? 1 : 0);
