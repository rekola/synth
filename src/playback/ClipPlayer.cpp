#include "ClipPlayer.h"

#include "PlaybackControlEvent.h"
#include "../Controller.h"
#include "../launchpad/LaunchpadTiming.h"
#include "../model/ArrangementOps.h"
#include "../model/Song.h"
#include "../state/PlaybackInfo.h"
#include "../state/LiveTrackInfo.h"

#include <algorithm>

using namespace std;

namespace {
  bool hasClipAt(const vector<Clip> & clips, int clip_index) {
    return clip_index >= 0 && clip_index < static_cast<int>(clips.size()) && !clips[static_cast<size_t>(clip_index)].isEmpty();
  }

  // Whether an empty slot stops its track when launched - a slot past the
  // end of the list has its default stop button.
  bool hasStopButtonAt(const vector<Clip> & clips, int clip_index) {
    return clip_index < 0 || clip_index >= static_cast<int>(clips.size()) || clips[static_cast<size_t>(clip_index)].hasStopButton();
  }

  // How many rows tick() walks to catch up after a slow frame; a longer
  // gap is a jump, not rows played.
  constexpr int kMaxCatchUpRows = 64;
}

const LiveTrackInfo *
ClipPlayer::liveTrack(int track_id) const {
  return controller_.getPlaybackInfo().getLiveTrack(track_id);
}

bool
ClipPlayer::isLaunched(int track_id) const {
  auto * live_track = liveTrack(track_id);
  return live_track && live_track->clip_index >= 0;
}

bool
ClipPlayer::isTakenOver(int track_id) const {
  auto * live_track = liveTrack(track_id);
  return live_track && live_track->isTakenOver();
}

void
ClipPlayer::queueChange(int track_id, int target) {
  seq_++;
  controller_.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::QUEUE_LAUNCH, controller_.getActiveBufferName(), track_id, target, seq_));
  auto info = controller_.getPlaybackInfo();
  auto tracks = info.getLiveTracks();
  queueLaunch(tracks, track_id, target);
  info.setLiveTracks(move(tracks));
  info.setLiveSeq(seq_);
  controller_.setPlaybackInfo(info);
}

void
ClipPlayer::shiftLaunchedClips(int delta_rows) {
  if (delta_rows == 0) return;
  seq_++;
  controller_.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SHIFT_LIVE_POSITION, controller_.getActiveBufferName(), delta_rows, seq_));
  auto info = controller_.getPlaybackInfo();
  auto tracks = info.getLiveTracks();
  shiftLiveTracks(tracks, info.getLiveClock(), delta_rows);
  info.setLiveTracks(move(tracks));
  info.setLiveSeq(seq_);
  controller_.setPlaybackInfo(info);
}

void ClipPlayer::queueSceneChange(int tempo, TimeSignature signature, bool clear_running) {
  auto flags = (clear_running ? 1 : 0) | (controller_.getPlaybackInfo().isPlaying() ? 0 : 2);
  controller_.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(
      PlaybackControlEvent::QUEUE_SCENE_CHANGE, controller_.getActiveBufferName(), tempo, signature.numerator * 100 + signature.denominator, flags, ++scene_seq_));
}

void
ClipPlayer::startTransport() {
  if (!controller_.getPlaybackInfo().isPlaying()) controller_.togglePlaying();
}

