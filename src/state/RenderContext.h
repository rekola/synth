#ifndef _RENDERCONTEXT_H_
#define _RENDERCONTEXT_H_

#include "../model/Command.h"
#include "../model/NoteCoordinate.h"
#include "../ambisonic/ChannelConfiguration.h"

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

class Clip;

// Both TrackEvent and SampleTrackEvent below are queued entries on a
// RenderContext timeline: something a track's own chunked render() must
// apply at an exact within-block frame, rather than something the sound
// producer itself (a Voice, a SampleClipVoice) decides on its own -
// InstrumentTrackState::render() and SampleTrackState::render() each split
// their block at every entry due within it, apply it exactly there, then
// keep rendering, so a start or a stop always lands on the sample it was
// actually due on regardless of where in the block that falls. A sound
// producer should never need to know why or when it's being started or
// stopped - that scheduling problem belongs entirely up here.

// A pattern note's own on/off, resolved (frequency/velocity/note_value)
// by the caller before queuing - see NoteCoordinate.h for why this stays
// separate from the coordinate itself.
class TrackEvent {
 public:
  TrackEvent(short _id, float _frequency, float _velocity, int _note_value = -1, const NoteCoordinate & _note_coord = {})
    : id(_id), frequency(_frequency), velocity(_velocity), note_value(_note_value), note_coord(_note_coord) { }

  short getId() const { return id; }

  bool isAftertouch() const { return frequency == 0.0f && velocity > 0.0f; }
  bool isOff() const { return velocity == 0.0f; }

  float getFrequency() const { return frequency; }
  float getVelocity() const { return velocity; }
  int getNoteValue() const { return note_value; }
  const NoteCoordinate & getNoteCoordinate() const { return note_coord; }

 private:
  short id;
  float frequency, velocity;
  int note_value;
  NoteCoordinate note_coord;
};

// A SampleTrack clip start or stop, due at a specific within-block frame -
// pending_events_'s own sibling timeline for raw sample playback, not
// reused from TrackEvent itself since none of its fields (frequency/
// velocity/note_value) are meaningful for raw sample playback - this
// needs a Clip reference instead, which TrackEvent has no room for.
// `clip` is a raw, non-owning pointer, only meaningful for START - safe
// for exactly as long as one SongState::renderBlock() call, the same
// lifetime a TrackEvent's own NoteCoordinate already assumes of whatever
// Song it was built against. A START carries no duration/cap of its own -
// the voice it starts just plays itself; whatever eventually ends it (a
// later lap's own START superseding it, an explicit STOP, the scene
// boundary) is a separate, later entry on this same timeline, exactly
// the way a note's own note-off is a separate pending_events_ entry
// rather than something baked into its note-on. A STOP is always a
// release (SampleTrackState::render()'s own consumer calls stopVoices(),
// the same short natural fade a superseding START already triggers via
// triggerClip()'s own stopVoices() call), never a hard cut - an abrupt
// stop would click.
struct SampleTrackEvent {
  enum Kind { START, STOP };
  Kind kind;
  const Clip * clip = nullptr;
};

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

  // A SampleTrack clip's own trigger (SongState.h's per-row scheduling -
  // a fresh instance becoming active, or a looping clip's own later lap)
  // due at a specific within-block frame - queued here rather than
  // calling SampleTrackState::triggerClip() directly, so a sound producer
  // never has to know how to start mid-block itself: SampleTrackState::
  // render()'s own chunked loop (mirroring InstrumentTrackState's exactly,
  // just against this timeline instead of pending_events_) is what
  // actually calls it, precisely at this frame, the same way a pattern
  // note's own chunked render already does.
  void addPendingSampleStart(int track_id, int frame, const Clip * clip) {
    pending_sample_events_[track_id][frame] = SampleTrackEvent{SampleTrackEvent::START, clip};
  }

  // The stop side of the same timeline (SampleTrackEvent's own comment
  // has the full reasoning on why it's always a release, never a hard
  // cut) - a one-shot clip's own real audio outlasting the scene it's
  // placed in, an explicit stop instance, eventually pause/seek. The
  // voice itself never decides when it ends for any of these reasons -
  // only for running out of its own audio - so each needs to land here
  // instead, at the exact frame it's actually due, not be approximated
  // by a caller.
  void addPendingSampleStop(int track_id, int frame) {
    pending_sample_events_[track_id][frame] = SampleTrackEvent{SampleTrackEvent::STOP};
  }

  // A plain map, not map-of-vector like pending_events_ - a SampleTrack
  // only ever has one clip playing at a time, so at most one of these
  // (a start or a stop) can ever be due on the same frame, unlike a
  // chord's several simultaneous note columns.
  std::map<int, SampleTrackEvent> & getPendingSampleEvents(int track_id) {
    return pending_sample_events_[track_id];
  }

  bool empty() const {
    for (auto & td : pending_events_) {
      if (!td.second.empty()) return false;
    }
    for (auto & td : pending_azimuth_ticks_) {
      if (!td.second.empty()) return false;
    }
    for (auto & td : pending_sample_events_) {
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

    for (auto & [ track_id, events ] : pending_sample_events_) {
      auto old_events = events;
      events.clear();
      for (auto & [ frame, event ] : old_events) {
	auto new_frame = frame + offset;
	if (new_frame >= 0) events[new_frame] = event;
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
  std::unordered_map<int, std::map<int, SampleTrackEvent> > pending_sample_events_;
  float bpm_ = 0.0f;
};

#endif
