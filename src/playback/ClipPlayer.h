#ifndef _CLIPPLAYER_H_
#define _CLIPPLAYER_H_

#include "../launchpad/ClipHighlight.h"
#include "../model/Song.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

class Controller;
struct LiveTrackInfo;

// Live View: launching clips, stopping tracks and returning them to
// the arrangement, all inside the one transport. A launch takes its track
// over from the arrangement at the transport's next bar; the audio thread
// (SongState::queueLaunch()) owns that and plays the launched
// clips, and each playback snapshot reports it back. This keeps the
// UI-side bookkeeping - the clip takes - and predicts a queued
// change until a snapshot has caught up with it. Owned by Controller, so
// every launch - a Launchpad pad, the clip grid, a command - goes through
// the same place, with or without a device connected.
class ClipPlayer {
 public:
  explicit ClipPlayer(Controller & controller) : controller_(controller) { }

  // A Live View pad press on (track_id, clip_index). With Record Arm
  // off, a populated slot queues the clip to launch at the next bar,
  // starting the transport if it's stopped; on an armed track an empty
  // slot queues a fresh take, and pressing the slot being recorded into
  // queues that take's stop - a populated slot only ever launches, never
  // records (overdubbing is toggleOverdub()'s job). With Record Arm on it
  // places the clip into the arrangement at the transport's bar instead.
  // An out-of-range or empty clip_index on an unarmed track stops it.
  void triggerClip(int track_id, int clip_index);
  // Deletes what slot (track_id, clip_index) holds (Controller::
  // deleteClipSlot()): instantly while the transport is stopped, or when
  // the clip isn't sounding. A clip that is playing (or queued) on its
  // track while the transport runs is never pulled out from under the
  // playhead: its track is stopped at the next bar and the clip removed
  // once that stop has taken effect (tick()).
  void deleteClip(int track_id, int clip_index);
  // Session Record: overdubs the clip playing on each armed track (the
  // fallback track if none is armed) from the next bar, in place - the
  // clip keeps looping, the take lines up with it. While any take is in
  // flight, stops those takes at the next bar instead, leaving the clips
  // playing. Returns false when there was nothing to overdub or stop.
  bool toggleOverdub(int fallback_track_id);
  // Queues `track_ids`' clip at `clip_index`, launching together at the
  // next bar, along with the scene's own tempo if it has one
  // (Song::getSceneTempo()).
  void launchScene(int clip_index, const std::vector<int> & track_ids);
  // Stop Clip for one track: silences it from the next bar, taking it over
  // from the arrangement if it wasn't already; a queued stop of its take
  // while recording; a stop instance in the arrangement with Record Arm
  // on.
  void stopTrack(int track_id);
  // stopTrack() for every playable track.
  void stopAllTracks();
  // Stops every launched clip now and forgets everything queued.
  void silenceAll();
  // Back to arrangement: the track (or every taken-over track) follows
  // the arrangement again from the next bar, at the transport's position.
  void returnToArrangement(int track_id);
  // Also hands the bars back to the arrangement's (a launched scene's time
  // signature no longer applies).
  void returnAllToArrangement();

  // Where an arrangement assign (a launch with Record Arm on) writes when
  // the transport doesn't start: the arrangement cursor's row.
  void setAssignRow(int row) { assign_row_ = row; }
  // Starts the transport for an arrangement assign made while stopped, so
  // whoever tracks auto-started playback can stop it again on disarm.
  void setAssignPlaybackStarter(std::function<void()> start) { assign_playback_starter_ = std::move(start); }

  // Once per UI frame: lets a finished take loop back, and walks the
  // transport's rows since the last call, resolving queued takes on the
  // first row of a bar and growing the takes in flight.
  void tick();

  // The live clock's step nearest to now - a live press just after a
  // row boundary is more likely an early attempt at the next one - and the
  // step the bar it falls in began at.
  // `delay` is the sub-row offset (0-255 of a row, a Note's own delay
  // unit) a raw step carries; a quantized one has none.
  struct Step { int step; int bar_start; int delay = 0; };
  Step quantizedStep() const;
  // The live clock's step the press falls in, rounded down, with how far
  // into that row it landed - what an unquantized live take records.
  Step rawStep() const;

