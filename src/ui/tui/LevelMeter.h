#ifndef _LEVELMETER_H_
#define _LEVELMETER_H_

#include "SubcellGlyphs.h"
#include "../../util/Utf8.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Vertical level meters drawn from sub-cell glyphs, sharing one dB mapping
// so every meter reads the same level the same way.
namespace level_meter {

// A linear RMS level's position on a meter: 0 at kFloorDb or below
// (silence, and TrackInfo's -1 "no reading yet" too), 1 at full scale.
// dB-mapped so quiet material still shows.
constexpr float kFloorDb = -40.0f;
inline float fraction(float linear) {
  if (linear <= 0.0f) return 0.0f;
  return std::clamp((20.0f * std::log10(linear) - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
}

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
  return Utf8::encodeCodepoint(glyphCodepoint(glyphs, mask));
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

// A displayed level's rise and fall, so a low note doesn't make the bar
// jump between the peaks and zero crossings of successive short blocks.
// Fed the linear RMS of each block; the smoothing is done on power (never
// on RMS or dB values, which still ripple), and the shown level then falls
// at a fixed dB rate.
class Ballistics {
public:
  static constexpr float kIntegrationSeconds = 0.08f;
  static constexpr float kReleaseDbPerSecond = 40.0f;
  static constexpr float kSilenceDb = -100.0f;

  // Returns the level to display as a linear RMS.
  float update(float rms, float dt_seconds) {
    rms = std::max(rms, 0.0f); // "no reading yet" is silence
    float weight = dt_seconds > 0.0f ? 1.0f - std::exp(-dt_seconds / kIntegrationSeconds) : 0.0f;
    power_ += (rms * rms - power_) * weight;
    float target_db = power_ > 1e-10f ? 10.0f * std::log10(power_) : kSilenceDb;
    shown_db_ = target_db >= shown_db_ ? target_db : std::max(target_db, shown_db_ - kReleaseDbPerSecond * dt_seconds);
    return shown_db_ <= kSilenceDb ? 0.0f : std::pow(10.0f, shown_db_ / 20.0f);
  }

private:
  float power_ = 0.0f;
  float shown_db_ = kSilenceDb;
};

// A peak marker: follows a rising level at once, holds still for a moment,
// then falls back much more slowly than the level itself. Works on a
// meter's 0..1 fraction, so it doesn't depend on the meter's size.
class PeakHold {
public:
  static constexpr float kHoldSeconds = 0.5f;
  static constexpr float kFallFractionPerSecond = 0.6f;

  float update(float fraction, float dt_seconds) {
    if (fraction >= held_) {
      held_ = fraction;
      hold_left_ = kHoldSeconds;
    } else if (hold_left_ > 0.0f) {
      hold_left_ -= dt_seconds;
    } else {
      held_ = std::max(fraction, held_ - kFallFractionPerSecond * dt_seconds);
    }
    return held_;
  }

private:
  float held_ = 0.0f;
  float hold_left_ = 0.0f;
};

}

#endif
