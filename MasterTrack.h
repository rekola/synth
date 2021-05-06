#ifndef _MASTERTRACK_H_
#define _MASTERTRACK_H_

#include "Track.h"

class Song;
class SongState;
class TrackEventQueue;

class MasterTrack : public Track {
 public:
  SampleData render(size_t frames, Song & song, SongState & state, TrackEventQueue & track_events);
};

#endif
