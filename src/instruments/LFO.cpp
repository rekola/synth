#include "LFO.h"

#include "OscillatorVoice.h"

using namespace std;

std::unique_ptr<VoiceState>
LFO::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord, bool needs_decorrelation) const {
  // LFO constructs its own OscillatorVoice directly (it's not itself a
  // modulator, an FM carrier's LFO target is) - the voice encodes its own
  // ambisonic output directly (InstrumentVoice::encodePosition()); since a
  // modulator's own position is always SphericalPosition{} (see
  // Oscillator.cpp), this just spreads unity gain into W, same as before.
  // Its own start phase is derived internally from note_coord
  // (InstrumentVoice's own constructor), not computed here.
  auto voice = std::make_unique<OscillatorVoice>(config, position, detune, WaveformType::SINE, level_, 0.5f, sends, note_coord);
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
