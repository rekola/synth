#pragma once

#include <cellui/term/SubcellGlyphs.h>
#include <cellui/Utf8.h>

#include <algorithm>
#include <string>
#include <vector>

namespace cellui::term {

// Vertical level meters drawn from sub-cell glyphs: the terminal's
// fallback when a display shows no pictures.
namespace glyph_meter {

// The sub-cell glyphs a meter is drawn with. Both have two columns per
// cell; BRAILLE has 4 steps per cell, BLOCKS (sextants, needs terminal
// support) 3.
enum class Glyphs { BRAILLE, BLOCKS };

constexpr int stepsPerCell(Glyphs glyphs) { return glyphs == Glyphs::BRAILLE ? 4 : 3; }

inline unsigned int glyphCodepoint(Glyphs glyphs, int mask) {
  return glyphs == Glyphs::BRAILLE ? brailleCodepoint(mask) : sextantCodepoint(mask);
}

// One column of a cell: `fill` steps lit from the bottom, and an optional
// single lit step (`peak`, 1-based from the bottom, 0 = none) that can
// float above the fill.
struct CellColumn {
  int fill = 0;
  int peak = 0;
};

// One cell of two side-by-side vertical columns.
inline std::string cell(Glyphs glyphs, CellColumn left, CellColumn right) {
  int rows = stepsPerCell(glyphs);
  // The glyph masks' row-major bits: row * 2 + column, row 0 on top.
  int mask = 0;
  for (int row = 0; row < rows; row++) {
    int step = rows - row; // 1-based from the bottom
    if (step <= left.fill || step == left.peak) mask |= 1 << (row * 2);
    if (step <= right.fill || step == right.peak) mask |= 1 << (row * 2 + 1);
  }
  return utf8::encodeCodepoint(glyphCodepoint(glyphs, mask));
}

// One cell of a vertical bar in its right column - the pattern editor's
// one-cell track meters.
inline std::string verticalCell(int dots) {
  return cell(Glyphs::BRAILLE, {}, {std::clamp(dots, 0, 4), 0});
}

// A column of a bar, in whole-bar steps: `steps` lit from the bottom, and
// a peak marker at `peak_steps` (0 = none).
struct BarColumn {
  int steps = 0;
  int peak_steps = 0;
};

inline int barSteps(float fraction, int height, Glyphs glyphs = Glyphs::BRAILLE) {
  return static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * static_cast<float>(stepsPerCell(glyphs) * height) + 0.5f);
}

// A bar `height` cells tall, top cell first, of one or two columns.
inline std::vector<std::string> verticalBar(Glyphs glyphs, int height, BarColumn left, BarColumn right) {
  std::vector<std::string> cells;
  if (height <= 0) return cells;
  int per_cell = stepsPerCell(glyphs);
  auto columnIn = [&](const BarColumn & column, int index) {
    int base = index * per_cell;
    CellColumn result;
    result.fill = std::clamp(column.steps - base, 0, per_cell);
    if (column.peak_steps > base && column.peak_steps <= base + per_cell) result.peak = column.peak_steps - base;
    return result;
  };
  for (int index = height - 1; index >= 0; index--) cells.push_back(cell(glyphs, columnIn(left, index), columnIn(right, index)));
  return cells;
}

// A vertical bar in the right column only, 4 * height steps; the left
// column stays free for a caller that has no use for it.
inline std::vector<std::string> verticalBar(float fraction, int height, float peak_fraction = 0.0f, Glyphs glyphs = Glyphs::BRAILLE) {
  return verticalBar(glyphs, height, {}, {barSteps(fraction, height, glyphs), barSteps(peak_fraction, height, glyphs)});
}

}  // namespace glyph_meter

}  // namespace cellui::term
