#include "TestFramework.h"

#include "../src/instruments/PadSynthPartialProfile.h"

#include <algorithm>
#include <stdexcept>

using namespace PadSynthProfile;

TEST(profile_alpha_matches_given_values_for_gauss_6_258) {
  ProfileParams p;
  p.type = ProfileType::Gaussian;
  p.beta = 6.258f;
  p.autoscale = true;
  auto profile = buildProfile(p);
  float alpha = computeProfileAlpha(profile, true);
  CHECK_NEAR(alpha, 0.4258f, 0.01f);
}

TEST(profile_alpha_matches_given_values_for_gauss_21_72) {
  ProfileParams p;
  p.type = ProfileType::Gaussian;
  p.beta = 21.72f;
  p.autoscale = true;
  auto profile = buildProfile(p);
  float alpha = computeProfileAlpha(profile, true);
  CHECK_NEAR(alpha, 0.2031f, 0.01f);
}

TEST(profile_alpha_matches_given_values_for_rect_6_258) {
  ProfileParams p;
  p.type = ProfileType::Rectangular;
  p.beta = 6.258f;
  p.autoscale = true;
  auto profile = buildProfile(p);
  float alpha = computeProfileAlpha(profile, true);
  CHECK_NEAR(alpha, 0.3711f, 0.01f);
}

TEST(profile_alpha_without_autoscale_is_fixed_at_half) {
  ProfileParams p;
  p.type = ProfileType::Gaussian;
  p.beta = 6.258f;
  p.autoscale = false;
  auto profile = buildProfile(p);
  CHECK_NEAR(computeProfileAlpha(profile, false), 0.5f, 1e-6f);
}

TEST(profile_is_normalized_and_nonnegative) {
  ProfileParams p;
  p.type = ProfileType::Gaussian;
  p.beta = 6.258f;
  auto profile = buildProfile(p);
  float peak = 0.0f;
  for (float v : profile) {
    CHECK(v >= 0.0f);
    peak = std::max(peak, v);
  }
  CHECK_NEAR(peak, 1.0f, 1e-4f);
}

TEST(positions_type_zero_is_identity) {
  PositionParams p;
  p.type = 0;
  for (int h = 1; h <= 20; h++) CHECK_NEAR(partialPosition(h, p), static_cast<float>(h), 1e-6f);
}

TEST(bells_positions_land_as_specified) {
  PositionParams p;
  p.type = 6;
  p.p1 = 255;
  p.p2 = 75;
  p.p3 = 255;
  int expected[10] = { 1, 2, 4, 5, 7, 9, 11, 13, 15, 17 };
  for (int h = 1; h <= 10; h++) {
    CHECK_NEAR(partialPosition(h, p), static_cast<float>(expected[h - 1]), 1e-3f);
  }
}

TEST(bells3_positions_land_as_specified) {
  PositionParams p;
  p.type = 6;
  p.p1 = 255;
  p.p2 = 107;
  p.p3 = 255;
  int expected[10] = { 1, 3, 5, 8, 12, 16, 21, 27, 32, 38 };
  for (int h = 1; h <= 10; h++) {
    CHECK_NEAR(partialPosition(h, p), static_cast<float>(expected[h - 1]), 1e-3f);
  }
}

TEST(synth_piano3_a_partial_ten_lands_at_10_04) {
  PositionParams p;
  p.type = 6;
  p.p1 = 78;
  p.p2 = 56;
  p.p3 = 0;
  CHECK_NEAR(partialPosition(10, p), 10.04f, 0.01f);
}

TEST(synth_piano3_b_partial_ten_lands_at_10_06) {
  PositionParams p;
  p.type = 6;
  p.p1 = 92;
  p.p2 = 56;
  p.p3 = 0;
  CHECK_NEAR(partialPosition(10, p), 10.06f, 0.01f);
}

namespace {
float spectrumEnergy(const std::vector<float> & spectrum) {
  double sum = 0.0;
  for (float v : spectrum) sum += static_cast<double>(v) * static_cast<double>(v);
  return static_cast<float>(sum);
}
} // namespace

TEST(placed_partial_energy_is_independent_of_bandwidth_narrow_branch) {
  // Both bandwidths chosen to land in the "omega <= 512" (interpolated
  // accumulation) branch at a moderate partial frequency/sample rate.
  ProfileParams pp;
  pp.type = ProfileType::Gaussian;
  pp.beta = 6.258f;
  auto profile = buildProfile(pp);
  float alpha = computeProfileAlpha(profile, true);

  int S = 1 << 16;
  std::vector<float> spectrum_narrow(static_cast<size_t>(S), 0.0f);
  std::vector<float> spectrum_wide(static_cast<size_t>(S), 0.0f);
  placePartial(spectrum_narrow, 1.0f, 1000.0f, /* bandwidth_cents */ 20.0f, alpha, profile, 44100.0f);
  placePartial(spectrum_wide, 1.0f, 1000.0f, /* bandwidth_cents */ 90.0f, alpha, profile, 44100.0f);

  float e_narrow = spectrumEnergy(spectrum_narrow);
  float e_wide = spectrumEnergy(spectrum_wide);
  CHECK(e_narrow > 0.0f);
  CHECK(std::fabs(e_narrow - e_wide) / e_narrow < 0.01f);
}

TEST(placed_partial_energy_is_independent_of_bandwidth_wide_branch) {
  // A high partial frequency and wide bandwidth pushes omega past the
  // profile's own 512-bin size, exercising the "omega > 512" (nearest-
  // neighbour stretch) branch for both bandwidths compared.
  ProfileParams pp;
  pp.type = ProfileType::Gaussian;
  pp.beta = 6.258f;
  auto profile = buildProfile(pp);
  float alpha = computeProfileAlpha(profile, true);

  int S = 1 << 16;
  std::vector<float> spectrum_a(static_cast<size_t>(S), 0.0f);
  std::vector<float> spectrum_b(static_cast<size_t>(S), 0.0f);
  placePartial(spectrum_a, 1.0f, 15000.0f, /* bandwidth_cents */ 400.0f, alpha, profile, 44100.0f);
  placePartial(spectrum_b, 1.0f, 15000.0f, /* bandwidth_cents */ 800.0f, alpha, profile, 44100.0f);

  float e_a = spectrumEnergy(spectrum_a);
  float e_b = spectrumEnergy(spectrum_b);
  CHECK(e_a > 0.0f);
  CHECK(std::fabs(e_a - e_b) / e_a < 0.01f);
}

TEST(unsupported_position_type_is_rejected) {
  PositionParams p;
  p.type = 3;
  bool threw = false;
  try {
    partialPosition(1, p);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw);
}
