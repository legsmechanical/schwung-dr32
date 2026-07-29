"""verb_null.py — null depth for the Native reverb against the device's render.

Usage:  python3 tools/verb_null.py <native.wav> <ours.wav> [--bands]

Why this exists alongside tools/nulltest.mjs: that tool is built for the pad
path, where both renders are the same signal at nearly the same level and the
job is to align a steep transient. A reverb reference is neither. It arrives
65 frames late, it is scaled by the send amount, the cell volume, the return
gain and the master gain all at once, and it has no transient to align on —
its energy builds over tens of milliseconds. A sparse-probe alignment on that
finds a plausible offset that is simply wrong.

So this fits the two free parameters explicitly and reports what is left:

    null = 20*log10( ||a - g*b_shifted|| / ||a|| )

`g` is the least-squares scalar, which is the honest thing to remove — a level
error is not a modelling error, and leaving it in would report one as the other.
Everything else stays in the residual.

⚠⚠ AND THE HEADLINE RESULT: a sample-level null DOES NOT TRANSFER TO A REVERB.

Measured against the device, the tails' waveform correlation is -0.008 and the
null is -0.34 dB. That is not a verdict on the model. Two reverbs whose delay
lines differ by a single sample produce completely decorrelated tails while
sounding identical, so the null is all-or-nothing: it reads 0 dB for everything
except a bit-exact port, and cannot distinguish "very close" from "wildly
wrong". DR32's usual acceptance rule — "the acceptance test is a null test, not
an ear test" — holds for the pad effects, which are deterministic transforms of
a sample. It does not hold here.

So this tool reports BOTH, and the second one is the acceptance number:

  * the null depth, for the record and to catch a gross error;
  * the ENERGY DECAY CURVE deviation per band, which is graded, is what a
    reverb is actually judged on, and is insensitive to phase.

Envelope correlation is printed too as a sanity check on the alignment.
"""
import sys, math
import numpy as np

sys.path.insert(0, 'tools')
from irtools import load

BANDS = [(20, 200), (200, 671), (671, 1470), (1470, 5000), (5000, 16000)]


def stereo(path):
    s, ch, sr = load(path)
    a = np.asarray(s, dtype=np.float64).reshape(-1, ch)
    return a[:, :2] if ch >= 2 else np.repeat(a, 2, axis=1), sr


def fit(a, b, max_shift=512):
    """Best integer shift of b against a, and the least-squares gain.

    Returns (residual_energy, shift, gain, a_used, b_used) — the two slices as
    well, because computing the residual against a differently-sliced copy of
    `a` is exactly the bug this signature is here to prevent. It reported a
    50 dB null on two signals whose waveform correlation is -0.001.
    """
    n = min(len(a), len(b)) - max_shift - 1
    av = a[:n].ravel()
    best = None
    for d in range(-max_shift, max_shift + 1):
        if d >= 0:
            bv = b[d: d + n].ravel()
        else:
            bv = np.concatenate([np.zeros(-d * 2), b[: n + d].ravel()])
        if len(bv) != len(av):
            continue
        den = float(bv @ bv)
        if den <= 0:
            continue
        g = float(av @ bv) / den
        res = float(np.sum((av - g * bv) ** 2))
        if best is None or res < best[0]:
            best = (res, d, g, av, bv)
    return best


def edc(v):
    """Energy decay curve in dB, by Schroeder backward integration."""
    e = np.cumsum((v ** 2)[::-1])[::-1]
    return 10 * np.log10(np.maximum(e / e[0], 1e-12))


def bandpass(x, sr, f_lo, f_hi, sections=6, q=1.4):
    from scipy.signal import lfilter
    f0 = math.sqrt(max(f_lo, 1e-6) * min(f_hi, sr * 0.45))
    w = 2 * math.pi * f0 / sr
    al = math.sin(w) / (2 * q)
    a0 = 1 + al
    b = [al / a0, 0.0, -al / a0]
    a = [1.0, -2 * math.cos(w) / a0, (1 - al) / a0]
    for _ in range(sections):
        x = lfilter(b, a, x)
    return x


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    a, sr = stereo(sys.argv[1])
    b, _ = stereo(sys.argv[2])
    # ⚠ The reference is a WHOLE-TRACK render: the dry click goes to master
    # alongside the return. That one dry sample carries roughly 50x the energy
    # of the entire tail, and our render has no dry path at all — so including
    # it makes the least-squares fit correctly conclude "subtract almost
    # nothing", and the null reports 0 dB however good the model is. Skip past
    # the dry before comparing.
    skip = 512
    for f in sys.argv:
        if f.startswith('--skip='):
            skip = int(f.split('=', 1)[1])
    a = a[skip:]
    b = b[skip:]
    res, d, g, av, bv = fit(a, b)
    ea = float(av @ av)
    null = 10 * math.log10(res / ea) if ea > 0 and res > 0 else float('-inf')

    print(f"offset     {d:+d} frames        (the stock renderer's latency is 65)")
    print(f"gain       {g:.6g}  ({20*math.log10(abs(g)):.2f} dB)")
    print(f"NULL DEPTH {null:7.2f} dB below the device's own render")

    xa = av.reshape(-1, 2).sum(1)
    xb = bv.reshape(-1, 2).sum(1)
    ea_, eb_ = np.abs(xa), np.abs(xb)
    print(f"waveform correlation  {float(xa @ xb) / math.sqrt(float(xa @ xa) * float(xb @ xb)):+.4f}"
          "   (a reverb tail decorrelates on a one-sample delay error)")

    # ── The acceptance metric ──────────────────────────────────────────────
    print("\nENERGY DECAY CURVE deviation, over each band's -5..-35 dB span:")
    print(f"  {'band':>16}  {'rms':>8}  {'max':>8}")
    worst = 0.0
    for lo, hi in BANDS:
        fa = np.asarray(bandpass(xa, sr, lo, hi))
        fb = np.asarray(bandpass(xb, sr, lo, hi))
        ca, cb = edc(fa), edc(fb)
        m = (ca <= -5) & (ca >= -35)
        if m.sum() < 100:
            print(f"  {lo:>6}-{hi:<7}   (too short to fit)")
            continue
        d = cb[m] - ca[m]
        rms = float(np.sqrt((d ** 2).mean()))
        worst = max(worst, rms)
        print(f"  {lo:>6}-{hi:<7}  {rms:7.2f} dB  {float(np.abs(d).max()):7.2f} dB")
    print(f"\nWORST BAND {worst:.2f} dB rms")
    return 0


if __name__ == '__main__':
    sys.exit(main())
