/* DR32 canvas config for schwung-canvaskit (../../schwung-canvaskit).
 * SOURCE for src/canvas.js — regenerate after editing:
 *   node ../schwung-canvaskit/build.mjs src/canvas.config.js src/canvas.js
 * Concatenated between the kit prelude (cell constructors in scope) and the
 * kit engine (which reads CONFIG) inside one IIFE.
 *
 * ---- wire contract (dsp/dr32_params.c dr32_read_param / dr32_set_param) ----
 * Unlike most kit consumers, DR32's params are NOT 0..1: they are in real
 * engineering units, written and read as "%g" decimal strings —
 *   cutoff 30..22000 Hz      attack 0.0001..20 s     volume -36..+12 dB
 *   hold/decay 0.001..60 s   pan/detune -50..+50     send1/2 -70..+6 dB
 * Enums (filter_type, env_mode, the FX type pickers) read back the option
 * LABEL, not an index. So every cell carries a parse/format codec pair, and
 * the wide ratio ranges (cutoff spans 733:1, attack 200000:1) are mapped
 * LOGARITHMICALLY — linear would bury everything useful in the first few
 * knob units.
 *
 * ---- pad binding ----
 * The 32 pads are per-pad keys (pad<N>_<key>), not a single aliased set, so
 * every cell is bound through dynamicCells to whichever pad is selected.
 * s.pad holds it. The static `knobs` fallback is pad 0.
 */

/* 255, not the kit's usual 100: our codecs map engineering ranges onto this
 * domain, so it sets the RESOLUTION. At 100 a 76 dB send range quantises to
 * 0.76 dB per step — coarser than the hierarchy menu's 0.5 dB. */
KIT_PARAM_MAX = 255;
/* KIT_PICK_SENS, not KIT_ENUM_SENS: the latter was a v30 tunable the kit has
 * since dropped (enum stepping folded into the pick class). Assigning it is a
 * ReferenceError, because the host evaluates canvas.js as a strict MODULE. */
KIT_PICK_SENS = 6;

const PAD_COUNT = 32;
const PAD_BANK_COUNT = 3;      /* banks 0..2 are the pad pages */
/* ⚠ Move's PADS send notes 68..99 on the control-surface path — NOT the 36..67
 * a drum rack uses musically. (Host: clearLedBatch() reserves knob touch 0-7,
 * steps 16-31, pads 68-99; docs/MIDI_INJECTION.md says the same.) Matching on
 * the pad's musical `note` param instead looked right and silently did
 * nothing, because no pad claims note 68. */
const PAD_NOTE_LO = 68;
const PAD_NOTE_HI = PAD_NOTE_LO + PAD_COUNT - 1;   /* 99 */

/* ---------- codecs: engineering units <-> the kit's 0..100 int domain ---- */

