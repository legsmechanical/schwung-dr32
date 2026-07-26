// nulltest.mjs — compare two float-stereo WAVs and report the residual.
//
// This is the acceptance measurement for DR32: "does it sound right" becomes
// "how far is it from the stock engine's own output, in dB".
//
// Usage: node tools/nulltest.mjs <native.wav> <dr32.wav> [--json]

import { readFileSync } from 'node:fs';

function readFloatWav(path) {
    const b = readFileSync(path);
    if (b.toString('ascii', 0, 4) !== 'RIFF' || b.toString('ascii', 8, 12) !== 'WAVE') {
        throw new Error(`${path}: not RIFF/WAVE`);
    }
    let off = 12, fmt = null, data = null;
    while (off + 8 <= b.length) {
        const id = b.toString('ascii', off, off + 4);
        const size = b.readUInt32LE(off + 4);
        if (id === 'fmt ') {
            fmt = { tag: b.readUInt16LE(off + 8), ch: b.readUInt16LE(off + 10),
                    rate: b.readUInt32LE(off + 12), bits: b.readUInt16LE(off + 22) };
        } else if (id === 'data') {
            data = b.subarray(off + 8, off + 8 + size);
        }
        off += 8 + size + (size & 1);
    }
    if (!fmt || !data) throw new Error(`${path}: missing fmt/data`);
    if (fmt.tag !== 3 || fmt.bits !== 32) throw new Error(`${path}: expected 32-bit float, got tag ${fmt.tag}/${fmt.bits}`);
    const n = Math.floor(data.length / 4);
    const out = new Float32Array(n);
    for (let i = 0; i < n; i++) out[i] = data.readFloatLE(i * 4);
    return { fmt, samples: out, frames: n / fmt.ch };
}

const [aPath, bPath, ...flags] = process.argv.slice(2);
if (!aPath || !bPath) {
    console.error('usage: node tools/nulltest.mjs <native.wav> <dr32.wav>');
    process.exit(2);
}

const A = readFloatWav(aPath);
const B = readFloatWav(bPath);

// The stock renderer has a fixed startup latency (measured: 65 frames on the
// kick fixture) that our offline renderer does not. Align before diffing, or
// the residual measures the offset rather than the engine.
function bestOffset(a, b, maxFrames = 512) {
    let best = 0, bestErr = Infinity;
    const n = Math.min(a.length, b.length) - maxFrames * 2;
    for (let off = 0; off <= maxFrames; off++) {
        let err = 0;
        for (let i = 0; i < n; i += 37) {           // sparse probe: fast, adequate
            const d = a[(i + off * 2)] - b[i];
            err += d * d;
        }
        if (err < bestErr) { bestErr = err; best = off; }
    }
    return best;
}
// Sub-sample refinement. The stock renderer's note placement need not land on
// an integer output frame (a 96 kHz source advances 2.18 input frames per
// output frame), and on a steep transient a fraction of a frame dominates the
// residual — it would otherwise be misread as an engine mismatch.
function shift(x, frac) {
    if (!frac) return x;
    const out = new Float32Array(x.length);
    for (let i = 0; i + 2 < x.length / 2; i++) {
        for (let c = 0; c < 2; c++) {
            const a = x[i * 2 + c], b = x[(i + 1) * 2 + c];
            out[i * 2 + c] = a + frac * (b - a);
        }
    }
    return out;
}
let align = flags.includes('--no-align') ? 0 : bestOffset(A.samples, B.samples);
if (align) A.samples = A.samples.subarray(align * 2);

