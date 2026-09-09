#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "../model/Track.h"
#include "SF2GeneratorTable.h"

#include <memory>
#include <string>
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
  // today (Oscillator/Noise/LFO all have nothing that reads an SF2
  // generator id in the first place). A nullptr return tells the
  // caller to keep using the shared instance unchanged, silently ignoring
  // the override - the same "backend ignores what it doesn't handle"
  // contract the override element's own design already commits to.
  virtual std::unique_ptr<Instrument> cloneWithOverrides(const std::unordered_map<SF2Generator, float> & overrides) const {
    return nullptr;
  }

  void loadParameters(const ParameterSource & input) {
    Track::loadParameters(input);

    harmonic_ = input.get<int>("harmonic", 1);
    subharmonic_ = input.get<int>("subharmonic", 1);
    description_ = input.get<std::string>("description");
  }

  void storeParameters(ParameterSource & output) const {
    Track::storeParameters(output);

    if (harmonic_ != 1) output.set("harmonic", harmonic_);
    if (subharmonic_ != 1) output.set("subharmonic", subharmonic_);
    if (!description_.empty()) output.set("description", description_);
  }

  int getHarmonic() const { return harmonic_; }
  int getSubharmonic() const { return subharmonic_; }

  // A user-authored description for this pool slot, shown in the Details
  // panel the same way a Library instrument's own curated one is
  // (OutlineView.cpp's own buildDetailsLines()) - empty by default.
  // OutlineView only actually shows this when it's non-empty; a
  // GenericInstrument slot with nothing authored here instead falls back
  // to its resolved SoundFont/taxonomy entry's own description
  // (GmInstrumentDescriptions.h, keyed by getFrom()) - that fallback
  // lives in OutlineView, not here, since this class has no reason to
  // depend on the UI's own curated description table; a type with no
  // such fallback source (e.g. Oscillator) just stays undescribed until
  // one is authored here.
  const std::string & getDescription() const { return description_; }
  void setDescription(std::string description) { description_ = std::move(description); }

private:
  int harmonic_ = 1, subharmonic_ = 1;
  std::string description_;
};

#endif
