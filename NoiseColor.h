#ifndef _NOISECOLOR_H_
#define _NOISECOLOR_H_

#include <string>

enum class NoiseColor {
  WHITE = 1,
  PINK
};

static inline const std::string to_string(NoiseColor color) {
  switch (color) {
  case NoiseColor::WHITE: return "white";
  case NoiseColor::PINK: return "pink";
  default: return "";
  }
}

#endif
