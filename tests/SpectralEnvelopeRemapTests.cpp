#include "TestFramework.h"

#include "../src/dsp/SpectralEnvelopeRemap.h"

#include <cmath>
#include <vector>

using namespace std;

namespace {
vector<float> makeRamp(size_t count) {
  vector<float> v(count);
  for (size_t i = 0; i < count; i++) v[i] = 1.0f / static_cast<float>(i + 1);
  return v;
}
}

TEST(remap_tracking_zero_is_identity) {
  auto prototype = makeRamp(16);
  vector<float> out;
  anchoredSpectralEnvelopeRemap(prototype, 440.0f, 220.0f, 0.0f, true, out);
  CHECK(out.size() == prototype.size());
  for (size_t i = 0; i < prototype.size(); i++) CHECK_NEAR(out[i], prototype[i], 1e-6f);
}

TEST(remap_note_at_anchor_frequency_is_identity) {
  auto prototype = makeRamp(16);
  vector<float> out;
  anchoredSpectralEnvelopeRemap(prototype, 220.0f, 220.0f, 0.734f, true, out);
  for (size_t i = 0; i < prototype.size(); i++) CHECK_NEAR(out[i], prototype[i], 1e-6f);
}

TEST(remap_is_deterministic) {
  auto prototype = makeRamp(24);
  vector<float> out_a, out_b;
  anchoredSpectralEnvelopeRemap(prototype, 660.0f, 220.0f, 1.0f, true, out_a);
  anchoredSpectralEnvelopeRemap(prototype, 660.0f, 220.0f, 1.0f, true, out_b);
  CHECK(out_a.size() == out_b.size());
  for (size_t i = 0; i < out_a.size(); i++) CHECK(out_a[i] == out_b[i]);
}

TEST(remap_sub_fundamental_contributions_fold_into_harmonic_one) {
  // Gather branch (r < 1): harmonic 1's own read position is r < 1, i.e.
  // strictly between prototype "harmonic 0" (== 0 by construction) and
  // prototype harmonic 1 - so h=1 should be a fraction of P[1], never 0,
  // and no energy should ever land below h=1 (there is no bin for it).
  vector<float> prototype{1.0f, 0.0f, 0.0f, 0.0f};
  vector<float> out;
  anchoredSpectralEnvelopeRemap(prototype, 100.0f, 400.0f, 1.0f, true, out); // r = 0.25
  CHECK(out[0] > 0.0f);
  CHECK_NEAR(out[0], 0.25f, 1e-5f); // lerp(P[0]=0, P[1]=1, frac=0.25) = 0.25

  // Scatter branch (r > 1): prototype harmonic 1 alone can scatter to
  // position 1/r < 1, which must fold into h=1 rather than being dropped.
  vector<float> prototype2{1.0f, 0.0f, 0.0f, 0.0f};
  vector<float> out2;
  anchoredSpectralEnvelopeRemap(prototype2, 400.0f, 100.0f, 1.0f, true, out2); // r = 4
  float total_energy = 0.0f;
  for (float v : out2) total_energy += v;
  CHECK(out2[0] > 0.0f);
  CHECK_NEAR(total_energy, out2[0], 1e-6f); // all of it landed on h=1
}

TEST(remap_scatter_accumulates_in_power_when_requested) {
  // r = 2 (note two octaves... tracking=1, f = 2*f_a): harmonic 1 (value 3)
  // scatters to position 0.5 - weight 0.5 folds (position 0 -> h=1) and
  // weight 0.5 lands directly on h=1 - two separate contributions of 1.5
  // each. Harmonic 2 (value 4) scatters to position 1.0 exactly - one
  // contribution of 4.0 directly on h=1. All three contributions
  // ([1.5, 1.5, 4.0]) land on output h=1: linear sum = 7.0, power sum =
  // sqrt(1.5^2 + 1.5^2 + 4.0^2) = sqrt(20.5).
  vector<float> collide{3.0f, 4.0f}; // harmonic 1 = 3, harmonic 2 = 4
  vector<float> out_power, out_linear;
  anchoredSpectralEnvelopeRemap(collide, 200.0f, 100.0f, 1.0f, true, out_power); // r = 2
  anchoredSpectralEnvelopeRemap(collide, 200.0f, 100.0f, 1.0f, false, out_linear);
  CHECK_NEAR(out_linear[0], 7.0f, 1e-4f);
  CHECK_NEAR(out_power[0], std::sqrt(20.5f), 1e-4f);
}

