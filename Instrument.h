#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "Effect.h"
#include "Filter.h"
#include "Note.h"
#include "InstrumentVoice.h"
#include "Envelope.h"

#include <string>
#include <vector>
#include <cmath>
#include <memory>

class Instrument {
public:
  explicit Instrument(size_t _num_channels) : num_channels(_num_channels) { }
  virtual ~Instrument() { }

  virtual std::shared_ptr<InstrumentVoice> createVoice(int _identifier) const = 0;

  size_t getNumChannels() const { return num_channels; }
  
  void setName(const std::string & _name) { name = _name; }
  const std::string & getName() const { return name; }
  
  int getTranspose() const { return transpose; }
  
  void setTranspose(int _transpose) { transpose = _transpose; }
      
  void setFilter(float fcut, float fres, bool is_highpass = false) {
    addEffect(std::make_unique<Filter>(fcut, fres, is_highpass));
  }

  void setEnvelope(const Envelope & _envelope) { envelope = _envelope; }  
  void setADSR(int _a, int _d, float _s, int _r) { setEnvelope(Envelope(_a / 255.0f, _d / 255.0f, _s, _r / 255.0f)); }
  const Envelope & getEnvelope() { return envelope; }
  
  void applyEffects(SampleData & data) {
    for (auto & effect : effects) {
      effect->apply(data);
    }
  }

  void addEffect(std::unique_ptr<Effect> effect) { effects.push_back(std::move(effect)); }
  
protected:
  size_t num_channels;
  std::string name;
  Envelope envelope;
  short transpose = 0;

  std::vector<std::unique_ptr<Effect> > effects;
};

#endif