void
ClipPlayer::triggerClip(int track_id, int clip_index) {
  auto & song = controller_.getSong();
  bool has_clip = hasClipAt(song.getClips(track_id), clip_index);
  // An empty slot without a stop button leaves the track alone - alone
  // or in a scene, armed or not.
  if (!has_clip && !hasStopButtonAt(song.getClips(track_id), clip_index)) return;
  auto * track = song.getMasterTrack().getChildByInternalId(track_id);
  bool is_sample_track = track && track->getType() == TrackType::SAMPLE;

  if (controller_.isTrackArmed(track_id) && is_sample_track) {
    triggerSampleCapture(track_id, clip_index);
    return;
  }

  if (controller_.isTrackArmed(track_id)) {
    // Pressing the slot being recorded into stops just that take.
    if (controller_.isLiveRecording(track_id) && controller_.getLiveRecordingClipIndex(track_id) == clip_index) {
      queueTakeStop(track_id);
      return;
    }
    // Nothing else launches over a take in flight.
    if (controller_.isLiveRecording(track_id)) return;
    if (!has_clip) {
      // A fresh take silences whatever the track was playing.
      queued_recording_[track_id] = QueuedRecording{QueuedRecording::FRESH_TAKE, clip_index};
      queueChange(track_id, LiveTrackInfo::kSilent);
      startTransport();
      return;
    }
    // A populated slot only launches, below.
  }

  if (!controller_.isNoteCaptureArmed()) {
    if (!has_clip) {
      queueChange(track_id, LiveTrackInfo::kSilent);
      return;
    }
    // A launch never toggles: pressing the playing clip relaunches it
    // from row 0.
    queueChange(track_id, clip_index);
    startTransport();
    return;
  }

  // Record Arm on: place the clip into the arrangement - a real instance
  // event, not a live reference - at the transport's bar, snapped
  // forward. The transport has to run for the arrangement to grow, so a
  // stopped one starts; an empty slot writes a stop instead.
  if (!has_clip) {
    placeRecordingStop(track_id);
    return;
  }
  auto & playback_info = controller_.getPlaybackInfo();
  if (!playback_info.isPlaying() && assign_playback_starter_) assign_playback_starter_();
  auto raw_row = playback_info.isPlaying() ? playback_info.getAbsolutePosition() : assign_row_;
  placeClipInstance(song, track_id, quantizedBarRow(song.getArrangementBars(), raw_row), clip_index);
  song.incVersion();
}

bool
ClipPlayer::triggerSampleCapture(int track_id, int clip_index) {
  auto * track = controller_.getSong().getMasterTrack().getChildByInternalId(track_id);
  if (!track || track->getType() != TrackType::SAMPLE) return false;
  // Real audio capture is one immediate, global (track, clip) target -
  // there's only one input stream - never bar-quantized.
  if (controller_.isRecording() && controller_.getRecordingTrackId() == track_id) {
    stopSampleTrackRecording(track_id);
    return true;
  }
  if (controller_.isThresholdArmed() && controller_.getRecordingTrackId() == track_id) {
    // Nothing captured yet: the same slot cancels, another retargets.
    if (controller_.getLiveRecordingClipIndex(track_id) == clip_index) stopSampleTrackRecording(track_id);
    else controller_.armLiveTrackRecording(track_id, clip_index);
    return true;
  }
  // Another track owns the capture pipeline.
  if (controller_.isRecording() || controller_.isThresholdArmed()) return true;
  controller_.armLiveTrackRecording(track_id, clip_index);
  controller_.armThresholdRecording(track_id);
  return true;
}

void
ClipPlayer::queueTakeStop(int track_id) {
  queued_recording_[track_id] = QueuedRecording{QueuedRecording::STOP};
  // An overdub's clip is already playing; a fresh take's starts looping on
  // the bar the take stops on.
  auto clip_index = controller_.getLiveRecordingClipIndex(track_id);
  if (clip_index >= 0 && !(isLaunched(track_id) && liveTrack(track_id)->clip_index == clip_index)) {
    queueChange(track_id, clip_index);
  }
}

bool
ClipPlayer::toggleOverdub(int fallback_track_id) {
  // Takes in flight stop, the clips they were recorded into keep playing.
  bool stopping = false;
  for (auto track_id : controller_.getLiveRecordingTrackIds()) {
    auto * track = controller_.getSong().getMasterTrack().getChildByInternalId(track_id);
    if (track && track->getType() == TrackType::SAMPLE) stopSampleTrackRecording(track_id);
    else queueTakeStop(track_id);
    stopping = true;
  }
  if (stopping) return true;

  vector<int> targets;
  for (auto track_id : controller_.getSong().getPlayableTrackIds()) {
    if (controller_.isTrackArmed(track_id) && isLaunched(track_id)) targets.push_back(track_id);
  }
  bool any_armed = false;
  for (auto track_id : controller_.getSong().getPlayableTrackIds()) any_armed = any_armed || controller_.isTrackArmed(track_id);
  if (!any_armed && isLaunched(fallback_track_id)) targets.push_back(fallback_track_id);
  for (auto track_id : targets) {
    auto clip_index = liveTrack(track_id)->clip_index;
    if (!triggerSampleCapture(track_id, clip_index)) queued_recording_[track_id] = QueuedRecording{QueuedRecording::OVERDUB, clip_index};
  }
  return !targets.empty();
}

