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
