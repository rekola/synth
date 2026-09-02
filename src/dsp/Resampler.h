#ifndef _RESAMPLER_H_
#define _RESAMPLER_H_

#include <vector>

// Linear-interpolation sample-rate converter for a single mono signal -
// SampleFileLoader.cpp's own use (a loaded file's native rate brought to
// the project's output rate) is the only call site today. Adequate for
// one-shot/loop sample-clip content (spoken/percussive material); a
// higher-quality windowed-sinc resampler is a plausible future upgrade
// (third_party/pocketfft/ is already vendored and could back one) if this
// ever proves audibly inadequate - not built now, not an oversight.
//
// `in_rate`/`out_rate` <= 0, or an empty input, return `input` unchanged
// rather than dividing by zero or fabricating frames from nothing.
inline std::vector<float> resampleMonoLinear(const std::vector<float> & input, int in_rate, int out_rate) {
  if (in_rate <= 0 || out_rate <= 0 || in_rate == out_rate || input.empty()) return input;

  double ratio = static_cast<double>(in_rate) / static_cast<double>(out_rate);
  auto out_frames = static_cast<size_t>(static_cast<double>(input.size()) / ratio);
  std::vector<float> output(out_frames);

  for (size_t i = 0; i < out_frames; i++) {
    double src_pos = static_cast<double>(i) * ratio;
    auto idx0 = static_cast<size_t>(src_pos);
    auto idx1 = idx0 + 1 < input.size() ? idx0 + 1 : idx0;
    double frac = src_pos - static_cast<double>(idx0);
    output[i] = static_cast<float>(input[idx0] * (1.0 - frac) + input[idx1] * frac);
  }
  return output;
}

#endif
