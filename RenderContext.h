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

  void addPendingEvent(int track_id, int frame, short id, float frequency, float velocity, int note_value = -1, const NoteCoordinate & note_coord = {}) {
    pending_events_[track_id][frame].push_back(TrackEvent(id, frequency, velocity, note_value, note_coord));
  }
  
  std::map<int, std::vector<TrackEvent> > & getPendingEvents(int track_id) {
    return pending_events_[track_id];
  }

  // 2Lxx/2Rxx azimuth slide ticks (SongState::scheduleAzimuthSlide()) - an
  // independent timeline from the note pending_events_ above, keyed the
  // same way (block-relative frame -> what happens there), but the
  // payload is a plain accumulated delta rather than a list of discrete
  // TrackEvents, since two ticks landing on the same frame (a very short
  // row split across constants::TICKS_PER_ROW steps) should just sum
  // rather than needing to be replayed individually.
  void addPendingAzimuthTick(int track_id, int frame, float delta) {
    pending_azimuth_ticks_[track_id][frame] += delta;
  }

  std::map<int, float> & getPendingAzimuthTicks(int track_id) {
    return pending_azimuth_ticks_[track_id];
  }

  bool empty() const {
    for (auto & td : pending_events_) {
      if (!td.second.empty()) return false;
    }
    for (auto & td : pending_azimuth_ticks_) {
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

    for (auto & [ track_id, ticks ] : pending_azimuth_ticks_) {
      auto old_ticks = ticks;
      ticks.clear();
      for (auto & [ frame, delta ] : old_ticks) {
	auto new_frame = frame + offset;
	if (new_frame >= 0) ticks[new_frame] += delta;
      }
    }
  }
  
  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

  void setBpm(float bpm) { bpm_ = bpm; }
  float getBpm() const { return bpm_; }
  
 private:
  ChannelConfiguration channel_config_;
  std::unordered_map<int, std::map<int, std::vector<TrackEvent> > > pending_events_;
  std::unordered_map<int, std::map<int, float> > pending_azimuth_ticks_;
  float bpm_ = 0.0f;
};

#endif
