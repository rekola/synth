#include "AmbisonicStackEncoder.h"

#include <algorithm>
#include <cstring>

namespace {

typedef float v8f __attribute__((vector_size(32)));

constexpr int kLanes = 8;

inline v8f splat(float x) { return v8f{ x, x, x, x, x, x, x, x }; }

// N is the channel count rounded up to what a fixed-size accumulator set
// can hold; only the first n channels are real (the rest have zero gain and
// are never written).
template <int N>
void encodeChannels(float * const * channels, int n, const float * dry, size_t stride, size_t signals, const float * g0, const float * dg, int frames) {
  const int full = frames & ~(kLanes - 1);
  const v8f lane = v8f{ 0, 1, 2, 3, 4, 5, 6, 7 };

  for (int i = 0; i < full; i += kLanes) {
    v8f acc[static_cast<size_t>(N)];
    for (int c = 0; c < N; c++) acc[c] = splat(0.0f);
    const v8f index = splat(static_cast<float>(i)) + lane;

    for (size_t s = 0; s < signals; s++) {
      v8f x;
      std::memcpy(&x, dry + s * stride + static_cast<size_t>(i), sizeof(x));
      const float * a = g0 + s * N;
      const float * b = dg + s * N;
      for (int c = 0; c < N; c++) acc[c] += (splat(a[c]) + splat(b[c]) * index) * x;
    }

    for (int c = 0; c < n; c++) {
      v8f o;
      std::memcpy(&o, channels[c] + i, sizeof(o));
      o += acc[c];
      std::memcpy(channels[c] + i, &o, sizeof(o));
    }
  }

  for (int i = full; i < frames; i++) {
    for (int c = 0; c < n; c++) {
      float sum = 0.0f;
      for (size_t s = 0; s < signals; s++) sum += (g0[s * N + static_cast<size_t>(c)] + dg[s * N + static_cast<size_t>(c)] * static_cast<float>(i)) * dry[s * stride + static_cast<size_t>(i)];
      channels[c][i] += sum;
    }
  }
}

}

void
AmbisonicStackEncoder::encodeBlock(AudioBuffer & out, const float * dry, size_t stride, const std::vector<AmbisonicGains> & targets, int frames) {
  const size_t signals = targets.size();
  if (prev_.size() != signals) prev_ = targets; // first block (or a changed count): no ramp
  if (signals == 0 || frames <= 0) return;

  const int n = std::min(out.regularChannelCount(), kAmbisonicChannelCount);
  if (n <= 0) {
    prev_ = targets;
    return;
  }

  const int width = n <= 1 ? 1 : n <= 4 ? 4 : n <= 9 ? 9 : kAmbisonicChannelCount;
  const size_t w = static_cast<size_t>(width);
  g0_.assign(signals * w, 0.0f);
  dg_.assign(signals * w, 0.0f);

  // Same ramp as AmbisonicVoiceEncoder: prev at the first sample, target at
  // the last (a one-sample block just takes the target).
  const float step = frames > 1 ? 1.0f / static_cast<float>(frames - 1) : 0.0f;
  for (size_t s = 0; s < signals; s++) {
    for (size_t c = 0; c < static_cast<size_t>(n); c++) {
      const float from = frames > 1 ? prev_[s][c] : targets[s][c];
      g0_[s * w + c] = from;
      dg_[s * w + c] = (targets[s][c] - prev_[s][c]) * step;
    }
  }

  float * channels[kAmbisonicChannelCount] = {};
  for (int c = 0; c < n; c++) channels[c] = out.getChannelData(c);

  switch (width) {
  case 1: encodeChannels<1>(channels, n, dry, stride, signals, g0_.data(), dg_.data(), frames); break;
  case 4: encodeChannels<4>(channels, n, dry, stride, signals, g0_.data(), dg_.data(), frames); break;
  case 9: encodeChannels<9>(channels, n, dry, stride, signals, g0_.data(), dg_.data(), frames); break;
  default: encodeChannels<kAmbisonicChannelCount>(channels, n, dry, stride, signals, g0_.data(), dg_.data(), frames); break;
  }

  prev_ = targets;
}
