#ifndef _MATHUTILS_H_
#define _MATHUTILS_H_

#include <cmath>

// The fractional part of x, always in [0, 1) - e.g. frac(2.75f) == 0.75f,
// frac(-0.25f) == 0.75f (unlike std::fmod, which would return -0.25f).
inline float frac(float x) {
  return x - std::floor(x);
}

#endif
