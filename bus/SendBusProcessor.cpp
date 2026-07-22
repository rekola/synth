#include "SendBusProcessor.h"

SendBusProcessor::SendBusProcessor(const ChannelConfiguration & config)
  : reverb_(config.getAudioOutSampleRate()),
    chorus_(config.getAudioOutSampleRate()),
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
SendBusProcessor::process(const SampleData & send_a_mono, const SampleData & send_b_mono, int frames) {
  reverb_.process(send_a_mono.getChannelData(0), frames);
  chorus_.process(send_b_mono.getChannelData(0), frames);

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

  // Chorus: trim, then fold its stereo output into the same ambisonic
  // accumulator at the fixed az +-90 points encodeStereoAsPoints() always
  // uses - accumulates into W/Y only (matching its own "added, not
  // overwritten" contract), never touching the reverb's higher-order taps.
  if (chorus_trimmed_.numberOfFrames() != frames) chorus_trimmed_ = SampleData(2, frames);
  auto & chorus_stereo = chorus_.getStereoOutput();
  auto cl = chorus_trimmed_.getChannelData(0), cr = chorus_trimmed_.getChannelData(1);
  auto scl = chorus_stereo.getChannelData(0), scr = chorus_stereo.getChannelData(1);
  for (int i = 0; i < frames; i++) {
    cl[i] = scl[i] * kChorusWetTrim;
    cr[i] = scr[i] * kChorusWetTrim;
  }
  chorus_trimmed_.setNonZero();
  encodeStereoAsPoints(chorus_trimmed_, bus_ambisonic_);

  bus_ambisonic_.setNonZero();
}
