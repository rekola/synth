#ifndef _PERCUSSIONTRACK_H_
#define _PERCUSSIONTRACK_H_

#include "LeafTrack.h"

// Plays through the pool's one drum kit (InstrumentPool::
// getDefaultKitInstrument(), via PercussionTrack.cpp's own local
// InstrumentTrackState subclass), not a per-track instrument-pool pick.
// No instrument_id_ of its own.
class PercussionTrack : public LeafTrack {
 public:
  PercussionTrack() : LeafTrack(TrackType::PERCUSSION_CONTROL) { }

  const char * getElementName() const override { return "percussionTrack"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;

 private:
};

#endif
