#include "TestFramework.h"

#include "../src/dsp/HalfbandFilter.h"
#include "../src/dsp/RealFFT.h"

#include <array>
#include <cmath>
#include <complex>
#include <vector>

using namespace std;

namespace {

// One stage's group delay is (kTaps-1)/2 == 15 samples at whichever rate
// it's running; settle past a healthy multiple of that before trusting
// steady-state output in any of these tests.
constexpr int kSettle = 256;

// Chosen so every frequency of interest below (this value, half of it,
// quarter of it, and 0.5/0.25 minus those) lands on an exact FFT bin for
// the window sizes these tests use (2048 for single-stage, 4096 for the
// 4x cascade) - eliminates spectral leakage entirely rather than needing
// a window function or a leakage-tolerant threshold.
constexpr float kLowRateCycles = 400.0f / 1024.0f; // 0.390625

vector<float> makeSine(int n, float cyclesPerSample, float amplitude = 1.0f) {
  vector<float> out(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) {
    out[static_cast<size_t>(i)] = amplitude * sinf(2.0f * static_cast<float>(M_PI) * cyclesPerSample * static_cast<float>(i));
  }
  return out;
}

// Magnitude of the FFT bin nearest `cyclesPerSample` (as a fraction of the
// analysis window's own sample rate, i.e. 0..0.5), computed over the last
// `size` samples of `signal` (skips the settle-in transient at the front).
float binMagnitudeNear(const vector<float> & signal, size_t size, float cyclesPerSample) {
  vector<float> window(signal.end() - static_cast<long>(size), signal.end());
  RealFFT<float> fft(size);
  auto & spectrum = fft.forward(window);
  size_t bin = static_cast<size_t>(cyclesPerSample * static_cast<float>(size) + 0.5f);
  if (bin >= spectrum.size()) bin = spectrum.size() - 1;
  return abs(spectrum[bin]);
}

}

TEST(halfband_upsample_preserves_dc) {
  HalfbandFilter f;
  int frames = 512;
  vector<float> in(static_cast<size_t>(frames), 1.0f);
  vector<float> out(static_cast<size_t>(frames) * 2);
  f.upsample(in.data(), frames, out.data());

  for (int i = kSettle; i < frames * 2; i++) {
    CHECK_NEAR(out[static_cast<size_t>(i)], 1.0f, 0.01f);
  }
}

TEST(halfband_downsample_preserves_dc) {
  HalfbandFilter f;
  int frames = 512;
  vector<float> in(static_cast<size_t>(frames) * 2, 1.0f);
  vector<float> out(static_cast<size_t>(frames));
  f.downsample(in.data(), frames, out.data());

  for (int i = kSettle / 2; i < frames; i++) {
    CHECK_NEAR(out[static_cast<size_t>(i)], 1.0f, 0.01f);
  }
}

TEST(halfband_round_trip_preserves_passband_energy) {
  // Upsample then downsample a passband sine (well clear of both
  // filters' transition bands) - net effect should be close to identity
  // in amplitude (phase/delay aren't checked here, just level).
  HalfbandFilter up, down;
  int frames = 4096;
  float cyclesPerSample = 0.15f;
  auto in = makeSine(frames, cyclesPerSample, 0.6f);
  vector<float> mid(static_cast<size_t>(frames) * 2);
  up.upsample(in.data(), frames, mid.data());
  vector<float> out(static_cast<size_t>(frames));
  down.downsample(mid.data(), frames, out.data());

  double energyIn = 0.0, energyOut = 0.0;
  int n = 0;
  for (int i = kSettle; i < frames; i++) {
    energyIn += static_cast<double>(in[static_cast<size_t>(i)]) * in[static_cast<size_t>(i)];
    energyOut += static_cast<double>(out[static_cast<size_t>(i)]) * out[static_cast<size_t>(i)];
    n++;
  }
  double rmsIn = sqrt(energyIn / n);
  double rmsOut = sqrt(energyOut / n);
  CHECK(rmsOut > rmsIn * 0.9);
  CHECK(rmsOut < rmsIn * 1.1);
}

TEST(halfband_upsample_attenuates_image_above_old_nyquist) {
  // A sine near the OLD Nyquist (low rate) zero-stuffs into a "wanted"
  // component at kLowRateCycles/2 (new rate) plus a mirror image at
  // 0.5 - kLowRateCycles/2 - the halfband lowpass's whole job is to knock
  // that image down before it reaches the waveshaper.
  HalfbandFilter f;
  int frames = 4096;
  auto in = makeSine(frames, kLowRateCycles, 1.0f);
  vector<float> out(static_cast<size_t>(frames) * 2);
  f.upsample(in.data(), frames, out.data());

  size_t window = 2048; // kLowRateCycles chosen so both bins below land exactly
  float wantedMag = binMagnitudeNear(out, window, kLowRateCycles / 2.0f);
  float imageMag = binMagnitudeNear(out, window, 0.5f - kLowRateCycles / 2.0f);

  CHECK(wantedMag > 0.0f);
  // At least 40dB of image rejection (ratio >= 100x) - comfortably below
  // the filter's theoretical Blackman-Harris sidelobe floor, so this is a
  // "it's actually filtering, not a no-op" check, not a precise spec.
  CHECK(imageMag < wantedMag / 100.0f);
}

