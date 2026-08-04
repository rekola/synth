#include "TestFramework.h"

#include "../AmbisonicDiffuseEncoder.h"
#include "../AmbisonicEncoding.h"
#include "../SampleData.h"
#include "../dsp/NoiseGenerator.h"

#include <cmath>
#include <vector>

using namespace std;

namespace {

vector<float> whiteNoise(int n, uint32_t seed) {
  NoiseGenerator gen(seed);
  vector<float> out(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) out[static_cast<size_t>(i)] = gen.next();
  return out;
}

double rms(const float * data, int frames) {
  double energy = 0.0;
  for (int i = 0; i < frames; i++) energy += static_cast<double>(data[i]) * data[i];
  return sqrt(energy / frames);
}

// Normalized cross-correlation at lag 0.
double correlation(const float * a, const float * b, int frames) {
  double num = 0.0, ea = 0.0, eb = 0.0;
  for (int i = 0; i < frames; i++) {
    num += static_cast<double>(a[i]) * b[i];
    ea += static_cast<double>(a[i]) * a[i];
    eb += static_cast<double>(b[i]) * b[i];
  }
  return num / sqrt(ea * eb);
}

SampleData makeAmbisonicAccumulator(int channels, int frames) {
  SampleData out(static_cast<short>(channels), frames);
  out.zero();
  return out;
}

}

TEST(diffuse_encoder_diffusion_zero_is_w_only) {
  AmbisonicDiffuseEncoder enc(44100, 0);
  int frames = 2048;
  auto in = whiteNoise(frames, 12345);
  auto out = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);

  enc.encode(out, in.data(), frames, 0.0f, 1.0f);

  CHECK(rms(out.getChannelData(0), frames) > 0.0);
  for (int c = 1; c < kAmbisonicChannelCount; c++) {
    CHECK_NEAR(rms(out.getChannelData(c), frames), 0.0, 1e-6);
  }
}

TEST(diffuse_encoder_diffusion_one_is_fully_isotropic) {
  AmbisonicDiffuseEncoder enc(44100, 0);
  int frames = 2048;
  auto in = whiteNoise(frames, 54321);
  auto out = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);

  enc.encode(out, in.data(), frames, 1.0f, 1.0f);

  for (int c = 0; c < kAmbisonicChannelCount; c++) {
    CHECK(rms(out.getChannelData(c), frames) > 0.0);
  }
}

TEST(diffuse_encoder_order_weighting_matches_sqrt_2n_plus_1) {
  // At full diffusion every degree's taper is 1.0 (see the class's own
  // taper breakpoints), so the only remaining per-channel scale is
  // sqrt(2n+1) - the RMS ratio between any channel and W should match
  // that exactly (both draw from independently-decorrelated but
  // equal-*expected*-RMS allpass chains fed the same white noise).
  AmbisonicDiffuseEncoder enc(44100, 0);
  int frames = 8192;
  auto in = whiteNoise(frames, 777);
  auto out = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);

  enc.encode(out, in.data(), frames, 1.0f, 1.0f);

  double rmsW = rms(out.getChannelData(0), frames);
  for (int c = 1; c < kAmbisonicChannelCount; c++) {
    int degree = acnDegree(c);
    double expectedRatio = sqrt(2.0 * degree + 1.0);
    double actualRatio = rms(out.getChannelData(c), frames) / rmsW;
    // Generous tolerance - each channel is an independent finite-sample
    // noise realization, not literally the same signal scaled.
    CHECK(actualRatio > expectedRatio * 0.7);
    CHECK(actualRatio < expectedRatio * 1.3);
  }
}

