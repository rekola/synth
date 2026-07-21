#ifndef _GENERICINSTRUMENT_H_
#define _GENERICINSTRUMENT_H_

#include "Instrument.h"
#include "InstrumentProvider.h"
#include "SphericalPosition.h"

class GenericInstrument : public Instrument {
 public:
  GenericInstrument() { }

  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const override {
    detune *= getHarmonic();
    detune /= getSubharmonic();

    auto voice = concrete_instrument_->playNote(channel_config, position, frequency, detune, velocity, start_phase, note_value, send_a, send_b);

    // don't pass velocity, position, or sends to children - a modulator
    // doesn't produce audible output of its own that should reach a bus.
    for (auto & child : getChildren()) {
      auto modulator = child->playNote(channel_config, SphericalPosition{}, frequency, detune, 1.0, start_phase, note_value, 0.0f, 0.0f);
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
