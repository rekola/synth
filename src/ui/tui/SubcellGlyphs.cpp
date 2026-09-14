#include "SubcellGlyphs.h"

const unsigned int kQuadrantCodepoints[16] = {
  0x0020, 0x2598, 0x259D, 0x2580,
  0x2596, 0x258C, 0x259E, 0x259B,
  0x2597, 0x259A, 0x2590, 0x259C,
  0x2584, 0x2599, 0x259F, 0x2588,
};

unsigned int
sextantCodepoint(int mask) {
  if (mask == 0) return 0x0020;
  if (mask == 63) return 0x2588;
  if (mask == 21) return 0x258C;
  if (mask == 42) return 0x2590;
  int index = 0;
  for (int m = 1; m < mask; m++) {
    if (m != 21 && m != 42) index++;
  }
  return 0x1FB00u + static_cast<unsigned int>(index);
}

unsigned int
brailleCodepoint(int mask) {
  if (mask == 0) return 0x0020;
  unsigned int dots = 0;
  if (mask & 0x01) dots |= 0x01; // row0,col0 -> dot1
  if (mask & 0x02) dots |= 0x08; // row0,col1 -> dot4
  if (mask & 0x04) dots |= 0x02; // row1,col0 -> dot2
  if (mask & 0x08) dots |= 0x10; // row1,col1 -> dot5
  if (mask & 0x10) dots |= 0x04; // row2,col0 -> dot3
  if (mask & 0x20) dots |= 0x20; // row2,col1 -> dot6
  if (mask & 0x40) dots |= 0x40; // row3,col0 -> dot7
  if (mask & 0x80) dots |= 0x80; // row3,col1 -> dot8
  return 0x2800u + dots;
}

// How far apart two splits' costs (summed squared 8-bit color components)
// have to be before one genuinely beats the other rather than tying. A
// uniform cell scores an exact zero for every split, and comparing exactly
// leaves that tie for the optimizer to break however its own reassociation
// and reciprocal-based division happen to fall in a fast-math build - so a
// flat cell would come out as an arbitrary half-block with two identical
// colors on one compiler and the plain space of mask 0 on the next. Both
// bounds sit far below any visible color difference.
static constexpr float kTieAbsolute = 1e-3f;
static constexpr float kTieRelative = 1e-5f;

int
quantizeToTwoColors(const std::vector<SubcellRgb> & samples, SubcellRgb & on_color, SubcellRgb & off_color) {
  int n = static_cast<int>(samples.size());
  int best_mask = 0;
  float best_cost = -1.0f;
  SubcellRgb best_on{}, best_off{};

  for (int mask = 0; mask < (1 << n); mask++) {
    SubcellRgb sum_on{0, 0, 0}, sum_off{0, 0, 0};
    int count_on = 0, count_off = 0;
    for (int i = 0; i < n; i++) {
      if (mask & (1 << i)) { sum_on.r += samples[static_cast<size_t>(i)].r; sum_on.g += samples[static_cast<size_t>(i)].g; sum_on.b += samples[static_cast<size_t>(i)].b; count_on++; }
      else { sum_off.r += samples[static_cast<size_t>(i)].r; sum_off.g += samples[static_cast<size_t>(i)].g; sum_off.b += samples[static_cast<size_t>(i)].b; count_off++; }
    }
    // An empty group's sum is exactly zero, so dividing it by a stand-in 1
    // gives the same {0,0,0} mean a zero count would - and never forms the
    // 0/0 a guarding branch would still let a fast-math build speculate
    // its way into computing.
    int divisor_on = count_on > 0 ? count_on : 1, divisor_off = count_off > 0 ? count_off : 1;
    SubcellRgb mean_on{sum_on.r / divisor_on, sum_on.g / divisor_on, sum_on.b / divisor_on};
    SubcellRgb mean_off{sum_off.r / divisor_off, sum_off.g / divisor_off, sum_off.b / divisor_off};

    float cost = 0.0f;
    for (int i = 0; i < n; i++) {
      auto & mean = (mask & (1 << i)) ? mean_on : mean_off;
      float dr = samples[static_cast<size_t>(i)].r - mean.r, dg = samples[static_cast<size_t>(i)].g - mean.g, db = samples[static_cast<size_t>(i)].b - mean.b;
      cost += dr * dr + dg * dg + db * db;
    }

    // Ties keep the earliest candidate, so mask 0 wins a uniform cell.
    if (best_cost < 0.0f || cost < best_cost - (kTieAbsolute + kTieRelative * best_cost)) {
      best_cost = cost;
      best_mask = mask;
      best_on = mean_on;
      best_off = mean_off;
    }
  }

  on_color = best_on;
  off_color = best_off;
  return best_mask;
}
