#ifndef _RENDERCONTEXT_H_
#define _RENDERCONTEXT_H_

#include "../ambisonic/ChannelConfiguration.h"
#include "../model/Command.h"
#include "../model/NoteCoordinate.h"
#include "../model/SampleContent.h"
#include "../model/Tuning.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

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

// What a single note takes in place of its track's: an azimuth and/or an
// ambisonic extent (the note's own fx Pxx / Wxx), and a pitch correction in
// cents (a +hh / -hh fx).
struct NoteOverride {
  bool has_azimuth = false, has_extent = false, has_detune = false;
  // 0Gxx for this note's column: glide the column's sounding note here, at
  // `glide_cents_per_frame` for the `glide_frames` left of the row (instantly
  // when `glide_instant`).
  bool has_glide = false;
  float glide_cents_per_frame = 0;
  int glide_frames = 0;
  bool glide_instant = false;
  float azimuth = 0, extent = 0, detune_cents = 0;
};

// A pattern note's own on/off, resolved (tuning/velocity/note_value)
// by the caller before queuing - see NoteCoordinate.h for why this stays
// separate from the coordinate itself.
//
// What an event is (a note-on, an OFF, aftertouch) is its kind, never read
// from the velocity: a note-on at velocity 0 is a real, quiet note. The velocity
// is a level in [0,1]; an OFF's is its release velocity (the lift).
class TrackEvent {
 public:
  enum class Kind { NOTE_ON,
                    OFF,
                    AFTERTOUCH };

  TrackEvent(short _id, Tuning _tuning, float _velocity, int _note_value = -1, const NoteCoordinate & _note_coord = {}, const NoteOverride & _note_override = {}, Kind _kind = Kind::NOTE_ON)
      : id(_id), tuning(_tuning), velocity(_velocity), note_value(_note_value), note_coord(_note_coord), note_override(_note_override), kind(_kind) {}

  // A clip ending or giving way: every voice of the track released (no
  // column's id is negative).
  static constexpr short kStopAll = -1;

  short getId() const { return id; }

  bool isStopAll() const { return id == kStopAll; }
  bool isAftertouch() const { return kind == Kind::AFTERTOUCH; }
  bool isOff() const { return kind == Kind::OFF; }

  Tuning getTuning() const { return tuning; }
  float getVelocity() const { return velocity; }
  int getNoteValue() const { return note_value; }
  const NoteCoordinate & getNoteCoordinate() const { return note_coord; }
  const NoteOverride & getNoteOverride() const { return note_override; }

 private:
  short id;
  Tuning tuning;
  float velocity;
  int note_value;
  NoteCoordinate note_coord;
  NoteOverride note_override;
  Kind kind;
};

// A SampleTrack content start or stop, due at a specific within-block
// frame - pending_events_'s own sibling timeline for raw sample playback,
// not reused from TrackEvent itself since none of its fields (frequency/
// velocity/note_value) are meaningful for raw sample playback - this
// needs a SampleContent reference instead, which TrackEvent has no room
// for. `content` is a raw, non-owning pointer, only meaningful for START -
// safe for exactly as long as one SongState::renderBlock() call, the same
// lifetime a TrackEvent's own NoteCoordinate already assumes of whatever
// Song it was built against; points at either a real placed clip's own
// SampleContent or a SampleTrack's own background bed (Arrangement::
// getSampleBackgroundContent()). `is_background` tells
// SampleTrackState::render() which of a SampleTrack's exactly two fixed
// voice roles this event is for - the one clip that can be playing, or
// the track's own always-on background bed, which mixes with it
// rather than being masked by it (SongState.h's own comment on why) - not
// a generalized "which of N slots" index; a SampleTrack never has more
// than these two. A START carries no duration/cap of its own - the voice
// it starts just plays itself; whatever eventually ends it (a later lap's
// own START superseding it, an explicit STOP) is a
// separate, later entry on this same timeline, exactly the way a note's
// own note-off is a separate pending_events_ entry rather than something
// baked into its note-on. A STOP is always a release (SampleTrackState::
// render()'s own consumer calls stopVoices(), the same short natural
// fade a superseding START already triggers), never a hard cut - an
// abrupt stop would click. `start_offset_frames`, only meaningful for
// START, is how many frames into the content's own (post-trim, post-
// resample) audio to begin from instead of its own beginning - 0 for an
// ordinary fresh trigger or a new lap's own retrigger (both always land
// exactly on the instance's/lap's own leading row), nonzero only when the
// playhead itself lands mid-instance with nothing already sounding to
// explain why (SongState.h's own "just resumed playback" case - see its
// comment).
struct SampleTrackEvent {
  enum Kind { START, STOP };
  Kind kind;
  // Its own copy (sharing the audio): the event can outlive the block it was
  // queued in, and the content it was made from belongs to a snapshot the
  // UI thread may have retired by then.
  std::shared_ptr<const SampleContent> content;
  int start_offset_frames = 0;
  bool is_background = false;
};

