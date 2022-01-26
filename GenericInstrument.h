#ifndef _GENERICINSTRUMENT_H_
#define _GENERICINSTRUMENT_H_

#include "Instrument.h"
#include "InstrumentProvider.h"

class GenericInstrument : public Instrument {
 public:
  GenericInstrument() { }

  std::unique_ptr<TrackState> playNote(ChannelConfiguration channel_config, int outSampleRate, float azimuth, float frequency, float velocity, float start_phase) const override {
    return concrete_instrument->playNote(channel_config, outSampleRate, azimuth, frequency, velocity, start_phase);
  }

  std::string getElementName() const { return "genericInstrument"; }

  void prepare(const InstrumentProvider & provider) override {
    concrete_instrument = provider.getInstrumentByName(getName());
  }

 private:
  std::shared_ptr<Instrument> concrete_instrument;
};

#endif
