#ifndef _RENDERCONTEXT_H_
#define _RENDERCONTEXT_H_

#include "TrackEvent.h"
#include "ChannelConfiguration.h"

#include <map>
#include <unordered_map>
#include <vector>

class RenderContext {
 public:
  RenderContext(ChannelConfiguration config) : channel_config_(config) { }

  void addPendingEvent(int track_id, int frame, short id, float frequency, float velocity) {
    pending_events_[track_id][frame].push_back(TrackEvent(id, frequency, velocity));
  }
  
  std::map<int, std::vector<TrackEvent> > & getPendingEvents(int track_id) {
    return pending_events_[track_id];
  }

  bool empty() const {
    for (auto & td : pending_events_) {
      if (!td.second.empty()) return false;
    }
    return true;
  }

  void updateFrameOffset(int offset) {
    for (auto & [ track_id, events ] : pending_events_) {
      auto old_events = events;
      events.clear();
      for (auto & [ frame, frame_events ] : old_events) {
	auto new_frame = frame + offset;
	if (new_frame >= 0) events[new_frame] = frame_events;
      }
    }
  }

  TrackState * getTrackState(int id) {
    auto it = track_states_.find(id);
    if (it != track_states_.end()) return it->second.get();
    return nullptr;    
  }
  
  void setTrackState(int id, std::unique_ptr<TrackState> state) {
    track_states_[id] = std::move(state);
  }

  const std::unordered_map<int, std::unique_ptr<TrackState> > & getTrackStates() const { return track_states_; }

  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

 private:
  ChannelConfiguration channel_config_;
  std::unordered_map<int, std::map<int, std::vector<TrackEvent> > > pending_events_;
  std::unordered_map<int, std::unique_ptr<TrackState> > track_states_;
};

#endif
