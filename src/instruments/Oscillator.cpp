#include "Oscillator.h"

#include "OscillatorVoice.h"

using namespace std;

std::unique_ptr<VoiceState>
Oscillator::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const {
  detune *= getHarmonic();
  detune /= getSubharmonic();

  // The voice encodes its own ambisonic output directly from its own
  // position (see InstrumentVoice::encodePosition()) - no external reduce/
  // re-encode step needed. Its own start phase is derived internally from
  // note_coord (InstrumentVoice's own constructor), not computed here.
  auto voice = std::make_unique<OscillatorVoice>(config, position, detune, type_, level_, pulse_width_, sends, note_coord);
  voice->playNote(frequency, velocity, note_value);

  // don't pass velocity, position, or sends to children - a modulator
  // doesn't produce audible output of its own that should reach a bus (see
  // SendLevels.h's own doc comment for why SendLevels{} - not sends - is
  // the correct value here, not just an inert placeholder). note_coord
  // does still forward unchanged, though - it identifies the note, not the
  // audio path, so a modulator child composing its own jitter from it
  // still wants the same coordinate this oscillator itself got.
  for (auto & child : getChildren()) {
    auto modulator = child->playNote(config, SphericalPosition{}, frequency, detune, 1.0, note_value, SendLevels{}, note_coord);
    if (modulator) voice->addChild(child->getInternalId(), move(modulator));
  }

  return voice;
}

void
Oscillator::loadParameters(const ParameterSource & input) {
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
Oscillator::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);

  output.set("type", to_string(type_));
  output.set("level", level_);
  output.set("width", pulse_width_);
}
