#ifndef _FMINDEXDECAY_H_
#define _FMINDEXDECAY_H_

#include <cmath>

// Exponentially decaying modulation index. The value is snapped to exactly 0
// once it is far below audibility, so it never lingers in the denormal range
// (a CPU slowdown on some hardware) however long the voice keeps sounding.
class FMIndexDecay {
public:
  // time_constant <= 0 keeps the index constant.
  FMIndexDecay(float initial, float time_constant, float sample_rate)
    : value_(initial),
      step_(time_constant > 0.0f ? std::exp(-1.0 / (static_cast<double>(time_constant) * static_cast<double>(sample_rate))) : 1.0) { }

  void scale(float factor) { value_ *= factor; }
  float value() const { return value_; }

  // Advances one sample.
  void advance() {
    value_ = static_cast<float>(static_cast<double>(value_) * step_);
    if (value_ < kFloor) value_ = 0.0f;
  }

private:
  // Phase deviation (radians) far below anything audible.
  static constexpr float kFloor = 1e-6f;

  float value_;
  double step_;
};

#endif
