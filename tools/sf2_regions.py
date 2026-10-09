#!/usr/bin/env python3
"""List the regions a SoundFont 2 preset starts for one note (standard library only).

  sf2_regions.py FILE.sf2                         list the presets
  sf2_regions.py FILE.sf2 --preset Yamaha --key 60 --velocity 100
                                                  the regions that note starts

--preset is a case-insensitive part of the preset name, or BANK:PROGRAM
(e.g. 0:0 for the first General MIDI piano). A region is one sample played with
its own pan, level, filter and envelope; a note plays every region whose key
and velocity ranges contain it, so stereo pairs, velocity layers and extra
layers show up as several rows.
"""
import argparse
import struct
import sys

# Generator numbers (SoundFont 2.04, section 8.1.2).
G_KEYRANGE, G_VELRANGE = 43, 44
G_INSTRUMENT, G_SAMPLEID = 41, 53
GEN_NAMES = {
    8: 'filterFc', 9: 'filterQ', 17: 'pan', 33: 'volEnvDelay', 34: 'volEnvAttack',
    35: 'volEnvHold', 36: 'volEnvDecay', 37: 'volEnvSustain', 38: 'volEnvRelease',
    48: 'attenuation', 51: 'coarseTune', 52: 'fineTune', 54: 'sampleModes',
    58: 'rootKey', 56: 'scaleTuning', 57: 'exclusiveClass', 15: 'chorusSend', 16: 'reverbSend',
}
SAMPLE_TYPES = {1: 'mono', 2: 'right', 4: 'left', 8: 'linked'}


