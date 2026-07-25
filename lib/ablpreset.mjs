// ablpreset.mjs — reader/writer for Move `.ablpreset` drum-rack files.
//
// CANONICAL HOME: schwung-dr32. davebox vendors a COPY of this file; it is not a
// live dependency. Do not add back-compat shims here for a stale copy elsewhere —
// re-vendor instead.
//
// Design rule: LOSSLESS. parseKit() keeps the entire original document in `_raw`,
// and serializeKit() overlays only the fields we model back onto it. Unmodelled
// params (rack FX, macro mappings, future schema fields) survive a save untouched.
//
// Format facts verified against real device presets on 2026-07-25 —
// see docs/specs/2026-07-25-dr32-design.md §2.

export const USER_LIBRARY = '/data/UserData/UserLibrary';
const URI_PREFIX = 'ableton:/user-library';

/** Effect_Type enum values, exactly as they appear in the JSON (note the
 *  lowercase 'b' in '8-bit' — observed across 74 cells in the device corpus). */
export const EFFECT_TYPES = [
  'Stretch', 'Loop', 'Pitch Env', 'Punch', '8-bit',
  'FM', 'Ring Mod', 'Sub Osc', 'Noise',
];

/** The two params each effect owns, in encoder order (Main Bank slots 7 and 8). */
export const EFFECT_PARAMS = {
  'Stretch':   ['Effect_StretchFactor', 'Effect_StretchGrainSize'],
  'Loop':      ['Effect_LoopOffset', 'Effect_LoopLength'],
  'Pitch Env': ['Effect_PitchEnvelopeAmount', 'Effect_PitchEnvelopeDecay'],
  'Punch':     ['Effect_PunchAmount', 'Effect_PunchTime'],
  '8-bit':     ['Effect_EightBitResamplingRate', 'Effect_EightBitFilterDecay'],
  'FM':        ['Effect_FmAmount', 'Effect_FmFrequency'],
  'Ring Mod':  ['Effect_RingModAmount', 'Effect_RingModFrequency'],
  'Sub Osc':   ['Effect_SubOscAmount', 'Effect_SubOscFrequency'],
  'Noise':     ['Effect_NoiseAmount', 'Effect_NoiseFrequency'],
};

/** Envelope modes. 'A-H-D' = manual's "Trigger", 'A-S-R' = "Gate".
 *  Verified: extending-move's Gate Kit example uses A-S-R. */
export const ENVELOPE_MODES = ['A-H-D', 'A-S-R'];

/** Voice_Filter_Type enum. Measured 2026-07-25 by crafting each type on the
 *  device and diffing the saved JSON — the corpus is 100% 'Lowpass', and the
 *  binary only carries UI labels, so this could not be derived any other way.
 *  Note the asymmetry: 24 dB is the UNSUFFIXED default for both LP and HP,
 *  while 12 dB LP carries an explicit suffix. Don't "normalize" these strings. */
export const FILTER_TYPES = {
  'Lowpass': { mode: 'lp', slope: 24 },        // device UI: "Low-Pass 24dB"
  'Lowpass 12dB': { mode: 'lp', slope: 12 },   // device UI: "Low-Pass 12dB"
  'Highpass': { mode: 'hp', slope: 24 },       // device UI: "High-Pass 24dB"
  'Peak': { mode: 'peak', slope: 0 },          // uses Voice_Filter_PeakGain
};

/** Hold sentinel: 60.0 s is the "inf" (play whole sample) value.
 *  Verified: the Choke Kit example and the 138-kit corpus both top out at exactly 60. */
export const HOLD_INFINITE = 60.0;

/** Defaults for params that are legitimately ABSENT in some cells.
 *  Voice_PitchToEnvelopeModulation is missing from 896 of 2208 corpus cells —
 *  a parser that assumes every key is present will throw on real files. */
const CELL_DEFAULTS = {
  Voice_PitchToEnvelopeModulation: false,
  NotePitchBend: true,
  Enabled: true,
  Effect_On: true,
  Pan: 0.0,
  Volume: 0.0,
};

// -------------------------------------------------- float-faithful JSON
//
// Ableton writes `0.0`; JSON.stringify writes `0`. Ableton also writes
// `999.9998168945313` where JS's shortest round-trip repr of the very same
// float64 is `999.9998168945312`. Same JSON values, different bytes — and we
// are overwriting the user's own preset files, so "probably equivalent" is not
// good enough.
//
// So parse records the ORIGINAL LITERAL for every number, keyed by JSON-pointer
// path. On write, an untouched value is re-emitted verbatim; a value we actually
// changed is emitted fresh, with `.0` appended if the original was a float
// (so an edited param still looks like Ableton wrote it).
//
// Paths (not object references) are used deliberately: the serializer rebuilds
// objects via spread, which would drop any marker property.

