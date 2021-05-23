#ifndef _GROUPTRACK_H_
#define _GROUPTRACK_H_

#include "Track.h"

class GroupTrack : public Track {
 public:
  GroupTrack() : Track(GROUP) { }

  SampleData render(size_t frames, TrackState & state, Instrument & instrument, std::map<unsigned int, std::vector<TrackEvent> > & pending_events) override {
    if (getChildren().empty()) {
      return SampleData(1, frames);
    } else {
      auto it = getChildren().begin();
      auto sd = it->render(frames, state, instrument, pending_events);
      for ( ; it != getChildren().end(); it++) {
	auto sd2 = it->render(frames, state, instrument, pending_events);
	sd.mix(sd2);
      }
      return sd;
    }
  }  
};
#endif
