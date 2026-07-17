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

  void addPendingEvent(int track_id, int frame, short id, float frequency, float velocity, int note_value = -1) {
    pending_events_[track_id][frame].push_back(TrackEvent(id, frequency, velocity, note_value));
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
  
  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

  void setBpm(float bpm) { bpm_ = bpm; }
  float getBpm() const { return bpm_; }
  
 private:
  ChannelConfiguration channel_config_;
  std::unordered_map<int, std::map<int, std::vector<TrackEvent> > > pending_events_;
  float bpm_ = 0.0f;
};

#endif