void
ClipPlayer::launchScene(int clip_index, const vector<int> & track_ids) {
  // The scene's tempo and time signature go to the audio thread, which
  // applies them with its clips: on the bar they launch on, or at the first
  // row played from a stopped transport. Sent even when the scene has
  // neither, so one still waiting from an earlier launch is dropped.
  auto & song = controller_.getSong();
  // One batch, so the audio thread never takes part of the scene on one bar
  // and the rest on the next.
  auto & events = controller_.getPlaybackEventQueue();
  events.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::BATCH_BEGIN, controller_.getActiveBufferName()));
  if (!track_ids.empty()) {
    queueSceneChange(song.getSceneTempo(clip_index), song.getSceneTimeSignature(clip_index), false);
  }
  for (auto track_id : track_ids) triggerClip(track_id, clip_index);
  // A scene starts the transport even when every slot in it is empty.
  if (!track_ids.empty() && !controller_.isNoteCaptureArmed()) startTransport();
  events.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::BATCH_END, controller_.getActiveBufferName()));
}

void
ClipPlayer::stopTrack(int track_id) {
  if (controller_.isTrackArmed(track_id) && controller_.isLiveRecording(track_id)) {
    auto * track = controller_.getSong().getMasterTrack().getChildByInternalId(track_id);
    if (track && track->getType() == TrackType::SAMPLE) stopSampleTrackRecording(track_id);
    else queueTakeStop(track_id);
    return;
  }
  // Recording an arrangement, a stop has to be song data.
  if (controller_.isNoteCaptureArmed()) {
    placeRecordingStop(track_id);
    return;
  }
  queueChange(track_id, LiveTrackInfo::kSilent);
}

void
ClipPlayer::stopAllTracks() {
  for (auto track_id : controller_.getSong().getPlayableTrackIds()) stopTrack(track_id);
}

void
ClipPlayer::silenceAll() {
  seq_++;
  controller_.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SILENCE_LIVE, controller_.getActiveBufferName(), seq_));
  auto info = controller_.getPlaybackInfo();
  auto tracks = info.getLiveTracks();
  silenceLiveTracks(tracks);
  info.setLiveTracks(move(tracks));
  info.setLiveSeq(seq_);
  controller_.setPlaybackInfo(info);
  queued_recording_.clear();
}

void
ClipPlayer::returnToArrangement(int track_id) {
  queueChange(track_id, LiveTrackInfo::kArrangement);
}

void
ClipPlayer::returnAllToArrangement() {
  // The song's own bars come back with it.
  queueSceneChange(0, {}, true);
  vector<int> track_ids;
  for (auto & [ track_id, unused ] : controller_.getPlaybackInfo().getLiveTracks()) track_ids.push_back(track_id);
  for (auto track_id : track_ids) returnToArrangement(track_id);
}

void
ClipPlayer::joinCompletedTakes(int launched_track_id) {
  auto & song = controller_.getSong();
  while (auto completed = controller_.takeCompletedLiveRecording()) {
    if (completed->track_id == launched_track_id) continue;
    if (!hasClipAt(song.getClips(completed->track_id), completed->clip_index)) continue;
    queueChange(completed->track_id, completed->clip_index);
  }
}

void
ClipPlayer::placeRecordingStop(int track_id) {
  auto & playback_info = controller_.getPlaybackInfo();
  if (!playback_info.isPlaying()) return;
  auto & song = controller_.getSong();
  auto row = quantizedBarRow(song.getArrangementBars(), playback_info.getAbsolutePosition());
  placeStopInstance(song, track_id, row);
  song.incVersion();
}

void
ClipPlayer::stopSampleTrackRecording(int track_id) {
  // finishSampleCapture() finalizes real audio; a take still waiting on
  // the loudness threshold only needs disarming.
  if (controller_.isRecording() && controller_.getRecordingTrackId() == track_id) {
    controller_.finishSampleCapture();
  } else if (controller_.isThresholdArmed() && controller_.getRecordingTrackId() == track_id) {
    controller_.disarmThresholdRecording();
  }
  controller_.clearLiveRecordingTake(track_id);
}

void
ClipPlayer::deleteClip(int track_id, int clip_index) {
  auto & clips = controller_.getSong().getClips(track_id);
  bool populated = clip_index >= 0 && clip_index < static_cast<int>(clips.size()) && !clips[static_cast<size_t>(clip_index)].isEmpty();
  if (populated && controller_.getPlaybackInfo().isPlaying()) {
    auto heads = playheads();
    auto it = heads.find(track_id);
    if (it != heads.end() && (it->second.clip_index == clip_index || it->second.queued_clip == clip_index)) {
      pending_deletes_.emplace_back(track_id, clips[static_cast<size_t>(clip_index)].getId());
      stopTrack(track_id);
      return;
    }
  }
  controller_.deleteClipSlot(track_id, clip_index);
}