function clamp01(v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

/* Linear range. */
function linCodec(lo, hi) {
  return {
    parse: (w) => Math.round(clamp01(((parseFloat(w) || 0) - lo) / (hi - lo)) * KIT_PARAM_MAX),
    format: (v) => String(lo + (hi - lo) * (v / KIT_PARAM_MAX))
  };
}

/* Logarithmic range (both bounds > 0). */
function logCodec(lo, hi) {
  const lr = Math.log(hi / lo);
  return {
    parse: (w) => {
      const x = parseFloat(w);
      if (!(x > 0)) return 0;
      return Math.round(clamp01(Math.log(x / lo) / lr) * KIT_PARAM_MAX);
    },
    format: (v) => String(lo * Math.exp(lr * (v / KIT_PARAM_MAX)))
  };
}

/* ---------- display helpers (cell.text) -------------------------------- */

function fmtHz(x) {
  return x >= 1000 ? (x / 1000).toFixed(x >= 10000 ? 0 : 1) + "k" : String(Math.round(x));
}
function fmtSec(x) {
  if (x < 0.001) return "0ms";
  return x < 1 ? Math.round(x * 1000) + "ms" : (x < 10 ? x.toFixed(2) : x.toFixed(1)) + "s";
}
function fmtDb(x) {
  const r = Math.abs(x) < 10 ? x.toFixed(1) : String(Math.round(x));
  return (x > 0 ? "+" : "") + r;
}
function fmtPan(x) {
  const n = Math.round(x);
  if (n === 0) return "C";
  return (n < 0 ? "L" : "R") + Math.abs(n);
}

/* Value of a cell in ENGINEERING units (undo the codec).
 *
 * NOTE the two different readers, which are easy to confuse:
 *   getRaw(ctx, CELL)     -> the cell's value already PARSED to the 0..100 int
 *   ctx.getParam(KEY)     -> the raw wire STRING (engine-wrapped, cached)
 * getRaw takes a cell object, not a key; passing a key silently yields the
 * fallback, which reads as "every value is at its minimum". */
function eng(cell, ctx) { return parseFloat(cell.format(getRaw(ctx, cell))) || 0; }

/* ---------- cell builders ---------------------------------------------- */

/* Continuous, linear, with an optional unit formatter. */
function plin(key, label, name, lo, hi, fmt) {
  const c = (lo < 0 && hi > 0) ? bip(key, label) : uni(key, label);
  Object.assign(c, linCodec(lo, hi), { name });
  if (fmt) c.text = (ctx) => fmt(eng(c, ctx));
  return c;
}
/* Continuous, logarithmic. `asFader` for cells inside an envelope span —
 * those are the graphic's vertices, and the kit requires the fader kind. */
function plog(key, label, name, lo, hi, fmt, asFader) {
  const c = asFader ? fader(key, label) : uni(key, label);
  Object.assign(c, logCodec(lo, hi), { name });
  if (fmt) c.text = (ctx) => fmt(eng(c, ctx));
  return c;
}
/* Integer read-out (native int wire — no codec needed). */
function pint(key, label, name, lo, hi) {
  const c = (lo < 0) ? oct(key, label, lo, hi) : count(key, label, lo, hi);
  c.name = name;
  return c;
}
/* Enum whose get_param returns the option LABEL. Writes stay the index —
 * dr32_params.c resolves both, and digits-first would mis-hit label "12dB". */
function penum(key, label, name, options, sq) {
  const c = enumc(key, label, options, sq);
  c.name = name;
  c.parse = (w) => {
    const i = options.indexOf(String(w == null ? "" : w).trim());
    if (i >= 0) return i;
    const n = parseInt(w, 10);
    return Number.isFinite(n) ? Math.max(0, Math.min(options.length - 1, n)) : 0;
  };
  /* Write the LABEL, not the index. dr32_efx_from_name() resolves names ONLY,
   * so an index write always resolved to Off and the send/insert type pickers
   * appeared stuck. filter_type and env_mode accept either, so labels are the
   * one rule that works for all three — and they round-trip exactly, since
   * that is what get_param returns. */
  c.format = (v) => options[Math.max(0, Math.min(options.length - 1, v | 0))];
  return c;
}

/* ---------- per-pad cells ---------------------------------------------- */

const kFilterTypes = ["Lowpass 12dB", "Lowpass", "Highpass", "Peak"];
const kFilterSq    = ["L12", "LP", "HP", "PK"];
const kEnvModes    = ["A-H-D", "A-S-R"];
const kEnvSq       = ["AHD", "ASR"];

/* Every per-pad cell, as builders taking the pad index. Keeping them in one
 * table is what makes dynamicCells, dynamicKeys and DEFAULTS agree by
 * construction rather than by hand. */
const PAD_BANK_SPECS = [
  /* No Note cell: pad notes are not a user setting. They are Move's drum-rack
   * layout (36-51 for the first 16, 52-67 for DR32's extra 16) and arrive with
   * the kit. NOT forced to 36+index, because real racks are addressed
   * positionally but ROUTED by note and are not always sorted — MD1Kit13 has
   * 46/47 swapped, and overriding it would mis-route that kit. */
  /* Start and Length lead so the waveform can own cells 0-1: the picture IS
   * the display for those two, and their labels still show the numbers. */
  { label: "Sample", icon: "pulse",
    cellViz: { cell: 0, span: 2, draw: drawWave },
    cells: [
    (p) => plin(`pad_start`, "Strt", "Start", 0, 1, (x) => Math.round(x * 100) + "%"),
    (p) => plin(`pad_length`, "Len", "Length", 0, 1, (x) => Math.round(x * 100) + "%"),
    (p) => pint(`pad_transpose`, "Trsp", "Transpose", -48, 48),
    (p) => plin(`pad_detune`, "Detn", "Detune", -50, 50, (x) => String(Math.round(x))),
    (p) => pint(`pad_choke`, "Chok", "Choke Group", 0, 16)
  ] },
  /* The envelope graphic spans attack/hold/decay. Both modes use those same
   * three params — A-H-D is a timed hold, A-S-R holds at full until note-off
   * and then releases — so one A-H-D shape reads correctly for both. */
  { label: "Amp", icon: "enva", env: { startCol: 0, cellCount: 3, roles: "ahd" }, cells: [
    (p) => plog(`pad_attack`, "Atk", "Attack", 0.0001, 20, fmtSec, true),
    (p) => plog(`pad_hold`, "Hold", "Hold", 0.001, 60, fmtSec, true),
    (p) => plog(`pad_decay`, "Dcy", "Decay", 0.001, 60, fmtSec, true),
    (p) => { const c = penum(`pad_env_mode`, "Env", "Envelope", kEnvModes, kEnvSq);
             c.widget = "enumsq"; return c; },
    (p) => plin(`pad_volume`, "Vol", "Volume", -36, 12, fmtDb),
    (p) => plin(`pad_pan`, "Pan", "Pan", -50, 50, fmtPan)
  ] },
  { label: "Filter", icon: "lp",
    /* By CELL INDEX, not key: every cell rebinds to the selected pad, so a key
     * here would only ever match pad 0. Modes follow kFilterTypes' order. */
    filterViz: { cell: 0, cutoffCell: 0, resoCell: 1,
                 mode: { cell: 2, modes: ["lp", "lp", "hp", "peak"] } },
    cells: [
    (p) => plog(`pad_cutoff`, "Cut", "Filter Freq", 30, 22000, fmtHz),
    (p) => plin(`pad_resonance`, "Res", "Filter Reso", 0, 0.9, (x) => x.toFixed(2)),
    (p) => penum(`pad_filter_type`, "Type", "Filter Type", kFilterTypes, kFilterSq),
    (p) => plin(`pad_send1`, "Snd1", "Send 1", -70, 6, fmtDb),
    (p) => plin(`pad_send2`, "Snd2", "Send 2", -70, 6, fmtDb)
  ] }
];

/* The focused pad lives in the DSP (kit->ui_current_pad), because only the DSP
 * sees pad hits — the host consumes them before the canvas MIDI dispatch, so
 * the canvas never receives a pad note at all (verified on device: jog arrives,
 * pads never do). */
function padOf(ctx) {
  /* Read through the cache — cheap, and refreshed by padPoll() below. */
  const p = parseInt(ctx.getParam("ui_current_pad"), 10) | 0;
  return p < 0 ? 0 : (p >= PAD_COUNT ? PAD_COUNT - 1 : p);
}

/* Focus is now owned by CONFIG.onMidi, which writes ui_current_pad the instant
 * a pad is hit — so there is nothing to poll for. The earlier version
 * force-refreshed ui_current_pad every third frame to catch the DSP moving it,
 * and that was the reported lag: a device getParam blocks ~2.6 ms, so the poll
 * alone cost ~40 ms/sec and still trailed the press by up to a frame.
 *
 * Kept as the single place that reads focus so a redraw stays consistent. */
function padPoll(ctx, s) {
  const cur = padOf(ctx);
  if (s.lastSeenPad !== cur) {
    s.lastSeenPad = cur;
    if (ctx._pcache) ctx._pcache = {};
    s.padMapUntil = (s.frames | 0) + 40;
  }
  return cur;
}

/* Header carries the target, because every cell is bound to it and getting
 * that wrong is silent: "PAD 07 - Kick01". */
function padHeader() {
  /* Deliberately WITHOUT the bank name: the section picker already says which
   * page you are on, and the header only fits ~17 glyphs — including it
   * truncated away the sample name, which is the part you actually need to
   * know the cells are pointed at the right pad. */
  return (ctx) => {
    const p = padOf(ctx);
    const path = ctx.getParam("pad_sample") || "";
    let nm = String(path).split("/").pop() || "empty";
    nm = nm.replace(/\.(wav|aif|aiff)$/i, "");
    return "Pad " + (p + 1) + ": " + nm;
  };
}

/* One static cell set. The DSP resolves the "pad_" alias to whichever pad has
 * focus, so nothing here rebinds and there is no per-pad cache to invalidate —
 * the previous version generated 576 keys and flushed the cache on every
 * selection change. */
const padBanks = PAD_BANK_SPECS.map((spec) => ({
  label: spec.label,
  env: spec.env,
  filterViz: spec.filterViz,
  cellViz: spec.cellViz,
  knobs: spec.cells.map((f) => f(0)),
  header: padHeader()
}));

/* ---------- sample waveform -------------------------------------------- */

/* The DSP publishes 128 min/max pairs for the focused pad as a flat CSV
 * (dr32_params.c, key "pad_waveform"); the canvas cannot read audio itself.
 * Re-fetched only when the pad or its sample changes — a device getParam is
 * ~2.6 ms blocking, so this must not run every frame. */
function wavePeaks(ctx, s) {
  const stamp = padOf(ctx) + "|" + (ctx.getParam("pad_sample") || "");
  if (s.waveStamp === stamp) return s.wavePeaks;
  s.waveStamp = stamp;
  const raw = ctx.getParam("pad_waveform") || "";
  if (!raw) { s.wavePeaks = null; return null; }
  const parts = raw.split(",");
  const out = [];
  for (let i = 0; i + 1 < parts.length; i += 2)
    out.push([(parseInt(parts[i], 10) || 0) / 100, (parseInt(parts[i + 1], 10) || 0) / 100]);
  s.wavePeaks = out.length ? out : null;
  return s.wavePeaks;
}

/* Waveform with the play region. Outside the region the wave is drawn as a
 * baseline only, so what will actually sound is obvious at a glance. */
function drawWave(ctx, g, cells, s) {
  const peaks = wavePeaks(ctx, s);
  const midY = g.y + (g.h >> 1);
  if (!peaks) {                       // empty pad: just the axis
    for (let x = g.x; x < g.x + g.w; x += 2) ctx.setPixel(x, midY, 1);
    return;
  }
  const st = normOf(ctx, cells[0]);                  // Start 0..1
  const ln = normOf(ctx, cells[1]);                  // Length 0..1
  const x0 = g.x + Math.round(st * g.w);
  const x1 = Math.min(g.x + g.w, x0 + Math.max(1, Math.round(ln * g.w)));
  const half = (g.h >> 1) - 1;
  for (let i = 0; i < g.w; i++) {
    const x = g.x + i;
    const p = peaks[Math.min(peaks.length - 1, Math.floor(i / g.w * peaks.length))];
    const inRegion = x >= x0 && x < x1;
    if (!inRegion) { ctx.setPixel(x, midY, 1); continue; }
    let top = midY - Math.round(p[1] * half);
    let bot = midY - Math.round(p[0] * half);
    if (bot < top) { const t = top; top = bot; bot = t; }
    if (top === bot) bot = top + 1;
    ctx.fillRect(x, top, 1, bot - top, 1);
  }
  /* Region edges: solid verticals, so a zero-length region is still visible. */
  ctx.fillRect(x0, g.y, 1, g.h, 1);
  ctx.fillRect(Math.max(g.x, x1 - 1), g.y, 1, g.h, 1);
}

/* ---------- send / insert FX ------------------------------------------- */

const kSendTypes   = ["Off", "Plate", "Spaces"];
const kSendSq      = ["OFF", "PLT", "SPC"];
const kInsertTypes = ["Off", "Drum Bus", "Plate", "Spaces"];
const kInsertSq    = ["OFF", "BUS", "PLT", "SPC"];

function sendBank(n) {
  const p = "send" + n + "_";
  return {
    label: "Send " + n,
    knobs: [
      penum(p + "type", "Type", "Send " + n + " Type", kSendTypes, kSendSq),
      plin(p + "size", "Size", "Size", 0, 1),
      plin(p + "damp", "Damp", "Damping", 0, 1),
      plin(p + "decay", "Dcy", "Decay", 0, 1),
      plin(p + "predelay", "Pre", "Pre-delay", 0, 1),
      plin(p + "return", "Ret", "Return", 0, 2)
    ],
    header: (ctx) => "Send " + n + ": " + (ctx.getParam(p + "type") || "Off")
  };
}

/* The middle four cells depend on the armed type: the reverbs expose
 * Size/Damp/Decay/Pre-delay, the Drum Bus exposes Compress/Crunch/Attack/
 * Sustain. Relabelling them here beats the hierarchy's visible_if, which
 * makes rows appear and vanish under the cursor. */
function insertBank(n) {
  const p = "insert" + n + "_";
  const typeCell = penum(p + "type", "Type", "Insert " + n + " Type", kInsertTypes, kInsertSq);
  const mixCell  = plin(p + "mix", "Mix", "Dry/Wet", 0, 1, (x) => Math.round(x * 100) + "%");
  const verbCells = [
    plin(p + "size", "Size", "Size", 0, 1),
    plin(p + "damp", "Damp", "Damping", 0, 1),
    plin(p + "decay", "Dcy", "Decay", 0, 1),
    plin(p + "predelay", "Pre", "Pre-delay", 0, 1)
  ];
  const busCells = [
    plin(p + "comp", "Comp", "Compress", 0, 1),
    plin(p + "crunch", "Crch", "Crunch", 0, 1),
    plin(p + "attack", "Atk", "Attack", 0, 1),
    plin(p + "sustain", "Sus", "Sustain", 0, 1)
  ];
  const isBus = (ctx) => String(ctx.getParam(p + "type") || "") === "Drum Bus";
  return {
    label: "Insert " + n,
    knobs: [typeCell].concat(verbCells, [mixCell]),
    dynamicCells: (ctx) => [typeCell].concat(isBus(ctx) ? busCells : verbCells, [mixCell]),
    dynamicKeys: busCells.map((c) => c.key),
    header: (ctx) => "Insert " + n + ": " + (ctx.getParam(p + "type") || "Off")
  };
}

const DR32_BANKS = padBanks.concat([sendBank(1), sendBank(2), insertBank(1), insertBank(2)]);

/* ---------- pad map overlay -------------------------------------------- */

/* A 4x8 map of the kit: selected pad inverted, loaded pads filled, empty
 * outlined. Shown while SHIFT is held and for a beat after the pad changes,
 * so a MIDI-driven jump is never silent. Pad 1 is bottom-left, matching the
 * hardware. */
function drawPadMap(ctx, cells, s) {
  /* Our own frame counter. The previous version compared against `s.frame`,
   * which the engine does not maintain — so it was always 0, always less than
   * the deadline, and the map sat on top of the parameters permanently. */
  s.frames = (s.frames | 0) + 1;
  const cur = padPoll(ctx, s);
  const showing = s.shift || (s.frames | 0) < (s.padMapUntil | 0);
  if (!showing) return;
  const W = 9, H = 7, GX = 2, GY = 2;
  const gw = 8 * W + 7 * GX, gh = 4 * H + 3 * GY;
  const x0 = ((ctx.width - gw) / 2) | 0, y0 = ((ctx.height - gh) / 2) | 0;
  ctx.fillRect(x0 - 4, y0 - 4, gw + 8, gh + 8, 0);
  ctx.drawRect(x0 - 4, y0 - 4, gw + 8, gh + 8, 1);
  const sel = cur;
  for (let i = 0; i < PAD_COUNT; i++) {
    const row = 3 - ((i >> 3) & 3), col = i & 7;
    const x = x0 + col * (W + GX), y = y0 + row * (H + GY);
    const loaded = String(ctx.getParam(`pad${i}_loaded`) || "0") === "1";
    if (i === sel) ctx.fillRect(x, y, W, H, 1);
    else if (loaded) ctx.fillRect(x + 2, y + 2, W - 4, H - 4, 1);
    else ctx.drawRect(x, y, W, H, 1);
  }
}

/* ---------- config ------------------------------------------------------ */

const CONFIG = {
  name: "DR32",
  banks: DR32_BANKS,

  /* Each pad page is its own picker row: that is where the editing happens,
   * so it should be one SHIFT+jog away, not buried behind bank stepping. */
  sections: [
    { name: "Pad Smpl", bank: 0 },
    { name: "Pad Amp", bank: 1 },
    { name: "Pad Filt", bank: 2 },
    { name: "SendFX", bank: 3 },
    { name: "InsertFX", bank: 5 }
  ],
  icons: ["pulse", "enva", "lp", "sine", "sine", "routes", "routes"],

  /* Edit focus follows the pad you physically hit.
   *
   * The host forwards raw hardware pad notes (68-99) to a canvas while one is
   * open — see MODULES.md, "Pad presses in a canvas UI". That is the ONLY
   * signal that separates a finger from the sequencer: measured on device, a
   * live hit and a sequenced note are identical by the time they reach the DSP
   * (same status, channel, note, and both tagged EXTERNAL). Owning focus here
   * rather than in the DSP is what stops playback from stealing the edit
   * target.
   *
   * Observation only — never sound these. The ordinary note is already on its
   * way to the synth; playing them too would double-trigger every pad. */
  onMidi: function (ctx, s, payload) {
    const d = payload && payload.data;
    if (!d || d.length < 3) return false;
    if ((d[0] & 0xF0) !== 0x90 || d[2] === 0) return false;
    const pad = d[1] - 68;
    if (pad < 0 || pad >= PAD_COUNT) return false;
    if (pad !== s.lastSeenPad) {
      /* Flush BEFORE the write: every cell addresses "pad_*", which now means
       * a different pad, and setParam write-through must survive the flush. */
      if (ctx._pcache) ctx._pcache = {};
      ctx.setParam("ui_current_pad", String(pad));
      s.lastSeenPad = pad;
      s.padMapUntil = (s.frames | 0) + 40;      /* flash the map */
    }
    return true;
  },

  overlays: [drawPadMap],

  /* Loading a kit rewrites all 32 pads and both FX chains; changing an FX type
   * reloads that slot's whole parameter set. Caching through either is the
   * classic display-desync. */
  writeInvalidates: (key) => {
    if (/^kit(_move|_user)?$/.test(key)) return true;
    if (/_sample$/.test(key)) return true;
    if (/^(send|insert)\d_type$/.test(key)) return true;
    return null;
  },

  defaults: (() => {
    const d = {};
    for (const b of DR32_BANKS) {
      for (const c of (b.knobs || [])) d[c.key] = 0;
      for (const k of (b.dynamicKeys || [])) d[k] = 0;
    }
    return d;
  })(),

  testExports: { kFilterTypes, kEnvModes, kInsertTypes, kSendTypes, PAD_COUNT, padOf }
};