  // Whether a launched clip is playing on `track_id`, and whether Live
  // view has taken the track over from the arrangement at all (a stopped
  // track stays taken over, silent).
  bool isLaunched(int track_id) const;
  bool isTakenOver(int track_id) const;

  // What a track's clip playback is doing right now, for display - a
  // launched clip, or while the transport runs the clip the arrangement
  // plays on it. A track with no clip playing and nothing queued has no
  // entry.
  struct Playhead {
    int clip_index = -1; // the clip playing, or -1 if none
    int row = -1; // its current row (kept while paused); -1 for a track with no launched clip, or a finished one-shot
    bool looping = false;
    int elapsed = -1; // rows since the clip started, not wrapped by its length - continuous across a loop; -1 as row
    std::optional<int> queued_clip; // a pending launch (clip index) or stop/return (-1), taking effect at the next bar
  };
  std::unordered_map<int, Playhead> playheads() const;
  // Moves every launched clip's playhead by `delta_rows` (clamped at row 0)
  // and predicts it locally; the paused transport's cursor move.
  void shiftLaunchedClips(int delta_rows);

  // A clip slot's transport/recording state - what its Launchpad pad and
  // the terminal's clip grid both show. A slot with no clip on an unarmed
  // track is NONE.
  ClipHighlight clipHighlight(int track_id, int clip_index) const;

 private:
  const LiveTrackInfo * liveTrack(int track_id) const;
  // Sends a queued change to the audio thread and predicts it locally.
  void queueChange(int track_id, int target);
  void startTransport();
  // Queues the stop of `track_id`'s note take for the next bar - where a
  // fresh take also starts looping, launched on the same bar.
  void queueTakeStop(int track_id);
  // One transport row, `step` on the live clock and `row` in the
  // arrangement.
  void advanceToRow(int step, int row);
  // Queues every take that just finished to loop back from the next bar,
  // except `launched_track_id`'s, already launched on the bar it stopped.
  void joinCompletedTakes(int launched_track_id = -1);

  // A stop instance at the transport's bar, while recording an
  // arrangement. A no-op while the transport is stopped.
  void placeRecordingStop(int track_id);
  // Finalizes or cancels `track_id`'s real audio capture - never the
  // note-take trim, which would read the take's empty Pattern as nothing
  // recorded and reset it to one bar.
  void stopSampleTrackRecording(int track_id);
  // Real audio capture into (track_id, clip_index): one immediate, global
  // target, never bar-quantized. Handles a press on an armed SampleTrack's
  // slot; false if the track isn't a SampleTrack.
  bool triggerSampleCapture(int track_id, int clip_index);

  // Removes the clips deleteClip() queued once nothing is sounding them.
  void resolvePendingDeletes();

  Controller & controller_;

  // Clips (track, clip id) waiting for their track's stop to take effect.
  std::vector<std::pair<int, std::string>> pending_deletes_;

  // The last sequence number sent with a queued change.
  int seq_ = 0;
  // The live clock step tick() last walked to, or -1 while stopped.
  int last_step_ = -1;

  // A pending take. STOP ends just the in-flight take, leaving the track
  // armed; FRESH_TAKE carries the pressed empty slot; OVERDUB the playing
  // clip to record into without restarting it.
  struct QueuedRecording { enum Kind { STOP, FRESH_TAKE, OVERDUB } kind; int clip_index = 0; };
  std::unordered_map<int, QueuedRecording> queued_recording_;

  // Sends a launched scene's tempo and time signature (or `clear_running`)
  // to the audio thread, which applies them on the bar - see
  // SongState::queueSceneChange().
  void queueSceneChange(int tempo, TimeSignature signature, bool clear_running);
  int scene_seq_ = 0;
  int assign_row_ = 0;
  std::function<void()> assign_playback_starter_;
};

#endif
