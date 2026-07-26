// score.mjs — turn a .abl song into a flat score the C renderer can execute.
//
// This is the JS half of the null test: it reuses the SAME parser the module
// uses (lib/ablpreset.mjs) and emits the SAME flat param keys the plugin
// receives, so what we validate is what ships.
//
// Usage: node tools/score.mjs <song.abl> <out.score> [--samples=<local-root>]
//
// --samples remaps the device's /data/UserData/UserLibrary to a local mirror so
// the renderer can run off-device.

import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { parseKit, USER_LIBRARY, CORE_LIBRARY } from '../lib/ablpreset.mjs';

const [songPath, outPath, ...flags] = process.argv.slice(2);
if (!songPath || !outPath) {
    console.error('usage: node tools/score.mjs <song.abl> <out.score> [--samples=<root>]');
    process.exit(2);
}
const opt = Object.fromEntries(flags.map((f) => {
    const [k, v = '1'] = f.replace(/^--/, '').split('=');
    return [k, v];
}));
const SR = 44100;

const song = JSON.parse(readFileSync(songPath, 'utf8'));
const tempo = song.tempo || 120;
const secPerBeat = 60 / tempo;

function find(node, kind, out = []) {
    if (Array.isArray(node)) for (const v of node) find(v, kind, out);
    else if (node && typeof node === 'object') {
        if (node.kind === kind) out.push(node);
        for (const k in node) find(node[k], kind, out);
    }
    return out;
}

const lines = [];
const track = song.tracks[0];
if (!track) throw new Error('no tracks');

// Track mixer volume is applied as a master gain — the null test isolates the
// rack, and the track fader sits downstream of it.
const trackDb = (track.mixer && track.mixer.volume) || 0;
lines.push(`master ${Math.pow(10, trackDb / 20)}`);

// The parser wants a device-preset shape; a song embeds the same rack, so hand
// it the track's instrument rack directly.
const rackHost = { chains: [{ devices: track.devices }] };
const kit = parseKit(JSON.stringify(rackHost));

const pads = kit.pads.slice().sort((a, b) => a.note - b.note);
pads.forEach((pad, i) => {
    const p = pad.params;
    let path = pad.samplePath;
    // Remap BOTH device roots onto the local mirror. Factory kits live in
    // CoreLibrary, not the user library, so remapping only the latter silently
    // drops every factory sample.
    if (path && opt.samples) {
        path = path.replace(CORE_LIBRARY, `${opt.samples}/CoreLibrary`)
                   .replace(USER_LIBRARY, `${opt.samples}/UserLibrary`);
    }
    if (path && !existsSync(path)) {
        console.error(`  ! missing sample for pad ${i}: ${path}`);
        path = '';
    }
    const put = (k, v) => lines.push(`p pad${i}_${k} ${v}`);
    put('note', pad.note);
    put('sending_note', pad.sendingNote ?? 60);
    put('choke', pad.chokeGroup == null ? 0 : pad.chokeGroup);
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
    put('volume', pad.mixer.volume ?? 0);
    put('cell_volume', p.Volume ?? 0);
    put('pan', pad.mixer.pan ?? 0);
    put('speaker_on', pad.mixer.speakerOn === false ? 0 : 1);
    if (path) put('sample', path);           // last: loading resets the voice
});

// --- note events from the playing clip
const events = [];
for (const slot of track.clipSlots || []) {
    const clip = slot && slot.clip;
    if (!clip || !clip.isPlaying || !clip.notes) continue;
    for (const n of clip.notes) {
        const onFrame = Math.round(n.startTime * secPerBeat * SR);
        const offFrame = Math.round((n.startTime + n.duration) * secPerBeat * SR);
        events.push([onFrame, 1, n.noteNumber, Math.round(n.velocity)]);
        events.push([offFrame, 0, n.noteNumber, 0]);
    }
    break;                                    // one clip per track in a fixture
}
// Stable order: by frame, note-offs before note-ons at the same frame (a
// re-hit of the same pad must not be killed by the previous note's off).
events.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
for (const [f, on, note, vel] of events) lines.push(`e ${f} ${on} ${note} ${vel}`);

writeFileSync(outPath, lines.join('\n') + '\n');
console.error(`${outPath}: ${pads.length} pads, ${events.length} events, tempo ${tempo}`);
