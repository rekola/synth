#include "Noise.h"

using namespace std;

#include "InstrumentVoice.h"
#include "WaveformType.h"
#include "PanLaw.h"
#include "AmbisonicEncoding.h"
#include "NoiseColor.h"
#include "NoiseGenerator.h"
#include "PinkNoiseFilter.h"

namespace {
// Pairs a NoiseGenerator (white) with a PinkNoiseFilter (optional shaping) -
// one independent instance per channel (left/right), so NoiseVoice's two
// channels stay decorrelated exactly as before, without now stealing
// samples from the shared getRandF()/rand() sequence at audio rate.
class NoiseStream {
 public:
  explicit NoiseStream(uint32_t seed) : rng_(seed) { }

  float next(NoiseColor color) {
    float white = rng_.next();
    return color == NoiseColor::PINK ? pink_.process(white) : white;
  }

 private:
  NoiseGenerator rng_;
  PinkNoiseFilter pink_;
};
}

class NoiseVoice : public InstrumentVoice {
public:
  NoiseVoice(ChannelConfiguration config, const SphericalPosition & position, float level, NoiseColor color, float send_a = 0.0f, float send_b = 0.0f)
    : InstrumentVoice(config, position, 1.0f, 0.0f, send_a, send_b), level_(level), color_(color),
      left_(seedFromRand()), right_(seedFromRand()) {
  }

  SampleData render(int frames) override {
    // base_gain (undistanced) feeds the sends - see InstrumentVoice::getDistanceGain().
    float base_gain = decibelsToGain(getGainDB()) * level_;
    float gain = base_gain * getDistanceGain();

    auto channels = regularChannelsFor(getChannelConfiguration());
    if (getSendA() > 0.0f) channels.push_back(Channel::SendA);
    if (getSendB() > 0.0f) channels.push_back(Channel::SendB);

    SampleData data(channels, frames);
    data.setNonZero();

    auto num_channels = getChannelConfiguration().numberOfChannels();
    auto left_buffer = data.getChannelData(0);
    auto right_buffer = num_channels == 2 ? data.getChannelData(1) : nullptr;
    auto send_a = data.getChannel(Channel::SendA);
    auto send_b = data.getChannel(Channel::SendB);

    if (right_buffer) {
      auto gains = panToStereoGains(azimuthToPan(getAzimuth()));
      float left_gain = gains.left * gain, right_gain = gains.right * gain;

      for (int k = 0; k < frames; k++) {
	float l = left_.next(color_), r = right_.next(color_);
	left_buffer[k] = left_gain * l;
	right_buffer[k] = right_gain * r;
	// Sends get a mono copy of the actual dry noise, not their own
	// independent draw - matching how every other leaf voice derives its
	// sends from the same sample driving the dry signal.
	float mono = 0.5f * (l + r);
	if (send_a) send_a[k] = base_gain * mono * getSendA();
	if (send_b) send_b[k] = base_gain * mono * getSendB();
      }
    } else {
      for (int k = 0; k < frames; k++) {
	auto a = left_.next(color_);
	left_buffer[k] = a * gain;
	if (send_a) send_a[k] = a * base_gain * getSendA();
	if (send_b) send_b[k] = a * base_gain * getSendB();
      }
    }

    return data;
  }

private:
  // One-time-per-voice seed draw from the shared getRandF() (same cost as
  // the existing per-note start-phase randomization) - everything at audio
  // rate afterwards comes from NoiseStream's own NoiseGenerator, not rand().
  static uint32_t seedFromRand() {
    return static_cast<uint32_t>(getRandF() * 4294967295.0f);
  }

  float level_;
  NoiseColor color_;
  NoiseStream left_, right_;
};

std::unique_ptr<TrackState>
Noise::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const {
  auto voice = std::make_unique<NoiseVoice>(reduceForPositionalGroup(config), position, level_, color_, send_a, send_b);
  voice->playNote(frequency, velocity, note_value);
  return voice;
}
