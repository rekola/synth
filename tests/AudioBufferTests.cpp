#include "TestFramework.h"

#include "../src/AudioBuffer.h"

#include <cmath>

TEST(sample_data_with_zero_regular_channels_has_no_main) {
  // The only way to get an AudioBuffer with Main absent - the raw/
  // ChannelConfiguration constructors always imply Main present (see
  // AudioBuffer.h's own doc comments) - is the 4-arg constructor with
  // regular_channels = 0, e.g. a voice whose Send Main level is 0.
  AudioBuffer data(0, false, false, 16);
  CHECK(!data.hasChannel(Channel::Main));
  CHECK(!data.isClipping());
  CHECK(data.numberOfChannels() == 0);
  CHECK(data.numberOfFrames() == 16);
}

TEST(sample_data_channel_presence_is_never_mutated_by_zero_or_clear) {
  // Main's presence is derived purely from the channel counts fixed at
  // construction (see AudioBuffer.h's Channel enum doc comment) - zero()/
  // clear() reset sample values/frame count, never channel presence.
  AudioBuffer data(2, 8);
  CHECK(data.hasChannel(Channel::Main));
  data.zero();
  CHECK(data.hasChannel(Channel::Main));
  data.clear();
  CHECK(data.hasChannel(Channel::Main)); // channels_ untouched by clear()
}

TEST(sample_data_zero_fills_silence) {
  AudioBuffer data(2, 8);
  for (int c = 0; c < 2; c++) {
    auto buf = data.getChannelData(c);
    for (int i = 0; i < 8; i++) buf[i] = 1.0f;
  }

  data.zero();
  for (int c = 0; c < 2; c++) {
    auto buf = data.getChannelData(c);
    for (int i = 0; i < 8; i++) CHECK(buf[i] == 0.0f);
  }
}

TEST(sample_data_mix_sums_matching_channel_counts) {
  AudioBuffer a(2, 4);
  a.zero();
  AudioBuffer b(2, 4);
  b.zero();
  for (int c = 0; c < 2; c++) {
    auto ba = a.getChannelData(c);
    auto bb = b.getChannelData(c);
    for (int i = 0; i < 4; i++) {
      ba[i] = 0.25f;
      bb[i] = 0.5f;
    }
  }
  a.mix(b);

  for (int c = 0; c < 2; c++) {
    auto buf = a.getChannelData(c);
    for (int i = 0; i < 4; i++) CHECK_NEAR(buf[i], 0.75f, 1e-6f);
  }
}

TEST(sample_data_mix_of_zeroed_other_is_noop) {
  // Unlike the old is_zero_-flag days, mix() no longer skips a real,
  // properly-shaped operand just because its content happens to be all
  // zero (there's no cheap "definitely silent" flag left to check) - this
  // now tests that adding real zeros is a mathematical no-op, not a skip
  // optimization.
  AudioBuffer a(2, 4);
  a.zero();
  for (int c = 0; c < 2; c++) {
    auto buf = a.getChannelData(c);
    for (int i = 0; i < 4; i++) buf[i] = 0.3f;
  }

  AudioBuffer zeroed(2, 4);
  zeroed.zero();
  a.mix(zeroed);

  for (int c = 0; c < 2; c++) {
    auto buf = a.getChannelData(c);
    for (int i = 0; i < 4; i++) CHECK_NEAR(buf[i], 0.3f, 1e-6f);
  }
}

TEST(sample_data_mix_of_empty_other_is_noop) {
  // A genuinely empty (default-constructed, 0 channels/0 frames) operand
  // is the one case mix() still explicitly guards against (other.empty()),
  // regardless of content.
  AudioBuffer a(2, 4);
  a.zero();
  for (int c = 0; c < 2; c++) {
    auto buf = a.getChannelData(c);
    for (int i = 0; i < 4; i++) buf[i] = 0.3f;
  }

  AudioBuffer empty;
  a.mix(empty);

  for (int c = 0; c < 2; c++) {
    auto buf = a.getChannelData(c);
    for (int i = 0; i < 4; i++) CHECK_NEAR(buf[i], 0.3f, 1e-6f);
  }
}

TEST(sample_data_assign_copies_at_position) {
  AudioBuffer dest(1, 8);
  dest.zero();
  AudioBuffer src(1, 4);
  src.zero();
  auto sbuf = src.getChannelData(0);
  for (int i = 0; i < 4; i++) sbuf[i] = static_cast<float>(i + 1);

  dest.assign(src, 2);

  auto dbuf = dest.getChannelData(0);
  for (int i = 0; i < 2; i++) CHECK_NEAR(dbuf[i], 0.0f, 1e-6f);
  for (int i = 0; i < 4; i++) CHECK_NEAR(dbuf[2 + i], static_cast<float>(i + 1), 1e-6f);
}

