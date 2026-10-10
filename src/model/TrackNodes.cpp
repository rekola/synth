#include "TrackNodes.h"

#include "Group.h"
#include "InstrumentTrack.h"
#include "MasterTrack.h"
#include "PercussionTrack.h"
#include "SampleTrack.h"
#include "../instruments/Additive.h"
#include "../instruments/FM.h"
#include "../instruments/GenericInstrument.h"
#include "../instruments/Noise.h"
#include "../instruments/Oscillator.h"
#include "../instruments/PadSynth.h"
#include "../effects/Amplifier.h"
#include "../effects/BiquadFilter.h"
#include "../effects/Chorus.h"
#include "../effects/Compressor.h"
#include "../effects/Distortion.h"
#include "../effects/EnvelopeFilter.h"
#include "../effects/Phaser.h"
#include "../effects/ResonantFilter.h"
#include "../effects/TapeDegradation.h"
#include "../effects/Tremolo.h"

#include <fmt/core.h>

#include <cstdlib>

namespace tracknodes {

std::unique_ptr<Track> makeTrack(std::string_view name) {
  if (name == "master") return std::make_unique<MasterTrack>();
  if (name == "track") return std::make_unique<InstrumentTrack>();
  if (name == "percussionTrack") return std::make_unique<PercussionTrack>();
  if (name == "sampleTrack") return std::make_unique<SampleTrack>();
  if (name == "group") return std::make_unique<Group>();

  // effects
  if (name == "distortion") return std::make_unique<Distortion>();
  if (name == "resonantFilter") return std::make_unique<ResonantFilter>();
  if (name == "biquadFilter") return std::make_unique<BiquadFilter>();
  if (name == "chorus") return std::make_unique<Chorus>();
  if (name == "phaser") return std::make_unique<Phaser>();
  if (name == "tremolo") return std::make_unique<Tremolo>();
  if (name == "envelope") return std::make_unique<EnvelopeFilter>();
  if (name == "amplifier") return std::make_unique<Amplifier>();
  if (name == "compressor") return std::make_unique<Compressor>();
  if (name == "tapeDegradation") return std::make_unique<TapeDegradation>();

  // instruments
  if (name == "instrument") return std::make_unique<GenericInstrument>();
  if (name == "oscillator") return std::make_unique<Oscillator>(WaveformType::SAW);
  if (name == "padsynth") return std::make_unique<PadSynth>();
  if (name == "noise") return std::make_unique<Noise>();
  if (name == "fm") return std::make_unique<FM>();
  if (name == "additive") return std::make_unique<Additive>();

  return nullptr;
}

static std::string floatText(float value) { return fmt::format("{}", value); }

void ParamBag::set(const std::string & name, float value) { values_[name] = floatText(value); }

int ParamBag::getIntImpl(const std::string & name, int default_value) const {
  auto it = values_.find(name);
  return it != values_.end() ? std::atoi(it->second.c_str()) : default_value;
}

float ParamBag::getFloatImpl(const std::string & name, float default_value) const {
  auto it = values_.find(name);
  return it != values_.end() ? std::strtof(it->second.c_str(), nullptr) : default_value;
}

std::string ParamBag::getTextImpl(const std::string & name, const std::string & default_value) const {
  auto it = values_.find(name);
  return it != values_.end() ? it->second : default_value;
}

const std::string * NodeParameterSource::find(const std::string & name) const {
  auto node = document_.get(node_);
  auto value = node ? node->find(name) : nullptr;
  return value ? std::get_if<std::string>(value) : nullptr;
}

void NodeParameterSource::write(const std::string & name, const std::string & value) {
  if (writable_) writable_->setProperty(node_, name, doc::Value(value));
}

void NodeParameterSource::set(const std::string & name, float value) { write(name, floatText(value)); }

int NodeParameterSource::getIntImpl(const std::string & name, int default_value) const {
  auto text = find(name);
  return text ? std::atoi(text->c_str()) : default_value;
}

float NodeParameterSource::getFloatImpl(const std::string & name, float default_value) const {
  auto text = find(name);
  return text ? std::strtof(text->c_str(), nullptr) : default_value;
}

std::string NodeParameterSource::getTextImpl(const std::string & name, const std::string & default_value) const {
  auto text = find(name);
  return text ? *text : default_value;
}

ParamBag nodeAttributes(const doc::Document & document, doc::NodeId id) {
  ParamBag bag;
  auto node = document.get(id);
  if (!node) return bag;
  for (auto & [ key, value ] : node->properties) {
    if (key == kIidKey) continue;
    if (auto text = std::get_if<std::string>(&value)) bag.set(key, *text);
  }
  return bag;
}

doc::NodeId trackToNode(doc::Document & document, const Track & track) {
  auto id = document.create(track.getElementName());
  ParamBag bag;
  track.storeParameters(bag);
  for (auto & [ key, value ] : bag.values()) document.setProperty(id, key, doc::Value(value));
  document.setProperty(id, kIidKey, doc::Value(static_cast<int64_t>(track.getInternalId())));

  if (auto * generic = dynamic_cast<const GenericInstrument *>(&track)) {
    auto add = [&](const std::string & name, float value) {
      auto generator = document.create(kGeneratorType);
      document.setProperty(generator, "name", doc::Value(name));
      document.setProperty(generator, "value", doc::Value(floatText(value)));
      document.appendToDetached(id, kChildrenSlot, generator);
    };
    for (auto & [ generator_id, value ] : generic->getGeneratorOverrides()) {
      if (auto name = sf2GeneratorNameForId(generator_id)) add(name, value);
    }
    for (auto & [ name, value ] : generic->getUnknownGeneratorOverrides()) add(name, value);
  }

  for (auto & child : track.getChildren()) document.appendToDetached(id, kChildrenSlot, trackToNode(document, *child));
  return id;
}

}  // namespace tracknodes
