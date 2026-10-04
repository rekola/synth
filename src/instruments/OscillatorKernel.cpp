#include "OscillatorKernel.h"

#include "../dsp/Vec8.h"

#include <cmath>

namespace {

using dsp::kLanes;
using dsp::splat;
using dsp::v8f;

using dsp::fract;
using dsp::loadu;
using dsp::sineOfFraction;
using dsp::storeu;

}

namespace oscillator_kernel {

void
mix(WaveformType type, float pulse_width, double phase, double rate, float level, int frames, float * out, PitchDriftMember * drift, const PitchDriftPoint * points) {
  const v8f index = v8f{ 0, 1, 2, 3, 4, 5, 6, 7 };
  v8f lanes = index * splat(static_cast<float>(rate));
  const bool drifting = drift && points && drift->active();
  const double drift_origin = drifting ? drift->integral(points[0]) : 0.0;
  const v8f level_v = splat(level);
  const v8f pulse = splat(pulse_width);
  const v8f half = splat(0.5f), one = splat(1.0f);

  const size_t groups = paddedFrames(frames) / kLanes;
  for (size_t g = 0; g < groups; g++) {
    // Each group of eight starts from a double-derived phase, so float
    // rounding never accumulates across the block.
    double start = phase + rate * static_cast<double>(g * kLanes);
    if (drifting) {
      double deviation, integral;
      drift->sample(points[g], deviation, integral);
      start += rate * (integral - drift_origin);
      lanes = index * splat(static_cast<float>(rate * (1.0 + deviation)));
    }
    start -= std::floor(start);
    v8f f = fract(splat(static_cast<float>(start)) + lanes);

    v8f a;
    switch (type) {
    case WaveformType::SINE: a = sineOfFraction(f); break;
    case WaveformType::SAW: a = (f < half) ? f * splat(2.0f) : f * splat(2.0f) - splat(2.0f); break;
    case WaveformType::TRIANGLE: a = (f < half) ? one - f * splat(4.0f) : f * splat(4.0f) - splat(3.0f); break;
    case WaveformType::SQUARE: a = (f < pulse) ? -one : one; break;
    default: a = splat(0.0f); break;
    }

    storeu(out + g * kLanes, a * level_v + loadu(out + g * kLanes));
  }
}

}
