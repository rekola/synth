#include "SendBusProcessor.h"

SendBusProcessor::SendBusProcessor(const ChannelConfiguration & config)
  : reverb_(config.getAudioOutSampleRate()),
    delay_(config.getAudioOutSampleRate()),
    ambisonic_channels_(config.numberOfChannels()) {
  // Fixed for this instance's lifetime - a song's own position never
  // moves, so no per-block gain-interpolation (AmbisonicVoiceEncoder) is
  // needed here the way a moving voice needs it.
  auto directions = cubeVertexDirections();
  for (size_t i = 0; i < directions.size(); i++) {
    tap_gains_[i] = computeAmbisonicGains(SphericalPosition{ directions[i].azimuth, directions[i].elevation, 1.0f });
  }
}

void
SendBusProcessor::setReverbParameters(float size, float decayRT60Seconds, float damping, float preDelaySeconds, float wetLevel) {
  reverb_.setParameters(size, decayRT60Seconds, damping, preDelaySeconds);
  wet_level_ = wetLevel;
}

void
SendBusProcessor::setDelayParameters(float baseRows, float feedbackGain, float damping, DelayPattern pattern, float patternSpeed, float wetLevel, float rowDurationSeconds) {
  delay_.setParameters(baseRows, feedbackGain, damping, pattern, patternSpeed, rowDurationSeconds);
  delay_wet_ = wetLevel;
}

void
SendBusProcessor::process(const SampleData & send_a_mono, const SampleData & send_b_mono, int frames) {
  reverb_.process(send_a_mono.getChannelData(0), frames);
  delay_.process(send_b_mono.getChannelData(0), frames);

  if (bus_ambisonic_.numberOfFrames() != frames || bus_ambisonic_.numberOfChannels() != ambisonic_channels_) {
    bus_ambisonic_ = SampleData(static_cast<short>(ambisonic_channels_), frames);
  }
  bus_ambisonic_.zero();

  int channelLimit = ambisonic_channels_ < kAmbisonicChannelCount ? ambisonic_channels_ : kAmbisonicChannelCount;
  for (int tap = 0; tap < FDNReverb::kNumLines; tap++) {
    auto tapData = reverb_.getTap(tap);
    auto & gains = tap_gains_[static_cast<size_t>(tap)];
    for (int c = 0; c < channelLimit; c++) {
      auto dst = bus_ambisonic_.getChannelData(c);
      float g = gains[static_cast<size_t>(c)] * wet_level_;
      for (int i = 0; i < frames; i++) dst[i] += g * tapData[i];
    }
  }

  for (int tap = 0; tap < MultiTapDelay::kNumTaps; tap++) {
    auto gains = computeAmbisonicGains(delay_.getTapDirection(tap));
    for (auto & g : gains) g *= delay_wet_;
    delay_tap_encoders_[static_cast<size_t>(tap)].encodeBlock(bus_ambisonic_, delay_.getTap(tap), frames, gains);
  }

  bus_ambisonic_.setNonZero();
}
