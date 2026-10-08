#include "FM.h"

#include "FMIndexDecay.h"
#include "InstrumentVoice.h"

#include <cmath>
#include <vector>

using namespace std;

namespace {

class FMVoice : public InstrumentVoice {
public:
  FMVoice(ChannelConfiguration config, const SphericalPosition & position, float detune, float level, float ratio, float index, float index_decay, const SendLevels & sends, const NoteCoordinate & note_coord)
    : InstrumentVoice(config, position, detune, sends, note_coord), level_(level), ratio_(ratio),
      index_(index, index_decay, static_cast<float>(getChannelConfiguration().getAudioOutSampleRate())) { }

  AudioBuffer render(int frames) override {
    float gain = decibelsToGain(getGainDB()) * level_;

    double sample_rate = getChannelConfiguration().getAudioOutSampleRate();
    double pos = getSourceSamplePosition() / sample_rate;
    double rate = static_cast<double>(getFrequency()) / sample_rate;
    constexpr double two_pi = 2.0 * M_PI;

    if (static_cast<int>(dry_.size()) != frames) dry_.resize(static_cast<size_t>(frames));

    for (int k = 0; k < frames; k++) {
      double carrier = pos - floor(pos);
      double modulator = pos * ratio_;
      modulator -= floor(modulator);
      dry_[static_cast<size_t>(k)] = gain * static_cast<float>(sin(two_pi * carrier + static_cast<double>(index_.value()) * sin(two_pi * modulator)));

      index_.advance();
      pos += rate;
    }

    stepForward(frames);

    return encodePosition(dry_.data(), frames);
  }

  void playNote(float frequency, float velocity, int note_value) override {
    InstrumentVoice::playNote(frequency, velocity, note_value);
    index_.scale(velocity);
  }

private:
  float level_;
  float ratio_;
  FMIndexDecay index_;
  std::vector<float> dry_;
};

}

std::unique_ptr<VoiceState>
FM::playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const {
  detune *= powf(2.0f, detune_cents_ / 1200.0f);
  // A detuned copy starts at its own phase (keyed by its detune): layers
  // started in phase would cancel each other's partials wherever the
  // index makes them opposite in sign.
  NoteCoordinate coord = detune_cents_ != 0.0f ? note_coord.withInstance(static_cast<int>(lroundf(detune_cents_ * 16.0f))) : note_coord;
  float frequency = getFrequencyFor(tuning, note_value);
  constexpr float kMiddleC = 261.63f;
  float index = index_tracking_ != 0.0f && frequency > 0.0f ? index_ * powf(kMiddleC / frequency, index_tracking_) : index_;
  auto voice = std::make_unique<FMVoice>(config, position, detune, level_, ratio_, index, index_decay_, sends, coord);
  voice->playNote(frequency, velocity, note_value);
  return voice;
}

void
FM::loadParameters(const ParameterSource & input) {
  Instrument::loadParameters(input);

  level_ = input.get<float>("level", 1.0f);
  ratio_ = input.get<float>("ratio", 1.0f);
  index_ = input.get<float>("index", 1.0f);
  index_decay_ = input.get<float>("indexDecay", 0.0f);
  index_tracking_ = input.get<float>("indexTracking", 0.0f);
  detune_cents_ = input.get<float>("detune", 0.0f);
}

void
FM::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);

  output.set("level", level_);
  output.set("ratio", ratio_);
  output.set("index", index_);
  output.set("indexDecay", index_decay_);
  output.set("indexTracking", index_tracking_, 0.0f);
  output.set("detune", detune_cents_, 0.0f);
}