const ESC = (k) => String(k).replace(/~/g, '~0').replace(/\//g, '~1');

/** Minimal JSON scanner: returns [value, literals]. Only exists to capture the
 *  exact numeric spelling, which JSON.parse discards. */
export function parseJsonFaithful(text) {
  let i = 0;
  const lits = new Map();
  const ws = () => { while (i < text.length && ' \t\n\r'.includes(text[i])) i++; };

  function value(path) {
    ws();
    const c = text[i];
    if (c === '{') return object(path);
    if (c === '[') return array(path);
    if (c === '"') return string();
    if (text.startsWith('true', i)) { i += 4; return true; }
    if (text.startsWith('false', i)) { i += 5; return false; }
    if (text.startsWith('null', i)) { i += 4; return null; }
    return number(path);
  }
  function object(path) {
    const o = {}; i++; ws();
    if (text[i] === '}') { i++; return o; }
    for (;;) {
      ws();
      const k = string(); ws(); i++;              // skip ':'
      o[k] = value(path + '/' + ESC(k));
      ws();
      if (text[i] === ',') { i++; continue; }
      i++; return o;                              // '}'
    }
  }
  function array(path) {
    const a = []; i++; ws();
    if (text[i] === ']') { i++; return a; }
    for (let n = 0; ; n++) {
      a.push(value(path + '/' + n));
      ws();
      if (text[i] === ',') { i++; continue; }
      i++; return a;                              // ']'
    }
  }
  function string() {
    const start = i; i++;
    while (i < text.length) {
      if (text[i] === '\\') { i += 2; continue; }
      if (text[i] === '"') { i++; break; }
      i++;
    }
    return JSON.parse(text.slice(start, i));
  }
  function number(path) {
    const start = i;
    while (i < text.length && !',}] \t\n\r'.includes(text[i])) i++;
    const lit = text.slice(start, i);
    lits.set(path, lit);
    return Number(lit);
  }

  const v = value('');
  return [v, lits];
}

/** JSON.stringify with original numeric spelling restored from `lits`
 *  (a Map of JSON-pointer path -> source literal). */
export function stringifyFaithful(v, lits, indent = 1) {
  const pad = (n) => ' '.repeat(indent * n);
  function emit(x, path, depth) {
    if (x === null) return 'null';
    if (typeof x === 'boolean') return String(x);
    if (typeof x === 'number') {
      const lit = lits.get(path);
      if (lit !== undefined && Number(lit) === x) return lit;   // untouched: verbatim
      const s = String(x);                                      // edited: fresh
      const wasFloat = lit !== undefined && /[.eE]/.test(lit);
      return wasFloat && !/[.eE]/.test(s) ? s + '.0' : s;
    }
    if (typeof x === 'string') return JSON.stringify(x);
    if (Array.isArray(x)) {
      if (!x.length) return '[]';
      const items = x.map((e, n) => pad(depth + 1) + emit(e, path + '/' + n, depth + 1));
      return '[\n' + items.join(',\n') + '\n' + pad(depth) + ']';
    }
    const keys = Object.keys(x);
    if (!keys.length) return '{}';
    const items = keys.map((k) =>
      pad(depth + 1) + JSON.stringify(k) + ': ' + emit(x[k], path + '/' + ESC(k), depth + 1));
    return '{\n' + items.join(',\n') + '\n' + pad(depth) + '}';
  }
  return emit(v, '', 0);
}

// ---------------------------------------------------------------- URIs

/** `ableton:/user-library/Samples/Preset%20Samples/x.wav` -> absolute path. */
export function resolveUri(uri) {
  if (!uri) return null;
  if (!uri.startsWith(URI_PREFIX)) return null;   // packs/other roots: unresolved
  return USER_LIBRARY + decodeURIComponent(uri.slice(URI_PREFIX.length));
}

/** Absolute path -> `ableton:` URI. Inverse of resolveUri for library paths. */
export function toUri(path) {
  if (!path || !path.startsWith(USER_LIBRARY)) return null;
  const rel = path.slice(USER_LIBRARY.length);
  // Encode each segment; keep the separators.
  return URI_PREFIX + rel.split('/').map(encodeURIComponent).join('/');
}

// ---------------------------------------------------------------- parse

/** Cheap content check for the kit browser — the movy/mrdrums lesson: filter
 *  before loading, never crash on the wrong file type. */
export function isDrumKit(jsonText) {
  return typeof jsonText === 'string' && jsonText.includes('"drumRack"');
}

function findDevice(node, kind, out = []) {
  if (Array.isArray(node)) {
    for (const v of node) findDevice(v, kind, out);
  } else if (node && typeof node === 'object') {
    if (node.kind === kind) out.push(node);
    for (const k in node) findDevice(node[k], kind, out);
  }
  return out;
}

/**
 * parseKit(jsonText) -> kit
 *
 * kit = { name, pads[], rackFx, sendFx, _raw, _rack }
 * pad = { index, note, sendingNote, chokeGroup, color, name,
 *         mixer:{volume,pan,send}, sampleUri, samplePath, params, _chain }
 *
 * `index` is positional in the file; `note` is authoritative for routing.
 * Chains are NOT sorted by note in real files (MD1Kit13 has 46/47 swapped),
 * so never infer the note from the position.
 */
export function parseKit(jsonText) {
  let raw, floats;
  if (typeof jsonText === 'string') [raw, floats] = parseJsonFaithful(jsonText);
  else { raw = jsonText; floats = new Set(); }
  const rack = findDevice(raw, 'drumRack')[0];
  if (!rack) throw new Error('not a drum rack preset');

  const pads = (rack.chains || []).map((chain, index) => {
    const cell = (chain.devices || []).find((d) => d.kind === 'drumCell') || null;
    const zone = chain.drumZoneSettings || {};
    const mixer = chain.mixer || {};
    const uri = cell && cell.deviceData ? cell.deviceData.sampleUri : null;
    const params = cell ? { ...CELL_DEFAULTS, ...cell.parameters } : { ...CELL_DEFAULTS };
    return {
      index,
      note: zone.receivingNote ?? (36 + index),
      sendingNote: zone.sendingNote ?? 60,
      chokeGroup: zone.chokeGroup ?? null,
      color: chain.color ?? 0,
      name: (cell && cell.name) || chain.name || '',
      mixer: {
        volume: mixer.volume ?? 0,            // dB
        pan: mixer.pan ?? 0,                  // -1..1
        send: mixer.sends && mixer.sends[0] ? mixer.sends[0].amount : -70,
      },
      sampleUri: uri || null,                 // null = empty pad (112 in corpus)
      samplePath: resolveUri(uri),
      params,
      _chain: chain,
      _cell: cell,
      // Which param keys the file actually carried. CELL_DEFAULTS fills gaps for
      // consumers, but a default must never be written back as a NEW key — that
      // would silently rewrite 56 of the 140 corpus kits.
      _origKeys: new Set(cell ? Object.keys(cell.parameters) : []),
    };
  });

  // The rack-wide insert FX sits beside the drumRack in the same chain; the send
  // FX lives in returnChains. Both are preserved verbatim and inert (phase 2).
  const hostChain = (raw.chains || [])[0] || {};
  const rackFx = (hostChain.devices || []).filter((d) => d.kind !== 'drumRack');
  const sendFx = rack.returnChains || [];

  return { name: raw.name || '', pads, rackFx, sendFx, _raw: raw, _rack: rack, _floats: floats };
}

// ------------------------------------------------------------ serialize

/** Deep-clone a chain so a new pad doesn't alias an existing one. */
function blankChainFrom(template, note) {
  const c = JSON.parse(JSON.stringify(template));
  c.drumZoneSettings = { receivingNote: note, sendingNote: 60, chokeGroup: null };
  const cell = (c.devices || []).find((d) => d.kind === 'drumCell');
  if (cell) cell.deviceData = { sampleUri: null };
  return c;
}

/**
 * serializeKit(kit, {indent}) -> jsonText
 *
 * Write-through: mutates `_raw` in place with only the modelled fields, so every
 * unknown key survives. Pads beyond the original chain count are appended by
 * cloning chain 0 — PROBE32 confirmed native Move loads a 32-chain file fine and
 * simply ignores chains past 16, so this stays native-legal.
 */
export function serializeKit(kit, { indent = 1 } = {}) {
  const rack = kit._rack;
  const template = rack.chains[0];

  rack.chains = kit.pads.map((pad) => {
    const chain = pad._chain || blankChainFrom(template, pad.note);
    chain.drumZoneSettings = {
      ...(chain.drumZoneSettings || {}),
      receivingNote: pad.note,
      sendingNote: pad.sendingNote,
      chokeGroup: pad.chokeGroup,
    };
    if (pad.color !== undefined) chain.color = pad.color;
    chain.mixer = { ...(chain.mixer || {}) };
    chain.mixer.volume = pad.mixer.volume;
    chain.mixer.pan = pad.mixer.pan;
    if (chain.mixer.sends && chain.mixer.sends[0]) {
      chain.mixer.sends[0].amount = pad.mixer.send;
    }
    const cell = pad._cell || (chain.devices || []).find((d) => d.kind === 'drumCell');
    if (cell) {
      // Overlay, never replace: unknown params ride along untouched. Keys the
      // file didn't have are only introduced if the value actually differs from
      // the default we invented for them.
      const orig = pad._origKeys || new Set(Object.keys(pad.params));
      const next = { ...cell.parameters };
      for (const k in pad.params) {
        if (orig.has(k) || pad.params[k] !== CELL_DEFAULTS[k]) next[k] = pad.params[k];
      }
      cell.parameters = next;
      cell.deviceData = { ...(cell.deviceData || {}), sampleUri: pad.sampleUri };
    }
    return chain;
  });

  if (kit.name) kit._raw.name = kit.name;
  return stringifyFaithful(kit._raw, kit._floats || new Map(), indent);
}

/** Convenience: the two encoder params for a pad's active effect. */
export function effectParamsFor(pad) {
  return EFFECT_PARAMS[pad.params.Effect_Type] || [];
}
