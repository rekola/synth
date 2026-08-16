#!/usr/bin/env python3
"""CI/CTest guard: fails if any banned non-deterministic randomness
construct appears in real code anywhere under the repo (outside
third_party/ and build/).

Banned: rand()/srand() (RAND_MAX differs by implementation, the algorithm
is unspecified, and it carries global state unsafe to call from the audio
thread), std::uniform_real_distribution/uniform_int_distribution/
normal_distribution/std::shuffle (the standard never specifies a
distribution's exact output shape, so it differs between libstdc++/
libc++/MSVC even with an identical, portable engine underneath), and
std::mt19937/drand48 directly (same "used to feed a non-portable
distribution" risk, and mt19937 in particular tends to arrive paired with
exactly the distributions above).

The one intended replacement, dsp/HashField.h (plus dsp/NoiseGenerator.h
for the audio-rate-noise case HashField itself deliberately doesn't
cover), is not banned by any of these patterns.

Matching is comment-aware only for single-line `//` comments (the portion
of a line before `//` is checked, the rest is not) - good enough for this
codebase's own comment style (block `/* */` comments are rare here), and
lets code keep discussing the retired rand()/getRandF() mechanism in prose
without tripping this check.
"""

import re
import subprocess
import sys
from pathlib import Path

BANNED_PATTERNS = [
    (re.compile(r"\brand\s*\("), "rand()"),
    (re.compile(r"\bsrand\s*\("), "srand()"),
    (re.compile(r"\buniform_real_distribution\b"), "std::uniform_real_distribution"),
    (re.compile(r"\buniform_int_distribution\b"), "std::uniform_int_distribution"),
    (re.compile(r"\bnormal_distribution\b"), "std::normal_distribution"),
    (re.compile(r"\bstd::shuffle\b"), "std::shuffle"),
    (re.compile(r"\bmt19937\b"), "std::mt19937"),
    (re.compile(r"\bdrand48\s*\("), "drand48()"),
]

EXCLUDED_DIR_PARTS = {"third_party", "build"}


def repo_root() -> Path:
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=Path(__file__).resolve().parent,
        capture_output=True,
        text=True,
        check=True,
    )
    return Path(out.stdout.strip())


def strip_line_comment(line: str) -> str:
    idx = line.find("//")
    return line if idx < 0 else line[:idx]


def main() -> int:
    root = repo_root()
    violations = []

    for path in sorted(root.rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        if EXCLUDED_DIR_PARTS & set(path.relative_to(root).parts[:-1]):
            continue

        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, raw_line in enumerate(text.splitlines(), start=1):
            code = strip_line_comment(raw_line)
            for pattern, label in BANNED_PATTERNS:
                if pattern.search(code):
                    violations.append(f"{path.relative_to(root)}:{lineno}: {label}: {code.strip()}")

    if violations:
        sys.stderr.write("Banned non-deterministic randomness construct(s) found:\n")
        for v in violations:
            sys.stderr.write(f"  {v}\n")
        sys.stderr.write(
            "\nUse dsp/HashField.h instead (per-note/per-object jitter, keyed on a\n"
            "stable coordinate - e.g. NoteCoordinate.h - so the same note always draws\n"
            "the same value, on any platform, any thread, any run), or\n"
            "dsp/NoiseGenerator.h for audio-rate noise (a small stateful PRNG, one\n"
            "instance per voice, seeded once via HashField rather than drawing from a\n"
            "shared/global sequence).\n"
        )
        return 1

    print(f"OK - no banned randomness constructs found ({len(list(root.rglob('*.h'))) + len(list(root.rglob('*.cpp')))} files scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
