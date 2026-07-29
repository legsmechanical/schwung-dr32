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
//                                    [--notes=36,37] [--filter=Peak] [--send=0] ...

import { readFileSync, writeFileSync } from 'node:fs';

const [src, out, ...flags] = process.argv.slice(2);
if (!src || !out) {
    console.error('usage: node tools/make_fixture.mjs <src.abl> <out.abl> [--k=v ...]');
    process.exit(2);
}
const opt = Object.fromEntries(flags.map((f) => {
    // Split on the FIRST '=' only: --return=Key=Value,Key2=Value2 carries its
    // own '=' signs, and a plain split() handed back just the first fragment,
    // so every parameter after the first was silently dropped.
    const body = f.replace(/^--/, '');
    const i = body.indexOf('=');
    return i < 0 ? [body, '1'] : [body.slice(0, i), body.slice(i + 1)];
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
    // --sample=<user-library path> — repoint the pad at one file. For impulse
    // response work the pad must play a CLICK, and nothing else in the fixture
    // chain could set the sample.
    if ('sample' in opt) {
        const cellDev = (chain.devices || []).find((d) => d.kind === 'drumCell');
        if (cellDev) {
            cellDev.deviceData = cellDev.deviceData || {};
            // ⚠ The CORE-LIBRARY pack scheme, not user-library. EnginePerfTool
            // has no user library configured and dies on the URI:
            //   ASSERT 'config.userLibraryPath().has_value()' failed
            //   (shared/abl-uri-scheme/src/AbletonScheme.cpp:163)
            // The stock benchmark's own samples all use this scheme, which is
            // why they resolve. Put the file in /data/CoreLibrary/Samples/.
            cellDev.deviceData.sampleUri =
                'ableton:/packs/abl-core-library/Samples/' + String(opt.sample);
        }
    }
    // --send=<dB> drives the RETURN path, which the stock fixture deliberately
    // silences at -70. Rendering the same fixture at -70 and at 0 dB and
    // subtracting isolates the return exactly, because the engine is
    // deterministic — sends are post-fader, so there is no way to mute the dry
    // and keep the send instead.
    if ('send' in opt && chain.mixer && Array.isArray(chain.mixer.sends)) {
        for (const s of chain.mixer.sends) {
            s.amount = Number(opt.send);
            s.isEnabled = true;
        }
    }
}

// --return=Key=Value,Key2=Value2 — parameters on the RETURN chain's device.
// The 61 stock drum kits all use RoomType SuperEco but differ in RoomSize,
// DecayTime, PreDelay and the shelves, so a model has to track those, and each
// grid point is one render.
if ('return' in opt) {
    const rc = rack.returnChains || [];
    let n = 0;
    for (const pair of String(opt.return).split(',')) {
        const [k, v] = pair.split('=');
        for (const ch of rc) {
            for (const d of (ch.devices || [])) {
                if (!d.parameters) continue;
                // ⚠ Write the key even when the preset omits it. DecayTime is a
                // real field of this device — Live exposes it on the very same
                // Reverb, at 3310 ms in one of Josh's sets — but the benchmark's
                // instance does not carry it, and skipping absent keys meant the
                // first grid never swept the actual decay control at all.
                d.parameters[k] = /^-?\d+(\.\d+)?$/.test(v) ? Number(v) : v;
                n++;
            }
        }
    }
    console.error(`  return device: set ${n} param(s)`);
}

writeFileSync(out, JSON.stringify(song, null, 2) + '\n');
console.error(`${out}: notes ${notes.join(',')} ${Object.keys(opt).length ? JSON.stringify(opt) : ''}`);
