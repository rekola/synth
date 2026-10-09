#ifndef _COMPILEDTRACKS_H_
#define _COMPILEDTRACKS_H_

#include "InstrumentPool.h"
#include "Track.h"
#include "../bus/BusEffectRegistry.h"

#include <memory>

// The track tree, instrument pool and send bus as playback and the UI read
// them: immutable objects built from the document's nodes (TrackCompiler).
// An edit makes a new one that shares every object whose nodes did not
// change, so a pointer to an untouched track stays good across edits.
struct CompiledTracks {
  std::shared_ptr<const Track> master;
  std::shared_ptr<const InstrumentPool> pool;
  struct BusSlot {
    BusEffectKind kind = BusEffectKind::None;
    // Only holds and (de)serializes the slot's parameters; never process()'d.
    std::shared_ptr<const BusEffect> effect;
  };
  BusSlot bus[2];
};

#endif