def read_chunks(data):
    assert data[:4] == b'RIFF' and data[8:12] == b'sfbk', 'not a SoundFont 2 file'
    pos, out = 12, {}
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], struct.unpack('<I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b'LIST':
            sub, pos2 = body[:4], 4
            while pos2 + 8 <= len(body):
                t2, s2 = body[pos2:pos2 + 4], struct.unpack('<I', body[pos2 + 4:pos2 + 8])[0]
                out[sub.decode() + '/' + t2.decode()] = body[pos2 + 8:pos2 + 8 + s2]
                pos2 += 8 + s2 + (s2 & 1)
        pos += 8 + size + (size & 1)
    return out


def records(blob, fmt):
    size = struct.calcsize(fmt)
    return [struct.unpack_from(fmt, blob, i * size) for i in range(len(blob) // size)]


def name(raw):
    return raw.split(b'\0')[0].decode('latin-1')


class Bank:
    def __init__(self, path):
        with open(path, 'rb') as f:
            c = read_chunks(f.read())
        self.presets = [(name(r[0]), r[1], r[2], r[3]) for r in records(c['pdta/phdr'], '<20sHHHIII')]
        self.pbag = records(c['pdta/pbag'], '<HH')
        self.pgen = records(c['pdta/pgen'], '<Hh')
        self.insts = [(name(r[0]), r[1]) for r in records(c['pdta/inst'], '<20sH')]
        self.ibag = records(c['pdta/ibag'], '<HH')
        self.igen = records(c['pdta/igen'], '<Hh')
        self.shdr = [(name(r[0]),) + r[1:] for r in records(c['pdta/shdr'], '<20sIIIIIBbHH')]

    @staticmethod
    def zones(bags, gens, first, last):
        """Each zone of bag range first..last as a dict of generator -> raw value."""
        out = []
        for b in range(first, last):
            lo, hi = bags[b][0], bags[b + 1][0]
            out.append(dict((g, v) for g, v in gens[lo:hi]))
        return out

    def find(self, text):
        if ':' in text and all(p.isdigit() for p in text.split(':')):
            bank, prog = map(int, text.split(':'))
            return [i for i, p in enumerate(self.presets[:-1]) if p[2] == bank and p[1] == prog]
        return [i for i, p in enumerate(self.presets[:-1]) if text.lower() in p[0].lower()]

    def regions(self, preset_index, key, velocity):
        pz = self.zones(self.pbag, self.pgen, self.presets[preset_index][3], self.presets[preset_index + 1][3])
        pglobal = pz[0] if pz and G_INSTRUMENT not in pz[0] else {}
        out = []
        for zone in pz:
            if G_INSTRUMENT not in zone:
                continue
            if not self._inside(self._merge(pglobal, zone), key, velocity):
                continue
            inst = zone[G_INSTRUMENT] & 0xFFFF
            iz = self.zones(self.ibag, self.igen, self.insts[inst][1], self.insts[inst + 1][1])
            iglobal = iz[0] if iz and G_SAMPLEID not in iz[0] else {}
            for z in iz:
                if G_SAMPLEID not in z:
                    continue
                merged = self._merge(iglobal, z)
                if not self._inside(merged, key, velocity):
                    continue
                if not self._inside(self._merge(pglobal, zone), key, velocity):
                    continue
                gens = dict(merged)
                for g, v in zone.items():  # preset-level values add to the instrument's
                    if g not in (G_INSTRUMENT, G_KEYRANGE, G_VELRANGE) and g in gens:
                        gens[g] += v
                out.append((self.insts[inst][0], self.shdr[z[G_SAMPLEID] & 0xFFFF], gens))
        return out

    @staticmethod
    def _merge(base, zone):
        merged = dict(base)
        merged.update(zone)
        return merged

    @staticmethod
    def _inside(gens, key, velocity):
        for g, v in ((G_KEYRANGE, key), (G_VELRANGE, velocity)):
            if g in gens:
                lo, hi = gens[g] & 0xFF, (gens[g] >> 8) & 0xFF
                if not lo <= v <= hi:
                    return False
        return True


def timecents(v):
    return 2.0 ** (v / 1200.0)


def describe(inst, sample, gens):
    def rng(g):
        if g not in gens:
            return 'all'
        return '%d-%d' % (gens[g] & 0xFF, (gens[g] >> 8) & 0xFF)
    parts = ['%-18s sample %-16s %-6s' % (inst, sample[0], SAMPLE_TYPES.get(sample[9], str(sample[9]))),
             'keys %-7s vel %-7s' % (rng(G_KEYRANGE), rng(G_VELRANGE)),
             'pan %+4.0f%%' % (gens.get(17, 0) / 10.0),
             'atten %.1f dB' % (gens.get(48, 0) / 10.0),
             'root %d' % (gens[58] if 58 in gens and gens[58] >= 0 else sample[6])]
    if 8 in gens:
        parts.append('filter %.0f Hz' % (8.176 * 2.0 ** (gens[8] / 1200.0)))
    mode = gens.get(54, 0) & 3
    parts.append('loop' if mode in (1, 3) else 'one-shot')
    for g, label in ((34, 'attack'), (36, 'decay'), (38, 'release')):
        if g in gens:
            parts.append('%s %.3g s' % (label, timecents(gens[g])))
    if 37 in gens:
        parts.append('sustain -%.1f dB' % (gens[37] / 10.0))
    return '  '.join(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('file')
    ap.add_argument('--preset')
    ap.add_argument('--key', type=int, default=60)
    ap.add_argument('--velocity', type=int, default=100)
    a = ap.parse_args()
    bank = Bank(a.file)
    if not a.preset:
        for p in bank.presets[:-1]:
            print('%3d:%-3d %s' % (p[2], p[1], p[0]))
        return
    found = bank.find(a.preset)
    if not found:
        sys.exit('no preset matches %r' % a.preset)
    for i in found:
        regions = bank.regions(i, a.key, a.velocity)
        print('%s (bank %d program %d): %d region(s) for key %d velocity %d' %
              (bank.presets[i][0], bank.presets[i][2], bank.presets[i][1], len(regions), a.key, a.velocity))
        for inst, sample, gens in regions:
            print('  ' + describe(inst, sample, gens))


if __name__ == '__main__':
    main()
