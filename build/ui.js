// src/ui.js
import { createSoundGeneratorUI } from "/data/UserData/schwung/shared/sound_generator_ui.mjs";

// src/ablpreset.mjs
var USER_LIBRARY = "/data/UserData/UserLibrary";
var CORE_LIBRARY = "/data/CoreLibrary";
var URI_ROOTS = [
  ["ableton:/user-library", USER_LIBRARY],
  ["ableton:/packs/abl-core-library", CORE_LIBRARY]
];
var FILTER_TYPES = {
  // `engine` is the native filter_type index used in the 320-kernel
  // specialization (DRUM_FILTER_RECON.md): 0 LP12, 1 LP24, 2 HP24, 3 Peak.
  // Note "Lowpass" — the JSON default — is the 24 dB slope, not 12.
  "Lowpass": { mode: "lp", slope: 24, engine: 1 },
  "Lowpass 12dB": { mode: "lp", slope: 12, engine: 0 },
  "Highpass": { mode: "hp", slope: 24, engine: 2 },
  "Peak": { mode: "peak", slope: 0, engine: 3 }
};
var CELL_DEFAULTS = {
  Voice_PitchToEnvelopeModulation: false,
  NotePitchBend: true,
  Enabled: true,
  Effect_On: true,
  Pan: 0,
  Volume: 0
};
var ESC = (k) => String(k).replace(/~/g, "~0").replace(/\//g, "~1");
function parseJsonFaithful(text) {
  let i = 0;
  const lits = /* @__PURE__ */ new Map();
  const ws = () => {
    while (i < text.length && " 	\n\r".includes(text[i])) i++;
  };
  function value(path) {
    ws();
    const c = text[i];
    if (c === "{") return object(path);
    if (c === "[") return array(path);
    if (c === '"') return string();
    if (text.startsWith("true", i)) {
      i += 4;
      return true;
    }
    if (text.startsWith("false", i)) {
      i += 5;
      return false;
    }
    if (text.startsWith("null", i)) {
      i += 4;
      return null;
    }
    return number(path);
  }
  function object(path) {
    const o = {};
    i++;
    ws();
    if (text[i] === "}") {
      i++;
      return o;
    }
    for (; ; ) {
      ws();
      const k = string();
      ws();
      i++;
      o[k] = value(path + "/" + ESC(k));
      ws();
      if (text[i] === ",") {
        i++;
        continue;
      }
      i++;
      return o;
    }
  }
  function array(path) {
    const a = [];
    i++;
    ws();
    if (text[i] === "]") {
      i++;
      return a;
    }
    for (let n = 0; ; n++) {
      a.push(value(path + "/" + n));
      ws();
      if (text[i] === ",") {
        i++;
        continue;
      }
      i++;
      return a;
    }
  }
  function string() {
    const start = i;
    i++;
    while (i < text.length) {
      if (text[i] === "\\") {
        i += 2;
        continue;
      }
      if (text[i] === '"') {
        i++;
        break;
      }
      i++;
    }
    return JSON.parse(text.slice(start, i));
  }
  function number(path) {
    const start = i;
    while (i < text.length && !",}] 	\n\r".includes(text[i])) i++;
    const lit = text.slice(start, i);
    lits.set(path, lit);
    return Number(lit);
  }
  const v = value("");
  return [v, lits];
}
function resolveUri(uri, roots = URI_ROOTS) {
  if (!uri) return null;
  for (const [prefix, dir] of roots) {
    if (uri.startsWith(prefix)) return dir + decodeURIComponent(uri.slice(prefix.length));
  }
  return null;
}
function isDrumKit(jsonText) {
  return typeof jsonText === "string" && jsonText.includes('"drumRack"');
}
function findDevice(node, kind, out = []) {
  if (Array.isArray(node)) {
    for (const v of node) findDevice(v, kind, out);
  } else if (node && typeof node === "object") {
    if (node.kind === kind) out.push(node);
    for (const k in node) findDevice(node[k], kind, out);
  }
  return out;
}
function parseKit(jsonText) {
  let raw, floats;
  if (typeof jsonText === "string") [raw, floats] = parseJsonFaithful(jsonText);
  else {
    raw = jsonText;
    floats = /* @__PURE__ */ new Set();
  }
  const rack = findDevice(raw, "drumRack")[0];
  if (!rack) throw new Error("not a drum rack preset");
  const pads = (rack.chains || []).map((chain, index) => {
    const cell = (chain.devices || []).find((d) => d.kind === "drumCell") || null;
    const zone = chain.drumZoneSettings || {};
    const mixer = chain.mixer || {};
    const uri = cell && cell.deviceData ? cell.deviceData.sampleUri : null;
    const params = cell ? { ...CELL_DEFAULTS, ...cell.parameters } : { ...CELL_DEFAULTS };
    return {
      index,
      note: zone.receivingNote ?? 36 + index,
      sendingNote: zone.sendingNote ?? 60,
      chokeGroup: zone.chokeGroup ?? null,
      color: chain.color ?? 0,
      name: cell && cell.name || chain.name || "",
      mixer: {
        volume: mixer.volume ?? 0,
        // dB; gain = 10^(dB/20)
        pan: mixer.pan ?? 0,
        // -50..+50 (see PAN_MIN/PAN_MAX)
        send: mixer.sends && mixer.sends[0] ? mixer.sends[0].amount : -70,
        speakerOn: mixer.speakerOn !== false
        // false = sample-exact silence
      },
      sampleUri: uri || null,
      // null = empty pad (112 in corpus)
      samplePath: resolveUri(uri),
      params,
      _chain: chain,
      _cell: cell,
      // Which param keys the file actually carried. CELL_DEFAULTS fills gaps for
      // consumers, but a default must never be written back as a NEW key — that
      // would silently rewrite 56 of the 140 corpus kits.
      _origKeys: new Set(cell ? Object.keys(cell.parameters) : [])
    };
  });
  const hostChain = (raw.chains || [])[0] || {};
  const rackFx = (hostChain.devices || []).filter((d) => d.kind !== "drumRack");
  const sendFx = rack.returnChains || [];
  return { name: raw.name || "", pads, rackFx, sendFx, _raw: raw, _rack: rack, _floats: floats };
}

// src/ui_kit.mjs
var PADS = 32;
function padWrites(i, pad) {
  const p = pad.params;
  const w = [];
  const put = (k, v) => w.push([`pad${i}_${k}`, String(v)]);
  put("note", pad.note);
  put("choke", pad.chokeGroup == null ? 0 : pad.chokeGroup);
  put("sample", pad.samplePath || "");
  put("start", p.Voice_PlaybackStart ?? 0);
  put("length", p.Voice_PlaybackLength ?? 1);
  put("transpose", p.Voice_Transpose ?? 0);
  put("detune", p.Voice_Detune ?? 0);
  put("gain", p.Voice_Gain ?? 1);
  put("vel_vol", p.Voice_VelocityToVolume ?? 0.35);
  put("attack", p.Voice_Envelope_Attack ?? 1e-4);
  put("hold", p.Voice_Envelope_Hold ?? 0.3);
  put("decay", p.Voice_Envelope_Decay ?? 1);
  put("env_mode", p.Voice_Envelope_Mode ?? "A-H-D");
  put("filter_on", p.Voice_Filter_On ? 1 : 0);
  put("filter_type", p.Voice_Filter_Type ?? "Lowpass");
  put("cutoff", p.Voice_Filter_Frequency ?? 22e3);
  put("resonance", p.Voice_Filter_Resonance ?? 0);
  put("peak_gain", p.Voice_Filter_PeakGain ?? 1);
  put("mod_target", p.Voice_ModulationTarget ?? "Filter");
  put("mod_amount", p.Voice_ModulationAmount ?? 0);
  put("pitch_env", p.Voice_PitchToEnvelopeModulation ? 1 : 0);
  put("volume", pad.mixer.volume ?? 0);
  put("cell_volume", p.Volume ?? 0);
  put("pan", pad.mixer.pan ?? 0);
  put("speaker_on", pad.mixer.speakerOn === false ? 0 : 1);
  put("sending_note", pad.sendingNote ?? 60);
  return w;
}
function createKitLoader({ setParam, getParam, readFile, log }) {
  let loadedPath = null;
  let lastKit = null;
  function loadPath(path) {
    if (!path) return { ok: false, error: "no kit" };
    const text = readFile(path);
    if (!text) return { ok: false, error: "unreadable" };
    if (!isDrumKit(text)) return { ok: false, error: "not a drum kit" };
    let kit;
    try {
      kit = parseKit(text);
    } catch (e) {
      return { ok: false, error: String(e && e.message || e) };
    }
    const pads = kit.pads.slice().sort((a, b) => a.note - b.note).slice(0, PADS);
    setParam("clear", "1");
    for (let i = 0; i < pads.length; i++) {
      for (const [k, v] of padWrites(i, pads[i])) setParam(k, v);
    }
    loadedPath = path;
    lastKit = kit;
    if (log) log(`dr32: loaded ${kit.name || path} (${pads.length} pads)`);
    return { ok: true, kit, pads: pads.length };
  }
  return {
    /** Call from tick(). Returns a result object when a load happened. */
    poll() {
      if (getParam("kit_dirty") !== "1") return null;
      setParam("kit_dirty", "0");
      const path = getParam("kit");
      if (path === loadedPath) return null;
      return loadPath(path);
    },
    loadPath,
    get kit() {
      return lastKit;
    },
    get path() {
      return loadedPath;
    },
    padWrites,
    FILTER_TYPES
  };
}

// src/ui.js
var loader = createKitLoader({
  setParam: (k, v) => host_module_set_param(k, v),
  getParam: (k) => host_module_get_param(k),
  readFile: (p) => host_read_file(p),
  log: (m) => {
    try {
      print(m);
    } catch (e) {
    }
  }
});
var status = "";
var ui = createSoundGeneratorUI({
  moduleName: "Drum Rack 32",
  showPolyphony: true,
  showOctave: false
  // pads are a fixed 36-67 map, octave shift would lie
});
var baseTick = ui.tick;
globalThis.init = () => {
  ui.init && ui.init();
  const path = host_module_get_param("kit");
  if (path) {
    const r = loader.loadPath(path);
    status = r.ok ? "" : `kit: ${r.error}`;
  }
};
globalThis.tick = () => {
  const r = loader.poll();
  if (r && !r.ok) status = `kit: ${r.error}`;
  else if (r) status = "";
  return baseTick ? baseTick() : void 0;
};
globalThis.onMidiMessageInternal = ui.onMidiMessageInternal;
globalThis.onMidiMessageExternal = ui.onMidiMessageExternal;
globalThis.dr32_load_kit = (path) => loader.loadPath(path);
globalThis.dr32_kit_status = () => status;
