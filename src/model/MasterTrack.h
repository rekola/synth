#ifndef _MASTERTRACK_H_
#define _MASTERTRACK_H_

#include "Track.h"

// The tree parent of every top-level track (Song::master_track_) - never
// itself an XML element (see Song.h's own comment on why), so it needs no
// createTrack() factory registration despite having a real
// getElementName() (pure virtual on Track, so still needs an override).
// A plain effect-command column, no note column of its own - see
// SongStructure.cpp's TrackType::MASTER branch.
class MasterTrack : public Track {
 public:
  MasterTrack() : Track(TrackType::MASTER) { }
  const char * getElementName() const override { return "master"; }
};

#endif