TEST(remap_envelope_invariance_gather_and_scatter) {
  // A single peak at harmonic k in the prototype should, after the remap,
  // stay at (very close to) the same absolute frequency k*f_a regardless
  // of the note's own frequency f - that is the entire point of the
  // feature. Checked across +/-1.5 octaves in 31-EDO steps, both gather
  // (f < f_a) and scatter (f > f_a) branches.
  constexpr int k = 8;
  constexpr size_t kCount = 64;
  vector<float> prototype(kCount, 0.0f);
  prototype[k - 1] = 1.0f;
  float anchor = 300.0f;

  for (int step = -46; step <= 46; step += 3) { // +/- 1.5 octaves of 31-EDO steps
    float f = anchor * std::pow(2.0f, static_cast<float>(step) / 31.0f);
    vector<float> out;
    anchoredSpectralEnvelopeRemap(prototype, f, anchor, 1.0f, true, out);

    int peak_h = 1;
    float peak_v = out[0];
    for (size_t i = 1; i < out.size(); i++) {
      if (out[i] > peak_v) { peak_v = out[i]; peak_h = static_cast<int>(i) + 1; }
    }
    float peak_freq = static_cast<float>(peak_h) * f;
    CHECK(std::fabs(peak_freq - k * anchor) < anchor); // within one harmonic spacing
  }
}

TEST(residue_class_weighting_matches_hand_computation) {
  vector<float> spectrum{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}; // harmonics 1-6
  residueClassWeighting(spectrum, /* n */ 2, /* r */ 1, /* amount */ 0.5f);
  // Harmonics with h mod 2 == 1 mod 2 == 1: h=1,3,5 keep full amplitude.
  // h=2,4,6 (residue 0) get scaled by (1-0.5)=0.5.
  CHECK_NEAR(spectrum[0], 1.0f, 1e-6f);
  CHECK_NEAR(spectrum[1], 0.5f, 1e-6f);
  CHECK_NEAR(spectrum[2], 1.0f, 1e-6f);
  CHECK_NEAR(spectrum[3], 0.5f, 1e-6f);
  CHECK_NEAR(spectrum[4], 1.0f, 1e-6f);
  CHECK_NEAR(spectrum[5], 0.5f, 1e-6f);
}

TEST(stretch_mix_matches_hand_computation) {
  vector<float> spectrum{1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f}; // harmonics 1-6, only 1-3 nonzero
  vector<float> out;
  stretchMix(spectrum, /* n */ 2, /* amount */ 0.5f, out);
  // S'[2*h] = S[h]: S'[2]=S[1]=1, S'[4]=S[2]=2, S'[6]=S[3]=3, rest 0.
  // out = 0.5*S + 0.5*S'.
  CHECK_NEAR(out[0], 0.5f * 1.0f, 1e-6f);          // h=1: 0.5*S[1] + 0
  CHECK_NEAR(out[1], 0.5f * 2.0f + 0.5f * 1.0f, 1e-6f); // h=2: 0.5*S[2] + 0.5*S'[2]
  CHECK_NEAR(out[2], 0.5f * 3.0f, 1e-6f);          // h=3: 0.5*S[3] + 0
  CHECK_NEAR(out[3], 0.5f * 2.0f, 1e-6f);          // h=4: 0 + 0.5*S'[4]
  CHECK_NEAR(out[4], 0.0f, 1e-6f);                 // h=5: nothing
  CHECK_NEAR(out[5], 0.5f * 3.0f, 1e-6f);          // h=6: 0 + 0.5*S'[6]
}

TEST(stretch_mix_discards_contributions_past_array_end) {
  vector<float> spectrum{1.0f, 1.0f, 1.0f}; // harmonics 1-3
  vector<float> out;
  stretchMix(spectrum, /* n */ 3, /* amount */ 1.0f, out); // fully stretched
  // S'[3]=S[1]=1, S'[6] and S'[9] are past the array end (size 3) -
  // discarded, not clamped/wrapped.
  CHECK_NEAR(out[0], 0.0f, 1e-6f);
  CHECK_NEAR(out[1], 0.0f, 1e-6f);
  CHECK_NEAR(out[2], 1.0f, 1e-6f);
}
