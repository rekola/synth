#include "Oscilator.h"

#include "OscilatorVoice.h"

using namespace std;

std::unique_ptr<TrackState>
Oscilator::playNote(const ChannelConfiguration & config, float azimuth, float frequency, float detune, float velocity, float start_phase) const {  
  detune *= getHarmonic();
  detune /= getSubharmonic();

  auto voice = std::make_unique<OscilatorVoice>(config, azimuth, detune, start_phase, type_, level_);
  voice->playNote(frequency, velocity);

  // ChannelConfiguration child_config = config;
  // child_config.setType(ChannelConfiguration::MONO);
  
  // don't pass velocity or azimuth to children
  for (auto & child : getChildren()) {    
    auto modulator = child->playNote(config, 0.0f, frequency, detune, 1.0, start_phase);
    if (modulator) voice->addChild(move(modulator));
  }
  
  return voice;
}

void
Oscilator::loadParameters(const ParameterSource & input) {
  Instrument::loadParameters(input);
  
  auto type_text = input.getText("type", "sine");
  if (type_text == "sine") type_ = WaveformType::SINE;
  else if (type_text == "saw") type_ = WaveformType::SAW;
  else if (type_text == "triangle") type_ = WaveformType::TRIANGLE;
  else if (type_text == "square") type_ = WaveformType::SQUARE;
  else if (type_text == "noise") type_ = WaveformType::NOISE;
  else type_ = WaveformType::SINE;
  
  level_ = input.getFloat("level", 1.0f);
}

void
Oscilator::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);
  
  output.set("type", to_string(type_));
}
