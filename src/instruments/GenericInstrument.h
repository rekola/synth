#ifndef _GENERICINSTRUMENT_H_
#define _GENERICINSTRUMENT_H_

#include "Instrument.h"
#include "InstrumentProvider.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

class GenericInstrument : public Instrument {
 public:
  GenericInstrument() { }

  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}) const override {
    detune *= getHarmonic();
    detune /= getSubharmonic();

    auto voice = concrete_instrument_->playNote(channel_config, position, frequency, detune, velocity, note_value, sends, note_coord);

    // don't pass velocity, position, or sends to children - a modulator
    // doesn't produce audible output of its own that should reach a bus
    // (see SendLevels.h's own doc comment for why SendLevels{} - not sends
    // - is the correct value here, not just an inert placeholder). note_coord
    // still forwards unchanged - see Oscillator::playNote()'s identical note.
    for (auto & child : getChildren()) {
      auto modulator = child->playNote(channel_config, SphericalPosition{}, frequency, detune, 1.0, note_value, SendLevels{}, note_coord);
      if (modulator) voice->addChild(child->getInternalId(), std::move(modulator));
    }

    return voice;
  }

  const char * getElementName() const override { return "genericInstrument"; }

  // Pure delegation, same shape as playNote()'s own forward above - this
  // node has no opinion of its own about extent, whatever it resolves to
  // (an SF2 preset, the built-in Oscillator, ...) does. concrete_instrument_
  // can be null before prepare() has run once.
  float getDefaultExtent() const override {
    return concrete_instrument_ ? concrete_instrument_->getDefaultExtent() : 0.0f;
  }

  void prepare(const InstrumentProvider & provider) override {
    concrete_instrument_ = provider.getInstrumentByName(getName());
  }

 private:
  std::shared_ptr<Instrument> concrete_instrument_;
};

#endif
