#include "Additive.h"

#include "AdditiveVoice.h"
#include "Tuning.h"

using namespace std;

std::unique_ptr<VoiceState>
Additive::playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord, bool needs_decorrelation) const {
  detune *= getHarmonic();
  detune /= getSubharmonic();

  auto voice = std::make_unique<AdditiveVoice>(config, position, detune, level_, attackNoiseLevel_, sends, note_coord);
  voice->playNote(getFrequencyFor(tuning, note_value), velocity, note_value);

  int edo_steps = edoStepsFor(tuning);
  voice->trigger(partials_, tilt_, velocityTilt_, inharmonicity_,
                 edo_steps, tuningMatched_, partialLimit_,
                 decayA_, decayB_, decayP_,
                 unisonVoices_, unisonDetune_,
                 note_coord);

  // No children loop, unlike Oscillator - <additive> has no established
  // modulator-input concept of its own (nothing here reads a child's
  // output the way OscillatorVoice reads a phase-modulator's); a future
  // modulation target would need its own design, not a copy of
  // Oscillator's.
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
  inharmonicity_ = input.get<float>("inharmonicity", preset.inharmonicity);
  decayA_ = input.get<float>("decayA", preset.decayA);
  decayB_ = input.get<float>("decayB", preset.decayB);
  decayP_ = input.get<float>("decayP", preset.decayP);
  tuningMatched_ = input.get<bool>("tuningMatched", preset.tuningMatched);
  partialLimit_ = input.get<int>("partialLimit", preset.partialLimit);
  attackNoiseLevel_ = input.get<float>("attackNoiseLevel", preset.attackNoiseLevel);

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
  output.set("inharmonicity", inharmonicity_, preset.inharmonicity);
  output.set("decayA", decayA_, preset.decayA);
  output.set("decayB", decayB_, preset.decayB);
  output.set("decayP", decayP_, preset.decayP);
  output.set("tuningMatched", tuningMatched_, preset.tuningMatched);
  output.set("partialLimit", partialLimit_, preset.partialLimit);
  output.set("attackNoiseLevel", attackNoiseLevel_, preset.attackNoiseLevel);

  output.set("level", level_, 1.0f);
}
