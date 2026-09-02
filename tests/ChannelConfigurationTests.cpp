#include "TestFramework.h"

#include "../src/ambisonic/ChannelConfiguration.h"

TEST(channel_configuration_channel_counts) {
  ChannelConfiguration mono(44100);
  ChannelConfiguration ambisonic(44100, 1);
  ChannelConfiguration order3(44100, 3);
  CHECK(mono.numberOfChannels() == 1);
  CHECK(ambisonic.numberOfChannels() == 4);
  CHECK(order3.numberOfChannels() == 16);
}

TEST(channel_configuration_is_mono_and_is_ambisonic) {
  ChannelConfiguration mono(44100);
  CHECK(mono.isMono());
  CHECK(!mono.isAmbisonic());

  ChannelConfiguration order1(44100, 1);
  CHECK(!order1.isMono());
  CHECK(order1.isAmbisonic());

  ChannelConfiguration order2(44100, 2);
  CHECK(!order2.isMono());
  CHECK(order2.isAmbisonic());

  ChannelConfiguration order3(44100, 3);
  CHECK(!order3.isMono());
  CHECK(order3.isAmbisonic());
}

TEST(channel_configuration_sample_interval_scales_with_tempo) {
  ChannelConfiguration config(44100, 1);
  // one pattern row is a 16th note; doubling the tempo halves its duration
  auto interval_120 = config.getSampleInterval(120);
  auto interval_240 = config.getSampleInterval(240);
  CHECK(interval_120 > 0);
  CHECK_NEAR(static_cast<float>(interval_120) / 2.0f, static_cast<float>(interval_240), 2.0f);
}

TEST(channel_configuration_equality) {
  ChannelConfiguration a(44100, 1);
  ChannelConfiguration b(44100, 1);
  ChannelConfiguration c(48000, 1);
  CHECK(a == b);
  CHECK(!(a == c));
}

TEST(channel_configuration_frames_to_rows_rounds_up_and_handles_degenerate_input) {
  ChannelConfiguration config(44100, 1);
  auto interval = config.getSampleInterval(120); // one row's own frame count at this tempo

  CHECK(config.framesToRows(interval, 120) == 1); // exactly one row's worth
  CHECK(config.framesToRows(interval + 1, 120) == 2); // one frame past a row - rounds up, never truncates
  CHECK(config.framesToRows(interval - 1, 120) == 1); // one frame short of a row - still counts as that row
  CHECK(config.framesToRows(0, 120) == 0); // nothing to place a window around
  CHECK(config.framesToRows(-5, 120) == 0); // degenerate/empty buffer, not a negative row count
}

TEST(channel_configuration_ambisonic_channel_count_and_device_channels) {
  ChannelConfiguration foa(44100, 1);
  CHECK(foa.numberOfChannels() == 4);
  CHECK(foa.getDeviceChannels() == 2); // always decoded to stereo for output

  ChannelConfiguration second_order(44100, 2);
  CHECK(second_order.numberOfChannels() == 9); // (order+1)^2

  ChannelConfiguration third_order(44100, 3);
  CHECK(third_order.numberOfChannels() == 16); // (order+1)^2
  CHECK(third_order.getDeviceChannels() == 2);

  ChannelConfiguration mono(44100);
  CHECK(mono.numberOfChannels() == 1); // 0th-order-ambisonic: W only
  CHECK(mono.getDeviceChannels() == 2); // still broadcast to stereo, like every other mixer
}
