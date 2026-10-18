#!/usr/bin/env python3
"""Measurements on a `synth --render` WAV (32-bit float), standard library only.

  grid     F0 [--edo N] [--stretch E] [--partials N]
           the model's partial frequencies for a note
  lines    WAV --freq F --start S --dur D [--width HZ]
           spectral lines near F: the shared-partial check for a chord
  partials WAV --start S --dur D --freqs F1,F2,... [--hop SEC]
           level in dB of each frequency per hop
  aweight  WAV --start S --dur D --f0 F [--edo N] [--stretch E] [--partials N]
           A-weighted level in dB of a note, from its measured partials
  peak     WAV [--start S --dur D]
           peak absolute sample and its dB

Time arguments are in seconds. Channels are summed (mono) except in `peak`.
"""
import argparse
import cmath
import math
import struct
import sys


def read_wav(path):
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:4] == b'RIFF' and data[8:12] == b'WAVE', 'not a WAV file'
    pos, fmt = 12, None
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], struct.unpack('<I', data[pos + 4:pos + 8])[0]
        body = pos + 8
        if tag == b'fmt ':
            fmt = struct.unpack('<HHIIHH', data[body:body + 16])
        elif tag == b'data':
            fmt_tag, channels, rate, _, _, bits = fmt
            assert bits == 32 and fmt_tag in (3, 0xFFFE), 'expected 32-bit float'
            count = size // 4
            samples = struct.unpack('<%df' % count, data[body:body + count * 4])
            return samples, channels, rate
        pos = body + size + (size & 1)
    raise ValueError('no data chunk')


def mono(samples, channels, start, dur, rate):
    a = int(start * rate)
    b = len(samples) // channels if dur is None else min(len(samples) // channels, a + int(dur * rate))
    if channels == 1:
        return list(samples[a:b])
    return [sum(samples[i * channels:(i + 1) * channels]) / channels for i in range(a, b)]


def grid_partials(f0, edo, stretch, count):
    out = []
    for n in range(1, count + 1):
        ratio = 2 ** (round(edo * math.log2(n)) / edo) if edo else n
        out.append(f0 * (ratio + (n - 1) * stretch))
    return out


def a_weight_db(f):
    f2 = f * f
    ra = (12194.0 ** 2 * f2 * f2) / ((f2 + 20.6 ** 2) * math.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) * (f2 + 12194.0 ** 2))
    return 20 * math.log10(ra) + 2.0


def goertzel_power(x, f, rate):
    w = 2 * math.pi * f / rate
    n = len(x)
    s = 0j
    # Hann-windowed single-bin DFT.
    for i, v in enumerate(x):
        s += v * (0.5 - 0.5 * math.cos(2 * math.pi * i / n)) * cmath.exp(-1j * w * i)
    return (abs(s) * 2 / n * 2) ** 2 / 2


def db(p):
    return 10 * math.log10(max(p, 1e-20))


def cmd_grid(a):
    for n, f in enumerate(grid_partials(a.f0, a.edo, a.stretch, a.partials), 1):
        print('%2d  %9.3f Hz' % (n, f))


