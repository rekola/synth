#include "SessionPlayer.h"

#include "PlaybackControlEvent.h"
#include "../Controller.h"
#include "../launchpad/LaunchpadTiming.h"
#include "../model/ArrangementOps.h"
#include "../model/Song.h"
#include "../state/PlaybackInfo.h"
#include "../state/SessionTrackInfo.h"

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

const SessionTrackInfo *
SessionPlayer::sessionTrack(int track_id) const {
  return controller_.getPlaybackInfo().getSessionTrack(track_id);
}

bool
SessionPlayer::isLaunched(int track_id) const {
  auto * session_track = sessionTrack(track_id);
  return session_track && session_track->clip_index >= 0;
}

bool
SessionPlayer::isTakenOver(int track_id) const {
  auto * session_track = sessionTrack(track_id);
  return session_track && session_track->isTakenOver();
}

void
SessionPlayer::queueChange(int track_id, int target) {
  seq_++;
  controller_.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::QUEUE_SESSION_CHANGE, controller_.getActiveBufferName(), track_id, target, seq_));
  auto info = controller_.getPlaybackInfo();
  auto tracks = info.getSessionTracks();
  queueSessionChange(tracks, track_id, target);
  info.setSessionTracks(move(tracks));
  info.setSessionSeq(seq_);
  controller_.setPlaybackInfo(info);
}

void
SessionPlayer::shiftLaunchedClips(int delta_rows) {
  if (delta_rows == 0) return;
  seq_++;
  controller_.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SHIFT_SESSION_POSITION, controller_.getActiveBufferName(), delta_rows, seq_));
  auto info = controller_.getPlaybackInfo();
  auto tracks = info.getSessionTracks();
  shiftSessionTracks(tracks, info.getSessionClock(), delta_rows);
  info.setSessionTracks(move(tracks));
  info.setSessionSeq(seq_);
  controller_.setPlaybackInfo(info);
}

void
SessionPlayer::startTransport() {
  if (!controller_.getPlaybackInfo().isPlaying()) controller_.togglePlaying();
}

void
SessionPlayer::triggerClip(int track_id, int clip_index) {
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
    if (controller_.isSessionRecording(track_id) && controller_.getSessionRecordingClipIndex(track_id) == clip_index) {
      queueTakeStop(track_id);
      return;
    }
    // Nothing else launches over a take in flight.
    if (controller_.isSessionRecording(track_id)) return;
    if (!has_clip) {
      // A fresh take silences whatever the track was playing.
      queued_recording_[track_id] = QueuedRecording{QueuedRecording::FRESH_TAKE, clip_index};
      queueChange(track_id, SessionTrackInfo::kSilent);
      startTransport();
      return;
    }
    // A populated slot only launches, below.
  }

  if (!controller_.isNoteCaptureArmed()) {
    if (!has_clip) {
      queueChange(track_id, SessionTrackInfo::kSilent);
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
  placeClipInstance(song, track_id, quantizedBarRow(raw_row, song.getRowsPerBar()), clip_index);
  song.incVersion();
}

bool
SessionPlayer::triggerSampleCapture(int track_id, int clip_index) {
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
    if (controller_.getSessionRecordingClipIndex(track_id) == clip_index) stopSampleTrackRecording(track_id);
    else controller_.armSessionTrackRecording(track_id, clip_index);
    return true;
  }
  // Another track owns the capture pipeline.
  if (controller_.isRecording() || controller_.isThresholdArmed()) return true;
  controller_.armSessionTrackRecording(track_id, clip_index);
  controller_.armThresholdRecording(track_id);
  return true;
}

void
SessionPlayer::queueTakeStop(int track_id) {
  queued_recording_[track_id] = QueuedRecording{QueuedRecording::STOP};
  // An overdub's clip is already playing; a fresh take's starts looping on
  // the bar the take stops on.
  auto clip_index = controller_.getSessionRecordingClipIndex(track_id);
  if (clip_index >= 0 && !(isLaunched(track_id) && sessionTrack(track_id)->clip_index == clip_index)) {
    queueChange(track_id, clip_index);
  }
}

bool
SessionPlayer::toggleOverdub(int fallback_track_id) {
  // Takes in flight stop, the clips they were recorded into keep playing.
  bool stopping = false;
  for (auto track_id : controller_.getSessionRecordingTrackIds()) {
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
    auto clip_index = sessionTrack(track_id)->clip_index;
    if (!triggerSampleCapture(track_id, clip_index)) queued_recording_[track_id] = QueuedRecording{QueuedRecording::OVERDUB, clip_index};
  }
  return !targets.empty();
}

void
SessionPlayer::launchScene(int clip_index, const vector<int> & track_ids) {
  for (auto track_id : track_ids) triggerClip(track_id, clip_index);
  // A scene starts the transport even when every slot in it is empty.
  if (!track_ids.empty() && !controller_.isNoteCaptureArmed()) startTransport();
}

