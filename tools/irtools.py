"""irtools.py — read the oracle's WAVs and measure decay. Import from analysis scripts.

Written because both halves were re-derived the hard way during the SuperEco work:

  * Python's `wave` module CANNOT read these files. EnginePerfTool writes
    32-bit FLOAT WAV (format tag 3) and `wave` raises "unknown format: 3".
    Hence the hand-rolled RIFF walk below.

  * ⚠ RT60 by "time to fall 30 dB, doubled" GIVES WRONG ANSWERS on these tails.
    It reported a flat 2.0-2.2 s across an entire parameter grid — which looked
    like a real (null) result and was actually the measurement pinning at the
    render length. Fit the log-envelope slope instead; that is `rt60()` here.

Usage:
    import sys; sys.path.insert(0, 'tools')
    from irtools import load, mono, rt60
"""
import struct, math


def load(path):
    """-> (samples, channels, samplerate). Handles float32 and int16 RIFF."""
    d = open(path, 'rb').read()
    assert d[:4] == b'RIFF', f'{path}: not RIFF'
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]
        sz = struct.unpack('<I', d[pos + 4:pos + 8])[0]
        body = d[pos + 8:pos + 8 + sz]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', body[:16])
        elif cid == b'data':
            data = body
        pos += 8 + sz + (sz & 1)
    tag, ch, sr, _, _, bits = fmt
    if tag == 3 and bits == 32:
        s = struct.unpack('<%df' % (len(data) // 4), data)
    elif tag == 1 and bits == 16:
        s = [x / 32768.0 for x in struct.unpack('<%dh' % (len(data) // 2), data)]
    else:
        raise SystemExit(f'{path}: unsupported format tag {tag} / {bits} bits')
    return list(s), ch, sr


def mono(path):
    """-> (left channel, samplerate)."""
    s, ch, sr = load(path)
    return [s[i * ch] for i in range(len(s) // ch)], sr


def envelope(sig, sr, win_s=0.02, skip_first=True):
    """RMS envelope in `win_s` blocks. skip_first drops the block holding the
    dry impulse, which otherwise dominates the peak and flattens everything."""
    win = max(1, int(win_s * sr))
    env = [math.sqrt(sum(v * v for v in sig[k * win:(k + 1) * win]) / win)
           for k in range(len(sig) // win)]
    return (env[1:] if skip_first else env), win


def rt60(sig, sr, lo_db=-35.0, hi_db=-5.0, band=None):
    """RT60 by least-squares fit of the log-envelope decay slope.

    `band` = (f_lo, f_hi) applies a STEEP bandpass first, for PER-BAND RT60 —
    the measurement that separates the shelves from the feedback law. ⚠ Read
    the note on _bandpass before changing its steepness: at 6 dB/octave this
    same measurement said the shelves were ruled out, and it was wrong.
    Returns None if there is not enough decay to fit."""
    if band:
        sig = _bandpass(sig, sr, band[0], band[1])
    env, win = envelope(sig, sr)
    if not env:
        return None
    pk = max(env)
    if pk <= 0:
        return None
    pts = [(k * win / sr, 20 * math.log10(v / pk)) for k, v in enumerate(env) if v > 0]
    seg = [(t, d) for t, d in pts if lo_db <= d <= hi_db]
    if len(seg) < 6:
        return None
    n = len(seg)
    sx = sum(t for t, _ in seg); sy = sum(d for _, d in seg)
    sxx = sum(t * t for t, _ in seg); sxy = sum(t * d for t, d in seg)
    denom = n * sxx - sx * sx
    if denom == 0:
        return None
    slope = (n * sxy - sx * sy) / denom      # dB per second
    return -60.0 / slope if slope < 0 else None


def _biquad_bp(sig, sr, f0, q):
    """One RBJ constant-skirt bandpass section."""
    w = 2.0 * math.pi * f0 / sr
    al = math.sin(w) / (2.0 * q)
    cw = math.cos(w)
    a0 = 1.0 + al
    b = [al / a0, 0.0, -al / a0]
    a = [1.0, -2.0 * cw / a0, (1.0 - al) / a0]
    try:
        # scipy when it is there: this filter is six sections over multi-minute
        # renders, and the pure-Python loop below turns a suite run into
        # minutes. Same coefficients, same result.
        from scipy.signal import lfilter
        return lfilter(b, a, sig)
    except ImportError:
        pass
    z1 = z2 = 0.0
    out = []
    for x in sig:
        y = b[0] * x + z1
        z1 = b[1] * x - a[1] * y + z2
        z2 = b[2] * x - a[2] * y
        out.append(y)
    return out


# ⚠⚠ THE SKIRTS HAVE TO BE STEEP, and this is not a detail — it produced a
# WRONG CONCLUSION that stood for a day.
#
# This was a one-pole high-pass into a one-pole low-pass: 6 dB/octave. Against
# an ordinary spectrum that is fine. Against THIS reverb it is not, because the
# bands differ in DECAY RATE as well as level: the mid band both rings loudest
# and lasts longest, so at 6 dB/octave its tail leaks through the skirts of
# every other band's filter and IS the tail you measure there. Every band then
# reads back the mid band's RT60 and the result looks flat.
#
# That flatness was read as "every band saturates together, therefore the cause
# is broadband, therefore the shelves are ruled out". It is an artifact — the
# shelves ARE the cause. Re-measured with the cascade below, the device's own
# banked IRs separate: 1.85 s below 671 Hz against 5.90 s in the mid.
#
# Six cascaded pole-pairs, ~72 dB/octave. The only version whose answer means
# what it says.
_BAND_SECTIONS = 6
_BAND_Q = 1.4


def _bandpass(sig, sr, f_lo, f_hi):
    """Steep bandpass, geometric centre of (f_lo, f_hi). See the note above:
    a gentle filter gives a confidently wrong answer on these tails."""
    f0 = math.sqrt(max(f_lo, 1e-6) * min(f_hi, sr * 0.45))
    for _ in range(_BAND_SECTIONS):
        sig = _biquad_bp(sig, sr, f0, _BAND_Q)
    return sig