let frac = 0;
if (!flags.includes('--no-align')) {
    let best = Infinity;
    const cands = [0];
    for (let f = -0.95; f <= 0.95; f += 0.05) cands.push(Math.round(f * 100) / 100);
    for (const f of cands) {
        const cand = f < 0 ? shift(A.samples.subarray(2), 1 + f) : shift(A.samples, f);
        let err = 0;
        const m = Math.min(cand.length, B.samples.length);
        for (let i = 0; i < m; i += 3) { const d = cand[i] - B.samples[i]; err += d * d; }
        if (err < best - 1e-12) { best = err; frac = f; }
    }
    A.samples = frac < 0 ? shift(A.samples.subarray(2), 1 + frac) : shift(A.samples, frac);
}

const n = Math.min(A.samples.length, B.samples.length);
const db = (x) => (x > 0 ? 20 * Math.log10(x) : -Infinity);

let peakA = 0, peakB = 0, peakD = 0, sumA = 0, sumD = 0, argmax = 0;
for (let i = 0; i < n; i++) {
    const a = A.samples[i], b = B.samples[i], d = Math.abs(a - b);
    if (Math.abs(a) > peakA) peakA = Math.abs(a);
    if (Math.abs(b) > peakB) peakB = Math.abs(b);
    if (d > peakD) { peakD = d; argmax = i; }
    sumA += a * a;
    sumD += d * d;
}
const rmsA = Math.sqrt(sumA / n), rmsD = Math.sqrt(sumD / n);

// Diagnostic: the best single scalar that maps dr32 onto native, and how deep
// the null gets once that scalar is removed. If gain-compensated null is much
// deeper than raw null, the error is a LEVEL law; if it barely moves, the error
// is spectral or temporal and no gain fixes it.
let dot = 0, sumB = 0;
for (let i = 0; i < n; i++) { dot += A.samples[i] * B.samples[i]; sumB += B.samples[i] * B.samples[i]; }
const alpha = sumB > 0 ? dot / sumB : 0;
let sumDc = 0;
for (let i = 0; i < n; i++) { const d = A.samples[i] - alpha * B.samples[i]; sumDc += d * d; }
const rmsDc = Math.sqrt(sumDc / n);

// Where does the native signal first become non-trivial, and do we agree on it?
const onset = (S) => { for (let i = 0; i < S.length; i++) if (Math.abs(S[i]) > 1e-4) return i >> 1; return -1; };

const report = {
    frames: { native: A.frames, dr32: B.frames },
    peak: { native: peakA, dr32: peakB, ratio_db: db(peakB / peakA) },
    rms: { native: rmsA, dr32: rmsD && rmsA ? rmsA : rmsA },
    residual: { peak: peakD, peak_db: db(peakD), rms: rmsD, rms_db: db(rmsD) },
    null_depth_db: db(rmsD / rmsA),          // the headline number
    best_fit_gain: alpha,
    best_fit_gain_db: db(Math.abs(alpha)),
    null_depth_gain_compensated_db: db(rmsDc / rmsA),
    worst_frame: argmax >> 1,
    align,
    onset: { native: onset(A.samples), dr32: onset(B.samples) },
};

if (flags.includes('--json')) {
    console.log(JSON.stringify(report, null, 2));
} else {
    console.log(`frames        native ${A.frames}   dr32 ${B.frames}   (aligned +${align}${frac ? frac.toFixed(2) : ''} frames)`);
    console.log(`peak          native ${peakA.toFixed(6)}   dr32 ${peakB.toFixed(6)}   (${db(peakB / peakA).toFixed(2)} dB)`);
    console.log(`onset frame   native ${report.onset.native}   dr32 ${report.onset.dr32}`);
    console.log(`residual      peak ${peakD.toExponential(3)} (${db(peakD).toFixed(1)} dBFS)  rms ${rmsD.toExponential(3)}`);
    console.log(`NULL DEPTH    ${report.null_depth_db.toFixed(2)} dB below the native signal   (worst at frame ${report.worst_frame})`);
    console.log(`best-fit gain ${alpha.toFixed(6)} (${db(Math.abs(alpha)).toFixed(2)} dB)  ->  null ${db(rmsDc / rmsA).toFixed(2)} dB after gain match`);
}