TEST(halfband_downsample_attenuates_content_above_new_nyquist) {
  // Feed the (high-rate) downsampler a sine that lands above the new
  // (post-decimation) Nyquist - it must be suppressed before decimation,
  // or it would alias back into the passband as an unrelated tone.
  HalfbandFilter f;
  int frames = 4096;
  float cyclesPerSampleHigh = 0.4f; // > 0.25, i.e. above the new Nyquist once halved
  auto in = makeSine(frames * 2, cyclesPerSampleHigh, 1.0f);
  vector<float> out(static_cast<size_t>(frames));
  f.downsample(in.data(), frames, out.data());

  // Whatever survives should be far quieter than the input amplitude -
  // check total energy in the settled region rather than a specific bin,
  // since aliased content can land anywhere in the output band.
  double energy = 0.0;
  int n = 0;
  for (int i = kSettle / 2; i < frames; i++) {
    energy += static_cast<double>(out[static_cast<size_t>(i)]) * out[static_cast<size_t>(i)];
    n++;
  }
  double rms = sqrt(energy / n);
  CHECK(rms < 0.05); // input amplitude was 1.0 - heavily suppressed
}

TEST(halfband_4x_cascade_attenuates_images_above_old_nyquist) {
  // Two cascaded stages (1x->2x->4x), the shape the saturator's tanh/
  // asym/softclip curves use - both stages' own images should be
  // suppressed in the final 4x-rate signal.
  HalfbandFilter stage1, stage2;
  int frames = 4096;
  auto in = makeSine(frames, kLowRateCycles, 1.0f);
  vector<float> mid(static_cast<size_t>(frames) * 2);
  stage1.upsample(in.data(), frames, mid.data());
  vector<float> out(static_cast<size_t>(frames) * 4);
  stage2.upsample(mid.data(), frames * 2, out.data());

  size_t window = 4096; // kLowRateCycles chosen so all three bins below land exactly
  float wantedMag = binMagnitudeNear(out, window, kLowRateCycles / 4.0f);
  float image1Mag = binMagnitudeNear(out, window, 0.25f - kLowRateCycles / 4.0f); // stage 1's residual image, halved again by stage 2
  float image2Mag = binMagnitudeNear(out, window, 0.5f - kLowRateCycles / 4.0f);  // stage 2's own fresh image of the wanted tone

  CHECK(wantedMag > 0.0f);
  CHECK(image1Mag < wantedMag / 100.0f);
  CHECK(image2Mag < wantedMag / 100.0f);
}

namespace {

// The filter as it was before the polyphase rewrite: all 63 taps through a
// circular history, one sample at a time.
class ReferenceHalfband {
 public:
  ReferenceHalfband() {
    constexpr int kTaps = 63, kCenter = 31;
    double sum = 0.0;
    std::array<double, kTaps> hd{};
    for (int n = 0; n < kTaps; n++) {
      int j = n - kCenter;
      double ideal = j == 0 ? 0.5 : (j % 2 == 0 ? 0.0 : sin(M_PI * 0.5 * j) / (M_PI * j));
      double phase = 2.0 * M_PI * n / (kTaps - 1);
      double w = 0.35875 - 0.48829 * cos(phase) + 0.14128 * cos(2.0 * phase) - 0.01168 * cos(3.0 * phase);
      hd[static_cast<size_t>(n)] = ideal * w;
      sum += hd[static_cast<size_t>(n)];
    }
    for (int n = 0; n < kTaps; n++) h_[static_cast<size_t>(n)] = static_cast<float>(hd[static_cast<size_t>(n)] / sum);
  }

  void upsample(const float * in, int frames, float * out) {
    for (int i = 0; i < frames; i++) {
      out[2 * i] = push(in[i]) * 2.0f;
      out[2 * i + 1] = push(0.0f) * 2.0f;
    }
  }

  void downsample(const float * in, int frames, float * out) {
    for (int i = 0; i < frames; i++) {
      push(in[2 * i]);
      out[i] = push(in[2 * i + 1]);
    }
  }

 private:
  float push(float x) {
    hist_[static_cast<size_t>(pos_)] = x;
    float acc = 0.0f;
    for (int k = 0; k < 63; k++) {
      int idx = pos_ - k;
      if (idx < 0) idx += 63;
      acc += h_[static_cast<size_t>(k)] * hist_[static_cast<size_t>(idx)];
    }
    if (++pos_ >= 63) pos_ = 0;
    return acc;
  }

  std::array<float, 63> h_{}, hist_{};
  int pos_ = 0;
};

vector<float> noise(size_t n, unsigned seed) {
  vector<float> v(n);
  for (auto & x : v) {
    seed = seed * 1664525u + 1013904223u;
    x = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
  }
  return v;
}

}

TEST(halfband_matches_the_direct_63_tap_filter_across_odd_block_sizes) {
  HalfbandFilter up, down;
  ReferenceHalfband ref_up, ref_down;
  const int sizes[] = { 1, 7, 8, 9, 63, 64, 100, 5, 257 };
  unsigned seed = 1;
  for (int frames : sizes) {
    auto in = noise(static_cast<size_t>(frames), seed++);
    vector<float> a(static_cast<size_t>(frames) * 2), b(a.size());
    up.upsample(in.data(), frames, a.data());
    ref_up.upsample(in.data(), frames, b.data());
    for (size_t i = 0; i < a.size(); i++) CHECK_NEAR(a[i], b[i], 1e-5f);

    auto in2 = noise(static_cast<size_t>(frames) * 2, seed++);
    vector<float> c(static_cast<size_t>(frames)), d(c.size());
    down.downsample(in2.data(), frames, c.data());
    ref_down.downsample(in2.data(), frames, d.data());
    for (size_t i = 0; i < c.size(); i++) CHECK_NEAR(c[i], d[i], 1e-5f);
  }
}
