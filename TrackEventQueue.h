#ifndef _TRACKEVENTQUEUE_H_
#define _TRACKEVENTQUEUE_H_

#include "TrackEvent.h"

#include <map>
#include <unordered_map>
#include <vector>

class TrackEventQueue {
 public:
  TrackEventQueue() { }

  void addPendingEvent(int track_id, size_t frame, short id, float frequency, float velocity) {
    pending_events[track_id][frame].push_back(TrackEvent(id, frequency, velocity));
  }
  
  std::map<int, std::vector<TrackEvent> > & getPendingEvents(int track_id) {
    return pending_events[track_id];
  }

  bool empty() const {
    for (auto & td : pending_events) {
      if (!td.second.empty()) return false;
    }
    return true;
  }

  void updateFrameOffset(int offset) {
    for (auto & [ track_id, events ] : pending_events) {
      auto old_events = events;
      events.clear();
      for (auto & [ frame, frame_events ] : old_events) {
	auto new_frame = frame + offset;
	if (new_frame >= 0) events[new_frame] = frame_events;
      }
    }
  }

 private:
  std::unordered_map<int, std::map<int, std::vector<TrackEvent> > > pending_events;
};

#endif