class RenderContext {
 public:
  RenderContext(ChannelConfiguration config) : channel_config_(config) { }

  void addPendingEvent(int track_id, int frame, short id, Tuning tuning, float velocity, int note_value = -1, const NoteCoordinate & note_coord = {}, const NoteOverride & note_override = {}) {
    pending_events_[track_id][frame].push_back(TrackEvent(id, tuning, velocity, note_value, note_coord, note_override));
  }

  // The release of column `id`'s voices at `frame`; `lift` is the OFF's release
  // velocity as a level in [0,1].
  void addPendingOff(int track_id, int frame, short id, Tuning tuning, float lift) {
    pending_events_[track_id][frame].push_back(TrackEvent(id, tuning, lift, -1, {}, {}, TrackEvent::Kind::OFF));
  }
  void addPendingAftertouch(int track_id, int frame, short id, Tuning tuning, float pressure) {
    pending_events_[track_id][frame].push_back(TrackEvent(id, tuning, pressure, -1, {}, {}, TrackEvent::Kind::AFTERTOUCH));
  }

  // Every voice of `track_id` released at `frame` - the transition's own
  // sample, like a note-off, not wherever its render block starts. The
  // tuning argument is never read for a stop-all event (isStopAll() short-
  // circuits straight to stopAllVoices() before anything looks at it) -
  // any valid value works; EDO31 is just this engine's own default.
  void addPendingStopAll(int track_id, int frame) {
    pending_events_[track_id][frame].push_back(TrackEvent(TrackEvent::kStopAll, Tuning::EDO31, 0.0f));
  }

  std::map<int, std::vector<TrackEvent> > & getPendingEvents(int track_id) {
    return pending_events_[track_id];
  }

  // YLxx/YRxx azimuth slide ticks (SongState::scheduleAzimuthSlide()) - an
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

  // 0Uxx/0Dxx: from `frame`, raise every sounding voice of the track by
  // `cents` in all, linearly over `frames` frames.
  struct PitchSlide {
    float cents;
    int frames;
  };
  void addPendingPitchSlide(int track_id, int frame, float cents, int frames) {
    pending_pitch_slides_[track_id][frame].push_back({cents, frames});
  }
  std::map<int, std::vector<PitchSlide> > & getPendingPitchSlides(int track_id) {
    return pending_pitch_slides_[track_id];
  }

  // 0Gxx: from `frame`, the sounding notes of `column` (-1: every column)
  // that have a glide target move toward it by `cents_per_frame` for
  // `frames` frames (instantly when `instant`).
  struct PitchGlide {
    int column;
    float cents_per_frame;
    int frames;
    bool instant;
  };
  void addPendingPitchGlide(int track_id, int frame, int column, float cents_per_frame, int frames, bool instant) {
    pending_pitch_glides_[track_id][frame].push_back({column, cents_per_frame, frames, instant});
  }
  std::map<int, std::vector<PitchGlide> > & getPendingPitchGlides(int track_id) {
    return pending_pitch_glides_[track_id];
  }

