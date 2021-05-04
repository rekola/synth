#ifndef _TRACKEVENTQUEUE_H_
#define _TRACKEVENTQUEUE_H_

#include "TrackEvent.h"

#include <map>
#include <unordered_map>
#include <vector>

class TrackEventQueue {
 public:
  TrackEventQueue() { }

  void addPendingEvent(unsigned short track, size_t frame, short id, float delay, float frequency, float velocity) {
    pending_events[track][frame].push_back(TrackEvent(id, delay, frequency, velocity));
  }
  
  std::map<unsigned int, std::vector<TrackEvent> > & getPendingEvents(unsigned short track) { return pending_events[track]; }

  bool empty() const {
    for (auto & td : pending_events) {
      if (!td.second.empty()) return false;
    }
    return true;
  }

 private:
  std::unordered_map<unsigned short, std::map<unsigned int, std::vector<TrackEvent> > > pending_events;
};

#endif
