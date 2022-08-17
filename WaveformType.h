#ifndef _WAVEFORMTYPE_H_
#define _WAVEFORMTYPE_H_

enum class WaveformType {
  SINE = 1,
  SAW,
  TRIANGLE,
  SQUARE,
  NOISE,
};

static inline const std::string to_string(WaveformType type) {
  switch (type) {
  case WaveformType::SINE: return "sine";
  case WaveformType::SAW: return "saw";
  case WaveformType::TRIANGLE: return "triangle";
  case WaveformType::SQUARE: return "square";
  case WaveformType::NOISE: return "noise";
  default: return "";
  }
}

#endif
