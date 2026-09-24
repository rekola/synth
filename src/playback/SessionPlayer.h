#ifndef _SESSIONPLAYER_H_
#define _SESSIONPLAYER_H_

#include "../launchpad/LaunchpadTiming.h"
#include "../launchpad/SessionPadHighlight.h"

#include <chrono>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

class Controller;
class Clip;
class Song;

// Session view clip playback: which clip each track has launched and from
// which step, what's queued to launch, stop or record at the next bar,
// and the free-running clock that plays launched clips while the
// transport is stopped. Every launch and stop waits for the clock's next
// bar - bars counted from its step 0 - even the first one into silence. Owned by Controller, so every launch -
// a Launchpad pad, the clip grid, a command - goes through the same
// bookkeeping, with or without a device connected.
class SessionPlayer {
 public:
  using Clock = std::chrono::steady_clock;

  explicit SessionPlayer(Controller & controller) : controller_(controller) { }

  // Replaces the wall clock tick() reads - for tests, which drive time by
  // hand.
  void setTimeSource(std::function<Clock::time_point()> now) { now_ = std::move(now); }

  // A Session view pad press on (track_id, clip_index). Unarmed with
  // Record Arm off, it queues the clip for live playback, touching no song
  // data; on an armed track it queues a take, an overdub or a take's stop;
  // with Record Arm on it places the clip into the arrangement at the
  // transport's bar instead. An out-of-range or empty clip_index stops the
  // track.
  void triggerClip(int track_id, int clip_index);
  // Queues `track_ids`' clip at `clip_index`, launching together at the
  // next bar.
  void launchScene(int clip_index, const std::vector<int> & track_ids);
  // Stop Clip for one track: a quantized stop of its launched clip (or a
  // cancel of a pending launch), a queued stop of its take while
  // recording, or a stop instance in the arrangement with Record Arm on.
  void stopTrack(int track_id);
  // stopTrack() for every playable track.
  void stopAllTracks();
  // Releases every launched clip now and forgets everything queued.
  void silenceAll();

  // Where an arrangement assign (a launch with Record Arm on) writes when
  // the transport doesn't start: the arrangement cursor's section.
  void setAssignSection(int section_idx) { assign_section_idx_ = section_idx; }
  // Starts the transport for an arrangement assign made while stopped, so
  // whoever tracks auto-started playback can stop it again on disarm.
  void setAssignPlaybackStarter(std::function<void()> start) { assign_playback_starter_ = std::move(start); }

  // Once per UI frame: reacts to Record Arm arming, lets a just-finished
  // take join playback, and advances the clock - firing every step it
  // crossed. The clock runs only while the transport is stopped and
  // Record Arm is off, or a Session view take is recording (it's what
  // indexes that take).
  void tick();

  // The clock's step nearest to now - a live press just after a step
  // boundary is more likely an early attempt at the next one.
  int quantizedStep() const;

  bool isLaunched(int track_id) const { return launched_.count(track_id) > 0; }

  // What a track's clip playback is doing right now, for display. A track
  // with nothing playing or queued has no entry.
  struct Playhead {
    int clip_index = -1; // the clip playing, or -1 if none
    int row = -1; // its current row; -1 while the clock isn't running
    std::optional<int> queued_clip; // a pending launch (clip index) or stop (-1), taking effect at the next bar
  };
  std::unordered_map<int, Playhead> playheads() const;

  // A clip slot's transport/recording state - what its Launchpad pad and
  // the terminal's clip grid both show. A slot with no clip on an unarmed
  // track is NONE.
  SessionPadHighlight clipHighlight(int track_id, int clip_index) const;

 private:
  // Plays `step` rows into a launched clip: that row's notes, or for a
  // SampleTrack the whole clip at the start of each loop.
  void fireClipStep(int track_id, int clip_index, const Clip & clip, int step);
  // Applies whatever's queued at `step` if it's a bar boundary, then fires
  // that step of every launched clip.
  void advanceToStep(int step);
  // Launches every take that just finished: at `step` (the bar its stop
  // resolved on) for `track_id`, at the next bar for any other track.
  void joinCompletedTakes(int track_id = -1, int step = -1);
  void clear();

  // A stop instance at the transport's bar, while recording an
  // arrangement. A no-op while the transport is stopped.
  void placeRecordingStop(int track_id);
  // Finalizes or cancels `track_id`'s real audio capture - never the
  // note-take trim, which would read the take's empty Pattern as nothing
  // recorded and reset it to one bar.
  void stopSampleTrackRecording(int track_id);

  Controller & controller_;
  std::function<Clock::time_point()> now_ = [] { return Clock::now(); };

  StepClock clock_;
  Clock::time_point last_tick_;

  // launch_step is the clock step the clip's row 0 fired at.
  struct Launched { int clip_index; int launch_step; };
  std::unordered_map<int, Launched> launched_;
  // A pending launch (clip index, which relaunches an already-playing
  // clip from row 0) or stop (-1), per track, taking effect at the next
  // bar - a track can have one with nothing launched yet.
  std::unordered_map<int, int> queued_;
  // A pending take on an armed track. STOP ends just the in-flight take,
  // leaving the track armed; FRESH_TAKE/OVERDUB carry the exact pressed
  // index, decided at press time by whether it held a clip then.
  struct QueuedRecording { enum Kind { STOP, FRESH_TAKE, OVERDUB } kind; int clip_index = 0; };
  std::unordered_map<int, QueuedRecording> queued_recording_;

  bool was_note_capture_armed_ = false;
  int assign_section_idx_ = 0;
  std::function<void()> assign_playback_starter_;
};

#endif