  // The transport stopped or jumped: slides not yet begun are dropped, and
  // the epoch tells each track to end the ones running (it freezes them
  // where they are).
  void noteTransportJump() {
    pending_pitch_slides_.clear();
    pending_pitch_glides_.clear();
    transport_epoch_++;
    if (keep_global_pitch_)
      keep_global_pitch_ = false;
    else
      freezeGlobalPitch();
  }
  // The next noteTransportJump() is part of the song's flow (a pattern
  // break): the song's pitch slides run on through it.
  void keepGlobalPitchThroughJump() { keep_global_pitch_ = true; }
  int getTransportEpoch() const { return transport_epoch_; }

  // The song's pitch, shared by every track: the standard pitch and the
  // YUxx/YDxx offset, applied after every per-track pitch effect.
  // The offset is kept in sixteenths of a step and converted with the
  // tuning in force when it is read, so a tuning change keeps its count.
  // Call once per block, before any track renders.
  void setGlobalPitchTuning(float standard_pitch_hz, int edo_steps) {
    global_base_cents_ = standardPitchCents(standard_pitch_hz);
    global_cents_per_unit_ = 1200.0 / (16.0 * static_cast<double>(edo_steps > 0 ? edo_steps : 12));
  }
  // YUxx/YDxx: from `frame`, move the offset by `units` sixteenths of a step
  // in all, linearly over `frames` frames.
  void addGlobalPitchSlide(int frame, double units, int frames) {
    if (frames > 0 && units != 0.0) global_slides_.push_back({units, frame, frames});
  }
  // The offset (sixteenths of a step) at block-relative `frame`.
  double globalPitchUnits(int frame) const {
    double units = global_completed_units_;
    for (auto & slide : global_slides_) {
      int elapsed = std::clamp(frame - slide.start, 0, slide.length);
      units += slide.units * static_cast<double>(elapsed) / static_cast<double>(slide.length);
    }
    return units;
  }
  // The song's pitch in cents from the nominal standard pitch at `frame`.
  // Never infinite or NaN however far the offset has run: only the voices'
  // own frequencies are limited, where they are computed.
  double globalPitchCents(int frame) const {
    double cents = global_base_cents_ + global_cents_per_unit_ * globalPitchUnits(frame);
    return finiteValue(cents) ? cents : 0.0;
  }
  // The first frame after `frame` and before `limit` where a global slide
  // starts or ends (`limit` if none): a voice's pitch ramp is linear within
  // a chunk, so chunks are cut there.
  int nextGlobalPitchBreak(int frame, int limit) const {
    int next = limit;
    for (auto & slide : global_slides_) {
      if (slide.start > frame) next = std::min(next, slide.start);
      if (slide.start + slide.length > frame) next = std::min(next, slide.start + slide.length);
    }
    return next;
  }
  // A seek or restart: the offset is what the song reached by that row.
  void setGlobalPitchUnits(double units) {
    global_slides_.clear();
    global_completed_units_ = units;
  }

  // A SampleTrack content's own trigger (SongState.h's per-row scheduling -
  // a fresh instance becoming active, a looping clip's own later lap, or a
  // track's own background bed starting or being jumped into) due at a specific
  // within-block frame - queued here rather than calling
  // SampleTrackState's own trigger method directly, so a sound producer
  // never has to know how to start mid-block itself: SampleTrackState::
  // render()'s own chunked loop (mirroring InstrumentTrackState's exactly,
  // just against this timeline instead of pending_events_) is what
  // actually calls it, precisely at this frame, the same way a pattern
  // note's own chunked render already does.
  void addPendingSampleStart(int track_id, int frame, const SampleContent & content, int start_offset_frames = 0, bool is_background = false) {
    pending_sample_events_[track_id][frame].push_back(SampleTrackEvent{SampleTrackEvent::START, std::make_shared<const SampleContent>(content), start_offset_frames, is_background});
  }

