// make_fixture.mjs — build a single-track .abl fixture that isolates the dry
// Drum Rack path, so its native render can be compared sample-for-sample.
//
// Derived from the preserved stock `16drums.abl` so the document shape is
// exactly what the engine's validator accepts (it REJECTS malformed shapes
// rather than normalising them).
//
// Key isolation choices:
//   - keep only track 0, whose pad sends are all -70 dB (return path silent),
//     so no reverb is needed to match the output;
//   - track volume forced to 0 dB (the stock song uses -29.8);
//   - notes replaced with a sparse, deterministic pattern so individual voices
//     are separable in the residual.
//
// Usage: node tools/make_fixture.mjs <16drums.abl> <out.abl> [--effect=Stretch]
//                                    [--notes=36,37] [--filter=Peak] ...

import { readFileSync, writeFileSync } from 'node:fs';

const [src, out, ...flags] = process.argv.slice(2);
if (!src || !out) {
    console.error('usage: node tools/make_fixture.mjs <src.abl> <out.abl> [--k=v ...]');
    process.exit(2);
}
const opt = Object.fromEntries(flags.map((f) => {
    const [k, v = '1'] = f.replace(/^--/, '').split('=');
    return [k, v];
}));

const song = JSON.parse(readFileSync(src, 'utf8'));

// --- one track only
song.tracks = song.tracks.slice(0, 1);
const track = song.tracks[0];
track.mixer.volume = 0.0;
track.mixer.pan = 0.0;

function find(node, kind, out = []) {
    if (Array.isArray(node)) for (const v of node) find(v, kind, out);
    else if (node && typeof node === 'object') {
        if (node.kind === kind) out.push(node);
        for (const k in node) find(node[k], kind, out);
    }
    return out;
}

const rack = find(track.devices, 'drumRack')[0];

// --- deterministic note pattern: one hit per requested note, spaced 0.5 beats
// (0.25 s at 120 bpm) so voices do not overlap unless we want them to.
const notes = (opt.notes || '36').split(',').map(Number);
const velocity = Number(opt.velocity || 100);
const spacing = Number(opt.spacing || 0.5);
const simultaneous = opt.simultaneous === '1';

const events = notes.map((n, i) => ({
    noteNumber: n,
    startTime: simultaneous ? 0.0 : i * spacing,
    duration: Number(opt.duration || 0.25),
    velocity,
    offVelocity: 64.0,
}));

for (let i = 0; i < track.clipSlots.length; i++) {
    const slot = track.clipSlots[i];
    if (!slot.clip) continue;
    if (i === 0) {
        slot.clip.notes = events;
        slot.clip.isPlaying = true;
        slot.clip.region.end = 8.0;
        slot.clip.region.loop.end = 8.0;
        slot.clip.region.loop.isEnabled = false;
    } else {
        slot.clip = null;
        slot.hasStop = true;
    }
}

// --- optional per-pad parameter overrides, applied to EVERY pad so whichever
// note we play is affected.
const OVERRIDES = {
    effect: 'Effect_Type',
    filter: 'Voice_Filter_Type',
    cutoff: 'Voice_Filter_Frequency',
    resonance: 'Voice_Filter_Resonance',
    peakgain: 'Voice_Filter_PeakGain',
    attack: 'Voice_Envelope_Attack',
    hold: 'Voice_Envelope_Hold',
    decay: 'Voice_Envelope_Decay',
    envmode: 'Voice_Envelope_Mode',
    transpose: 'Voice_Transpose',
    detune: 'Voice_Detune',
    velvol: 'Voice_VelocityToVolume',
    start: 'Voice_PlaybackStart',
    length: 'Voice_PlaybackLength',
    gain: 'Voice_Gain',
};

for (const chain of rack.chains) {
    const cell = chain.devices.find((d) => d.kind === 'drumCell');
    if (!cell) continue;
    for (const [flag, key] of Object.entries(OVERRIDES)) {
        if (!(flag in opt)) continue;
        const raw = opt[flag];
        cell.parameters[key] = /^-?\d+(\.\d+)?$/.test(raw) ? Number(raw) : raw;
    }
    if ('filteron' in opt) cell.parameters.Voice_Filter_On = opt.filteron === '1';
    // Mixer isolation unless explicitly probed.
    if ('pan' in opt) chain.mixer.pan = Number(opt.pan);
    if ('volume' in opt) chain.mixer.volume = Number(opt.volume);
    if ('choke' in opt) chain.drumZoneSettings.chokeGroup = Number(opt.choke) || null;
}

writeFileSync(out, JSON.stringify(song, null, 2) + '\n');
console.error(`${out}: notes ${notes.join(',')} ${Object.keys(opt).length ? JSON.stringify(opt) : ''}`);
