#include "Oscilator.h"

#include "OscilatorVoice.h"

using namespace std;

std::unique_ptr<TrackState>
Oscilator::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const {
  detune *= getHarmonic();
  detune /= getSubharmonic();

  // The voice encodes its own ambisonic output directly from its own
  // position (see InstrumentVoice::encodePosition()) - no external reduce/
  // re-encode step needed.
  auto voice = std::make_unique<OscilatorVoice>(config, position, detune, start_phase, type_, level_, pulse_width_, send_a, send_b);
  voice->playNote(frequency, velocity, note_value);

  // don't pass velocity, position, or sends to children - a modulator
  // doesn't produce audible output of its own that should reach a bus.
  for (auto & child : getChildren()) {
    auto modulator = child->playNote(config, SphericalPosition{}, frequency, detune, 1.0, start_phase, note_value, 0.0f, 0.0f);
    if (modulator) voice->addChild(child->getInternalId(), move(modulator));
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
  else type_ = WaveformType::SINE;
  
  level_ = input.getFloat("level", 1.0f);
  pulse_width_ = input.getFloat("width", 0.5f);
}

void
Oscilator::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);
  
  output.set("type", to_string(type_));
  output.set("level", level_);
  output.set("width", pulse_width_);
}
