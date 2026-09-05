#pragma once

#include <vector>

// Unicode Block Elements (2x2, always-supported) and Symbols for Legacy
// Computing sextant (2x3, capability-gated) sub-cell glyph tables - shared
// by every widget that paints sub-character-resolution content (currently
// TerminalHeatmapChart's 2D field and PatternEditor's own waveform boxes),
// so the mask-to-codepoint mapping is declared exactly once.

// Unicode Block Elements (U+2580-U+259F) quadrant glyphs, indexed by a
// 4-bit mask - bit0=upper-left, bit1=upper-right, bit2=lower-left,
// bit3=lower-right, 1 = the "on"/foreground group. Covers all 16 on/off
// patterns of a 2x2 sub-cell (mask 0, all-off, is plain space U+0020,
// outside the Block Elements range). Near-universally supported - the
// fallback tier for a terminal that doesn't report sextant support (see
// sextantCodepoint() below).
extern const unsigned int kQuadrantCodepoints[16];

// Unicode 13 sextant glyph (Symbols for Legacy Computing) for a 6-bit
// mask over a 2-column x 3-row sub-cell, bit weights top-left=1,
// top-right=2, mid-left=4, mid-right=8, bottom-left=16, bottom-right=32
// (i.e. bit = row*2+col, the same row-major convention kQuadrantCodepoints
// uses, just with 3 rows instead of 2), 1 = "on"/foreground. Of the 64
// patterns, 4 reuse pre-existing codepoints instead of the new block:
// mask 0 (all off) = space U+0020, mask 63 (all on) = full block U+2588,
// mask 21 (0b010101, left column only) = LEFT HALF BLOCK U+258C, mask 42
// (0b101010, right column only) = RIGHT HALF BLOCK U+2590. The remaining
// 60 masks get consecutive new codepoints U+1FB00..U+1FB3B in mask order,
// skipping 21/42. Only actually a real glyph on a terminal that reports
// notcurses_cansextant() - checked once via UIPlane::canRenderSextants(),
// not per call.
unsigned int sextantCodepoint(int mask);

// Unicode Braille Patterns (U+2800-U+28FF) for an 8-bit mask over a
// 2-column x 4-row sub-cell, row-major like the two tables above (bit =
// row*2+col, 1 = "on"/foreground) - finer than sextants' 2x3 and needs no
// capability check at all: the block is old/near-universally supported.
// The codepoint's own bit order isn't row-major (it's the historical
// 6/8-dot braille cell numbering: dot1/2/3/7 down the left column,
// dot4/5/6/8 down the right), so this permutes the row-major mask into
// that order rather than adding row*2+col directly to the block's base
// codepoint. Mask 0 (all off) is plain space U+0020, matching the other
// two tables' own convention, rather than the technically-blank but
// visually-identical braille cell U+2800.
unsigned int brailleCodepoint(int mask);

// A plain float-triple RGB color - kept separate from the model-layer
// Color class (Color.h, 8-bit components, no += operator) since
// quantizeToTwoColors() below needs to accumulate running sums/means at
// full precision; converted to/from a real Color at each caller's own
// edges.
struct SubcellRgb { float r, g, b; };

// Exact (brute-force) optimal 2-color quantization of `samples` (one
// character cell's own sub-cell colors, already computed by the caller -
// up to 4 for quadrants, 6 for sextants) into an "on"/"off" group,
// minimizing total squared color error against each group's own mean -
// cheap at this size (at most 2^n candidate splits for n <= 6) and,
// unlike a fixed brightness threshold, accounts for hue/saturation
// variation too, not just brightness. Returns the winning bitmask (bit i
// set = sample i is in the "on"/foreground group, row-major, matching
// kQuadrantCodepoints'/sextantCodepoint's own bit convention) and writes
// the two group-mean colors out. Shared by TerminalHeatmapChart (its own
// DirAC directional field) and PatternEditor's own waveform boxes (its
// per-row antialiased coverage) - the same "best 2-color fit for a
// handful of already-computed samples" problem either way, regardless of
// what the samples themselves represent.
int quantizeToTwoColors(const std::vector<SubcellRgb> & samples, SubcellRgb & on_color, SubcellRgb & off_color);
