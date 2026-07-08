#include "TestFramework.h"

#include "../ChannelConfiguration.h"

TEST(channel_configuration_channel_counts) {
  ChannelConfiguration mono(ChannelConfiguration::MONO, 44100);
  ChannelConfiguration stereo(ChannelConfiguration::STEREO, 44100);
  CHECK(mono.numberOfChannels() == 1);
  CHECK(stereo.numberOfChannels() == 2);
}

TEST(channel_configuration_sample_interval_scales_with_tempo) {
  ChannelConfiguration config(ChannelConfiguration::STEREO, 44100);
  // one pattern row is a 16th note; doubling the tempo halves its duration
  auto interval_120 = config.getSampleInterval(120);
  auto interval_240 = config.getSampleInterval(240);
  CHECK(interval_120 > 0);
  CHECK_NEAR(static_cast<float>(interval_120) / 2.0f, static_cast<float>(interval_240), 2.0f);
}

TEST(channel_configuration_equality) {
  ChannelConfiguration a(ChannelConfiguration::STEREO, 44100);
  ChannelConfiguration b(ChannelConfiguration::STEREO, 44100);
  ChannelConfiguration c(ChannelConfiguration::STEREO, 48000);
  CHECK(a == b);
  CHECK(!(a == c));
}
