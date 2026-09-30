#include "Additive.h"

#include "AdditiveVoice.h"
#include "Tuning.h"
#include "../dsp/SpectralEnvelopeRemap.h"

using namespace std;

std::unique_ptr<VoiceState>
Additive::playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord, bool needs_decorrelation) const {
  detune *= getHarmonic();
  detune /= getSubharmonic();

  auto voice = std::make_unique<AdditiveVoice>(config, position, detune, level_, attackNoiseLevel_, sends, note_coord);
  voice->playNote(getFrequencyFor(tuning, note_value), velocity, note_value);

  int edo_steps = edoStepsFor(tuning);
  SpectralPostprocessKind postprocess_kind = envelopePostprocess_ == "residue" ? SpectralPostprocessKind::ResidueClassWeighting
    : envelopePostprocess_ == "stretch" ? SpectralPostprocessKind::StretchMix
    : SpectralPostprocessKind::None;
  voice->trigger(partials_, tilt_, velocityTilt_, inharmonicity_,
                 edo_steps, tuningMatched_, partialLimit_,
                 decayA_, decayB_, decayP_,
                 unisonVoices_, unisonDetune_,
                 envelopeAnchor_, envelopeTracking_,
                 postprocess_kind, envelopePostprocessN_, envelopePostprocessR_, envelopePostprocessAmount_,
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

  envelopeAnchor_ = input.get<float>("envelopeAnchor", 0.0f);
  envelopeTracking_ = input.get<float>("envelopeTracking", 0.0f);
  envelopePostprocess_ = input.get<std::string>("envelopePostprocess", std::string());
  envelopePostprocessN_ = input.get<int>("envelopePostprocessN", 0);
  envelopePostprocessR_ = input.get<int>("envelopePostprocessR", 0);
  envelopePostprocessAmount_ = input.get<float>("envelopePostprocessAmount", 0.0f);
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

  output.set("envelopeAnchor", envelopeAnchor_, 0.0f);
  output.set("envelopeTracking", envelopeTracking_, 0.0f);
  output.set("envelopePostprocess", envelopePostprocess_, std::string());
  output.set("envelopePostprocessN", envelopePostprocessN_, 0);
  output.set("envelopePostprocessR", envelopePostprocessR_, 0);
  output.set("envelopePostprocessAmount", envelopePostprocessAmount_, 0.0f);
}
