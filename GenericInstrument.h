#ifndef _GENERICINSTRUMENT_H_
#define _GENERICINSTRUMENT_H_

#include "Instrument.h"
#include "InstrumentProvider.h"
#include "SphericalPosition.h"

class GenericInstrument : public Instrument {
 public:
  GenericInstrument() { }

  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value) const override {
    detune *= getHarmonic();
    detune /= getSubharmonic();

    auto voice = concrete_instrument_->playNote(channel_config, position, frequency, detune, velocity, start_phase, note_value);

    // don't pass velocity or position to children
    for (auto & child : getChildren()) {
      auto modulator = child->playNote(channel_config, SphericalPosition{}, frequency, detune, 1.0, start_phase, note_value);
      if (modulator) voice->addChild(child->getInternalId(), std::move(modulator));
    }

    return voice;
  }

  const char * getElementName() const override { return "genericInstrument"; }

  void prepare(const InstrumentProvider & provider) override {
    concrete_instrument_ = provider.getInstrumentByName(getName());
  }

 private:
  std::shared_ptr<Instrument> concrete_instrument_;
};

#endif
