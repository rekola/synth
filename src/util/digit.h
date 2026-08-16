#ifndef _DIGIT_H_
#define _DIGIT_H_

#include <cstdint>

// Parses a single digit character in the given radix (e.g. 16 for hex);
// -1 if `c` isn't a valid digit for that radix. Also accepts the fullwidth
// uppercase/lowercase Latin letter code points (U+FF21-FF3A/FF41-FF5A),
// not just ASCII a-z/A-Z.
inline int digit(int32_t c, int radix) noexcept {
  if (c >= '0' && c <= '9') {
    c -= '0';
  }
  else if (c >= 'a' && c <= 'z') {
    c -= 'a' - 10;
  }
  else if (c >= 'A' && c <= 'Z') {
    c -= 'A' - 10;
  }
  else if (c >= 0xff21 && c <= 0xff3a) { /* fullwidth uppercase Latin letters */
    c -= 0xff21 - 10;
  }
  else if (c >= 0xff41 && c <= 0xff5a) { /* fullwidth lowercase Latin letters */
    c -= 0xff41 - 10;
  }
  else {
    return -1;
  }
  if (c < radix)
    return c;
  else
    return -1;
}

#endif
