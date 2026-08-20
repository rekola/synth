#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "../model/Track.h"
#include "SF2GeneratorTable.h"

#include <memory>
#include <unordered_map>

class InstrumentProvider;

class Instrument : public Track {
public:
  explicit Instrument() : Track(TrackType::INSTRUMENT) { }

  virtual void prepare(const InstrumentProvider & provider) { }

  // Returns a private copy of this instrument with `overrides` (SF2
  // generator id -> value, GenericInstrument's own generator_overrides_)
  // baked in, for GenericInstrument::prepare() to substitute in place of
  // the shared, provider-registered instance whenever a song actually
  // authors an override for this pool slot. Never mutates the shared
  // instance itself - two pool slots resolving to the same taxonomy path
  // share one C++ object (InstrumentProvider's registry), so writing an
  // override into it directly would leak across every slot using that
  // path, not just the one that authored the override.
  //
  // Overrides are load-time configuration (baked in once, when the clone
  // is made), not a per-note-event parameter - they don't change between
  // one playNote() call and the next the way frequency/velocity/position
  // do, so they're not threaded through playNote()'s own signature at all.
  // Default: nullptr, meaning "this backend doesn't support generator
  // overrides" - correct for every backend except SoundFontInstrument
  // today (Oscillator/Noise/LFO/FileInstrument all have nothing that reads
  // an SF2 generator id in the first place). A nullptr return tells the
  // caller to keep using the shared instance unchanged, silently ignoring
  // the override - the same "backend ignores what it doesn't handle"
  // contract the override element's own design already commits to.
  virtual std::unique_ptr<Instrument> cloneWithOverrides(const std::unordered_map<SF2Generator, float> & overrides) const {
    return nullptr;
  }

  void loadParameters(const ParameterSource & input) {
    Track::loadParameters(input);
  
    harmonic_ = input.getInt("harmonic", 1);
    subharmonic_ = input.getInt("subharmonic", 1);  
  }

  void storeParameters(ParameterSource & output) const {
    Track::storeParameters(output);
    
    if (harmonic_ != 1) output.set("harmonic", harmonic_);
    if (subharmonic_ != 1) output.set("subharmonic", subharmonic_);
  }

  int getHarmonic() const { return harmonic_; }
  int getSubharmonic() const { return subharmonic_; }

private:
  int harmonic_ = 1, subharmonic_ = 1;
};

#endif
