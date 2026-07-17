#include "LFO.h"

#include "OscilatorVoice.h"

using namespace std;

std::unique_ptr<TrackState>
LFO::playNote(const ChannelConfiguration & config, float azimuth, float frequency, float detune, float velocity, float start_phase, int note_value) const {
  auto voice = std::make_unique<OscilatorVoice>(config, azimuth, detune, start_phase, WaveformType::SINE, level_, 0.5f);
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
