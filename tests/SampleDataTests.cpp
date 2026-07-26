#include "TestFramework.h"

#include "../SampleData.h"

#include <cmath>

TEST(sample_data_starts_zero) {
  SampleData data(2, 16);
  CHECK(data.isZero());
  CHECK(!data.isClipping());
  CHECK(data.numberOfChannels() == 2);
  CHECK(data.numberOfFrames() == 16);
}

TEST(sample_data_zero_fills_silence_and_marks_zero) {
  SampleData data(2, 8);
  data.setNonZero();
  for (int c = 0; c < 2; c++) {
    auto buf = data.getChannelData(c);
    for (int i = 0; i < 8; i++) buf[i] = 1.0f;
  }
  CHECK(!data.isZero());

  data.zero();
  CHECK(data.isZero());
  for (int c = 0; c < 2; c++) {
    auto buf = data.getChannelData(c);
    for (int i = 0; i < 8; i++) CHECK(buf[i] == 0.0f);
  }
}

TEST(sample_data_mix_sums_matching_channel_counts) {
  SampleData a(2, 4);
  a.zero();
  SampleData b(2, 4);
  b.zero();
  for (int c = 0; c < 2; c++) {
    auto ba = a.getChannelData(c);
    auto bb = b.getChannelData(c);
    for (int i = 0; i < 4; i++) {
      ba[i] = 0.25f;
      bb[i] = 0.5f;
    }
  }
  a.setNonZero();
  b.setNonZero();
  a.mix(b);

  for (int c = 0; c < 2; c++) {
    auto buf = a.getChannelData(c);
    for (int i = 0; i < 4; i++) CHECK_NEAR(buf[i], 0.75f, 1e-6f);
  }
}

TEST(sample_data_mix_of_silent_other_is_noop) {
  SampleData a(2, 4);
  a.zero();
  for (int c = 0; c < 2; c++) {
    auto buf = a.getChannelData(c);
    for (int i = 0; i < 4; i++) buf[i] = 0.3f;
  }
  a.setNonZero();

  SampleData silent(2, 4); // default-constructed, stays isZero()
  a.mix(silent);

  for (int c = 0; c < 2; c++) {
    auto buf = a.getChannelData(c);
    for (int i = 0; i < 4; i++) CHECK_NEAR(buf[i], 0.3f, 1e-6f);
  }
}

TEST(sample_data_assign_copies_at_position) {
  SampleData dest(1, 8);
  dest.zero();
  SampleData src(1, 4);
  src.zero();
  auto sbuf = src.getChannelData(0);
  for (int i = 0; i < 4; i++) sbuf[i] = static_cast<float>(i + 1);
  src.setNonZero();

  dest.assign(src, 2);

  auto dbuf = dest.getChannelData(0);
  for (int i = 0; i < 2; i++) CHECK_NEAR(dbuf[i], 0.0f, 1e-6f);
  for (int i = 0; i < 4; i++) CHECK_NEAR(dbuf[2 + i], static_cast<float>(i + 1), 1e-6f);
}

TEST(sample_data_resize_grow_preserves_existing_samples) {
  SampleData data(2, 4);
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
  SampleData data(2, 8);
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
  SampleData data(1, 4);
  data.zero();
  CHECK(!data.isClipping());

  auto buf = data.getChannelData(0);
  buf[2] = 1.5f;
  data.setNonZero();
  CHECK(data.isClipping());
}

TEST(sample_data_copy_is_independent_of_original) {
  SampleData original(1, 4);
  original.zero();
  auto obuf = original.getChannelData(0);
  for (int i = 0; i < 4; i++) obuf[i] = 1.0f;
  original.setNonZero();

  SampleData copy(original);
  auto cbuf = copy.getChannelData(0);
  for (int i = 0; i < 4; i++) cbuf[i] = 0.0f;

  // mutating the copy must not affect the original's buffer
  for (int i = 0; i < 4; i++) CHECK_NEAR(obuf[i], 1.0f, 1e-6f);
}

TEST(sample_data_repeated_copy_assign_does_not_leak) {
  // operator=(const SampleData&) must free its previous buffer before
  // allocating a new one; run under -DSYNTH_ENABLE_SANITIZERS=ON to have
  // LeakSanitizer catch a regression (it fails the whole process, not just
  // this CHECK).
  SampleData dest(2, 4);
  dest.zero();
  for (int iter = 0; iter < 50; iter++) {
    SampleData src(2, 4);
    src.zero();
    dest = src;
  }
  CHECK(dest.numberOfChannels() == 2);
}

