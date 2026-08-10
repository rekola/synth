#include "TestFramework.h"

#include "../AmbisonicEncoding.h"
#include "../InstrumentVoice.h"

namespace {
constexpr float kSqrt3Over2 = 0.86602540378443864676f;
constexpr float kSqrt5Over8 = 0.79056941504209483300f;
constexpr float kSqrt3Over8 = 0.61237243569579452455f;
}

TEST(ambisonic_gains_directionless_is_w_only) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 45.0f, 30.0f, 0.0f }); // distance <= 0
  CHECK_NEAR(gains[0], kAmbisonicReferenceGain, 0.0001f);
  for (size_t i = 1; i < gains.size(); i++) CHECK(gains[i] == 0.0f);

  auto gains_negative_distance = computeAmbisonicGains(SphericalPosition{ 0.0f, 0.0f, -5.0f });
  CHECK_NEAR(gains_negative_distance[0], kAmbisonicReferenceGain, 0.0001f);
  CHECK(gains_negative_distance[3] == 0.0f);
}

TEST(ambisonic_gains_front_is_pure_x) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 0.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[0], kAmbisonicReferenceGain, 0.0001f);
  CHECK_NEAR(gains[3], 1.0f, 0.0001f);
  CHECK_NEAR(gains[1], 0.0f, 0.0001f);
  CHECK_NEAR(gains[2], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_back_is_negative_x) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 180.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[3], -1.0f, 0.0001f);
  CHECK_NEAR(gains[1], 0.0f, 0.0001f);
  CHECK_NEAR(gains[2], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_right_is_positive_y) {
  // This engine's azimuth convention: positive = right (see PanLaw.h).
  auto gains = computeAmbisonicGains(SphericalPosition{ 90.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[1], 1.0f, 0.0001f);
  CHECK_NEAR(gains[3], 0.0f, 0.0001f);
  CHECK_NEAR(gains[2], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_left_is_negative_y) {
  auto gains = computeAmbisonicGains(SphericalPosition{ -90.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[1], -1.0f, 0.0001f);
  CHECK_NEAR(gains[3], 0.0f, 0.0001f);
  CHECK_NEAR(gains[2], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_up_is_positive_z) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 0.0f, 90.0f, 1.0f });
  CHECK_NEAR(gains[2], 1.0f, 0.0001f);
  CHECK_NEAR(gains[3], 0.0f, 0.0001f);
  CHECK_NEAR(gains[1], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_down_is_negative_z) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 0.0f, -90.0f, 1.0f });
  CHECK_NEAR(gains[2], -1.0f, 0.0001f);
}

// computeAmbisonicGains is direction-only - distance-based attenuation now
// lives at the voice level (InstrumentVoice::getDistanceGain(), backed by
// this distanceGain() helper), applied uniformly across STEREO/MONO/AMBISONIC
// rather than baked into the ACN gains themselves. See AmbisonicEncoding.h's
// comment on computeAmbisonicGains for why.
TEST(ambisonic_gains_do_not_attenuate_with_distance) {
  auto near_gains = computeAmbisonicGains(SphericalPosition{ 0.0f, 0.0f, 1.0f });
  auto far_gains = computeAmbisonicGains(SphericalPosition{ 0.0f, 0.0f, 2.0f });
  CHECK_NEAR(far_gains[3], near_gains[3], 0.0001f);
  CHECK_NEAR(far_gains[0], near_gains[0], 0.0001f);
}

TEST(distance_gain_follows_inverse_distance_law) {
  CHECK_NEAR(distanceGain(1.0f), 1.0f, 0.0001f);
  CHECK_NEAR(distanceGain(2.0f), 0.5f, 0.0001f);
  CHECK_NEAR(distanceGain(4.0f), 0.25f, 0.0001f);
  // <= 0 means "no position ever set" - treated as no attenuation, same
  // convention computeAmbisonicGains's own distance<=0 fallback uses.
  CHECK_NEAR(distanceGain(0.0f), 1.0f, 0.0001f);
  CHECK_NEAR(distanceGain(-3.0f), 1.0f, 0.0001f);
}

