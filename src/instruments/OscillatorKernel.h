#ifndef _OSCILLATORKERNEL_H_
#define _OSCILLATORKERNEL_H_

#include "WaveformType.h"
#include "../dsp/Vec8.h"

#include <cstddef>

// The naive oscillator waveforms (sine/saw/triangle/square), computed eight
// samples at a time through compiler vector types (compiled to SSE/AVX/NEON
// as the target allows).
namespace oscillator_kernel {

// Samples of space `mix()` needs for `frames`.
inline size_t paddedFrames(int frames) {
  size_t n = static_cast<size_t>(frames);
  return (n + dsp::kLanes - 1) / dsp::kLanes * dsp::kLanes;
}

// Adds `level` times the next `frames` samples of the waveform into out
// (which must hold paddedFrames(frames) floats), starting at `phase` cycles
// and advancing `rate` cycles per sample. Each group of eight samples starts
// from a double-precision phase, so float rounding doesn't accumulate over a
// block. `pulse_width` is for SQUARE only.
void mix(WaveformType type, float pulse_width, double phase, double rate, float level, int frames, float * out);

}

#endif
