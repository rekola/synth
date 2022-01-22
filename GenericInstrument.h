#ifndef _GENERICINSTRUMENT_H_
#define _GENERICINSTRUMENT_H_

#include "Instrument.h"
#include "InstrumentProvider.h"
#include "tinyxml2.h"

class GenericInstrument : public Instrument {
 public:
  GenericInstrument() : Instrument(1) { }

  std::unique_ptr<TrackState> playNote(float frequency, float velocity, unsigned int outSampleRate, float start_phase) const override {
    return concrete_instrument->playNote(frequency, velocity, outSampleRate, start_phase);
  }

  tinyxml2::XMLElement * createXML(tinyxml2::XMLDocument & doc) const {
    auto element = doc.NewElement("genericInstrument");
    if (!getName().empty()) element->SetAttribute("name", getName().c_str());
    return element;
  }

  void prepare(const InstrumentProvider & provider) override {
    concrete_instrument = provider.getInstrumentByName(getName());
  }

 private:
  std::shared_ptr<Instrument> concrete_instrument;
};

#endif