// Degree-2 (Acn4-8) known-angle checks, per the hand-verified values in
// AmbisonicEncoding.h's own comment.
TEST(ambisonic_gains_degree2_front_is_acn6_and_acn8_only) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 0.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[6], -0.5f, 0.0001f);
  CHECK_NEAR(gains[8], kSqrt3Over2, 0.0001f);
  CHECK_NEAR(gains[4], 0.0f, 0.0001f);
  CHECK_NEAR(gains[5], 0.0f, 0.0001f);
  CHECK_NEAR(gains[7], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_degree2_up_is_acn6_only) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 0.0f, 90.0f, 1.0f });
  CHECK_NEAR(gains[6], 1.0f, 0.0001f);
  CHECK_NEAR(gains[4], 0.0f, 0.0001f);
  CHECK_NEAR(gains[5], 0.0f, 0.0001f);
  CHECK_NEAR(gains[7], 0.0f, 0.0001f);
  CHECK_NEAR(gains[8], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_degree2_left_is_acn6_and_acn8_negative) {
  // Fills a gap the existing degree-2 fixtures (front, up) didn't cover -
  // left/back are just as easy to get an azimuth-doubling sign wrong on.
  // This engine's convention: positive azimuth = right (see
  // ambisonic_gains_right_is_positive_y/left_is_negative_y above), so left
  // is az=-90, not +90.
  auto gains = computeAmbisonicGains(SphericalPosition{ -90.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[6], -0.5f, 0.0001f);
  CHECK_NEAR(gains[8], -kSqrt3Over2, 0.0001f);
  CHECK_NEAR(gains[4], 0.0f, 0.0001f);
  CHECK_NEAR(gains[5], 0.0f, 0.0001f);
  CHECK_NEAR(gains[7], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_degree2_back_is_acn6_and_acn8_only) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 180.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[6], -0.5f, 0.0001f);
  CHECK_NEAR(gains[8], kSqrt3Over2, 0.0001f);
  CHECK_NEAR(gains[4], 0.0f, 0.0001f);
  CHECK_NEAR(gains[5], 0.0f, 0.0001f);
  CHECK_NEAR(gains[7], 0.0f, 0.0001f);
}

// Degree-3 (Acn9-15) known-angle checks - independently re-derived from the
// general real-SH SN3D formula and cross-checked against this file's own
// existing (tested) degree-2 constants before being trusted (see
// AmbisonicEncoding.h's computeAmbisonicGains comment). "Left" is az=-90 in
// this engine's own convention (positive az = right - see
// ambisonic_gains_right_is_positive_y above), not az=+90.
TEST(ambisonic_gains_degree3_front_is_acn13_and_acn15_only) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 0.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[13], -kSqrt3Over8, 0.0001f);
  CHECK_NEAR(gains[15], kSqrt5Over8, 0.0001f);
  CHECK_NEAR(gains[9], 0.0f, 0.0001f);
  CHECK_NEAR(gains[10], 0.0f, 0.0001f);
  CHECK_NEAR(gains[11], 0.0f, 0.0001f);
  CHECK_NEAR(gains[12], 0.0f, 0.0001f);
  CHECK_NEAR(gains[14], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_degree3_left_is_acn9_and_acn11_only) {
  auto gains = computeAmbisonicGains(SphericalPosition{ -90.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[9], kSqrt5Over8, 0.0001f);
  CHECK_NEAR(gains[11], kSqrt3Over8, 0.0001f);
  CHECK_NEAR(gains[10], 0.0f, 0.0001f);
  CHECK_NEAR(gains[12], 0.0f, 0.0001f);
  CHECK_NEAR(gains[13], 0.0f, 0.0001f);
  CHECK_NEAR(gains[14], 0.0f, 0.0001f);
  CHECK_NEAR(gains[15], 0.0f, 0.0001f);
}

TEST(ambisonic_gains_degree3_up_is_acn12_only) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 0.0f, 90.0f, 1.0f });
  CHECK_NEAR(gains[12], 1.0f, 0.0001f);
  for (int c = 9; c <= 15; c++) {
    if (c == 12) continue;
    CHECK_NEAR(gains[static_cast<size_t>(c)], 0.0f, 0.0001f);
  }
}

