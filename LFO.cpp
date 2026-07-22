#include "LFO.h"

#include "OscilatorVoice.h"

using namespace std;

std::unique_ptr<TrackState>
LFO::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const {
  // LFO constructs its own OscilatorVoice directly (it's not itself a
  // modulator, an FM carrier's LFO target is) - the voice encodes its own
  // ambisonic output directly (InstrumentVoice::encodePosition()); since a
  // modulator's own position is always SphericalPosition{} (see
  // Oscilator.cpp), this just spreads unity gain into W, same as before.
  auto voice = std::make_unique<OscilatorVoice>(config, position, detune, start_phase, WaveformType::SINE, level_, 0.5f, send_a, send_b);
  voice->playNote(frequency_, velocity, note_value);
  return voice;
}

void
LFO::loadParameters(const ParameterSource & input) {
  Instrument::loadParameters(input);

  frequency_ = input.getFloat("frequency");
  level_ = input.getFloat("level");
}

void
LFO::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);
  
  output.set("frequency", frequency_);
  output.set("level", level_);
}
