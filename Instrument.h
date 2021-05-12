#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "Effect.h"
#include "Envelope.h"

#include <string>
#include <vector>
#include <memory>

class InstrumentVoice;

namespace tinyxml2 {
  class XMLDocument;
  class XMLElement;
};

class Instrument {
public:
  explicit Instrument(size_t _num_channels) : num_channels(_num_channels) { }
  explicit Instrument(size_t _num_channels, std::string _name) : num_channels(_num_channels), name(_name) { }
  virtual ~Instrument() { }

  virtual std::unique_ptr<InstrumentVoice> createVoice(unsigned int outSampleRate, int _identifier) const = 0;
  virtual tinyxml2::XMLElement * createXML(tinyxml2::XMLDocument & doc) const { return 0; }

  size_t getNumChannels() const { return num_channels; }
  
  void setName(const std::string & _name) { name = _name; }
  const std::string & getName() const { return name; }
  
  void setAmpEnvelope(const Envelope & _amp_envelope) { amp_envelope = _amp_envelope; }  
  const Envelope & getAmpEnvelope() const { return amp_envelope; }
  const Envelope & getModEnvelope() const { return mod_envelope; }
  
  void addEffect(std::unique_ptr<Effect> effect) { effects.push_back(std::move(effect)); }
  const std::vector<std::unique_ptr<Effect> > & getEffects() const { return effects; }
  
  float getGain() const { return gain; }

protected:
  size_t num_channels;
  std::string name;
  Envelope amp_envelope, mod_envelope;
  float gain = 1.0f;

  std::vector<std::unique_ptr<Effect> > effects;
};

#endif
