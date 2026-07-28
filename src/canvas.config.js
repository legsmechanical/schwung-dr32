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
   * so an index write always resolved to Off and the send type pickers
   * appeared stuck. filter_type and env_mode accept either, so labels are the
   * one rule that works for all three — and they round-trip exactly, since
   * that is what get_param returns. */
  c.format = (v) => options[Math.max(0, Math.min(options.length - 1, v | 0))];
  return c;
}

/* Folder-browse cell: position of this pad's sample among its neighbours.
 *
 * A plain count, deliberately NOT an enum. An enum would hand the list to the
 * kit's own picker overlay, which is sized and fonted for short enum labels —
 * filenames need a wider box and a proportional font. DR32 draws its own
 * instead (drawBrowsePicker below), which is also where a module-specific UI
 * belongs: the kit is a reference to build FROM, not a place to push one
 * module's needs into. Backport it later if it earns its way in.
 *
 * The face shows "3/17" — at ~16 px a name truncates to about three glyphs, so
 * position is the useful readout here and the picker carries the names. */
function browseCell() {
  const c = count(`pad_browse`, "Smpl", 0, 511);
  c.name = "Browse Folder";
  c.text = (ctx) => {
    const n = parseInt(ctx.getParam(`pad_browse_count`), 10) || 0;
    const i = parseInt(ctx.getParam(`pad_browse`), 10);
    /* -1 = empty pad, or a sample whose folder no longer lists it. */
    if (!n || !Number.isFinite(i) || i < 0) return "--";
    return (i + 1) + "/" + n;
  };
  return c;
}

/* ---------- browse picker overlay --------------------------------------- */

/* DR32's own scrolling list of the folder's samples, drawn while the Browse
 * knob is the one being touched. CONFIG.overlays runs after the kit's own
 * overlay pass, and the browse cell carries no `options`, so the kit's enum
 * picker never fires and there is nothing to draw over.
 *
 * Uses mvPrint — the movy font, the SAME one under every widget label, so the
 * picker reads as part of the page rather than a different control.
 *
 * The win is that it is PROPORTIONAL where the kit's 5x5 mcufont is a fixed
 * 6 px advance. Measured against this box's 112 px of text width:
 * "MD1_Kick_Sub_02 (alt)" is 96 px in movy and fits whole, but 125 px in
 * mcufont and gets cut. Typical text runs ~22 chars a row against ~18.
 *
 * ⚠ Both fonts are effectively CAPS ONLY — movy's lowercase codepoints map to
 * the same shapes (verified: "Kick" and "KICK" render identical pixels and
 * width), which is unavoidable at a 5 px cap height. Names therefore display
 * uppercase whichever font is used; do not pick one expecting mixed case. */
const PICK_X = 2, PICK_Y = 10, PICK_W = 124, PICK_H = 54;
const PICK_ROW_H = 7;                    /* 5px movy glyph + 2px leading */