  // The stop side of the same timeline (SampleTrackEvent's own comment
  // has the full reasoning on why it's always a release, never a hard
  // cut) - a one-shot clip's own real audio outlasting its length, an
  // explicit stop instance, eventually pause/seek. The
  // voice itself never decides when it ends for any of these reasons -
  // only for running out of its own audio - so each needs to land here
  // instead, at the exact frame it's actually due, not be approximated
  // by a caller.
  void addPendingSampleStop(int track_id, int frame, bool is_background = false) {
    pending_sample_events_[track_id][frame].push_back(SampleTrackEvent{SampleTrackEvent::STOP, nullptr, 0, is_background});
  }

  // Map-of-vector, unlike a plain map: the clip role and the background-
  // bed role (SampleTrackEvent::is_background) are independent timelines
  // sharing this one queue, so a clip's own transition and the bed's own
  // can legitimately both land on the exact same frame - at most one
  // event per role per frame in practice, but never coalesced into one
  // slot the way this used to work back when a SampleTrack could only
  // ever have one thing playing at all.
  std::map<int, std::vector<SampleTrackEvent> > & getPendingSampleEvents(int track_id) {
    return pending_sample_events_[track_id];
  }

  bool empty() const {
    for (auto & td : pending_events_) {
      if (!td.second.empty()) return false;
    }
    for (auto & td : pending_azimuth_ticks_) {
      if (!td.second.empty()) return false;
    }
    for (auto & td : pending_pitch_slides_) {
      if (!td.second.empty()) return false;
    }
    for (auto & td : pending_pitch_glides_) {
      if (!td.second.empty()) return false;
    }
    for (auto & td : pending_sample_events_) {
      if (!td.second.empty()) return false;
    }
    return true;
  }

  void updateFrameOffset(int offset) {
    for (auto & slide : global_slides_) slide.start += offset;
    for (auto & slide : global_slides_)
      if (slide.start + slide.length <= 0) global_completed_units_ += slide.units;
    global_slides_.erase(std::remove_if(global_slides_.begin(), global_slides_.end(), [](const GlobalSlide & g) { return g.start + g.length <= 0; }), global_slides_.end());

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

    for (auto & [track_id, glides] : pending_pitch_glides_) {
      auto old_glides = glides;
      glides.clear();
      for (auto & [frame, list] : old_glides) {
        auto new_frame = frame + offset;
        if (new_frame >= 0) glides[new_frame] = list;
      }
    }

    for (auto & [track_id, slides] : pending_pitch_slides_) {
      auto old_slides = slides;
      slides.clear();
      for (auto & [frame, list] : old_slides) {
        auto new_frame = frame + offset;
        if (new_frame >= 0) slides[new_frame] = list;
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
  std::unordered_map<int, std::map<int, std::vector<PitchSlide> > > pending_pitch_slides_;
  std::unordered_map<int, std::map<int, std::vector<PitchGlide> > > pending_pitch_glides_;
  std::unordered_map<int, std::map<int, std::vector<SampleTrackEvent> > > pending_sample_events_;
  float bpm_ = 0.0f;
  int transport_epoch_ = 0;

  // A stop: running global slides keep the offset they reached.
  void freezeGlobalPitch() {
    global_completed_units_ = globalPitchUnits(0);
    global_slides_.clear();
  }
  struct GlobalSlide {
    double units;
    int start; // block-relative; negative once it began in an earlier block
    int length;
  };
  std::vector<GlobalSlide> global_slides_;
  double global_completed_units_ = 0.0;
  bool keep_global_pitch_ = false;
  double global_base_cents_ = 0.0;
  double global_cents_per_unit_ = 1200.0 / (16.0 * 31.0);
};

#endif
