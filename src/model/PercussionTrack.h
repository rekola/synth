#ifndef _PERCUSSIONTRACK_H_
#define _PERCUSSIONTRACK_H_

#include "LeafTrack.h"

#include <string>
#include <unordered_map>
#include <vector>

class Song;
class Pattern;

// Plays through the pool's one drum kit (InstrumentPool::
// getDefaultKitInstrument(), via PercussionTrack.cpp's own local
// PercussionTrackState), not a per-track instrument-pool pick - no
// instrument_id_ of its own.
//
// A percussion "note" is just which drum (a GM number), never a harmony, so
// the track takes note columns for simultaneous hits like any other
// LeafTrack. A step is an ordinary Note in an ordinary Pattern; there is no
// per-track drum list - the Launchpad's fixed 4x4 kit
// (LaunchpadLayout::drumPadNoteForPad()) is the one playing surface.
class PercussionTrack : public LeafTrack {
 public:
  PercussionTrack() : LeafTrack(TrackType::PERCUSSION_CONTROL) { }

  const char * getElementName() const override { return "percussionTrack"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;
};

#endif
