#ifndef _SATURATORSHAPE_H_
#define _SATURATORSHAPE_H_

#include <string>

// Haze's (the bus saturator, bus/Haze.h) waveshaper curve - same
// standalone, no-dependency-on-the-effect's-own-header shape as
// bus/DelayPattern.h.
enum class SaturatorShape { Tanh = 0, Asym, SoftClip, Fold };

static inline const std::string to_string(SaturatorShape shape) {
  switch (shape) {
  case SaturatorShape::Tanh: return "tanh";
  case SaturatorShape::Asym: return "asym";
  case SaturatorShape::SoftClip: return "softclip";
  case SaturatorShape::Fold: return "fold";
  }
  return "tanh";
}

static inline SaturatorShape parseSaturatorShape(const std::string & text) {
  if (text == "asym") return SaturatorShape::Asym;
  if (text == "softclip") return SaturatorShape::SoftClip;
  if (text == "fold") return SaturatorShape::Fold;
  return SaturatorShape::Tanh;
}

#endif