TEST(sample_data_raw_count_constructor_marks_no_named_channels) {
  SampleData data(2, 4);
  CHECK(!data.hasChannel(Channel::SendA));
  CHECK(!data.hasChannel(Channel::SendB));
  CHECK(data.getChannel(Channel::SendA) == nullptr);
}

// The ChannelConfiguration constructor only ever produces regular
// (ambisonic) channels - accessed by plain raw index (0=W, 1=Y, ... - see
// AmbisonicEncoding.h's ACN ordering), not a per-channel name - so all this
// checks is the channel *count* at each order, plus that no send is ever
// marked present.
TEST(sample_data_channel_configuration_constructor_has_no_named_sends) {
  SampleData mono(ChannelConfiguration(44100), 4);
  CHECK(mono.numberOfChannels() == 1);
  CHECK(!mono.hasChannel(Channel::SendA));

  SampleData order1(ChannelConfiguration(44100, 1), 4);
  CHECK(order1.numberOfChannels() == 4);

  SampleData order2(ChannelConfiguration(44100, 2), 4);
  CHECK(order2.numberOfChannels() == 9);

  SampleData order3(ChannelConfiguration(44100, 3), 4);
  CHECK(order3.numberOfChannels() == 16);
}

TEST(sample_data_regular_plus_send_constructor_derives_send_indices) {
  // SendA's raw index accounts for however many regular channels precede
  // it - here just 1 (W only), so SendA lands at index 1.
  SampleData data(1, true, false, 4);
  CHECK(data.numberOfChannels() == 2);
  CHECK(data.hasChannel(Channel::SendA));
  CHECK(!data.hasChannel(Channel::SendB));
  CHECK(data.getChannel(Channel::SendA) == data.getChannelData(1));
  CHECK(data.sendCount() == 1);
}

TEST(sample_data_regular_plus_send_constructor_matches_configuration_constructor) {
  ChannelConfiguration order2(44100, 2);
  SampleData built(order2.numberOfChannels(), false, true, 4); // regular + SendB only

  SampleData reference(order2, 4);
  CHECK(built.numberOfChannels() == reference.numberOfChannels() + 1);
  CHECK(built.getChannel(Channel::SendB) == built.getChannelData(9));
}

TEST(sample_data_mix_named_sums_regular_channels_and_shared_sends) {
  SampleData acc(2, true, false, 4); // W, Y regular + SendA
  acc.zero();
  for (int i = 0; i < 4; i++) {
    acc.getChannelData(0)[i] = 0.1f;
    acc.getChannelData(1)[i] = 0.2f;
    acc.getChannel(Channel::SendA)[i] = 0.3f;
  }
  acc.setNonZero();

  // `other` lacks SendA but has SendB (which acc never marks present) -
  // both should be silently ignored where only one side has them.
  SampleData other(2, false, true, 4); // W, Y regular + SendB
  other.zero();
  for (int i = 0; i < 4; i++) {
    other.getChannelData(0)[i] = 1.0f;
    other.getChannelData(1)[i] = 2.0f;
    other.getChannel(Channel::SendB)[i] = 5.0f;
  }
  other.setNonZero();

  acc.mixNamed(other);

  CHECK_NEAR(acc.getChannelData(0)[0], 1.1f, 1e-6f);
  CHECK_NEAR(acc.getChannelData(1)[0], 2.2f, 1e-6f);
  CHECK_NEAR(acc.getChannel(Channel::SendA)[0], 0.3f, 1e-6f); // other had none - untouched
  CHECK(!acc.hasChannel(Channel::SendB)); // acc never claimed it - other's SendB is dropped
}

TEST(sample_data_mix_named_broadcasts_mono_into_stereo_like_mix) {
  SampleData acc(2, 4);
  acc.zero();
  SampleData mono(1, true, false, 4); // W regular + SendA
  mono.zero();
  for (int i = 0; i < 4; i++) {
    mono.getChannelData(0)[i] = 1.0f;
    mono.getChannel(Channel::SendA)[i] = 9.0f;
  }
  mono.setNonZero();

  acc.mixNamed(mono);

  CHECK_NEAR(acc.getChannelData(0)[0], 1.0f, 1e-6f);
  CHECK_NEAR(acc.getChannelData(1)[0], 1.0f, 1e-6f);
}
