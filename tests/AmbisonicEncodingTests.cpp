#include "TestFramework.h"

#include "../AmbisonicEncoding.h"
#include "../InstrumentVoice.h"

namespace {
constexpr float kSqrt3Over2 = 0.86602540378443864676f;
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

TEST(ambisonic_voice_encoder_seeds_first_block_at_target_no_fade_in) {
  AmbisonicVoiceEncoder encoder;
  SampleData out(9, 8);
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
  SampleData out1(9, 8);
  out1.zero();
  encoder.encodeBlock(out1, mono.data(), 8, first);
  // Constant target throughout the first block.
  CHECK_NEAR(out1.getChannelData(0)[0], 1.0f, 0.0001f);
  CHECK_NEAR(out1.getChannelData(0)[7], 1.0f, 0.0001f);

  AmbisonicGains second{ 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  SampleData out2(9, 8);
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

TEST(ambisonic_voice_encoder_ignores_trailing_send_channels) {
  // A 3-channel accumulator (1 regular Mono + SendA + SendB) should only
  // ever be written to by encodeBlock on channel 0 - PositionalMixer::encode
  // handles SendA/SendB itself, separately (see below).
  AmbisonicVoiceEncoder encoder;
  SampleData out({ Channel::Mono, Channel::SendA, Channel::SendB }, 4);
  out.zero();
  AmbisonicGains target{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  std::vector<float> mono(4, 1.0f);
  encoder.encodeBlock(out, mono.data(), 4, target);
  CHECK_NEAR(out.getChannelData(0)[0], 1.0f, 0.0001f);
  CHECK_NEAR(out.getChannel(Channel::SendA)[0], 0.0f, 0.0001f);
  CHECK_NEAR(out.getChannel(Channel::SendB)[0], 0.0f, 0.0001f);
}

TEST(positional_mixer_encode_sums_sends_without_spatial_gain) {
  PositionalMixer mixer;
  SampleData accumulator({ Channel::W, Channel::Y, Channel::Z, Channel::X, Channel::SendA, Channel::SendB }, 4);
  accumulator.zero();

  SampleData voice({ Channel::Mono, Channel::SendA, Channel::SendB }, 4);
  voice.zero();
  for (int i = 0; i < 4; i++) {
    voice.getChannelData(0)[i] = 1.0f;
    voice.getChannel(Channel::SendA)[i] = 0.5f;
    voice.getChannel(Channel::SendB)[i] = 0.25f;
  }
  voice.setNonZero();

  mixer.encode(&voice, voice, SphericalPosition{ 0.0f, 0.0f, 1.0f }, accumulator);

  // The mono signal was spatially encoded (front: W and X nonzero).
  CHECK(accumulator.getChannelData(0)[0] != 0.0f);
  CHECK(accumulator.getChannelData(3)[0] != 0.0f);
  // Sends are straight-summed, not spatially encoded.
  CHECK_NEAR(accumulator.getChannel(Channel::SendA)[0], 0.5f, 0.0001f);
  CHECK_NEAR(accumulator.getChannel(Channel::SendB)[0], 0.25f, 0.0001f);
}

TEST(stereo_decode_reencode_preserves_left_right_direction) {
  SampleData ambisonic_left(4, 4);
  ambisonic_left.zero();
  SampleData stereo_left(2, 4);
  stereo_left.zero();
  for (int i = 0; i < 4; i++) {
    stereo_left.getChannelData(0)[i] = 1.0f; // hard left
    stereo_left.getChannelData(1)[i] = 0.0f;
  }
  encodeStereoAsPoints(stereo_left, ambisonic_left);

  SampleData decoded_left(2, 4);
  decodeToStereo(ambisonic_left, decoded_left);
  CHECK(decoded_left.getChannelData(0)[0] > decoded_left.getChannelData(1)[0]);

  SampleData ambisonic_right(4, 4);
  ambisonic_right.zero();
  SampleData stereo_right(2, 4);
  stereo_right.zero();
  for (int i = 0; i < 4; i++) {
    stereo_right.getChannelData(0)[i] = 0.0f;
    stereo_right.getChannelData(1)[i] = 1.0f; // hard right
  }
  encodeStereoAsPoints(stereo_right, ambisonic_right);

  SampleData decoded_right(2, 4);
  decodeToStereo(ambisonic_right, decoded_right);
  CHECK(decoded_right.getChannelData(1)[0] > decoded_right.getChannelData(0)[0]);

  // A centered (equal L/R) signal decodes back symmetrically.
  SampleData ambisonic_center(4, 4);
  ambisonic_center.zero();
  SampleData stereo_center(2, 4);
  stereo_center.zero();
  for (int i = 0; i < 4; i++) {
    stereo_center.getChannelData(0)[i] = 1.0f;
    stereo_center.getChannelData(1)[i] = 1.0f;
  }
  encodeStereoAsPoints(stereo_center, ambisonic_center);

  SampleData decoded_center(2, 4);
  decodeToStereo(ambisonic_center, decoded_center);
  CHECK_NEAR(decoded_center.getChannelData(0)[0], decoded_center.getChannelData(1)[0], 0.0001f);
}

TEST(reduce_for_effect_narrows_ambisonic_to_stereo_only) {
  ChannelConfiguration ambisonic(ChannelConfiguration::AMBISONIC, 44100);
  auto reduced = reduceForEffect(ambisonic);
  CHECK(reduced.getType() == ChannelConfiguration::STEREO);

  ChannelConfiguration stereo(ChannelConfiguration::STEREO, 44100);
  CHECK(reduceForEffect(stereo).getType() == ChannelConfiguration::STEREO);

  ChannelConfiguration mono(ChannelConfiguration::MONO, 44100);
  CHECK(reduceForEffect(mono).getType() == ChannelConfiguration::MONO);
}

TEST(reduce_for_positional_group_narrows_ambisonic_to_mono_only) {
  ChannelConfiguration ambisonic(ChannelConfiguration::AMBISONIC, 44100);
  auto reduced = reduceForPositionalGroup(ambisonic);
  CHECK(reduced.getType() == ChannelConfiguration::MONO);

  ChannelConfiguration stereo(ChannelConfiguration::STEREO, 44100);
  CHECK(reduceForPositionalGroup(stereo).getType() == ChannelConfiguration::STEREO);
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