void
SessionPlayer::stopTrack(int track_id) {
  if (controller_.isTrackArmed(track_id) && controller_.isSessionRecording(track_id)) {
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
  queueChange(track_id, SessionTrackInfo::kSilent);
}

void
SessionPlayer::stopAllTracks() {
  for (auto track_id : controller_.getSong().getPlayableTrackIds()) stopTrack(track_id);
}

void
SessionPlayer::silenceAll() {
  seq_++;
  controller_.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SILENCE_SESSION, controller_.getActiveBufferName(), seq_));
  auto info = controller_.getPlaybackInfo();
  auto tracks = info.getSessionTracks();
  silenceSessionTracks(tracks);
  info.setSessionTracks(move(tracks));
  info.setSessionSeq(seq_);
  controller_.setPlaybackInfo(info);
  queued_recording_.clear();
}

void
SessionPlayer::returnToArrangement(int track_id) {
  queueChange(track_id, SessionTrackInfo::kArrangement);
}

void
SessionPlayer::returnAllToArrangement() {
  vector<int> track_ids;
  for (auto & [ track_id, unused ] : controller_.getPlaybackInfo().getSessionTracks()) track_ids.push_back(track_id);
  for (auto track_id : track_ids) returnToArrangement(track_id);
}

void
SessionPlayer::joinCompletedTakes(int launched_track_id) {
  auto & song = controller_.getSong();
  while (auto completed = controller_.takeCompletedSessionRecording()) {
    if (completed->track_id == launched_track_id) continue;
    if (!hasClipAt(song.getClips(completed->track_id), completed->clip_index)) continue;
    queueChange(completed->track_id, completed->clip_index);
  }
}

void
SessionPlayer::placeRecordingStop(int track_id) {
  auto & playback_info = controller_.getPlaybackInfo();
  if (!playback_info.isPlaying()) return;
  auto & song = controller_.getSong();
  auto row = quantizedBarRow(playback_info.getAbsolutePosition(), song.getRowsPerBar());
  placeStopInstance(song, track_id, row);
  song.incVersion();
}

void
SessionPlayer::stopSampleTrackRecording(int track_id) {
  // finishSampleCapture() finalizes real audio; a take still waiting on
  // the loudness threshold only needs disarming.
  if (controller_.isRecording() && controller_.getRecordingTrackId() == track_id) {
    controller_.finishSampleCapture();
  } else if (controller_.isThresholdArmed() && controller_.getRecordingTrackId() == track_id) {
    controller_.disarmThresholdRecording();
  }
  controller_.clearSessionRecordingTake(track_id);
}