function drawBrowsePicker(ctx, cells, s) {
  const k = s.lastKnob;
  const cell = k >= 0 ? cells[k] : null;
  if (!cell || cell.key !== `pad_browse`) return;

  const raw = String(ctx.getParam(`pad_browse_names`) || "");
  if (!raw) return;                                  /* empty pad: nothing to show */
  const names = raw.split("\n");
  const n = names.length;
  const sel = parseInt(ctx.getParam(`pad_browse`), 10);
  if (!n || !Number.isFinite(sel) || sel < 0) return;

  const X = PICK_X, Y = PICK_Y, W = PICK_W, H = PICK_H;
  ctx.fillRect(X, Y, W, H, 0);
  ctx.drawRect(X, Y, W, H, 1);

  const visible = Math.max(1, Math.min(n, Math.floor((H - 4) / PICK_ROW_H)));
  const hasScroll = n > visible;
  /* Keep the selection mid-list so there is context either side, clamping at
   * the ends rather than letting the window run past them. */
  const start = Math.max(0, Math.min(sel - Math.floor(visible / 2), n - visible));
  const listTop = Y + Math.floor((H - visible * PICK_ROW_H) / 2);
  const rowX = X + 2, rowW = W - 4 - (hasScroll ? 4 : 0);
  const availW = rowW - 4;

  for (let i = 0; i < visible; i++) {
    const idx = start + i;
    if (idx >= n) break;
    const y = listTop + i * PICK_ROW_H;
    let label = String(names[idx]);
    while (label.length > 1 && mvWidth(label) > availW) label = label.slice(0, -1);
    if (idx === sel) {
      ctx.fillRect(rowX, y, rowW, PICK_ROW_H, 1);
      mvPrint(ctx, rowX + 2, y + 1, label, 0);
    } else {
      mvPrint(ctx, rowX + 2, y + 1, label, 1);
    }
  }

  if (hasScroll) {
    const trackH = visible * PICK_ROW_H;
    const thumbH = Math.max(3, Math.round(trackH * visible / n));
    const thumbY = listTop + Math.round((trackH - thumbH) * start / Math.max(1, n - visible));
    ctx.fillRect(X + W - 2, listTop, 1, trackH, 1);
    ctx.fillRect(X + W - 3, thumbY, 2, thumbH, 1);
  }
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
    (p) => pint(`pad_choke`, "Chok", "Choke Group", 0, 16),
    /* Two placeholders so Browse lands on KNOB 8 (Josh) — the far right, away
     * from the five editing knobs, which is where a control that swaps the
     * sample out from under them belongs. blank() is keyless and draws
     * nothing; it exists to hold a knob position. */
    (p) => blank(),
    (p) => blank(),
    /* Walk the samples sitting NEXT TO this pad's own, without opening the
     * browser — the fastest way to try a different kick. Loads as you turn, so
     * it auditions rather than just selects.
     *
     * The 0..511 bound is the cell's, not the folder's — the DSP clamps to the
     * real count (which it alone knows) and reports the position back, so the
     * knob simply stops at the end of the folder. The readout is "3/17" because
     * the NAME is already in the bank header; an empty pad has no folder and
     * shows "--". */
    (p) => browseCell()
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

/* The focused pad lives in the DSP (kit->ui_current_pad) because every cell
 * addresses it through the "pad_" alias, but the canvas is what WRITES it (see
 * CONFIG.onMidi). The host forwards raw hardware pad notes 68-99 to an open
 * canvas — this file's earlier claim that "the canvas never receives a pad note
 * at all" was true only of the host before that landed. */
function padOf(ctx) {
  /* Read through the cache; CONFIG.onMidi flushes it when focus moves. */
  const p = parseInt(ctx.getParam("ui_current_pad"), 10) | 0;
  return p < 0 ? 0 : (p >= PAD_COUNT ? PAD_COUNT - 1 : p);
}

/* Focus is owned entirely by CONFIG.onMidi, which writes ui_current_pad the
 * instant a pad is hit, so nothing polls for it. Two earlier attempts are worth
 * not repeating: force-refreshing ui_current_pad every third frame to catch the
 * DSP moving focus was the reported lag (a device getParam blocks ~2.6 ms, so
 * the poll alone cost ~40 ms/sec and still trailed the press by up to a frame),
 * and the reconciling padPoll() that replaced it became dead weight once the
 * DSP stopped moving focus at all — onMidi is the only writer. */

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
/* ⚠ Every field a spec may carry must be forwarded here. The engine reads the
 * BANK, not the spec, so a field added above and not copied below is silently
 * ignored — no error, the feature just never happens. dynamicCells was exactly
 * that: added for the browse picker and dropped on the floor until this list
 * grew to match. */
const padBanks = PAD_BANK_SPECS.map((spec) => ({
  label: spec.label,
  env: spec.env,
  filterViz: spec.filterViz,
  cellViz: spec.cellViz,
  dynamicCells: spec.dynamicCells,
  dynamicKeys: spec.dynamicKeys,
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

/* ---------- send FX ------------------------------------------------------ */

const kSendTypes   = ["Off", "Plate", "Spaces", "Delay", "Gated", "Digital", "Hall", "NonLin"];
const kSendSq      = ["OFF", "PLT", "SPC", "DLY", "GAT", "DIG", "HAL", "NLN"];
/* Tempo-synced or free-running, for the Delay. Both times are stored either
 * way, so flipping this recalls what you last had in that mode rather than
 * reinterpreting one number in the wrong unit — which is what the native
 * device does (its Chicago Kit sits unsynced while still carrying its
 * SyncedSixteenth values). */
const kSyncModes   = ["Sync", "Free"];
const kSyncSq      = ["SYN", "FRE"];

/* The reverbs and the Delay share the same five generic slots in the DSP but
 * mean completely different things by them, so the PAGE has to change with the
 * armed type — a Delay showing "Size / Damp / Dcy / Pre" is just mislabelled.
 * `dynamicCells` is the kit's supported way to do that; the alternative of one
 * union page listing nine cells would leave five of them inert at all times. */
function sendBank(n) {
  const p = "send" + n + "_";
  const type = penum(p + "type", "Type", "Send " + n + " Type", kSendTypes, kSendSq);
  const ret  = plin(p + "return", "Ret", "Return", 0, 2);

  const verb = [
    type,
    plin(p + "size", "Size", "Size", 0, 1),
    plin(p + "damp", "Damp", "Damping", 0, 1),
    plin(p + "decay", "Dcy", "Decay", 0, 1),
    plin(p + "predelay", "Pre", "Pre-delay", 0, 1),
    ret
  ];
  /* The gated reverb reads the decay slot as the gate's HOLD time (50..500 ms
   * inside SpaceExtra), so the cell says so. Same underlying param — the DSP
   * takes `hold` as an alias for slot 2 — but a cell labelled "Dcy" on a gate
   * would be describing the wrong control. */
  const gate = [
    type,
    plin(p + "size", "Size", "Size", 0, 1),
    plin(p + "damp", "Damp", "Damping", 0, 1),
    plin(p + "hold", "Hold", "Gate Hold", 0, 1),
    /* The TANK's own decay. Slot 2 is the gate's hold, so this cannot also be
     * called Decay — a gated reverb has two lengths and conflating them is what
     * made this type sound like NonLin. */
    plin(p + "tail", "Tail", "Tail Decay", 0, 1),
    plin(p + "release", "Rel", "Release", 0, 1),
    plin(p + "predelay", "Pre", "Pre-delay", 0, 1),
    ret
  ];
  /* NonLin has no decay at all — slot 2 is the window LENGTH and slot 4 its
   * SHAPE (falling / flat / rising), which no other type uses. */
  const nonlin = [
    type,
    plin(p + "size", "Size", "Size", 0, 1),
    plin(p + "damp", "Damp", "Damping", 0, 1),
    plin(p + "length", "Len", "Length", 0, 1),
    plin(p + "shape", "Shape", "Shape", 0, 1),
    plin(p + "release", "Rel", "Release", 0, 1),
    plin(p + "predelay", "Pre", "Pre-delay", 0, 1),
    ret
  ];

  /* Sync vs free is a THIRD page, not a variant of the delay page: the two time
   * controls change unit with it, and one cell cannot honestly be both a count
   * of sixteenths and a millisecond value.
   *
   * Both pages label the two time cells just "L" and "R" (Josh, 2026-07-28).
   * The cell already sits under a TIME mode cell that says SYN or FRE, and the
   * value itself reads "4" or "250MS", so spelling the mode into the label too
   * ("TIML" vs "MSL") repeated what two neighbours were already saying and made
   * the pair harder to scan, not easier. */
  const sync = penum(p + "sync", "Time", "Time Mode", kSyncModes, kSyncSq);
  /* A 2-option enum defaults to the hbar toggle, which draws as an unlabelled
   * empty/filled bar — fine for on/off, useless here, because "filled" does not
   * say whether that means Sync or Free. The kit's own note on widgetFor()
   * calls out this case: a 2-way MODE is a choice you read, not a switch you
   * flip, so it asks for the labelled square. */
  sync.widget = "enumsq";
  const tail = [
    plin(p + "feedback", "FB", "Feedback", 0, 0.95),
    plin(p + "tone", "Tone", "Tone", 0, 1),
    plin(p + "pingpong", "PP", "Ping-Pong", 0, 1),
    ret
  ];
  /* ⚠ Time L/R are a COUNT OF SIXTEENTHS (1..16), not 0..1 — Move's own delay
   * device syncs in whole sixteenths, and the DSP stores exactly that. Two
   * traps in one cell: a 0..1 codec would send 1/16 of a sixteenth, and a
   * continuous cell would send 3.24 sixteenths, which is a synced control
   * landing off the grid. `pint` is the integer wire, which is both. */
  const dlySync = [
    type, sync,
    pint(p + "time_l", "L", "Time L", 1, 16),
    pint(p + "time_r", "R", "Time R", 1, 16)
  ].concat(tail);
  /* Free time is LOGARITHMIC: 10 ms to 2 s linearly would spend most of the
   * knob above half a second, where a drum delay rarely lives. */
  const msFmt = (x) => Math.round(x) + "ms";
  const dlyFree = [
    type, sync,
    plog(p + "ms_l", "L", "Time L", 10, 2000, msFmt),
    plog(p + "ms_r", "R", "Time R", 10, 2000, msFmt)
  ].concat(tail);

  const cellsFor = (ctx) => {
    const t = ctx.getParam(p + "type");
    if (t === "Gated") return gate;
    if (t === "NonLin") return nonlin;
    if (t !== "Delay") return verb;
    return ctx.getParam(p + "sync") === "Free" ? dlyFree : dlySync;
  };

  return {
    label: "Send " + n,
    knobs: verb,
    dynamicCells: cellsFor,
    /* Every key any of the three pages can address, so the defaults table
     * covers them all — a cell whose key is missing from DEFAULTS reads as 0 on
     * first paint. */
    dynamicKeys: [p + "type", p + "size", p + "damp", p + "decay", p + "hold",
                  p + "length", p + "shape", p + "tail", p + "release",
                  p + "predelay", p + "sync", p + "time_l", p + "time_r",
                  p + "ms_l", p + "ms_r", p + "feedback", p + "tone",
                  p + "pingpong", p + "return"],
    header: (ctx) => "Send " + n + ": " + (ctx.getParam(p + "type") || "Off")
  };
}

/* The Drum Bus is NOT a send and not selectable — it is a fixed stage at the end
 * of the kit's chain, after both send returns are summed (Josh, 2026-07-28). It
 * has a page because its four controls are still worth having; it has no Type
 * cell because there is nothing to choose. Everything starts neutral, where the
 * stage is bypassed outright. */
function busBank() {
  return {
    label: "Drum Bus",
    knobs: [
      plin("bus_comp", "Comp", "Compress", 0, 1),
      plin("bus_crunch", "Crnch", "Crunch", 0, 1),
      /* Attack and Sustain are BIPOLAR: down softens/shortens, up sharpens/
       * lengthens, and the middle is neutral. plin picks the bipolar cell off
       * the sign of the range, so -1..1 is what makes them draw centred —
       * as 0..1 they looked like ordinary unipolar knobs whose "off" position
       * was somewhere in the middle (Josh, 2026-07-28). */
      plin("bus_attack", "Atk", "Attack", -1, 1),
      plin("bus_sustain", "Sus", "Sustain", -1, 1),
      /* Dry/wet, i.e. parallel compression. This was on the Drum Bus when it
       * was a selectable insert and went missing when the stage was lifted onto
       * the master mix. */
      plin("bus_mix", "Mix", "Dry/Wet", 0, 1, (x) => Math.round(x * 100) + "%")
    ],
    header: () => "Drum Bus"
  };
}

/* No insert banks. DR32's kit-level inserts were removed (Josh, 2026-07-27): a
 * Schwung chain slot already carries its own insert FX in front of the output,
 * so a kit insert duplicated a facility the host provides — and only DR32 could
 * reach or persist it. The Drum Bus above is the one fixed stage, and it is not
 * a selectable insert either. */
const DR32_BANKS = padBanks.concat([sendBank(1), sendBank(2), busBank()]);

/* There is deliberately NO pad-map overlay. A 4x8 map of the kit used to be
 * drawn over the parameters whenever focus moved, because focus could move
 * without the player having touched anything and a silent jump would have been
 * confusing. Now that focus only ever follows a pad you physically pressed —
 * including under co-run, where the tool keeps the pad and the canvas merely
 * observes it — the map only ever confirmed what your own finger just did,
 * while covering the parameters you were reading. Removed 2026-07-27. */

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
    { name: "Drum Bus", bank: 5 }
  ],
  /* ⚠ Indexed by BANK, not by section (engine.js: BANK_ICONS[items[i].bank]).
   * Bank 4 is Send 2, which has no picker row of its own — you jog to it from
   * Send 1 — but its slot in this array still has to be filled or the Drum Bus
   * at bank 5 would pick up Send 2's icon. */
  icons: ["pulse", "enva", "lp", "sine", "sine", "env"],

  /* Edit focus follows the pad you physically hit.
   *
   * This handler contributes exactly ONE bit: "that was a finger, not the
   * sequencer." It deliberately does NOT decide which pad — the note does, and
   * the DSP already owns the note -> pad map, so no pad geometry lives here.
   *
   * That split is the whole design, and getting it wrong cost two rounds. The
   * host forwards raw hardware pad notes (68-99) to an open canvas (MODULES.md,
   * "Pad presses in a canvas UI"); that is the ONLY signal separating a live hit
   * from a sequenced one, since by the time a note reaches the DSP the two are
   * identical — same status, channel, note, both tagged EXTERNAL (measured on
   * device). But the grid note identifies a POSITION, not a pad, and position
   * is not pad: only the right 4x4 plays, and davebox transposes it up 16 notes
   * to reach pads 17-32 while sending the identical grid note. An earlier
   * version derived the pad from the grid note and could therefore only ever
   * address 1-16, with the rows mis-strided on top of that.
   *
   * So: signal liveness, let the note say which. A press on the dead left 4x4
   * arms nothing in practice — no note follows it, so the arm simply expires.
   *
   * Observation only — never sound these. The ordinary note is already on its
   * way to the synth; playing them too would double-trigger every pad. */
  onMidi: function (ctx, s, payload) {
    const d = payload && payload.data;
    if (!d || d.length < 3) return false;
    if ((d[0] & 0xF0) !== 0x90 || d[2] === 0) return false;
    if (d[1] < PAD_NOTE_LO || d[1] > PAD_NOTE_HI) return false;
    /* Flush BEFORE arming: every cell addresses "pad_*", which is about to mean
     * a different pad, and the cache must not outlive the change. */
    if (ctx._pcache) ctx._pcache = {};
    ctx.setParam("ui_live_press", "1");
    return true;
  },

  /* Loading a kit rewrites all 32 pads and both FX chains; changing an FX type
   * reloads that slot's whole parameter set. Caching through either is the
   * classic display-desync. */
  overlays: [drawBrowsePicker],

  writeInvalidates: (key) => {
    if (/^kit(_move|_user)?$/.test(key)) return true;
    if (/_sample$/.test(key)) return true;
    /* A browse step loads a new sample, so anything describing the SAMPLE is
     * stale — but the folder has not changed, so the name list must survive.
     * Returning true here would flush it and refetch several KB every detent. */
    if (/^pad\d*_browse$/.test(key))
      return ["pad_browse", "pad_sample", "pad_waveform", "pad_loaded", "pad_frames",
              key, "pad_browse_count"];
    /* Both of these swap the whole visible cell set — the type between reverb
     * and delay, sync between the sixteenth and millisecond time pages — so a
     * cache that survived either would be describing cells that are gone. */
    if (/^send\d_(type|sync)$/.test(key)) return true;
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

  testExports: { kFilterTypes, kEnvModes, kSendTypes, PAD_COUNT, padOf }
};