TEST(sample_data_resize_grow_preserves_existing_samples) {
  AudioBuffer data(2, 4);
  data.zero();
  for (int c = 0; c < 2; c++) {
    auto buf = data.getChannelData(c);
    for (int i = 0; i < 4; i++) buf[i] = static_cast<float>(c * 10 + i);
  }

  data.resize(8);
  CHECK(data.numberOfFrames() == 8);
  for (int c = 0; c < 2; c++) {
    auto buf = data.getChannelData(c);
    for (int i = 0; i < 4; i++) CHECK_NEAR(buf[i], static_cast<float>(c * 10 + i), 1e-6f);
  }
}

TEST(sample_data_resize_shrink_preserves_remaining_samples) {
  // resize() previously always copied the *old* frame count into the new
  // buffer, so shrinking wrote past the end of the smaller allocation.
  AudioBuffer data(2, 8);
  data.zero();
  for (int c = 0; c < 2; c++) {
    auto buf = data.getChannelData(c);
    for (int i = 0; i < 8; i++) buf[i] = static_cast<float>(c * 10 + i);
  }

  data.resize(4);
  CHECK(data.numberOfFrames() == 4);
  for (int c = 0; c < 2; c++) {
    auto buf = data.getChannelData(c);
    for (int i = 0; i < 4; i++) CHECK_NEAR(buf[i], static_cast<float>(c * 10 + i), 1e-6f);
  }
}

TEST(sample_data_is_clipping_detects_out_of_range_samples) {
  AudioBuffer data(1, 4);
  data.zero();
  CHECK(!data.isClipping());

  auto buf = data.getChannelData(0);
  buf[2] = 1.5f;
  CHECK(data.isClipping());
}

// isClipping()/calculateLoudness() must not gate on Main presence at all -
// a buffer can have zero Main channels and real, clippable Aux content (a
// fully aux-only voice, e.g. Send Main = 0).
TEST(sample_data_is_clipping_and_loudness_work_with_main_absent) {
  AudioBuffer data(0, true, false, 4); // AuxA only, no Main at all
  auto aux = data.getChannel(Channel::AuxA);
  for (int i = 0; i < 4; i++) aux[i] = 0.2f;
  CHECK(!data.isClipping());

  auto loudness = data.calculateLoudness();
  CHECK(loudness.size() == 1);
  CHECK(loudness[0] > 0.0f);

  aux[0] = 2.0f;
  CHECK(data.isClipping());
}

TEST(sample_data_copy_is_independent_of_original) {
  AudioBuffer original(1, 4);
  original.zero();
  auto obuf = original.getChannelData(0);
  for (int i = 0; i < 4; i++) obuf[i] = 1.0f;

  AudioBuffer copy(original);
  auto cbuf = copy.getChannelData(0);
  for (int i = 0; i < 4; i++) cbuf[i] = 0.0f;

  // mutating the copy must not affect the original's buffer
  for (int i = 0; i < 4; i++) CHECK_NEAR(obuf[i], 1.0f, 1e-6f);
}

TEST(sample_data_repeated_copy_assign_does_not_leak) {
  // operator=(const AudioBuffer&) must free its previous buffer before
  // allocating a new one; run under -DSYNTH_ENABLE_SANITIZERS=ON to have
  // LeakSanitizer catch a regression (it fails the whole process, not just
  // this CHECK).
  AudioBuffer dest(2, 4);
  dest.zero();
  for (int iter = 0; iter < 50; iter++) {
    AudioBuffer src(2, 4);
    src.zero();
    dest = src;
  }
  CHECK(dest.numberOfChannels() == 2);
}

TEST(sample_data_raw_count_constructor_marks_no_named_aux) {
  AudioBuffer data(2, 4);
  CHECK(data.hasChannel(Channel::Main));
  CHECK(!data.hasChannel(Channel::AuxA));
  CHECK(!data.hasChannel(Channel::AuxB));
  CHECK(data.getChannel(Channel::AuxA) == nullptr);
}

// The ChannelConfiguration constructor only ever produces regular
// (ambisonic) channels - accessed by plain raw index (0=W, 1=Y, ... - see
// AmbisonicEncoding.h's ACN ordering), not a per-channel name - so all this
// checks is the channel *count* at each order, plus that no aux channel is
// ever marked present.
TEST(sample_data_channel_configuration_constructor_has_no_named_aux) {
  AudioBuffer mono(ChannelConfiguration(44100), 4);
  CHECK(mono.numberOfChannels() == 1);
  CHECK(mono.hasChannel(Channel::Main));
  CHECK(!mono.hasChannel(Channel::AuxA));

  AudioBuffer order1(ChannelConfiguration(44100, 1), 4);
  CHECK(order1.numberOfChannels() == 4);

  AudioBuffer order2(ChannelConfiguration(44100, 2), 4);
  CHECK(order2.numberOfChannels() == 9);

  AudioBuffer order3(ChannelConfiguration(44100, 3), 4);
  CHECK(order3.numberOfChannels() == 16);
}

