#include "TestFramework.h"

#include "../dsp/DiracAnalyzer.h"
#include "../AmbisonicEncoding.h"
#include "../dsp/HashField.h"

#include <cmath>

namespace {
// 2 seconds at 44.1kHz is comfortably past the ~465ms (~40 hops) the
// analyzer's 100ms-time-constant one-pole smoothing (DiracAnalyzer.cpp's
// ie_alpha_) needs to settle within 1% of its steady-state value from a
// zero start, for every test below.
constexpr int kSampleRate = 44100;
constexpr int kFrames = kSampleRate * 2;

// 800Hz sits in the middle of band 3 (600-1200Hz, DiracAnalyzer.cpp's
// kBandEdgesHz) - far enough from either edge that a Hann-windowed tone's
// mainlobe leakage stays within that one band.
constexpr float kTestFrequency = 800.0f;
constexpr int kTestBand = 3;

// Fixed compile-time salt, test-local only - not part of the render path,
// just this file's own synthetic decorrelated-noise input (see
// dirac_decorrelated_eight_directions_is_near_full_diffuseness below).
// HashField, not std::mt19937/uniform_real_distribution: the standard
// never specifies a distribution's exact output shape, so it differs
// between libstdc++/libc++/MSVC - this keeps the test's own input (and
// therefore its result) bit-identical across platforms/compilers too, not
// just the engine's.
constexpr uint64_t kTestNoiseSalt = 0x44697261634E6F69ull; // "DiracNoi", arbitrary
}

TEST(dirac_single_source_is_low_diffuseness_at_correct_direction) {
  float az = 30.0f, el = 20.0f;
  auto gains = computeAmbisonicGains(SphericalPosition{az, el, 1.0f});

  AudioBuffer data(4, kFrames);
  auto w = data.getChannelData(0), y = data.getChannelData(1), z = data.getChannelData(2), x = data.getChannelData(3);
  for (int i = 0; i < kFrames; i++) {
    float s = sinf(2.0f * static_cast<float>(M_PI) * kTestFrequency * static_cast<float>(i) / static_cast<float>(kSampleRate));
    w[i] = gains[0] * s;
    y[i] = gains[1] * s;
    z[i] = gains[2] * s;
    x[i] = gains[3] * s;
  }

  DiracAnalyzer analyzer(kSampleRate);
  analyzer.process(data);

  auto & result = analyzer.getBandResult(kTestBand);
  CHECK_NEAR(result.azimuth, az, 3.0f);
  CHECK_NEAR(result.elevation, el, 3.0f);
  // Not just "low" - this specific threshold is what catches a missing
  // 1/2 weight in E's computation (see DiracAnalyzer.cpp), which would
  // read as ~0.5 diffuseness for this perfectly dry, correctly-positioned
  // source instead of ~0.
  CHECK(result.diffuseness < 0.05f);
}

TEST(dirac_decorrelated_eight_directions_is_near_full_diffuseness) {
  auto directions = cubeVertexDirections();

  AudioBuffer data(4, kFrames);
  data.zero(); // the raw-count constructor leaves the buffer uninitialized (aligned_alloc, not calloc) - the loop below accumulates with += across 8 sources, so it must start from real silence, not heap garbage.
  auto w = data.getChannelData(0), y = data.getChannelData(1), z = data.getChannelData(2), x = data.getChannelData(3);

  HashField field(kTestNoiseSalt);
  int dir_idx = 0;
  for (auto & dir : directions) {
    auto gains = computeAmbisonicGains(SphericalPosition{dir.azimuth, dir.elevation, 1.0f});
    for (int i = 0; i < kFrames; i++) {
      // independent white noise per source - coord combines dir_idx and
      // sample index into one value (kFrames comfortably exceeds any
      // realistic per-direction sample count, so this can never collide
      // across directions).
      float s = field.bipolar(static_cast<int64_t>(dir_idx) * kFrames + i, paramId("dirac_test_noise"), 1.0f);
      w[i] += gains[0] * s;
      y[i] += gains[1] * s;
      z[i] += gains[2] * s;
      x[i] += gains[3] * s;
    }
    dir_idx++;
  }

  DiracAnalyzer analyzer(kSampleRate);
  analyzer.process(data);

  // Broadband white noise excites every band - bands 2-7 reliably read as
  // near-fully diffuse. Bands 0/1 (20-150Hz, 150-300Hz) are deliberately
  // excluded here: at N=1024/44.1kHz they cover only 3 and 4 FFT bins
  // respectively, so few independent bins are being averaged per analysis
  // frame - band 0 can swing as low as ~0.76 by pure statistical variance,
  // not a real bias (the same few-bin unreliability plans/dirac-heatmap-
  // scope.md SS4 already documents for band 0 specifically). Bands 2-7
  // (7+ bins each) reliably stay above 0.9.
  for (int b = 2; b < DiracAnalyzer::kNumBands; b++) {
    CHECK(analyzer.getBandResult(b).diffuseness > 0.9f);
  }
}

TEST(dirac_w_only_is_also_near_full_diffuseness) {
  // W-only (X=Y=Z=0, e.g. an unpositioned/distance<=0 track - see
  // computeAmbisonicGains) reads the same high diffuseness as genuinely
  // diffuse content (the test above) - a real, documented ambiguity
  // (plans/dirac-heatmap-scope.md SS4), not a bug, checked in here as an
  // expected result rather than a surprise.
  AudioBuffer data(1, kFrames); // regularChannelCount()==1: only W present
  auto w = data.getChannelData(0);
  for (int i = 0; i < kFrames; i++) {
    w[i] = sinf(2.0f * static_cast<float>(M_PI) * kTestFrequency * static_cast<float>(i) / static_cast<float>(kSampleRate));
  }

  DiracAnalyzer analyzer(kSampleRate);
  analyzer.process(data);

  auto & result = analyzer.getBandResult(kTestBand);
  CHECK(result.diffuseness > 0.9f);

  // The data needed to disambiguate this from genuine diffuse content is
  // already computed internally, even though nothing displays it yet:
  // X/Y/Z magnitudes are near 0 here, unlike the genuinely-diffuse case
  // above where all four channels carry real, comparable energy.
  CHECK(result.magnitude[DiracAnalyzer::kW] > 0.1f);
  CHECK_NEAR(result.magnitude[DiracAnalyzer::kX], 0.0f, 1e-4f);
  CHECK_NEAR(result.magnitude[DiracAnalyzer::kY], 0.0f, 1e-4f);
  CHECK_NEAR(result.magnitude[DiracAnalyzer::kZ], 0.0f, 1e-4f);
}
