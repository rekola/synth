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
