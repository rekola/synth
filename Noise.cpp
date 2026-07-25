#include "Noise.h"

using namespace std;

#include "InstrumentVoice.h"
#include "WaveformType.h"
#include "NoiseColor.h"
#include "dsp/NoiseGenerator.h"
#include "dsp/PinkNoiseFilter.h"

#include <vector>

namespace {
// Pairs a NoiseGenerator (white) with a PinkNoiseFilter (optional shaping) -
// without stealing samples from the shared getRandF()/rand() sequence at
// audio rate.
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
  NoiseVoice(ChannelConfiguration config, const SphericalPosition & position, float level, NoiseColor color, const SendLevels & sends = {})
    : InstrumentVoice(config, position, 1.0f, 0.0f, sends), level_(level), color_(color),
      noise_(seedFromRand()) {
  }

  SampleData render(int frames) override {
    // No getDistanceGain() here - encodePosition() applies distance
    // attenuation itself now (see its own doc comment in InstrumentVoice.h).
    float gain = decibelsToGain(getGainDB()) * level_;

    if (static_cast<int>(dry_.size()) != frames) dry_.resize(static_cast<size_t>(frames));
    for (int k = 0; k < frames; k++) dry_[static_cast<size_t>(k)] = noise_.next(color_) * gain;

    return encodePosition(dry_.data(), frames);
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
  NoiseStream noise_;
  std::vector<float> dry_;
};

std::unique_ptr<TrackState>
Noise::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, const SendLevels & sends) const {
  auto voice = std::make_unique<NoiseVoice>(config, position, level_, color_, sends);
  voice->playNote(frequency, velocity, note_value);
  return voice;
}