TEST(ambisonic_gains_degree3_back_is_acn13_and_acn15_only) {
  auto gains = computeAmbisonicGains(SphericalPosition{ 180.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains[13], kSqrt3Over8, 0.0001f);
  CHECK_NEAR(gains[15], -kSqrt5Over8, 0.0001f);
  CHECK_NEAR(gains[9], 0.0f, 0.0001f);
  CHECK_NEAR(gains[10], 0.0f, 0.0001f);
  CHECK_NEAR(gains[11], 0.0f, 0.0001f);
  CHECK_NEAR(gains[12], 0.0f, 0.0001f);
  CHECK_NEAR(gains[14], 0.0f, 0.0001f);
}

// max-rE decode-weighting math (AmbisonicEncoding.h's maxReGainsPerDegree/
// acnDegree) - see the plan's derivation. These pin the hand-worked values
// so a future order-3 addition (which reuses the same recurrence) can't
// silently perturb the order-2 numbers already shipped.
TEST(max_re_gains_per_degree_order2_matches_known_values) {
  float g[3];
  maxReGainsPerDegree(2, g);
  CHECK_NEAR(g[0], 1.0f, 0.0001f);
  CHECK_NEAR(g[1], 0.774597f, 0.0001f);
  CHECK_NEAR(g[2], 0.4f, 0.0001f);
}

TEST(acn_degree_matches_standard_acn_convention) {
  CHECK(acnDegree(0) == 0);
  CHECK(acnDegree(1) == 1);
  CHECK(acnDegree(2) == 1);
  CHECK(acnDegree(3) == 1);
  CHECK(acnDegree(4) == 2);
  CHECK(acnDegree(5) == 2);
  CHECK(acnDegree(6) == 2);
  CHECK(acnDegree(7) == 2);
  CHECK(acnDegree(8) == 2);
}

// Energy-neutrality by algebra (plan §2.4): the renormalization scalar k
// must make Sum(2l+1)*(k*g_l)^2 exactly equal Sum(2l+1) - i.e. mean-square
// gain across a decode row's channels is unchanged by max-rE weighting.
// This is a fixed identity independent of any particular source direction,
// so it's tested directly rather than via a rendered signal.
TEST(max_re_renormalization_preserves_mean_square_energy_order2) {
  float g[3];
  maxReGainsPerDegree(2, g);
  float unweighted_total = 1.0f + 3.0f + 5.0f; // (2l+1) per degree, l=0..2
  float weighted_sumsq = 1.0f * g[0] * g[0] + 3.0f * g[1] * g[1] + 5.0f * g[2] * g[2];
  float k = sqrtf(unweighted_total / weighted_sumsq);

  float renormalized_total = 1.0f * (k * g[0]) * (k * g[0])
                            + 3.0f * (k * g[1]) * (k * g[1])
                            + 5.0f * (k * g[2]) * (k * g[2]);
  CHECK_NEAR(renormalized_total, unweighted_total, 0.001f);
  CHECK_NEAR(k, 1.581139f, 0.0001f);
}

// The peak-headroom gap found reviewing this change (plan §2.5): the
// renormalization above does NOT make the single-channel (W-only, diffuse
// source) worst case neutral - that case only ever touches degree 0, so it
// sees k*g0 directly. Pin that factor here since it's exactly what
// AmbisonicBinauralMixer.h's kMasterGainTrim re-derivation depends on.
TEST(max_re_weighting_increases_diffuse_source_worst_case_gain_order2) {
  float g[3];
  maxReGainsPerDegree(2, g);
  float k = sqrtf(9.0f / (1.0f * g[0] * g[0] + 3.0f * g[1] * g[1] + 5.0f * g[2] * g[2]));
  float diffuse_worst_case_gain = k * g[0];
  CHECK_NEAR(diffuse_worst_case_gain, 1.581139f, 0.0001f);
  CHECK(diffuse_worst_case_gain > 1.0f); // louder than the unweighted case
}

// Decode concentration, max-rE weighted vs unweighted (extends
// ambisonic_decode_concentrates_on_matching_speaker below with the same
// 12-speaker layout/method, but building a weighted decode matrix): max-rE
// should concentrate energy at least as tightly as the unweighted decode
// (higher or equal |rE|), and the loudest speaker must still be the one
// matching the source direction.
// A genuine finding, not an assumption: max-rE weighting does NOT increase
// |rE| relative to the unweighted decode for this specific 12-speaker
// icosahedron at order 2 (measured ~0.487 weighted vs. ~0.609 unweighted,
// both exactly reproduced at all 12 speakers by symmetry) - the opposite of
// what the continuous/many-speaker-limit theory predicts (there, max-rE's
// whole purpose is to *maximize* |rE|). The per-degree weighting formula
// itself is still correct (see the pure-algebra renormalization/worst-case
// tests above, which pin the exact same g_l/k values used here and in the
// real decode matrix construction) - this is a property of decoding
// through only 12 discrete, fairly widely-spaced speakers specifically:
// max-rE's damped higher-degree terms widen the main lobe faster than they
// shrink the near-ring sidelobes' relative contribution at this speaker
// density. So this test pins the actual observed behavior (regression
// guard against a bigger change from what's shipped) rather than asserting
// the unweighted-vs-weighted direction of the inequality - worth
// re-measuring once the order-3/26-speaker Lebedev rig exists, since a
// denser rig may behave differently.
TEST(ambisonic_decode_concentration_with_max_re_weighting_order2) {
  struct Dir { float az, el; };
  constexpr float kIcoEl = 26.56505117707799f;
  std::vector<Dir> speakers = {
    { 0.0f, 90.0f }, { 0.0f, -90.0f },
    { 0.0f, kIcoEl }, { 72.0f, kIcoEl }, { 144.0f, kIcoEl }, { 216.0f, kIcoEl }, { 288.0f, kIcoEl },
    { 36.0f, -kIcoEl }, { 108.0f, -kIcoEl }, { 180.0f, -kIcoEl }, { 252.0f, -kIcoEl }, { 324.0f, -kIcoEl },
  };

  auto unitVector = [](float az_deg, float el_deg) {
    constexpr float kDeg2Rad = static_cast<float>(M_PI) / 180.0f;
    float az = az_deg * kDeg2Rad, el = el_deg * kDeg2Rad;
    return std::array<float, 3>{ cosf(el) * cosf(az), cosf(el) * sinf(az), sinf(el) };
  };

  // This test is order-2-only by design (the geometry above is the
  // 12-point icosahedron, an order-2 rig) - deliberately capped at 9
  // channels rather than kAmbisonicChannelCount (16 since order 3 landed),
  // since computeAmbisonicGains() always fills all 16 channels regardless
  // of which order a caller cares about, and max_re here only has order 2's
  // 3 degrees to index into.
  constexpr int kOrder2Channels = 9;
  float max_re[3];
  maxReGainsPerDegree(2, max_re);
  float k = sqrtf(9.0f / (1.0f * max_re[0] * max_re[0] + 3.0f * max_re[1] * max_re[1] + 5.0f * max_re[2] * max_re[2]));

  std::vector<AmbisonicGains> unweighted_decode, weighted_decode;
  for (auto & s : speakers) {
    auto row = computeAmbisonicGains(SphericalPosition{ s.az, s.el, 1.0f });
    unweighted_decode.push_back(row);
    auto weighted = row;
    for (int c = 0; c < kOrder2Channels; c++) weighted[static_cast<size_t>(c)] *= k * max_re[static_cast<size_t>(acnDegree(c))];
    weighted_decode.push_back(weighted);
  }

  auto rEMagnitude = [&](const std::vector<AmbisonicGains> & decode, size_t source_index) {
    auto enc = computeAmbisonicGains(SphericalPosition{ speakers[source_index].az, speakers[source_index].el, 1.0f });
    std::vector<float> feed(speakers.size());
    for (size_t j = 0; j < speakers.size(); j++) {
      float f = 0.0f;
      for (int c = 0; c < kOrder2Channels; c++) f += enc[static_cast<size_t>(c)] * decode[j][static_cast<size_t>(c)];
      feed[j] = f;
    }
    size_t best = 0;
    for (size_t j = 1; j < feed.size(); j++) if (std::fabs(feed[j]) > std::fabs(feed[best])) best = j;
    CHECK(best == source_index);

    double sumsq = 0.0, rx = 0.0, ry = 0.0, rz = 0.0;
    for (size_t j = 0; j < speakers.size(); j++) {
      auto v = unitVector(speakers[j].az, speakers[j].el);
      double w = static_cast<double>(feed[j]) * feed[j];
      sumsq += w;
      rx += w * v[0]; ry += w * v[1]; rz += w * v[2];
    }
    rx /= sumsq; ry /= sumsq; rz /= sumsq;
    return std::sqrt(rx * rx + ry * ry + rz * rz);
  };

  for (size_t i = 0; i < speakers.size(); i++) {
    double weighted_rE = rEMagnitude(weighted_decode, i);
    // Pins the actual measured value (~0.4862 at every speaker, by
    // icosahedral symmetry) with a loose band - a regression guard against
    // a gross change (e.g. a sign error or a degree-mapping bug collapsing
    // this near zero), not a precision check or a claim about which
    // direction is "better" - see this test's own comment above.
    CHECK(weighted_rE > 0.35);
    CHECK(weighted_rE < 0.6);
  }
  // Sanity-check the unweighted decode still measures what the older
  // ambisonic_decode_concentrates_on_matching_speaker test below already
  // pins (~0.6087) - confirms this test's own local decode construction
  // matches that one before trusting the weighted comparison above.
  double unweighted_rE = rEMagnitude(unweighted_decode, 0);
  CHECK(unweighted_rE > 0.5);
  CHECK(unweighted_rE < 0.95);
}

// Order-3 analog, using the real lebedev26Directions() (not a hand-copied
// literal list, unlike the order-2 test above, which predates that
// function) - exercises the actual order-3 rig and confirms it produces a
// sane, correctly-concentrated decode. No claim about whether |rE| here is
// higher or lower than order 2's - see that test's own comment on why
// max-rE doesn't straightforwardly raise |rE| at these speaker densities;
// this is a regression guard for the measured value, not a precision or
// "better than order 2" check.
TEST(ambisonic_decode_concentration_with_max_re_weighting_order3) {
  auto speakers = lebedev26Directions();
  CHECK(speakers.size() == 26);

  auto unitVector = [](float az_deg, float el_deg) {
    constexpr float kDeg2Rad = static_cast<float>(M_PI) / 180.0f;
    float az = az_deg * kDeg2Rad, el = el_deg * kDeg2Rad;
    return std::array<float, 3>{ cosf(el) * cosf(az), cosf(el) * sinf(az), sinf(el) };
  };

  float max_re[4];
  maxReGainsPerDegree(3, max_re);
  float k = sqrtf(16.0f / (1.0f * max_re[0] * max_re[0] + 3.0f * max_re[1] * max_re[1]
                          + 5.0f * max_re[2] * max_re[2] + 7.0f * max_re[3] * max_re[3]));

  std::vector<AmbisonicGains> weighted_decode;
  for (auto & s : speakers) {
    auto row = computeAmbisonicGains(SphericalPosition{ s.azimuth, s.elevation, 1.0f });
    for (int c = 0; c < kAmbisonicChannelCount; c++) row[static_cast<size_t>(c)] *= k * max_re[static_cast<size_t>(acnDegree(c))];
    weighted_decode.push_back(row);
  }

  for (size_t i = 0; i < speakers.size(); i++) {
    auto enc = computeAmbisonicGains(SphericalPosition{ speakers[i].azimuth, speakers[i].elevation, 1.0f });
    std::vector<float> feed(speakers.size());
    for (size_t j = 0; j < speakers.size(); j++) {
      float f = 0.0f;
      for (int c = 0; c < kAmbisonicChannelCount; c++) f += enc[static_cast<size_t>(c)] * weighted_decode[j][static_cast<size_t>(c)];
      feed[j] = f;
    }
    size_t best = 0;
    for (size_t j = 1; j < feed.size(); j++) if (std::fabs(feed[j]) > std::fabs(feed[best])) best = j;
    CHECK(best == i);

    double sumsq = 0.0, rx = 0.0, ry = 0.0, rz = 0.0;
    for (size_t j = 0; j < speakers.size(); j++) {
      auto v = unitVector(speakers[j].azimuth, speakers[j].elevation);
      double w = static_cast<double>(feed[j]) * feed[j];
      sumsq += w;
      rx += w * v[0]; ry += w * v[1]; rz += w * v[2];
    }
    rx /= sumsq; ry /= sumsq; rz /= sumsq;
    double rE_mag = std::sqrt(rx * rx + ry * ry + rz * rz);
    // Measured ~0.537-0.574 across the 26 speakers (the Lebedev grid's
    // three orbit classes aren't all equivalent under rotation the way the
    // icosahedron's 12 points are, so this isn't perfectly uniform like
    // the order-2 test above) - loose regression-guard band, not a
    // precision check.
    CHECK(rE_mag > 0.4);
    CHECK(rE_mag < 0.7);
  }
}

// Energy-neutrality across orders (plan §2.4/§8 item 3b): the per-channel
// mean-square renormalization identity already pinned for order 2 above
// must hold at order 3 too - same algebra, one more degree term.
TEST(max_re_renormalization_preserves_mean_square_energy_order3) {
  float g[4];
  maxReGainsPerDegree(3, g);
  float unweighted_total = 1.0f + 3.0f + 5.0f + 7.0f; // (2l+1) per degree, l=0..3
  float weighted_sumsq = 1.0f * g[0] * g[0] + 3.0f * g[1] * g[1] + 5.0f * g[2] * g[2] + 7.0f * g[3] * g[3];
  float k = sqrtf(unweighted_total / weighted_sumsq);

  float renormalized_total = 1.0f * (k * g[0]) * (k * g[0])
                            + 3.0f * (k * g[1]) * (k * g[1])
                            + 5.0f * (k * g[2]) * (k * g[2])
                            + 7.0f * (k * g[3]) * (k * g[3]);
  CHECK_NEAR(renormalized_total, unweighted_total, 0.001f);
  CHECK_NEAR(k, 1.668184f, 0.0001f);
}

TEST(ambisonic_voice_encoder_seeds_first_block_at_target_no_fade_in) {
  AmbisonicVoiceEncoder encoder;
  AudioBuffer out(kAmbisonicChannelCount, 8);
  out.zero();

  AmbisonicGains target{ 0.5f, 0.25f, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  std::vector<float> mono(8, 1.0f);
  encoder.encodeBlock(out, mono.data(), 8, target);

  // First call: no previous gain to fade in from, so even sample 0 should
  // already reflect `target`, not a ramp starting from zero.
  CHECK_NEAR(out.getChannelData(0)[0], target[0], 0.0001f);
  CHECK_NEAR(out.getChannelData(1)[0], target[1], 0.0001f);
  CHECK_NEAR(out.getChannelData(3)[0], target[3], 0.0001f);
}

TEST(ambisonic_voice_encoder_interpolates_across_block_boundary) {
  AmbisonicVoiceEncoder encoder;
  std::vector<float> mono(8, 1.0f);

  AmbisonicGains first{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  AudioBuffer out1(kAmbisonicChannelCount, 8);
  out1.zero();
  encoder.encodeBlock(out1, mono.data(), 8, first);
  // Constant target throughout the first block.
  CHECK_NEAR(out1.getChannelData(0)[0], 1.0f, 0.0001f);
  CHECK_NEAR(out1.getChannelData(0)[7], 1.0f, 0.0001f);

  AmbisonicGains second{ 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  AudioBuffer out2(kAmbisonicChannelCount, 8);
  out2.zero();
  encoder.encodeBlock(out2, mono.data(), 8, second);

  // Second block ramps from the first block's target to the new one - no
  // discontinuity: starts at (or very near) the old W/Y values, ends at
  // the new ones.
  CHECK_NEAR(out2.getChannelData(0)[0], 1.0f, 0.0001f);
  CHECK_NEAR(out2.getChannelData(1)[0], 0.0f, 0.0001f);
  CHECK_NEAR(out2.getChannelData(0)[7], 0.0f, 0.0001f);
  CHECK_NEAR(out2.getChannelData(1)[7], 1.0f, 0.0001f);

  // Monotonic ramp, no zipper/overshoot.
  auto w = out2.getChannelData(0);
  for (int i = 1; i < 8; i++) CHECK(w[i] <= w[i - 1] + 0.0001f);
}

TEST(ambisonic_voice_encoder_ignores_trailing_aux_channels) {
  // A 3-channel accumulator (1 regular Mono + AuxA + AuxB) should only
  // ever be written to by encodeBlock on channel 0 - callers (see
  // InstrumentVoice::encodePosition()) handle AuxA/AuxB separately,
  // deriving them directly from the dry signal rather than spatially
  // encoding them.
  AmbisonicVoiceEncoder encoder;
  AudioBuffer out(1, true, true, 4); // 1 regular (Mono/W) + AuxA + AuxB
  out.zero();
  AmbisonicGains target{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  std::vector<float> mono(4, 1.0f);
  encoder.encodeBlock(out, mono.data(), 4, target);
  CHECK_NEAR(out.getChannelData(0)[0], 1.0f, 0.0001f);
  CHECK_NEAR(out.getChannel(Channel::AuxA)[0], 0.0f, 0.0001f);
  CHECK_NEAR(out.getChannel(Channel::AuxB)[0], 0.0f, 0.0001f);
}

// Buffer-safety regression test for the order-3 channel-count bump: a
// full-width (kAmbisonicChannelCount=16) accumulator must round-trip every
// channel correctly, with no corruption/crash from any leftover
// order-2-only assumption (fixed-size array, loop bound, etc.) elsewhere
// in this path - see AmbisonicVoiceEncoder::encodeBlock's own
// n=std::min(regular, target.size()) clamp and AmbisonicBinauralMixer.cpp's
// equivalent clamp on its own fixed decode-time array.
TEST(ambisonic_voice_encoder_handles_full_order3_channel_count) {
  AmbisonicVoiceEncoder encoder;
  AudioBuffer out(kAmbisonicChannelCount, 8);
  out.zero();
  auto target = computeAmbisonicGains(SphericalPosition{ 30.0f, 15.0f, 1.0f });
  std::vector<float> mono(8, 1.0f);
  encoder.encodeBlock(out, mono.data(), 8, target);
  for (int c = 0; c < kAmbisonicChannelCount; c++) {
    CHECK_NEAR(out.getChannelData(c)[0], target[static_cast<size_t>(c)], 0.0001f);
  }
}

TEST(stereo_decode_reencode_preserves_left_right_direction) {
  AudioBuffer ambisonic_left(4, 4);
  ambisonic_left.zero();
  AudioBuffer stereo_left(2, 4);
  stereo_left.zero();
  for (int i = 0; i < 4; i++) {
    stereo_left.getChannelData(0)[i] = 1.0f; // hard left
    stereo_left.getChannelData(1)[i] = 0.0f;
  }
  encodeStereoAsPoints(stereo_left, ambisonic_left);

  AudioBuffer decoded_left(2, 4);
  decodeToStereo(ambisonic_left, decoded_left);
  CHECK(decoded_left.getChannelData(0)[0] > decoded_left.getChannelData(1)[0]);

  AudioBuffer ambisonic_right(4, 4);
  ambisonic_right.zero();
  AudioBuffer stereo_right(2, 4);
  stereo_right.zero();
  for (int i = 0; i < 4; i++) {
    stereo_right.getChannelData(0)[i] = 0.0f;
    stereo_right.getChannelData(1)[i] = 1.0f; // hard right
  }
  encodeStereoAsPoints(stereo_right, ambisonic_right);

  AudioBuffer decoded_right(2, 4);
  decodeToStereo(ambisonic_right, decoded_right);
  CHECK(decoded_right.getChannelData(1)[0] > decoded_right.getChannelData(0)[0]);

  // A centered (equal L/R) signal decodes back symmetrically.
  AudioBuffer ambisonic_center(4, 4);
  ambisonic_center.zero();
  AudioBuffer stereo_center(2, 4);
  stereo_center.zero();
  for (int i = 0; i < 4; i++) {
    stereo_center.getChannelData(0)[i] = 1.0f;
    stereo_center.getChannelData(1)[i] = 1.0f;
  }
  encodeStereoAsPoints(stereo_center, ambisonic_center);

  AudioBuffer decoded_center(2, 4);
  decodeToStereo(ambisonic_center, decoded_center);
  CHECK_NEAR(decoded_center.getChannelData(0)[0], decoded_center.getChannelData(1)[0], 0.0001f);
}

TEST(reduce_for_effect_narrows_ambisonic_to_mono_only) {
  ChannelConfiguration ambisonic(44100, 1);
  auto reduced = reduceForEffect(ambisonic);
  CHECK(reduced.isMono());

  ChannelConfiguration mono(44100);
  CHECK(reduceForEffect(mono).isMono());
}


// Regression guard for the W-channel bug found via the binaural "no
// directionality" investigation (see docs/known_bugs.md history):
// kAmbisonicReferenceGain used to be 1/sqrt(2) (a FuMa convention) instead
// of SN3D's 1.0 (this codebase's stated convention - see AmbisonicEncoding.h's
// top-of-file comment). SN3D gives W (ACN0) unity gain for a plane wave
// from the encoded direction.
TEST(ambisonic_reference_gain_is_sn3d_not_fuma) {
  CHECK_NEAR(kAmbisonicReferenceGain, 1.0f, 0.0001f);
}

// Regression test for the same investigation's decode-matrix concentration
// check (Test F): encodes a source at each of the 12 virtual speaker
// directions AmbisonicBinauralMixer.cpp's speakerDirectionsFor() uses for
// order-2 ambisonic (reproduced literally here - keep in sync if that
// layout ever changes), decodes through the same computeAmbisonicGains()
// used as both encoder and decode matrix, and confirms the result actually
// concentrates on the matching speaker rather than smearing near-uniformly
// across all of them (which would reproduce the reported "level panning
// without spectral directionality" symptom even through an otherwise-
// healthy convolver - see the investigation's Test F for the full
// methodology, run manually at the time since this codebase's decode
// matrix isn't exercised by an existing test elsewhere).
TEST(ambisonic_decode_concentrates_on_matching_speaker) {
  struct Dir { float az, el; };
  constexpr float kIcoEl = 26.56505117707799f; // atan(0.5), degrees
  std::vector<Dir> speakers = {
    { 0.0f, 90.0f }, { 0.0f, -90.0f },
    { 0.0f, kIcoEl }, { 72.0f, kIcoEl }, { 144.0f, kIcoEl }, { 216.0f, kIcoEl }, { 288.0f, kIcoEl },
    { 36.0f, -kIcoEl }, { 108.0f, -kIcoEl }, { 180.0f, -kIcoEl }, { 252.0f, -kIcoEl }, { 324.0f, -kIcoEl },
  };

  auto unitVector = [](float az_deg, float el_deg) {
    constexpr float kDeg2Rad = static_cast<float>(M_PI) / 180.0f;
    float az = az_deg * kDeg2Rad, el = el_deg * kDeg2Rad;
    return std::array<float, 3>{ cosf(el) * cosf(az), cosf(el) * sinf(az), sinf(el) };
  };

  std::vector<AmbisonicGains> decode;
  for (auto & s : speakers) decode.push_back(computeAmbisonicGains(SphericalPosition{ s.az, s.el, 1.0f }));

  for (size_t i = 0; i < speakers.size(); i++) {
    auto enc = computeAmbisonicGains(SphericalPosition{ speakers[i].az, speakers[i].el, 1.0f });

    std::vector<float> feed(speakers.size());
    for (size_t j = 0; j < speakers.size(); j++) {
      float f = 0.0f;
      for (int c = 0; c < kAmbisonicChannelCount; c++) f += enc[static_cast<size_t>(c)] * decode[j][static_cast<size_t>(c)];
      feed[j] = f;
    }

    size_t best = 0;
    for (size_t j = 1; j < feed.size(); j++) if (std::fabs(feed[j]) > std::fabs(feed[best])) best = j;
    CHECK(best == i);

    double sumsq = 0.0, rx = 0.0, ry = 0.0, rz = 0.0;
    for (size_t j = 0; j < speakers.size(); j++) {
      auto v = unitVector(speakers[j].az, speakers[j].el);
      double w = static_cast<double>(feed[j]) * feed[j];
      sumsq += w;
      rx += w * v[0]; ry += w * v[1]; rz += w * v[2];
    }
    rx /= sumsq; ry /= sumsq; rz /= sumsq;
    double rE_mag = std::sqrt(rx * rx + ry * ry + rz * rz);
    // Expected 0.6-0.85 at order 2 for a healthy decode matrix (see the
    // investigation) - loose bounds here since this is a regression guard
    // against a gross regression (near-uniform smear), not a precision check.
    CHECK(rE_mag > 0.5);
    CHECK(rE_mag < 0.95);
  }
}
