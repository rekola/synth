#ifndef _GENERICINSTRUMENT_H_
#define _GENERICINSTRUMENT_H_

#include "Instrument.h"
#include "InstrumentProvider.h"
#include "tinyxml2.h"

class GenericInstrument : public Instrument {
 public:
 GenericInstrument(std::string _name, const InstrumentProvider & provider)
   : Instrument(1, _name), concrete_instrument(provider.getInstrumentByName(_name))
    {
    }

 GenericInstrument(const InstrumentProvider & provider)
   : Instrument(1), concrete_instrument(provider.getInstrumentByName("Piano"))
    {
    }

  std::unique_ptr<InstrumentVoice> createVoice(unsigned int outSampleRate, int identifier) const override {
    return concrete_instrument->createVoice(outSampleRate, identifier);
  }

  void playNote(size_t column, float frequency, float velocity, float delay, float detune, VoicePool & voices) const override {
    concrete_instrument->playNote(column, frequency, velocity, delay, detune, voices);    
  }

  tinyxml2::XMLElement * createXML(tinyxml2::XMLDocument & doc) const {
    auto element = doc.NewElement("genericInstrument");
    if (!getName().empty()) element->SetAttribute("name", getName().c_str());
    return element;
  }

 private:
  std::shared_ptr<Instrument> concrete_instrument;
};

#endif