TEST(diffuse_encoder_taper_ramps_degree_three_first) {
  AmbisonicDiffuseEncoder enc(44100, 0);
  int frames = 8192;
  auto in = whiteNoise(frames, 999);

  // Degree-3 channel (ACN9) should already be at full level by
  // diffusion=1/3, while degree-1 (ACN1) should still be silent there.
  auto outAtThird = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);
  enc.encode(outAtThird, in.data(), frames, 1.0f / 3.0f, 1.0f);

  auto outAtOne = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);
  AmbisonicDiffuseEncoder enc2(44100, 0);
  enc2.encode(outAtOne, in.data(), frames, 1.0f, 1.0f);

  double degree3AtThird = rms(outAtThird.getChannelData(9), frames);
  double degree3AtOne = rms(outAtOne.getChannelData(9), frames);
  CHECK(degree3AtThird > degree3AtOne * 0.9); // already essentially full level

  double degree1AtThird = rms(outAtThird.getChannelData(1), frames);
  CHECK_NEAR(degree1AtThird, 0.0, 1e-6); // hasn't started ramping in yet
}

TEST(diffuse_encoder_channels_are_mutually_decorrelated) {
  AmbisonicDiffuseEncoder enc(44100, 0);
  int frames = 8192;
  auto in = whiteNoise(frames, 42);
  auto out = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);
  enc.encode(out, in.data(), frames, 1.0f, 1.0f);

  // A handful of channel pairs - lag-0 correlation should be small (the
  // theoretical expectation for independently-phase-shifted copies of the
  // same broadband signal is 0; a few percent is well within finite-sample
  // noise for 8192 samples).
  int pairs[][2] = { {0, 1}, {0, 5}, {1, 4}, {3, 9}, {5, 12}, {8, 15} };
  for (auto & p : pairs) {
    double c = correlation(out.getChannelData(p[0]), out.getChannelData(p[1]), frames);
    CHECK(fabs(c) < 0.15);
  }
}

TEST(diffuse_encoder_distinct_salts_are_uncorrelated) {
  AmbisonicDiffuseEncoder a(44100, 0);
  AmbisonicDiffuseEncoder b(44100, 5);
  int frames = 8192;
  auto in = whiteNoise(frames, 2024);

  auto outA = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);
  auto outB = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);
  a.encode(outA, in.data(), frames, 1.0f, 1.0f);
  b.encode(outB, in.data(), frames, 1.0f, 1.0f);

  double c = correlation(outA.getChannelData(0), outB.getChannelData(0), frames);
  CHECK(fabs(c) < 0.15);
}

TEST(diffuse_encoder_accumulates_rather_than_overwrites) {
  AmbisonicDiffuseEncoder enc(44100, 0);
  int frames = 512;
  auto in = whiteNoise(frames, 8);
  auto out = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);

  // Seed channel 0 with a known nonzero value before encoding - it must
  // survive (added to), not get clobbered.
  out.getChannelData(0)[0] = 3.0f;
  enc.encode(out, in.data(), frames, 1.0f, 1.0f);
  CHECK(out.getChannelData(0)[0] != 3.0f); // encode() did add something
  // Re-run with gain 0 - should leave the accumulator exactly as it found it.
  auto out2 = makeAmbisonicAccumulator(kAmbisonicChannelCount, frames);
  out2.getChannelData(0)[0] = 3.0f;
  AmbisonicDiffuseEncoder enc2(44100, 0);
  enc2.encode(out2, in.data(), frames, 1.0f, 0.0f);
  CHECK_NEAR(out2.getChannelData(0)[0], 3.0f, 1e-6f);
}

TEST(diffuse_encoder_respects_lower_ambisonic_orders) {
  // Order 1 (4 channels) - must only touch channels 0-3, never read/write
  // past the accumulator's actual channel count.
  AmbisonicDiffuseEncoder enc(44100, 0);
  int frames = 256;
  auto in = whiteNoise(frames, 3);
  auto out = makeAmbisonicAccumulator(4, frames);
  enc.encode(out, in.data(), frames, 1.0f, 1.0f);
  for (int c = 0; c < 4; c++) CHECK(rms(out.getChannelData(c), frames) > 0.0);
}
