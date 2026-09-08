/*
 * DR32 — Move Drum Rack clone with 32 pads.
 *
 * This file is the PLAY VIEW only (the shared sound-generator UI base, as
 * String Machine / Dexed use). Every parameter page — the 32 pads, the sends,
 * the drum bus, the kit browser — is the host's own param-pages grid, planned
 * from the hierarchy the DSP serves (see dsp/dr32.c and src/module.json).
 * Kits are parsed and loaded by the DSP (dsp/dr32_preset.c) when the host's
 * file browser writes the kit path, so nothing here touches them.
 *
 * MIT.
 */

import { createSoundGeneratorUI } from '/data/UserData/schwung/shared/sound_generator_ui.mjs';

const ui = createSoundGeneratorUI({
    moduleName: 'Drum Rack 32',
    showPolyphony: true,
    showOctave: false,          // pads are a fixed 36-67 map, octave shift would lie
});

globalThis.init = () => { ui.init && ui.init(); };
globalThis.tick = () => (ui.tick ? ui.tick() : undefined);
globalThis.onMidiMessageInternal = ui.onMidiMessageInternal;
globalThis.onMidiMessageExternal = ui.onMidiMessageExternal;
