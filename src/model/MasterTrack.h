#ifndef _MASTERTRACK_H_
#define _MASTERTRACK_H_

#include "Track.h"

// The tree parent of every top-level track (Song's "master" node) - never
// itself an XML element (see Song.h's own comment on why), so no song file names
// it (the loader refuses a <master>); it has an element name only because
// its document node needs a type (tracknodes::makeTrack() builds it from one).
// A plain effect-command column, no note column of its own - see
// SongStructure.cpp's TrackType::MASTER branch.
class MasterTrack : public Track {
 public:
  MasterTrack() : Track(TrackType::MASTER) { }
  const char * getElementName() const override { return "master"; }
};

#endif
