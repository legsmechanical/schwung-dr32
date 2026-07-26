// ui_kit.mjs — bridges a .ablpreset file to the DSP's flat pad params.
//
// The DSP records the kit path chosen in the file browser and raises kit_dirty;
// this polls that, parses the preset, and pushes the pads down. The DSP never
// sees JSON.

import { parseKit, isDrumKit, FILTER_TYPES, EFFECT_PARAMS } from './ablpreset.mjs';

const PADS = 32;

/** Map a parsed pad's params onto the DSP's flat keys. */
function padWrites(i, pad) {
    const p = pad.params;
    const w = [];
    const put = (k, v) => w.push([`pad${i}_${k}`, String(v)]);

    put('note', pad.note);
    put('choke', pad.chokeGroup == null ? 0 : pad.chokeGroup);
    put('sample', pad.samplePath || '');
    put('start', p.Voice_PlaybackStart ?? 0);
    put('length', p.Voice_PlaybackLength ?? 1);
    put('transpose', p.Voice_Transpose ?? 0);
    put('detune', p.Voice_Detune ?? 0);
    put('gain', p.Voice_Gain ?? 1);
    put('vel_vol', p.Voice_VelocityToVolume ?? 0.35);
    put('attack', p.Voice_Envelope_Attack ?? 0.0001);
    put('hold', p.Voice_Envelope_Hold ?? 0.3);
    put('decay', p.Voice_Envelope_Decay ?? 1);
    put('env_mode', p.Voice_Envelope_Mode ?? 'A-H-D');
    put('filter_on', p.Voice_Filter_On ? 1 : 0);
    put('filter_type', p.Voice_Filter_Type ?? 'Lowpass');
    put('cutoff', p.Voice_Filter_Frequency ?? 22000);
    put('resonance', p.Voice_Filter_Resonance ?? 0);
    put('peak_gain', p.Voice_Filter_PeakGain ?? 1);
    put('mod_target', p.Voice_ModulationTarget ?? 'Filter');
    put('mod_amount', p.Voice_ModulationAmount ?? 0);
    put('pitch_env', p.Voice_PitchToEnvelopeModulation ? 1 : 0);
    // Playback effect: the JSON always carries ALL nine effects' params, so we
    // send only the active type's two, mapped by EFFECT_PARAMS. The DSP stays
    // generic (fx_p1/fx_p2) and never learns the per-effect key names.
    const fxType = (p.Effect_On === false) ? 'Standard' : (p.Effect_Type ?? 'Standard');
    put('fx_type', fxType);
    const keys = EFFECT_PARAMS[fxType] || [];
    put('fx_p1', keys[0] ? (p[keys[0]] ?? 0) : 0);
    put('fx_p2', keys[1] ? (p[keys[1]] ?? 0) : 0);
    // Per-pad mixer. Volume is dB (gain = 10^(dB/20), measured). Pan is the
    // -50..+50 serialized domain — NOT -1..+1 — and feeds the engine's
    // equal-power law.
    put('volume', pad.mixer.volume ?? 0);
    put('cell_volume', p.Volume ?? 0);
    put('pan', pad.mixer.pan ?? 0);
    put('speaker_on', pad.mixer.speakerOn === false ? 0 : 1);
    put('sending_note', pad.sendingNote ?? 60);
    return w;
}

export function createKitLoader({ setParam, getParam, readFile, log }) {
    let loadedPath = null;
    let lastKit = null;

    function loadPath(path) {
        if (!path) return { ok: false, error: 'no kit' };
        const text = readFile(path);
        if (!text) return { ok: false, error: 'unreadable' };
        // The movy/mrdrums lesson: filter before loading, never crash on the
        // wrong file type.
        if (!isDrumKit(text)) return { ok: false, error: 'not a drum kit' };

        let kit;
        try { kit = parseKit(text); }
        catch (e) { return { ok: false, error: String(e && e.message || e) }; }

        // Pads are addressed by position, but the NOTE is what routes — a native
        // kit is not necessarily sorted by note, so sort before assigning slots.
        const pads = kit.pads.slice().sort((a, b) => a.note - b.note).slice(0, PADS);

        setParam('clear', '1');
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
            if (getParam('kit_dirty') !== '1') return null;
            setParam('kit_dirty', '0');
            const path = getParam('kit');
            if (path === loadedPath) return null;
            return loadPath(path);
        },
        loadPath,
        get kit() { return lastKit; },
        get path() { return loadedPath; },
        padWrites,
        FILTER_TYPES,
    };
}
