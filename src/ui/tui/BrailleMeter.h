#ifndef _BRAILLEMETER_H_
#define _BRAILLEMETER_H_

#include "SubcellGlyphs.h"
#include "../../util/Utf8.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Level meters drawn in braille, sharing one dB mapping so every meter
// reads the same level the same way.
namespace braille_meter {

// A linear RMS level's position on a meter: 0 at kFloorDb or below
// (silence, and TrackInfo's -1 "no reading yet" too), 1 at full scale.
// dB-mapped so quiet material still shows.
constexpr float kFloorDb = -40.0f;
inline float fraction(float linear) {
  if (linear <= 0.0f) return 0.0f;
  return std::clamp((20.0f * std::log10(linear) - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
}

// One cell of a vertical bar: its right dot column filled `dots` (0-4)
// from the bottom - the pattern editor's one-cell track meters.
inline std::string verticalCell(int dots) {
  // brailleCodepoint()'s row-major bits: row * 2 + column, row 0 on top.
  int mask = 0;
  for (int d = 0; d < std::clamp(dots, 0, 4); d++) mask |= 1 << ((3 - d) * 2 + 1);
  return Utf8::encodeCodepoint(brailleCodepoint(mask));
}

// A vertical bar `height` cells tall, top cell first, 4 * height steps.
inline std::vector<std::string> verticalBar(float fraction, int height) {
  std::vector<std::string> cells;
  if (height <= 0) return cells;
  auto steps = static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * static_cast<float>(4 * height) + 0.5f);
  for (int cell = height - 1; cell >= 0; cell--) cells.push_back(verticalCell(steps - 4 * cell));
  return cells;
}

}

#endif
