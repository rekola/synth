#ifndef _OSCILLATORARRAY_H_
#define _OSCILLATORARRAY_H_

#include "WaveformType.h"

#include <cmath>
#include <cstddef>
#include <vector>

// A struct-of-arrays set of free-running naive oscillators (sine/saw/
// triangle/square), all sharing one base frequency and each with its own
// frequency ratio, phase, level and waveform. Rendering a copy is a plain
// pass over the block, eight samples at a time through compiler vector
// types (compiled to SSE/AVX/NEON as the target allows), so adding another
// unison/fifth/octave copy costs one more vector pass rather than another
// voice object.
//
// Phase is a double per copy, advanced once per block; inside a block each
// group of eight samples starts from a double-derived float phase, so float
// rounding never accumulates across the block.
class OscillatorArray {
 public:
  static constexpr int kLanes = 8;

  struct Copy {
    WaveformType type = WaveformType::SINE;
    float level = 1.0f;       // linear gain applied to the waveform
    float pulse_width = 0.5f; // SQUARE only
    double ratio = 1.0;       // frequency ratio to the base frequency
    double phase = 0.0;       // cycles
  };

  void add(const Copy & copy) { copies_.push_back(copy); }
  size_t size() const { return copies_.size(); }
  const Copy & operator[](size_t i) const { return copies_[i]; }

  // Samples of scratch space renderCopy() needs for `frames`.
  static size_t paddedFrames(int frames) {
    size_t n = static_cast<size_t>(frames);
    return (n + kLanes - 1) / kLanes * kLanes;
  }

  // Writes copy i's next `frames` samples (times its level) into out, which
  // must hold paddedFrames(frames) floats. `base_rate` is the base
  // frequency in cycles per sample. Doesn't advance the phase.
  void renderCopy(size_t i, double base_rate, int frames, float * out) const;

  // The same, but adds into out instead of overwriting it, so a stack sums
  // its members in the one pass that computes them.
  void mixCopy(size_t i, double base_rate, int frames, float * out) const;

  // Multiplies every copy's level, e.g. by the note's velocity.
  void scaleLevels(float scale) {
    for (auto & c : copies_) c.level *= scale;
  }

  // Advances every copy's phase by `frames` samples.
  void advance(double base_rate, int frames) {
    for (auto & c : copies_) {
      c.phase += c.ratio * base_rate * frames;
      c.phase -= std::floor(c.phase);
    }
  }

  // sin(2*pi*turns) by range reduction and an odd polynomial - the same
  // function the vector kernel evaluates, exposed so tests can compare.
  static float sineTurns(float turns);

 private:
  template <bool Add> void renderImpl(size_t i, double base_rate, int frames, float * out) const;

  std::vector<Copy> copies_;
};

#endif
