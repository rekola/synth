#ifndef _GENERICINSTRUMENT_H_
#define _GENERICINSTRUMENT_H_

#include "Instrument.h"
#include "InstrumentProvider.h"

class GenericInstrument : public Instrument {
 public:
  GenericInstrument() { }

  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, float azimuth, float frequency, float detune, float velocity, float start_phase) const override {
    detune *= getHarmonic();
    detune /= getSubharmonic();

    auto voice = concrete_instrument->playNote(channel_config, azimuth, frequency, detune, velocity, start_phase);

    // don't pass velocity or azimuth to children
    for (auto & child : getChildren()) {    
      auto modulator = child->playNote(channel_config, 0.0f, frequency, detune, 1.0, start_phase);
      if (modulator) voice->addChild(std::move(modulator));
    }
    
    return voice;
  }

  std::string getElementName() const { return "genericInstrument"; }

  void prepare(const InstrumentProvider & provider) override {
    concrete_instrument = provider.getInstrumentByName(getName());
  }

 private:
  std::shared_ptr<Instrument> concrete_instrument;
};

#endif