void
SessionPlayer::deleteClip(int track_id, int clip_index) {
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
SessionPlayer::resolvePendingDeletes() {
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
SessionPlayer::tick() {
  resolvePendingDeletes();

  // A take finished some other way than a queued stop (disarming, say)
  // loops back from the next bar, like a press on its own pad.
  joinCompletedTakes();

  auto & info = controller_.getPlaybackInfo();
  if (!info.isPlaying()) {
    last_step_ = -1;
    return;
  }
  // Every row played since the last frame, each on the session clock and
  // in the arrangement - they advance together between two snapshots.
  auto step = info.getSessionClock();
  auto row = info.getAbsolutePosition();
  // Just started: from the transport's first row.
  auto first = last_step_ < 0 ? max(info.getSessionStartClock(), step - kMaxCatchUpRows) : last_step_ + 1;
  if (step < last_step_ || step - last_step_ > kMaxCatchUpRows) first = step;
  for (auto s = first; s <= step; s++) advanceToRow(s, row - (step - s));
  last_step_ = step;
}

void
SessionPlayer::advanceToRow(int step, int row) {
  auto rows_per_bar = max(1, controller_.getSong().getRowsPerBar());
  if (row % rows_per_bar == 0) {
    auto queued = move(queued_recording_);
    queued_recording_.clear();
    for (auto & [ track_id, queued_value ] : queued) {
      if (queued_value.kind == QueuedRecording::STOP) {
        controller_.trimSessionRecordingClip(track_id);
        joinCompletedTakes(track_id);
      } else if (queued_value.kind == QueuedRecording::FRESH_TAKE) {
        controller_.armSessionTrackRecording(track_id, queued_value.clip_index);
      } else if (isLaunched(track_id) && sessionTrack(track_id)->clip_index == queued_value.clip_index) {
        // The clip is still playing: record into it without restarting
        // it, the take's rows lining up with its loop.
        controller_.armSessionTrackRecording(track_id, queued_value.clip_index);
        controller_.primeSessionRecordingOrigin(track_id, sessionTrack(track_id)->launch_clock);
      }
    }
  }
  for (auto track_id : controller_.getSessionRecordingTrackIds()) controller_.extendSessionRecordingClipIfNeeded(track_id, step);
}

SessionPlayer::Step
SessionPlayer::quantizedStep() const {
  auto & info = controller_.getPlaybackInfo();
  auto step = info.getSessionClock();
  auto row = info.getAbsolutePosition();
  if (info.getSampleInterval() > 0 && 2 * info.getSamplePos() >= info.getSampleInterval()) {
    step++;
    row++;
  }
  auto rows_per_bar = max(1, controller_.getSong().getRowsPerBar());
  return { step, step - row % rows_per_bar };
}

SessionPlayer::Step
SessionPlayer::rawStep() const {
  auto & info = controller_.getPlaybackInfo();
  auto step = info.getSessionClock();
  auto row = info.getAbsolutePosition();
  auto rows_per_bar = max(1, controller_.getSong().getRowsPerBar());
  return { step, step - row % rows_per_bar, min(255, info.getCurrentDelay()) };
}

unordered_map<int, SessionPlayer::Playhead>
SessionPlayer::playheads() const {
  auto & song = controller_.getSong();
  auto & info = controller_.getPlaybackInfo();
  unordered_map<int, Playhead> result;
  for (auto & [ track_id, session_track ] : info.getSessionTracks()) {
    Playhead playhead;
    auto & clips = song.getClips(track_id);
    if (session_track.clip_index >= 0 && session_track.clip_index < static_cast<int>(clips.size())) {
      auto & clip = clips[static_cast<size_t>(session_track.clip_index)];
      playhead.clip_index = session_track.clip_index;
      playhead.row = clipPlayheadRow(info.getSessionClock(), session_track.launch_clock, clip.getLength(), clip.isLooping());
      playhead.looping = clip.isLooping();
      if (playhead.row >= 0) playhead.elapsed = info.getSessionClock() - session_track.launch_clock;
    }
    if (session_track.queued != SessionTrackInfo::kNothingQueued) playhead.queued_clip = max(-1, session_track.queued);
    if (playhead.clip_index >= 0 || playhead.queued_clip) result[track_id] = playhead;
  }
  // A track following the arrangement plays whatever clip is placed at the
  // transport's row.
  if (info.isPlaying()) {
    auto position = info.getAbsolutePosition();
    for (auto track_id : song.getPlayableTrackIds()) {
      auto * session_track = info.getSessionTrack(track_id);
      if (session_track && session_track->isTakenOver()) continue;
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

SessionPadHighlight
SessionPlayer::clipHighlight(int track_id, int clip_index) const {
  auto & song = controller_.getSong();
  auto & playback_info = controller_.getPlaybackInfo();
  bool has_clip = hasClipAt(song.getClips(track_id), clip_index);

  // A track armed, recording or about to record shows its recording states
  // instead of the plain ones.
  bool armed = controller_.isTrackArmed(track_id);
  bool is_recording = controller_.isSessionRecording(track_id);
  auto queued_recording_it = queued_recording_.find(track_id);
  bool has_queued_recording = queued_recording_it != queued_recording_.end();
  if (armed || is_recording || has_queued_recording) {
    int recording_clip_index = is_recording ? controller_.getSessionRecordingClipIndex(track_id) : -1;
    auto queued_recording_kind = has_queued_recording ? queued_recording_it->second.kind : QueuedRecording::STOP;
    int queued_recording_clip_index = has_queued_recording ? queued_recording_it->second.clip_index : -1;
    if (is_recording && recording_clip_index == clip_index) {
      return (has_queued_recording && queued_recording_kind == QueuedRecording::STOP) ?
        SessionPadHighlight::RECORD_STOPPING : SessionPadHighlight::RECORDING;
    }
    if (has_queued_recording && queued_recording_kind != QueuedRecording::STOP && queued_recording_clip_index == clip_index) {
      return SessionPadHighlight::RECORD_QUEUED;
    }
    if (armed && !has_clip && hasStopButtonAt(song.getClips(track_id), clip_index)) return SessionPadHighlight::ARMED_EMPTY;
  }
  if (!has_clip) return SessionPadHighlight::NONE;

  // A taken-over track plays its launched clip; any other plays whatever
  // the arrangement has at the transport's position.
  auto * session_track = sessionTrack(track_id);
  bool playing = false;
  if (session_track && session_track->isTakenOver()) {
    playing = session_track->clip_index == clip_index;
  } else if (playback_info.isPlaying()) {
    playing = resolveInstanceAt(song, track_id, playback_info.getAbsolutePosition()).clip_index == clip_index;
  }
  // A launched clip stays launched while the transport is paused.
  if (playing) return playback_info.isPlaying() ? SessionPadHighlight::PLAYING : SessionPadHighlight::PAUSED;
  if (session_track && session_track->queued == clip_index) return SessionPadHighlight::QUEUED;
  return SessionPadHighlight::NONE;
}
