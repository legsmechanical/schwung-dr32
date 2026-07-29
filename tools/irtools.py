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

    `band` = (f_lo, f_hi) applies a one-pole bandpass first, for PER-BAND RT60 —
    which is the measurement that separates the shelves from the feedback law.
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


def _bandpass(sig, sr, f_lo, f_hi):
    """One-pole high-pass then low-pass. Gentle, but enough to separate the
    reverb's three regions (below ShelfLoFreq / between / above ShelfHiFreq)."""
    out = []
    a_hi = 1.0 - math.exp(-2.0 * math.pi * f_lo / sr)
    a_lo = 1.0 - math.exp(-2.0 * math.pi * f_hi / sr)
    z_hi = z_lo = 0.0
    for x in sig:
        z_hi += a_hi * (x - z_hi)
        hp = x - z_hi
        z_lo += a_lo * (hp - z_lo)
        out.append(z_lo)
    return out