void
ClipPlayer::resolvePendingDeletes() {
  if (pending_deletes_.empty()) return;
  auto playing = controller_.getPlaybackInfo().isPlaying();
  auto heads = playheads();
  auto & song = controller_.getSong();
  for (auto it = pending_deletes_.begin(); it != pending_deletes_.end(); ) {
    auto & clips = song.getClips(it->first);
    int index = -1;
    for (size_t i = 0; i < clips.size(); i++) {
      if (clips[i].getId() == it->second) { index = static_cast<int>(i); break; }
    }
    if (index < 0) { it = pending_deletes_.erase(it); continue; } // gone some other way
    auto head = heads.find(it->first);
    bool sounding = playing && head != heads.end() && (head->second.clip_index == index || head->second.queued_clip == index);
    if (sounding) { ++it; continue; }
    controller_.deleteClipSlot(it->first, index);
    it = pending_deletes_.erase(it);
  }
}

void
ClipPlayer::tick() {
  resolvePendingDeletes();

  // A take finished some other way than a queued stop (disarming, say)
  // loops back from the next bar, like a press on its own pad.
  joinCompletedTakes();

  auto & info = controller_.getPlaybackInfo();
  if (!info.isPlaying()) {
    last_step_ = -1;
    return;
  }
  // Every row played since the last frame, each on the live clock and
  // in the arrangement - they advance together between two snapshots.
  auto step = info.getLiveClock();
  auto row = info.getAbsolutePosition();
  // Just started: from the transport's first row.
  auto first = last_step_ < 0 ? max(info.getLiveStartClock(), step - kMaxCatchUpRows) : last_step_ + 1;
  if (step < last_step_ || step - last_step_ > kMaxCatchUpRows) first = step;
  for (auto s = first; s <= step; s++) advanceToRow(s, row - (step - s));
  last_step_ = step;
}

void
ClipPlayer::advanceToRow(int step, int row) {
  auto & song = controller_.getSong();
  if (song.isBarStart(row)) {
    auto queued = move(queued_recording_);
    queued_recording_.clear();
    for (auto & [ track_id, queued_value ] : queued) {
      if (queued_value.kind == QueuedRecording::STOP) {
        controller_.trimLiveRecordingClip(track_id);
        joinCompletedTakes(track_id);
      } else if (queued_value.kind == QueuedRecording::FRESH_TAKE) {
        controller_.armLiveTrackRecording(track_id, queued_value.clip_index);
      } else if (isLaunched(track_id) && liveTrack(track_id)->clip_index == queued_value.clip_index) {
        // The clip is still playing: record into it without restarting
        // it, the take's rows lining up with its loop.
        controller_.armLiveTrackRecording(track_id, queued_value.clip_index);
        controller_.primeLiveRecordingOrigin(track_id, liveTrack(track_id)->launch_clock);
      }
    }
  }
  for (auto track_id : controller_.getLiveRecordingTrackIds()) controller_.extendLiveRecordingClipIfNeeded(track_id, step);
}

ClipPlayer::Step
ClipPlayer::quantizedStep() const {
  auto & info = controller_.getPlaybackInfo();
  auto step = info.getLiveClock();
  auto row = info.getAbsolutePosition();
  if (info.getSampleInterval() > 0 && 2 * info.getSamplePos() >= info.getSampleInterval()) {
    step++;
    row++;
  }
  return {step, step - controller_.getSong().rowInBar(row)};
}

ClipPlayer::Step
ClipPlayer::rawStep() const {
  auto & info = controller_.getPlaybackInfo();
  auto step = info.getLiveClock();
  auto row = info.getAbsolutePosition();
  return {step, step - controller_.getSong().rowInBar(row), min(255, info.getCurrentDelay())};
}

