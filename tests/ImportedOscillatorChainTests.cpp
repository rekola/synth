#include "TestFramework.h"

#include "../src/instruments/ImportedOscillatorChain.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace std;
using namespace ImportedOscillator;

namespace {

int argmaxFrom(const vector<float> & a, int start) {
  int best = start;
  for (int i = start + 1; i < static_cast<int>(a.size()); i++) {
    if (a[static_cast<size_t>(i)] > a[static_cast<size_t>(best)]) best = i;
  }
  return best;
}

vector<float> normalized(vector<float> a) {
  float peak = 0.0f;
  for (float v : a) peak = std::max(peak, v);
  if (peak > 0.0f) {
    for (float & v : a) v /= peak;
  }
  return a;
}

OscillatorChainParams choirPad4(int shift) {
  OscillatorChainParams p;
  p.base_function = BaseFunction::WarpedHalfSine;
  p.base_shape_param = 0.943f;
  p.base_warp = { true, 0.1671f, 0.5039f, 10.0f };
  p.harmonics = { { 1, 0.984f } };
  p.filter = { FilterKind::ExponentialLowpass, 0.994256f, 0.00055f, 0, 1.0f };
  p.harmonic_shift = shift;
  return p;
}

OscillatorChainParams longSpaceChoir2(int shift) {
  OscillatorChainParams p;
  p.base_function = BaseFunction::WarpedHalfSine;
  p.base_shape_param = 0.943f;
  p.base_warp = { true, 0.1599f, 0.5118f, 13.0f };
  p.harmonics = { { 1, 0.984f } };
  p.filter = { FilterKind::ExponentialLowpass, 0.994256f, 0.00055f, 0, 1.0f };
  p.spectrum_adjust_kind = SpectrumAdjustKind::PowerLaw;
  p.spectrum_adjust_gamma = 0.6427f;
  p.harmonic_shift = shift;
  return p;
}

} // namespace

TEST(choir_pad4_base_spectrum_peaks_at_harmonic_ten_before_shift) {
  auto A = computeOscillatorMagnitudes(choirPad4(0));
  int peak = argmaxFrom(A, 1);
  CHECK(peak == 10);
}

TEST(choir_pad4_peak_moves_to_harmonic_three_after_shift_of_seven) {
  auto A = computeOscillatorMagnitudes(choirPad4(7));
  int peak = argmaxFrom(A, 1);
  CHECK(peak == 3);
}

TEST(long_spacechoir2_base_spectrum_peaks_at_harmonic_thirteen_before_shift) {
  auto A = computeOscillatorMagnitudes(longSpaceChoir2(0));
  int peak = argmaxFrom(A, 1);
  CHECK(peak == 13);
}

TEST(long_spacechoir2_peak_moves_to_harmonic_six_after_shift_of_seven) {
  auto A = computeOscillatorMagnitudes(longSpaceChoir2(7));
  int peak = argmaxFrom(A, 1);
  CHECK(peak == 6);
}

TEST(single_harmonic_boost_multiplies_target_before_peak_renormalization) {
  // Hand-computed: harmonics (1: 0.5, 2: 0.3), boost harmonic 1 by 1.946,
  // no waveshaper, no base spectrum (base_function = None), no shift.
  // Pre-rescale: [1] = 0.5*1.946 = 0.973, [2] = 0.3 (unchanged). Peak of
  // those two is 0.973, so after the filter's own peak renormalization:
  // [1] = 1.0 exactly, [2] = 0.3/0.973.
  OscillatorChainParams p;
  p.base_function = BaseFunction::None;
  p.harmonics = { { 1, 0.5f }, { 2, 0.3f } };
  p.filter = { FilterKind::SingleHarmonicBoost, 0.0f, 0.0f, /* boost_harmonic */ 1, /* boost_gain */ 1.946f };

  auto A = computeOscillatorMagnitudes(p);
  CHECK_NEAR(A[1], 1.0f, 1e-4f);
  CHECK_NEAR(A[2], 0.3f / 0.973f, 1e-4f);
}

TEST(oscillator_time_warp_is_identity_for_k1_zero_k3_one) {
  OscillatorChainParams plain;
  plain.base_function = BaseFunction::None;
  plain.harmonics = { { 1, 0.5f }, { 2, 0.3f }, { 3, 0.2f } };

  OscillatorChainParams warped = plain;
  warped.oscillator_warp = { true, 0.0f, 0.123f, 1.0f }; // k1=0, k3=1 -> identity regardless of phi

  auto a_plain = normalized(computeOscillatorMagnitudes(plain));
  auto a_warped = normalized(computeOscillatorMagnitudes(warped));
  for (size_t i = 1; i < 8; i++) CHECK_NEAR(a_plain[i], a_warped[i], 5e-3f);
}

TEST(base_time_warp_is_identity_for_k1_zero_k3_one) {
  OscillatorChainParams plain;
  plain.base_function = BaseFunction::WarpedHalfSine;
  plain.base_shape_param = 0.7f;
  plain.harmonics = { { 1, 1.0f } };

  OscillatorChainParams warped = plain;
  warped.base_warp = { true, 0.0f, 0.321f, 1.0f };

  auto a_plain = normalized(computeOscillatorMagnitudes(plain));
  auto a_warped = normalized(computeOscillatorMagnitudes(warped));
  for (size_t i = 1; i < 16; i++) CHECK_NEAR(a_plain[i], a_warped[i], 5e-3f);
}

TEST(unsupported_base_function_is_rejected) {
  OscillatorChainParams p;
  p.base_function = static_cast<BaseFunction>(2); // not in {None,1,4,5,7}
  p.harmonics = { { 1, 1.0f } };
  bool threw = false;
  try {
    computeOscillatorMagnitudes(p);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw);
}

TEST(oscillator_chain_is_deterministic) {
  auto a = computeOscillatorMagnitudes(choirPad4(7));
  auto b = computeOscillatorMagnitudes(choirPad4(7));
  CHECK(a.size() == b.size());
  for (size_t i = 0; i < a.size(); i++) CHECK(a[i] == b[i]);
}
