#ifndef _GROUPTRACK_H_
#define _GROUPTRACK_H_

#include "Track.h"

class GroupTrack : public Track {
 public:
  GroupTrack() : Track(GROUP) { }

  SampleData render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) override {
    if (getChildren().empty()) {
      return SampleData(1, frames);
    } else {
      auto it = getChildren().begin();
      auto sd = (*it)->render(frames, song_state, instruments, events);
      for ( ; it != getChildren().end(); it++) {
	auto sd2 = (*it)->render(frames, song_state, instruments, events);
	sd.mix(sd2);
      }
      return sd;
    }
  }  
};

#endif
