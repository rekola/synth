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
