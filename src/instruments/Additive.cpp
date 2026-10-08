#include "Additive.h"

#include "AdditiveVoice.h"
#include "Tuning.h"

using namespace std;

unique_ptr<VoiceState>
Additive::playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const {
  auto voice = make_unique<AdditiveVoice>(config, position, detune, level_, attackNoiseLevel_, keyboardSpread_, stringSpread_, sends, note_coord);
  voice->playNote(getFrequencyFor(tuning, note_value), velocity, note_value);

  AdditiveModelParams model;
  model.partials = partials_;
  model.tilt_db = tilt_;
  model.velocity_tilt_db = velocityTilt_;
  model.stretch = stretch_;
  model.tuning_matched = tuningMatched_;
  model.unison_voices = unisonVoices_;
  model.unison_detune_cents = unisonDetune_;
  model.decay_a = decayA_;
  model.decay_b = decayB_;
  model.decay_p = decayP_;
  voice->trigger(model, edoStepsFor(tuning), note_coord);

  // No children: <additive> has no modulator input of its own.
  return voice;
}

void
Additive::loadParameters(const ParameterSource & input) {
  Instrument::loadParameters(input);

  preset_ = input.get<std::string>("preset", "default");
  const auto & preset = getAdditivePreset(preset_);

  partials_ = input.get<int>("partials", preset.partials);
  tilt_ = input.get<float>("tilt", preset.tilt);
  velocityTilt_ = input.get<float>("velocityTilt", preset.velocityTilt);
  unisonVoices_ = input.get<int>("unisonVoices", preset.unisonVoices);
  unisonDetune_ = input.get<float>("unisonDetune", preset.unisonDetune);
  stretch_ = input.get<float>("stretch", preset.stretch);
  decayA_ = input.get<float>("decayA", preset.decayA);
  decayB_ = input.get<float>("decayB", preset.decayB);
  decayP_ = input.get<float>("decayP", preset.decayP);
  tuningMatched_ = input.get<bool>("tuningMatched", preset.tuningMatched);
  attackNoiseLevel_ = input.get<float>("attackNoiseLevel", preset.attackNoiseLevel);
  keyboardSpread_ = input.get<float>("keyboardSpread", preset.keyboardSpread);
  stringSpread_ = input.get<float>("stringSpread", preset.stringSpread);

  level_ = input.get<float>("level", 1.0f);
}

void
Additive::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);

  output.set("preset", preset_, std::string("default"));
  const auto & preset = getAdditivePreset(preset_);

  output.set("partials", partials_, preset.partials);
  output.set("tilt", tilt_, preset.tilt);
  output.set("velocityTilt", velocityTilt_, preset.velocityTilt);
  output.set("unisonVoices", unisonVoices_, preset.unisonVoices);
  output.set("unisonDetune", unisonDetune_, preset.unisonDetune);
  output.set("stretch", stretch_, preset.stretch);
  output.set("decayA", decayA_, preset.decayA);
  output.set("decayB", decayB_, preset.decayB);
  output.set("decayP", decayP_, preset.decayP);
  output.set("tuningMatched", tuningMatched_, preset.tuningMatched);
  output.set("attackNoiseLevel", attackNoiseLevel_, preset.attackNoiseLevel);
  output.set("keyboardSpread", keyboardSpread_, preset.keyboardSpread);
  output.set("stringSpread", stringSpread_, preset.stringSpread);

  output.set("level", level_, 1.0f);
}