TEST(sample_data_regular_plus_aux_constructor_derives_aux_indices) {
  // AuxA's raw index accounts for however many regular channels precede
  // it - here just 1 (W only), so AuxA lands at index 1.
  AudioBuffer data(1, true, false, 4);
  CHECK(data.numberOfChannels() == 2);
  CHECK(data.hasChannel(Channel::AuxA));
  CHECK(!data.hasChannel(Channel::AuxB));
  CHECK(data.getChannel(Channel::AuxA) == data.getChannelData(1));
  CHECK(data.auxCount() == 1);
}

TEST(sample_data_regular_plus_aux_constructor_matches_configuration_constructor) {
  ChannelConfiguration order2(44100, 2);
  AudioBuffer built(order2.numberOfChannels(), false, true, 4); // regular + AuxB only

  AudioBuffer reference(order2, 4);
  CHECK(built.numberOfChannels() == reference.numberOfChannels() + 1);
  CHECK(built.getChannel(Channel::AuxB) == built.getChannelData(9));
}

TEST(sample_data_mix_named_sums_regular_channels_and_shared_aux) {
  AudioBuffer acc(2, true, false, 4); // W, Y regular + AuxA
  acc.zero();
  for (int i = 0; i < 4; i++) {
    acc.getChannelData(0)[i] = 0.1f;
    acc.getChannelData(1)[i] = 0.2f;
    acc.getChannel(Channel::AuxA)[i] = 0.3f;
  }

  // `other` lacks AuxA but has AuxB (which acc never marks present) -
  // both should be silently ignored where only one side has them.
  AudioBuffer other(2, false, true, 4); // W, Y regular + AuxB
  other.zero();
  for (int i = 0; i < 4; i++) {
    other.getChannelData(0)[i] = 1.0f;
    other.getChannelData(1)[i] = 2.0f;
    other.getChannel(Channel::AuxB)[i] = 5.0f;
  }

  acc.mixNamed(other);

  CHECK_NEAR(acc.getChannelData(0)[0], 1.1f, 1e-6f);
  CHECK_NEAR(acc.getChannelData(1)[0], 2.2f, 1e-6f);
  CHECK_NEAR(acc.getChannel(Channel::AuxA)[0], 0.3f, 1e-6f); // other had none - untouched
  CHECK(!acc.hasChannel(Channel::AuxB)); // acc never claimed it - other's AuxB is dropped
}

TEST(sample_data_mix_named_broadcasts_mono_into_stereo_like_mix) {
  AudioBuffer acc(2, 4);
  acc.zero();
  AudioBuffer mono(1, true, false, 4); // W regular + AuxA
  mono.zero();
  for (int i = 0; i < 4; i++) {
    mono.getChannelData(0)[i] = 1.0f;
    mono.getChannel(Channel::AuxA)[i] = 9.0f;
  }

  acc.mixNamed(mono);

  CHECK_NEAR(acc.getChannelData(0)[0], 1.0f, 1e-6f);
  CHECK_NEAR(acc.getChannelData(1)[0], 1.0f, 1e-6f);
}

// mixNamed()'s other_regular == 0 case: a child with no Main channels at
// all (e.g. Send Main = 0) mixing into an accumulator that does have Main
// (because some other child does) - used to hit assert(0) before this was
// added as an explicit case; must be a clean no-op for the regular part,
// while Aux still sums normally.
TEST(sample_data_mix_named_handles_other_with_zero_regular_channels) {
  AudioBuffer acc(2, true, false, 4); // W, Y regular + AuxA
  acc.zero();
  for (int i = 0; i < 4; i++) {
    acc.getChannelData(0)[i] = 0.5f;
    acc.getChannelData(1)[i] = 0.6f;
    acc.getChannel(Channel::AuxA)[i] = 0.1f;
  }

  AudioBuffer other(0, true, false, 4); // no Main at all, AuxA only
  other.zero();
  for (int i = 0; i < 4; i++) other.getChannel(Channel::AuxA)[i] = 0.4f;

  acc.mixNamed(other);

  CHECK_NEAR(acc.getChannelData(0)[0], 0.5f, 1e-6f); // untouched - other had no Main
  CHECK_NEAR(acc.getChannelData(1)[0], 0.6f, 1e-6f);
  CHECK_NEAR(acc.getChannel(Channel::AuxA)[0], 0.5f, 1e-6f); // 0.1 + 0.4
}
