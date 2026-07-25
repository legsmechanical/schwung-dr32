/*
 * DR32 — Move Drum Rack clone with 32 pads.
 *
 * The on-device play view uses the shared sound-generator UI base (as
 * String Machine / Dexed do), so parameter editing goes through the standard
 * shadow-UI hierarchy. On top of that this file owns KIT LOADING: the file
 * browser stores an .ablpreset path on the DSP, and we parse it here and push
 * the 32 pads down as flat params.
 *
 * MIT.
 */

import { createSoundGeneratorUI } from '/data/UserData/schwung/shared/sound_generator_ui.mjs';
import { createKitLoader } from './ui_kit.mjs';

const loader = createKitLoader({
    setParam: (k, v) => host_module_set_param(k, v),
    getParam: (k) => host_module_get_param(k),
    readFile: (p) => host_read_file(p),
    log: (m) => { try { print(m); } catch (e) { /* no console in all contexts */ } },
});

let status = '';

const ui = createSoundGeneratorUI({
    moduleName: 'Drum Rack 32',
    showPolyphony: true,
    showOctave: false,          // pads are a fixed 36-67 map, octave shift would lie
});

const baseTick = ui.tick;

globalThis.init = () => {
    ui.init && ui.init();
    // A kit may already be set from restored state — load it on the way in.
    const path = host_module_get_param('kit');
    if (path) {
        const r = loader.loadPath(path);
        status = r.ok ? '' : `kit: ${r.error}`;
    }
};

globalThis.tick = () => {
    const r = loader.poll();
    if (r && !r.ok) status = `kit: ${r.error}`;
    else if (r) status = '';
    return baseTick ? baseTick() : undefined;
};

globalThis.onMidiMessageInternal = ui.onMidiMessageInternal;
globalThis.onMidiMessageExternal = ui.onMidiMessageExternal;

// Exposed for the remote UI / davebox control path (M4).
globalThis.dr32_load_kit = (path) => loader.loadPath(path);
globalThis.dr32_kit_status = () => status;
