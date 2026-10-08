#include "Additive.h"

#include "AdditiveVoice.h"
#include "Tuning.h"
#include "../state/MemoryParameterSource.h"

using namespace std;

// A fresh instrument is the default preset, as if loaded with no attributes.
Additive::Additive() {
  MemoryParameterSource none;
  loadParameters(none);
}

unique_ptr<VoiceState>
Additive::playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const {
  auto voice = make_unique<AdditiveVoice>(config, position, detune, level_, keyboardSpread_, stringSpread_, thumpWidth_, sends, note_coord);
  voice->playNote(getFrequencyFor(tuning, note_value), velocity, note_value);

  AdditiveModelParams model;
  model.partials = partials_;
  model.stretch = stretch_;
  model.tuning_matched = tuningMatched_;
  model.unison_voices = unisonVoices_;
  model.unison_detune_cents = unisonDetune_;
  model.modes = parseModeRatios(modes_);
  model.excitation = excitation_ == "pluck" ? Excitation::Pluck : Excitation::Hammer;
  model.strike = strike_;
  model.hammer_cutoff_hz = hammerCutoff_;
  model.hammer_tracking = hammerTracking_;
  model.hammer_velocity = hammerVelocity_;
  model.pluck_cutoff_hz = pluckCutoff_;
  model.partial_floor_db = partialFloor_;
  model.decay_a = decayA_;
  model.decay_b = decayB_;
  model.decay_p = decayP_;
  model.decay_tracking = decayTracking_;
  model.decay_spread = decaySpread_;
  model.thump = thump_;
  model.body = body_;
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
  stretch_ = input.get<float>("stretch", preset.stretch);
  tuningMatched_ = input.get<bool>("tuningMatched", preset.tuningMatched);
  unisonVoices_ = input.get<int>("unisonVoices", preset.unisonVoices);
  unisonDetune_ = input.get<float>("unisonDetune", preset.unisonDetune);
  modes_ = input.get<std::string>("modes", std::string(preset.modes));
  excitation_ = input.get<std::string>("excitation", std::string(preset.pluck ? "pluck" : "hammer"));
  strike_ = input.get<float>("strike", preset.strike);
  hammerCutoff_ = input.get<float>("hammerCutoff", preset.hammerCutoff);
  hammerTracking_ = input.get<float>("hammerTracking", preset.hammerTracking);
  hammerVelocity_ = input.get<float>("hammerVelocity", preset.hammerVelocity);
  pluckCutoff_ = input.get<float>("pluckCutoff", preset.pluckCutoff);
  partialFloor_ = input.get<float>("partialFloor", preset.partialFloor);
  decayA_ = input.get<float>("decayA", preset.decayA);
  decayB_ = input.get<float>("decayB", preset.decayB);
  decayP_ = input.get<float>("decayP", preset.decayP);
  decayTracking_ = input.get<float>("decayTracking", preset.decayTracking);
  decaySpread_ = input.get<float>("decaySpread", preset.decaySpread);
  thump_ = input.get<float>("thump", preset.thump);
  thumpWidth_ = input.get<float>("thumpWidth", preset.thumpWidth);
  body_ = preset.body;
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
  output.set("stretch", stretch_, preset.stretch);
  output.set("tuningMatched", tuningMatched_, preset.tuningMatched);
  output.set("unisonVoices", unisonVoices_, preset.unisonVoices);
  output.set("unisonDetune", unisonDetune_, preset.unisonDetune);
  output.set("modes", modes_, std::string(preset.modes));
  output.set("excitation", excitation_, std::string(preset.pluck ? "pluck" : "hammer"));
  output.set("strike", strike_, preset.strike);
  output.set("hammerCutoff", hammerCutoff_, preset.hammerCutoff);
  output.set("hammerTracking", hammerTracking_, preset.hammerTracking);
  output.set("hammerVelocity", hammerVelocity_, preset.hammerVelocity);
  output.set("pluckCutoff", pluckCutoff_, preset.pluckCutoff);
  output.set("partialFloor", partialFloor_, preset.partialFloor);
  output.set("decayA", decayA_, preset.decayA);
  output.set("decayB", decayB_, preset.decayB);
  output.set("decayP", decayP_, preset.decayP);
  output.set("decayTracking", decayTracking_, preset.decayTracking);
  output.set("decaySpread", decaySpread_, preset.decaySpread);
  output.set("thump", thump_, preset.thump);
  output.set("thumpWidth", thumpWidth_, preset.thumpWidth);
  output.set("keyboardSpread", keyboardSpread_, preset.keyboardSpread);
  output.set("stringSpread", stringSpread_, preset.stringSpread);

  output.set("level", level_, 1.0f);
}
