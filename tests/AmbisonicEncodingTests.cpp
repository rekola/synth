#include "TestFramework.h"

#include "../AmbisonicEncoding.h"

namespace {
constexpr float kRef = 0.70710678118654752f; // 1/sqrt(2)
}

TEST(foa_gains_directionless_is_w_only) {
  auto gains = computeFoaGains(SphericalPosition{ 45.0f, 30.0f, 0.0f }); // distance <= 0
  CHECK_NEAR(gains.w, kRef, 0.0001f);
  CHECK(gains.x == 0.0f);
  CHECK(gains.y == 0.0f);
  CHECK(gains.z == 0.0f);

  auto gains_negative_distance = computeFoaGains(SphericalPosition{ 0.0f, 0.0f, -5.0f });
  CHECK_NEAR(gains_negative_distance.w, kRef, 0.0001f);
  CHECK(gains_negative_distance.x == 0.0f);
}

TEST(foa_gains_front_is_pure_x) {
  auto gains = computeFoaGains(SphericalPosition{ 0.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains.w, kRef, 0.0001f);
  CHECK_NEAR(gains.x, 1.0f, 0.0001f);
  CHECK_NEAR(gains.y, 0.0f, 0.0001f);
  CHECK_NEAR(gains.z, 0.0f, 0.0001f);
}

TEST(foa_gains_back_is_negative_x) {
  auto gains = computeFoaGains(SphericalPosition{ 180.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains.x, -1.0f, 0.0001f);
  CHECK_NEAR(gains.y, 0.0f, 0.0001f);
  CHECK_NEAR(gains.z, 0.0f, 0.0001f);
}

TEST(foa_gains_right_is_positive_y) {
  // This engine's azimuth convention: positive = right (see PanLaw.h).
  auto gains = computeFoaGains(SphericalPosition{ 90.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains.y, 1.0f, 0.0001f);
  CHECK_NEAR(gains.x, 0.0f, 0.0001f);
  CHECK_NEAR(gains.z, 0.0f, 0.0001f);
}

TEST(foa_gains_left_is_negative_y) {
  auto gains = computeFoaGains(SphericalPosition{ -90.0f, 0.0f, 1.0f });
  CHECK_NEAR(gains.y, -1.0f, 0.0001f);
  CHECK_NEAR(gains.x, 0.0f, 0.0001f);
  CHECK_NEAR(gains.z, 0.0f, 0.0001f);
}

TEST(foa_gains_up_is_positive_z) {
  auto gains = computeFoaGains(SphericalPosition{ 0.0f, 90.0f, 1.0f });
  CHECK_NEAR(gains.z, 1.0f, 0.0001f);
  CHECK_NEAR(gains.x, 0.0f, 0.0001f);
  CHECK_NEAR(gains.y, 0.0f, 0.0001f);
}

TEST(foa_gains_down_is_negative_z) {
  auto gains = computeFoaGains(SphericalPosition{ 0.0f, -90.0f, 1.0f });
  CHECK_NEAR(gains.z, -1.0f, 0.0001f);
}

TEST(foa_gains_attenuate_with_distance) {
  auto near_gains = computeFoaGains(SphericalPosition{ 0.0f, 0.0f, 1.0f });
  auto far_gains = computeFoaGains(SphericalPosition{ 0.0f, 0.0f, 2.0f });
  CHECK_NEAR(far_gains.x, near_gains.x / 2.0f, 0.0001f);
  CHECK_NEAR(far_gains.w, near_gains.w / 2.0f, 0.0001f);
}

TEST(ambisonic_voice_encoder_seeds_first_block_at_target_no_fade_in) {
  AmbisonicVoiceEncoder encoder;
  SampleData out(4, 8);
  out.zero();

  FoaGains target{ 0.5f, 0.25f, 0.0f, 0.1f };
  std::vector<float> mono(8, 1.0f);
  encoder.encodeBlock(out, mono.data(), 8, target);

  // First call: no previous gain to fade in from, so even sample 0 should
  // already reflect `target`, not a ramp starting from zero.
  CHECK_NEAR(out.getChannelData(0)[0], target.w, 0.0001f);
  CHECK_NEAR(out.getChannelData(1)[0], target.y, 0.0001f);
  CHECK_NEAR(out.getChannelData(3)[0], target.x, 0.0001f);
}

TEST(ambisonic_voice_encoder_interpolates_across_block_boundary) {
  AmbisonicVoiceEncoder encoder;
  std::vector<float> mono(8, 1.0f);

  FoaGains first{ 1.0f, 0.0f, 0.0f, 0.0f };
  SampleData out1(4, 8);
  out1.zero();
  encoder.encodeBlock(out1, mono.data(), 8, first);
  // Constant target throughout the first block.
  CHECK_NEAR(out1.getChannelData(0)[0], 1.0f, 0.0001f);
  CHECK_NEAR(out1.getChannelData(0)[7], 1.0f, 0.0001f);

  FoaGains second{ 0.0f, 1.0f, 0.0f, 0.0f };
  SampleData out2(4, 8);
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
