#ifndef _TRACKEVENTQUEUE_H_
#define _TRACKEVENTQUEUE_H_

#include "TrackEvent.h"

#include <map>
#include <unordered_map>
#include <vector>

class TrackEventQueue {
 public:
  TrackEventQueue() { }

  void addPendingEvent(TrackEvent::Type type, int track_id, size_t frame, short id, float delay, float frequency, float velocity) {
    pending_events[track_id][frame].push_back(TrackEvent(type, id, delay, frequency, velocity));
  }
  
  std::map<unsigned int, std::vector<TrackEvent> > & getPendingEvents(int track_id) {
    return pending_events[track_id];
  }

  bool empty() const {
    for (auto & td : pending_events) {
      if (!td.second.empty()) return false;
    }
    return true;
  }

 private:
  std::unordered_map<int, std::map<unsigned int, std::vector<TrackEvent> > > pending_events;
};

#endif