unordered_map<int, ClipPlayer::Playhead>
ClipPlayer::playheads() const {
  auto & song = controller_.getSong();
  auto & info = controller_.getPlaybackInfo();
  unordered_map<int, Playhead> result;
  for (auto & [ track_id, live_track ] : info.getLiveTracks()) {
    Playhead playhead;
    auto & clips = song.getClips(track_id);
    if (live_track.clip_index >= 0 && live_track.clip_index < static_cast<int>(clips.size())) {
      auto & clip = clips[static_cast<size_t>(live_track.clip_index)];
      playhead.clip_index = live_track.clip_index;
      playhead.row = clipPlayheadRow(info.getLiveClock(), live_track.launch_clock, clip.getLength(), clip.isLooping());
      playhead.looping = clip.isLooping();
      if (playhead.row >= 0) playhead.elapsed = info.getLiveClock() - live_track.launch_clock;
    }
    if (live_track.queued != LiveTrackInfo::kNothingQueued) playhead.queued_clip = max(-1, live_track.queued);
    if (playhead.clip_index >= 0 || playhead.queued_clip) result[track_id] = playhead;
  }
  // A fresh take launches nothing, so its record head stands in for a
  // playhead: row 0 until the first note fixes where the take starts.
  for (auto track_id : controller_.getLiveRecordingTrackIds()) {
    auto clip_index = controller_.getLiveRecordingClipIndex(track_id);
    auto & head = result[track_id];
    if (head.row >= 0 && head.clip_index == clip_index) continue; // an overdub: the clip's own playhead
    auto origin = controller_.getLiveRecordingOrigin(track_id);
    head = Playhead();
    head.clip_index = clip_index;
    head.row = origin >= 0 ? max(0, info.getLiveClock() - origin) : 0;
  }
  // A track following the arrangement plays whatever clip is placed at the
  // transport's row.
  if (info.isPlaying()) {
    auto position = info.getAbsolutePosition();
    for (auto track_id : song.getPlayableTrackIds()) {
      auto * live_track = info.getLiveTrack(track_id);
      if (live_track && live_track->isTakenOver()) continue;
      auto active = resolveInstanceAt(song, track_id, position);
      if (active.clip_index < 0) continue;
      auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];
      auto & playhead = result[track_id];
      playhead.clip_index = active.clip_index;
      playhead.row = clipPlayheadRow(position, active.start_row, clip.getLength(), clip.isLooping());
      playhead.looping = clip.isLooping();
      playhead.elapsed = playhead.row >= 0 ? position - active.start_row : -1;
    }
  }
  return result;
}

ClipHighlight
ClipPlayer::clipHighlight(int track_id, int clip_index) const {
  auto & song = controller_.getSong();
  auto & playback_info = controller_.getPlaybackInfo();
  bool has_clip = hasClipAt(song.getClips(track_id), clip_index);

  // A track armed, recording or about to record shows its recording states
  // instead of the plain ones.
  bool armed = controller_.isTrackArmed(track_id);
  bool is_recording = controller_.isLiveRecording(track_id);
  auto queued_recording_it = queued_recording_.find(track_id);
  bool has_queued_recording = queued_recording_it != queued_recording_.end();
  if (armed || is_recording || has_queued_recording) {
    int recording_clip_index = is_recording ? controller_.getLiveRecordingClipIndex(track_id) : -1;
    auto queued_recording_kind = has_queued_recording ? queued_recording_it->second.kind : QueuedRecording::STOP;
    int queued_recording_clip_index = has_queued_recording ? queued_recording_it->second.clip_index : -1;
    if (is_recording && recording_clip_index == clip_index) {
      return (has_queued_recording && queued_recording_kind == QueuedRecording::STOP) ?
        ClipHighlight::RECORD_STOPPING : ClipHighlight::RECORDING;
    }
    if (has_queued_recording && queued_recording_kind != QueuedRecording::STOP && queued_recording_clip_index == clip_index) {
      return ClipHighlight::RECORD_QUEUED;
    }
    if (armed && !has_clip && hasStopButtonAt(song.getClips(track_id), clip_index)) return ClipHighlight::ARMED_EMPTY;
  }
  if (!has_clip) return ClipHighlight::NONE;

  // A taken-over track plays its launched clip; any other plays whatever
  // the arrangement has at the transport's position.
  auto * live_track = liveTrack(track_id);
  bool playing = false;
  if (live_track && live_track->isTakenOver()) {
    playing = live_track->clip_index == clip_index;
  } else if (playback_info.isPlaying()) {
    playing = resolveInstanceAt(song, track_id, playback_info.getAbsolutePosition()).clip_index == clip_index;
  }
  // A launched clip stays launched while the transport is paused.
  if (playing) return playback_info.isPlaying() ? ClipHighlight::PLAYING : ClipHighlight::PAUSED;
  if (live_track && live_track->queued == clip_index) return ClipHighlight::QUEUED;
  return ClipHighlight::NONE;
}