def cmd_lines(a):
    samples, ch, rate = read_wav(a.wav)
    x = mono(samples, ch, a.start, a.dur, rate)
    # Mix the band down to baseband and decimate, then take a fine DFT.
    dec = max(1, int(rate / (4 * a.width)))
    base = []
    for k in range(0, len(x) - dec, dec):
        s = 0j
        for i in range(dec):
            t = k + i
            s += x[t] * cmath.exp(-2j * math.pi * a.freq * t / rate)
        base.append(s / dec)
    n, drate = len(base), rate / dec
    win = [0.5 - 0.5 * math.cos(2 * math.pi * i / n) for i in range(n)]
    step = 0.05
    freqs = [a.freq - a.width + step * i for i in range(int(2 * a.width / step) + 1)]
    mags = []
    for fo in freqs:
        d = fo - a.freq
        s = 0j
        for i in range(n):
            s += base[i] * win[i] * cmath.exp(-2j * math.pi * d * i / drate)
        mags.append(abs(s))
    top = max(mags)
    peaks = [(freqs[i], mags[i]) for i in range(1, len(mags) - 1)
             if mags[i] > mags[i - 1] and mags[i] >= mags[i + 1] and mags[i] > top * 0.1]
    print('lines near %.2f Hz (%.2f s from %.2f s):' % (a.freq, a.dur, a.start))
    for f, m in sorted(peaks):
        print('  %9.2f Hz  %6.1f dB' % (f, 20 * math.log10(m / top)))
    if len(peaks) > 1:
        fs = [p[0] for p in peaks]
        print('  spread %.2f Hz over %d lines' % (max(fs) - min(fs), len(fs)))


def cmd_partials(a):
    samples, ch, rate = read_wav(a.wav)
    x = mono(samples, ch, a.start, a.dur, rate)
    freqs = [float(v) for v in a.freqs.split(',')]
    hop, win = int(a.hop * rate), int(0.1 * rate)
    print('time  ' + ' '.join('%8.1f' % f for f in freqs))
    for at in range(0, max(1, len(x) - win), hop):
        seg = x[at:at + win]
        print('%5.2f ' % (a.start + at / rate) + ' '.join('%8.1f' % db(goertzel_power(seg, f, rate)) for f in freqs))


def cmd_aweight(a):
    samples, ch, rate = read_wav(a.wav)
    x = mono(samples, ch, a.start, a.dur, rate)
    total = 0.0
    for f in grid_partials(a.f0, a.edo, a.stretch, a.partials):
        if f < rate / 2:
            total += goertzel_power(x, f, rate) * 10 ** (a_weight_db(f) / 10)
    print('A-weighted level of %.2f Hz note: %.1f dB (re full scale, partial power)' % (a.f0, db(total)))


def cmd_peak(a):
    samples, ch, rate = read_wav(a.wav)
    lo = int(a.start * rate) * ch
    hi = len(samples) if a.dur is None else min(len(samples), lo + int(a.dur * rate) * ch)
    peak = max(abs(v) for v in samples[lo:hi])
    print('peak %.4f (%.1f dBFS)' % (peak, 20 * math.log10(max(peak, 1e-12))))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)

    def add(name, fn, wav=True):
        p = sub.add_parser(name)
        if wav:
            p.add_argument('wav')
        p.set_defaults(fn=fn)
        return p

    p = add('grid', cmd_grid, wav=False)
    p.add_argument('f0', type=float)
    p.add_argument('--edo', type=int, default=31)
    p.add_argument('--stretch', type=float, default=0.0)
    p.add_argument('--partials', type=int, default=28)

    p = add('lines', cmd_lines)
    p.add_argument('--freq', type=float, required=True)
    p.add_argument('--start', type=float, default=0.0)
    p.add_argument('--dur', type=float, default=3.0)
    p.add_argument('--width', type=float, default=6.0)

    p = add('partials', cmd_partials)
    p.add_argument('--freqs', required=True)
    p.add_argument('--start', type=float, default=0.0)
    p.add_argument('--dur', type=float, default=3.0)
    p.add_argument('--hop', type=float, default=0.2)

    p = add('aweight', cmd_aweight)
    p.add_argument('--f0', type=float, required=True)
    p.add_argument('--start', type=float, default=0.0)
    p.add_argument('--dur', type=float, default=1.0)
    p.add_argument('--edo', type=int, default=12)
    p.add_argument('--stretch', type=float, default=0.0)
    p.add_argument('--partials', type=int, default=28)

    p = add('peak', cmd_peak)
    p.add_argument('--start', type=float, default=0.0)
    p.add_argument('--dur', type=float, default=None)

    a = ap.parse_args()
    a.fn(a)


if __name__ == '__main__':
    sys.exit(main())
