#!/usr/bin/env python3
"""Searches for the best tonic-anchor origin for the Launchpad isomorphic
note-entry grid (LaunchpadLayout::computeBasis/classifyPad/noteForPad in
the synth engine, and LaunchpadManager's GRID_ORIGIN_X/Y which shift pad
(x,y) before calling into those pure functions).

LaunchpadLayout's own functions always treat pad (0,0) as the tonic - a
pure, logical coordinate system with no notion of a physical "middle".
Anchoring that logical origin at the corner (the original behavior) pushes
the diatonic scale out along the grid's edges and leaves DIESIS (the fine
EDOs' dense, quarter-tone-ish in-between notes - there are far more of
them than diatonic degrees) dominating the middle, since the middle ends
up farthest from the one tonic-anchored corner.

This script re-implements that same math (computeBasis/diatonicDegrees/
classify below mirror LaunchpadLayout.cpp line for line - keep them in
sync if that file's algorithm ever changes) purely in Python so candidate
origins can be scored exhaustively without rebuilding/running the real
app. Re-run this whenever the priority EDO or the desired grid size
changes - the origin baked into LaunchpadManager.cpp's GRID_ORIGIN_X/Y
constants was chosen this way (31-EDO prioritized, then a second pass
centering the horizontal span between the two tonic pads that origin
brings into view - see this file's __main__ section for that exact
search sequence).

Usage:
  python3 tools/launchpad_grid_origin.py                  # search all origins for 31-EDO (8x8 grid), report the best
  python3 tools/launchpad_grid_origin.py --edo 53          # a different EDO
  python3 tools/launchpad_grid_origin.py --edo 31 --grid-size 8 --origin 1 3   # show one specific origin's grid
"""

import argparse
import math


def compute_basis(edo_steps):
    """Mirrors LaunchpadLayout::computeBasis exactly."""
    fifth = round(edo_steps * math.log2(3.0 / 2.0))
    whole_tone = 2 * fifth - edo_steps
    semitone = 3 * edo_steps - 5 * fifth
    degenerate = whole_tone <= 0 or semitone <= 0 or whole_tone == semitone
    return fifth, whole_tone, semitone, degenerate


def diatonic_degrees(edo_steps, fifth):
    """Mirrors LaunchpadLayout::diatonicScaleDegrees exactly - the chain of
    fifths from one below the tonic through five above it."""
    degrees = []
    for k in range(-1, 6):
        v = k * fifth
        degrees.append(((v % edo_steps) + edo_steps) % edo_steps)
    return degrees


def classify(edo_steps, degrees, pitch_class):
    """Mirrors LaunchpadLayout::classifyPad exactly (given a pitch class
    already reduced mod edo_steps)."""
    if pitch_class == 0:
        return "TONIC"
    dist_below = edo_steps
    dist_above = edo_steps
    for d in degrees:
        up = ((pitch_class - d) % edo_steps + edo_steps) % edo_steps
        if up == 0:
            return "DIATONIC"
        dist_below = min(dist_below, up)
        down = ((d - pitch_class) % edo_steps + edo_steps) % edo_steps
        dist_above = min(dist_above, down)
    if dist_below < dist_above:
        return "SHARP" if dist_below == 2 else "DIESIS"
    if dist_above < dist_below:
        return "FLAT" if dist_above == 2 else "DIESIS"
    return "ACCIDENTAL"


def build_grid(edo_steps, origin_x, origin_y, grid_size=8):
    """Returns (grid, tonic_positions) for the given EDO and origin - grid
    is a dict of y -> list of category strings, x=0 first (bottom-left)."""
    fifth, T, S, degenerate = compute_basis(edo_steps)
    if degenerate:
        raise ValueError(f"EDO {edo_steps} is degenerate (no meaningful diatonic structure)")
    degrees = diatonic_degrees(edo_steps, fifth)

    grid = {}
    tonic_positions = []
    for y in range(grid_size):
        row = []
        for x in range(grid_size):
            pc = ((x - origin_x) * T + (y - origin_y) * S) % edo_steps
            cat = classify(edo_steps, degrees, pc)
            if cat == "TONIC":
                tonic_positions.append((x, y))
            row.append(cat)
        grid[y] = row
    return grid, tonic_positions


def center_score(grid, grid_size=8):
    """(diatonic + tonic count) - (diesis count) in the middle half of the
    grid (rounded down) - the metric used to compare candidate origins."""
    lo, hi = grid_size // 4, grid_size - grid_size // 4
    diesis = sum(1 for y in range(lo, hi) for x in range(lo, hi) if grid[y][x] == "DIESIS")
    diatonic = sum(1 for y in range(lo, hi) for x in range(lo, hi) if grid[y][x] in ("DIATONIC", "TONIC"))
    return diatonic - diesis


def print_grid(grid, grid_size=8):
    symbol = {"TONIC": "T", "DIATONIC": "d", "SHARP": "#", "FLAT": "b", "DIESIS": "~", "ACCIDENTAL": "a"}
    for y in range(grid_size - 1, -1, -1):
        print(y, " ".join(symbol[c] for c in grid[y]))


def search_best_origin(edo_steps, grid_size=8):
    """All (ox, oy) candidates, sorted by center_score descending."""
    results = []
    for ox in range(grid_size):
        for oy in range(grid_size):
            grid, tonics = build_grid(edo_steps, ox, oy, grid_size)
            results.append((center_score(grid, grid_size), ox, oy, tonics, grid))
    results.sort(key=lambda r: -r[0])
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--edo", type=int, default=31, help="EDO to analyze (default: 31, the priority tuning)")
    parser.add_argument("--grid-size", type=int, default=8, help="grid width/height (default: 8, the Launchpad's)")
    parser.add_argument("--origin", type=int, nargs=2, metavar=("X", "Y"), help="show one specific origin instead of searching")
    args = parser.parse_args()

    if args.origin:
        ox, oy = args.origin
        grid, tonics = build_grid(args.edo, ox, oy, args.grid_size)
        print(f"EDO {args.edo}, origin=({ox},{oy}): score={center_score(grid, args.grid_size)}, tonics={tonics}")
        print_grid(grid, args.grid_size)
    else:
        results = search_best_origin(args.edo, args.grid_size)
        print(f"Top 5 origins for EDO {args.edo} ({args.grid_size}x{args.grid_size} grid), by center score:")
        for score, ox, oy, tonics, grid in results[:5]:
            print(f"\norigin=({ox},{oy}) score={score} tonics={tonics}")
            print_grid(grid, args.grid_size)
