#ifndef _INSTRUMENTPOOL_H_
#define _INSTRUMENTPOOL_H_

#include "Track.h"

#include <memory>
#include <vector>

class GenericInstrument;
class InstrumentProvider;

// A song's whole instrument pool ("the instrument list", the <instruments>
// element) - the flat, indexed list of pool entries (each an
// <instrument>/<oscillator>/... element, indexed by InstrumentTrack::
// instrument_id_ - see InstrumentTrack.h) plus the song's one default drum
// kit (the <instruments> element's own `from` attribute - see
// PercussionTrack.h, which sources its sound from this instead of a
// per-track index). Both live here, together, rather than
// threaded through the render path as two separately-passed pieces that
// only happen to come from the same place - this *is* that place. A future
// per-key override (swapping what plays at one percussion symbol, e.g.
// `BD`, without replacing the whole kit) belongs here too, for the same
// reason - not built yet.
//
// default_kit_ is a real GenericInstrument (same `from=` resolution -
// literal name first, then taxonomy path, same eventual fallback - every
// ordinary pool entry gets, not a hand-rolled duplicate of it), held via
// unique_ptr and only forward-declared here (GenericInstrument.h drags in
// the whole instruments/ backend - SoundFont.h, InstrumentProvider.h, ... -
// which model/ code otherwise never needs) - hence the destructor/move
// operations below are declared here but *defined* in InstrumentPool.cpp,
// the one place that includes GenericInstrument.h, per the usual
// unique_ptr-to-incomplete-type idiom. Copy is deleted, same as
// instruments_'s own unique_ptr vector makes copying this pool impossible
// already - InstrumentPool is move-only, matching Song's own.
class InstrumentPool {
 public:
  InstrumentPool();
  ~InstrumentPool();
  InstrumentPool(InstrumentPool &&) noexcept;
  InstrumentPool & operator=(InstrumentPool &&) noexcept;
  InstrumentPool(const InstrumentPool &) = delete;
  InstrumentPool & operator=(const InstrumentPool &) = delete;

  const std::vector<std::unique_ptr<Track> > & getInstruments() const { return instruments_; }
  const Track & getInstrument(int i) const { return *(instruments_[static_cast<size_t>(i)]); }
  void addInstrument(std::unique_ptr<Track> i) { instruments_.push_back(std::move(i)); }

  // Bounds-checked, nullptr-on-miss sibling of getInstrument() above - what
  // InstrumentTrackState::getInstrumentSource() resolves instrument_id_
  // through on every render()/live note-on call. An out-of-range index is
  // the ordinary "nothing authored for this track yet" case there, not a
  // caller bug the way getInstrument()'s own UI-facing callers (which
  // already range-check before calling) treat it.
  const Track * getByIndex(int i) const {
    return (i >= 0 && i < static_cast<int>(instruments_.size())) ? instruments_[static_cast<size_t>(i)].get() : nullptr;
  }

  // Reads the <instruments> element's own `from` attribute into a fresh
  // default_kit_ - the pool's one default drum kit path/native name (see
  // getDefaultKitInstrument() below). Mirrors GenericInstrument's own
  // loadParameters()/prepare() split: this stores the raw authored text
  // only; prepare() below does the actual resolve, once a provider is
  // available. Not SongObject-derived (no id/name/internal_id - nothing
  // ever references "the instrument pool" by id) so these aren't virtual
  // overrides, just this class's own small XML contract, the same shape
  // BusEffect/GenericInstrument's own is. Always called before prepare()
  // whenever a song is actually loaded (Song::open()) - see prepare()'s
  // own comment for why calling one without the other leaves
  // getDefaultKitInstrument() unsafe to use, the same load-then-prepare
  // contract every other pool entry already has.
  void loadParameters(const ParameterSource & input);
  void storeParameters(ParameterSource & output) const;

  // Resolves default_kit_'s `from` (defaulting it to "kit" first if
  // loadParameters() was never called or authored an empty one) via
  // `provider` - called once from Song::open(), right alongside where
  // every pool entry's own Instrument::prepare(provider) is already
  // called. "kit" is InstrumentProvider::resolvePath()'s own default-table
  // entry for that request, kit.standard, so a song that never mentions
  // this at all still has a working kit. "none" is the one explicit
  // opt-out - default_kit_ is left unprepared (its own concrete_instrument_
  // stays null) and getDefaultKitInstrument() below reports it as absent
  // rather than resolving it as if "none" were a literal (nonexistent)
  // taxonomy path.
  void prepare(const InstrumentProvider & provider);

  // The song's one drum kit (see prepare() above) - every percussion/
  // drum-machine note in the song plays through this one instrument, no
  // per-track/per-key choice yet. Null if the song authored from="none",
  // or if this pool was never loadParameters()/prepare()'d at all (a
  // freshly-constructed Song with no file behind it - the same "empty
  // until loaded" state its own getInstruments() starts in too).
  const Track * getDefaultKitInstrument() const;

 private:
  std::vector<std::unique_ptr<Track> > instruments_;
  std::unique_ptr<GenericInstrument> default_kit_;
};

#endif
