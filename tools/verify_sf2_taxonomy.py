#!/usr/bin/env python3
"""Verify docs/instrument-paths.md's (bank, program) -> GM-name table against
a real .sf2 file's own phdr chunk.

Usage: tools/verify_sf2_taxonomy.py path/to/font.sf2 [path/to/instrument-paths.md]

Parses only the RIFF/LIST structure down to pdta/phdr - not a full SF2
loader (see src/instruments/SoundFont.cpp for that). phdr is a fixed
38-byte record: 20-byte name, u16 preset, u16 bank, u16 presetBagNdx,
u32 library, u32 genre, u32 morphology (SoundFont.cpp's own
phdrSizeInFile confirms this layout). The file's real preset list always
ends with a terminal "EOP" sentinel record, excluded here the same way
SoundFont.cpp's tsf_load_presets() excludes it (hydra.phdrNum - 1).
"""

import re
import struct
import sys
from pathlib import Path


def read_chunks(data, start, end):
    """Yield (fourcc, payload_start, payload_end) for each plain chunk in [start, end)."""
    pos = start
    while pos + 8 <= end:
        fourcc = data[pos:pos + 4].decode("ascii", "replace")
        size = struct.unpack_from("<I", data, pos + 4)[0]
        payload_start = pos + 8
        payload_end = payload_start + size
        yield fourcc, payload_start, payload_end
        pos = payload_end + (size & 1)  # chunks are word-aligned


def find_pdta(data):
    assert data[0:4] == b"RIFF"
    riff_size = struct.unpack_from("<I", data, 4)[0]
    assert data[8:12] == b"sfbk"
    for fourcc, pstart, pend in read_chunks(data, 12, 12 + riff_size - 4):
        if fourcc != "LIST":
            continue
        list_type = data[pstart:pstart + 4].decode("ascii", "replace")
        if list_type == "pdta":
            return pstart + 4, pend
    raise ValueError("no pdta LIST chunk found")


def read_phdr(data, path):
    pdta_start, pdta_end = find_pdta(data)
    for fourcc, pstart, pend in read_chunks(data, pdta_start, pdta_end):
        if fourcc != "phdr":
            continue
        records = []
        pos = pstart
        while pos + 38 <= pend:
            name = data[pos:pos + 20].split(b"\0", 1)[0].decode("ascii", "replace")
            preset, bank = struct.unpack_from("<HH", data, pos + 20)
            records.append((bank, preset, name))
            pos += 38
        # Drop the terminal EOP sentinel, same as tsf_load_presets().
        if records and records[-1][2] == "EOP":
            records.pop()
        return records
    raise ValueError(f"no phdr sub-chunk found in {path}")


def parse_taxonomy_md(md_path):
    """Returns {(bank, program): (gm_name, path)} from both tables in the doc."""
    text = Path(md_path).read_text()
    result = {}

    # Bank 0 table: | Prog | GM# | GM name | Path |
    for m in re.finditer(
        r"^\|\s*(\d+)\s*\|\s*\d+\s*\|\s*(.+?)\s*\|\s*`([^`]+)`\s*\|\s*$",
        text, re.MULTILINE,
    ):
        prog, name, path = m.groups()
        result[(0, int(prog))] = (name, path)

    # Bank 128 table: | Bank:Prog | Kit | Path |
    for m in re.finditer(
        r"^\|\s*128:(\d+)\s*\|\s*(.+?)\s*\|\s*`([^`]+)`\s*\|\s*$",
        text, re.MULTILINE,
    ):
        prog, name, path = m.groups()
        result[(128, int(prog))] = (name, path)

    return result


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    font_path = sys.argv[1]
    md_path = sys.argv[2] if len(sys.argv) > 2 else "docs/instrument-paths.md"

    data = Path(font_path).read_bytes()
    records = read_phdr(data, font_path)
    by_bank_prog = {}
    for bank, preset, name in records:
        by_bank_prog.setdefault((bank, preset), []).append(name)

    taxonomy = parse_taxonomy_md(md_path)

    print(f"# {font_path}: {len(records)} presets, {len(taxonomy)} taxonomy rows checked\n")

    missing, name_mismatch, ok = [], [], 0
    for (bank, prog), (gm_name, path) in sorted(taxonomy.items()):
        names = by_bank_prog.get((bank, prog))
        if names is None:
            missing.append((bank, prog, gm_name, path))
        else:
            # Case/whitespace-insensitive containment check - sf2 preset
            # names are inconsistently capitalized/abbreviated across fonts.
            found = any(gm_name.lower() in n.lower() or n.lower() in gm_name.lower() for n in names)
            if found:
                ok += 1
            else:
                name_mismatch.append((bank, prog, gm_name, path, names))

    print(f"OK: {ok}")
    print(f"\nMISSING ({len(missing)}):")
    for bank, prog, gm_name, path in missing:
        print(f"  {bank}:{prog}  {gm_name!r} ({path}) - not present in font")

    print(f"\nNAME MISMATCH ({len(name_mismatch)}):")
    for bank, prog, gm_name, path, names in name_mismatch:
        print(f"  {bank}:{prog}  expected {gm_name!r} ({path}), font has {names!r}")

    # Also report: does array index == program number for contiguous bank-0
    # 0-127? (the assumption the current raw-index createInstrument() calls
    # depend on.)
    bank0 = sorted(p for b, p in by_bank_prog if b == 0)
    contiguous = bank0 == list(range(len(bank0)))
    print(f"\nBank-0 presets contiguous from 0 (index==program holds): {contiguous}")
    if not contiguous:
        gaps = [p for p in range(max(bank0) + 1) if p not in bank0] if bank0 else []
        print(f"  gaps/missing programs: {gaps}")


if __name__ == "__main__":
    main()
