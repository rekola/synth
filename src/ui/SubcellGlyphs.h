#pragma once

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
