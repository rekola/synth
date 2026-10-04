#include "Oscillator.h"

#include "OscillatorVoice.h"

using namespace std;

std::unique_ptr<VoiceState>
Oscillator::playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const {
  detune *= harmonic_;
  detune /= subharmonic_;

  // The voice encodes its own ambisonic output directly from its own
  // position (see InstrumentVoice::encodePosition()) - no external reduce/
  // re-encode step needed. Its own start phase is derived internally from
  // note_coord (InstrumentVoice's own constructor), not computed here.
  auto voice = std::make_unique<OscillatorVoice>(config, position, detune, type_, level_, pulse_width_, sends, note_coord, array_);
  voice->playNote(getFrequencyFor(tuning, note_value), velocity, note_value);

  return voice;
}

void
Oscillator::loadParameters(const ParameterSource & input) {
  Instrument::loadParameters(input);

  auto type_text = input.get<std::string>("type", "sine");
  if (type_text == "sine") type_ = WaveformType::SINE;
  else if (type_text == "saw") type_ = WaveformType::SAW;
  else if (type_text == "triangle") type_ = WaveformType::TRIANGLE;
  else if (type_text == "square") type_ = WaveformType::SQUARE;
  else type_ = WaveformType::SINE;

  harmonic_ = input.get<int>("harmonic", 1);
  subharmonic_ = input.get<int>("subharmonic", 1);
  level_ = input.get<float>("level", 1.0f);
  pulse_width_ = input.get<float>("width", 0.5f);

  array_.voices = input.get<int>("voices", 1);
  array_.ratio = input.get<float>("ratio", 1.0f);
  array_.falloff = input.get<float>("falloff", 1.0f);
  array_.detune_cents = input.get<float>("detune", 0.0f);
  array_.spread = input.get<float>("spread", 0.0f);
  array_.drift_period = input.get<float>("driftPeriod", 2.0f);
}

void
Oscillator::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);

  output.set("type", to_string(type_));
  if (harmonic_ != 1) output.set("harmonic", harmonic_);
  if (subharmonic_ != 1) output.set("subharmonic", subharmonic_);
  output.set("level", level_);
  output.set("width", pulse_width_);
  if (array_.voices != 1) {
    output.set("voices", array_.voices);
    if (array_.ratio != 1.0f) output.set("ratio", array_.ratio);
    if (array_.falloff != 1.0f) output.set("falloff", array_.falloff);
    if (array_.detune_cents != 0.0f) output.set("detune", array_.detune_cents);
    if (array_.spread != 0.0f) output.set("spread", array_.spread);
    if (array_.drift_period != 2.0f) output.set("driftPeriod", array_.drift_period);
  }
}
