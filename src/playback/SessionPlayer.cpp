#include "SessionPlayer.h"

#include "PlaybackControlEvent.h"
#include "../Controller.h"
#include "../model/ArrangementOps.h"
#include "../model/Song.h"
#include "../state/PlaybackInfo.h"

#include <algorithm>

using namespace std;

namespace {
  // A row is a sixteenth at the song's tempo; <= 0 (a degenerate tempo)
  // stops the clock advancing.
  float rowDuration(const Song & song) {
    auto tempo = song.getTempo();
    return tempo > 0 ? 60.0f / 4.0f / static_cast<float>(tempo) : 0.0f;
  }

  bool hasClipAt(const vector<Clip> & clips, int clip_index) {
    return clip_index >= 0 && clip_index < static_cast<int>(clips.size()) && !clips[static_cast<size_t>(clip_index)].isEmpty();
  }
}

void
SessionPlayer::fireClipStep(int track_id, int clip_index, const Clip & clip, int step) {
  auto & song = controller_.getSong();
  auto track = song.getMasterTrack().getChildByInternalId(track_id);
  if (!track) return;
  auto length = clip.getLength() > 0 ? clip.getLength() : 1;
  auto & event_queue = controller_.getPlaybackEventQueue();
  auto buffer_name = controller_.getActiveBufferName();

  if (track->getType() == TrackType::SAMPLE) {
    if (step % length == 0) {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_SAMPLE_CLIP, buffer_name, track_id, clip_index));
    }
    return;
  }

  auto & pattern = clip.getLeafPattern();
  auto & notes = pattern.getNotes(pattern.getEffectiveRow(step, length));
  for (size_t col = 0; col < notes.size(); col++) {
    auto & note = notes[col];
    // An explicit off ends a duration the performer recorded; a plain
    // rest leaves the voice to its own envelope.
    if (note.isOff()) {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, buffer_name, track_id, static_cast<int>(col)));
      continue;
    }
    if (!note.isDefined() || note.isAftertouch()) continue;
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, buffer_name, track_id, static_cast<int>(col), note.getValue(), note.getVelocity()));
  }
}

void
SessionPlayer::triggerClip(int track_id, int clip_index) {
  auto & song = controller_.getSong();
  bool has_clip = hasClipAt(song.getClips(track_id), clip_index);
  auto * track = song.getMasterTrack().getChildByInternalId(track_id);
  bool is_sample_track = track && track->getType() == TrackType::SAMPLE;

  if (controller_.isTrackArmed(track_id) && is_sample_track) {
    triggerSampleCapture(track_id, clip_index);
    return;
  }

  if (controller_.isTrackArmed(track_id)) {
    // Pressing the slot being recorded into stops just that take.
    if (controller_.isSessionRecording(track_id) && controller_.getSessionRecordingClipIndex(track_id) == clip_index) {
      queued_recording_[track_id] = QueuedRecording{QueuedRecording::STOP};
      return;
    }
    // Nothing else launches over a take in flight.
    if (controller_.isSessionRecording(track_id)) return;
    if (!has_clip) {
      queued_recording_[track_id] = QueuedRecording{QueuedRecording::FRESH_TAKE, clip_index};
      return;
    }
    // A populated slot only launches, below.
  }

  if (!controller_.isNoteCaptureArmed()) {
    if (!has_clip) {
      // An empty slot stops a launched clip at the next bar, or cancels a
      // pending launch outright.
      if (launched_.count(track_id)) queued_[track_id] = -1;
      else queued_.erase(track_id);
      return;
    }
    // A launch never toggles: pressing the playing clip relaunches it
    // from row 0.
    queued_[track_id] = clip_index;
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
  auto section_idx = playback_info.isPlaying() ? playback_info.getPatternIndex() : assign_section_idx_;
  auto & section = song.getOrCreateSection(section_idx);
  auto raw_row = playback_info.isPlaying() ? playback_info.getRowIndex() : 0;
  placeClipInstance(song, section, track_id, quantizedBarRow(raw_row, song.getRowsPerBar()), clip_index);
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

bool
SessionPlayer::toggleOverdub(int fallback_track_id) {
  // Takes in flight stop, the clips they were recorded into keep playing.
  bool stopping = false;
  for (auto track_id : controller_.getSessionRecordingTrackIds()) {
    auto * track = controller_.getSong().getMasterTrack().getChildByInternalId(track_id);
    if (track && track->getType() == TrackType::SAMPLE) stopSampleTrackRecording(track_id);
    else queued_recording_[track_id] = QueuedRecording{QueuedRecording::STOP};
    stopping = true;
  }
  if (stopping) return true;

  vector<int> targets;
  for (auto track_id : controller_.getSong().getPlayableTrackIds()) {
    if (controller_.isTrackArmed(track_id) && launched_.count(track_id)) targets.push_back(track_id);
  }
  bool any_armed = false;
  for (auto track_id : controller_.getSong().getPlayableTrackIds()) any_armed = any_armed || controller_.isTrackArmed(track_id);
  if (!any_armed && launched_.count(fallback_track_id)) targets.push_back(fallback_track_id);
  for (auto track_id : targets) {
    auto clip_index = launched_.at(track_id).clip_index;
    if (!triggerSampleCapture(track_id, clip_index)) queued_recording_[track_id] = QueuedRecording{QueuedRecording::OVERDUB, clip_index};
  }
  return !targets.empty();
}

void
SessionPlayer::launchScene(int clip_index, const vector<int> & track_ids) {
  for (auto track_id : track_ids) triggerClip(track_id, clip_index);
}

void
SessionPlayer::stopTrack(int track_id) {
  if (controller_.isTrackArmed(track_id) && controller_.isSessionRecording(track_id)) {
    auto * track = controller_.getSong().getMasterTrack().getChildByInternalId(track_id);
    if (track && track->getType() == TrackType::SAMPLE) stopSampleTrackRecording(track_id);
    else queued_recording_[track_id] = QueuedRecording{QueuedRecording::STOP};
    return;
  }
  // Recording an arrangement, a stop has to be song data.
  if (controller_.isNoteCaptureArmed()) {
    placeRecordingStop(track_id);
    return;
  }
  if (launched_.count(track_id)) queued_[track_id] = -1;
  else queued_.erase(track_id);
}

void
SessionPlayer::stopAllTracks() {
  for (auto track_id : controller_.getSong().getPlayableTrackIds()) stopTrack(track_id);
}

void
SessionPlayer::silenceAll() {
  for (auto & [ track_id, unused ] : launched_) {
    controller_.getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, controller_.getActiveBufferName(), track_id));
  }
  clear();
}

void
SessionPlayer::clear() {
  launched_.clear();
  queued_.clear();
  queued_recording_.clear();
}

void
SessionPlayer::joinCompletedTakes(int track_id, int step) {
  auto & song = controller_.getSong();
  while (auto completed = controller_.takeCompletedSessionRecording()) {
    auto & clips = song.getClips(completed->track_id);
    if (completed->clip_index < 0 || completed->clip_index >= static_cast<int>(clips.size())) continue;
    if (completed->track_id == track_id) {
      launched_.insert_or_assign(track_id, Launched{completed->clip_index, step});
      queued_.erase(track_id);
    } else {
      queued_[completed->track_id] = completed->clip_index;
    }
  }
}

void
SessionPlayer::placeRecordingStop(int track_id) {
  auto & playback_info = controller_.getPlaybackInfo();
  if (!playback_info.isPlaying()) return;
  auto & song = controller_.getSong();
  auto row = quantizedBarRow(playback_info.getRowIndex(), song.getRowsPerBar());
  placeStopInstance(song.getOrCreateSection(playback_info.getPatternIndex()), track_id, row);
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
SessionPlayer::tick() {
  auto & song = controller_.getSong();
  auto & playback_info = controller_.getPlaybackInfo();

  // Arming an ordinary take: launched clips would otherwise sit stale
  // under the recording and resume the moment it stops. A Session view
  // take in flight on another track keeps playing.
  bool note_capture_armed = controller_.isNoteCaptureArmed();
  if (note_capture_armed && !was_note_capture_armed_ && !controller_.isAnySessionRecording()) clear();
  was_note_capture_armed_ = note_capture_armed;

  // A take finished some other way than a queued stop (disarming, say)
  // loops back from the next bar, like a press on its own pad.
  joinCompletedTakes();

  // While playing, the arrangement drives the same tracks; while armed,
  // an uncontrolled loop would play under a deliberate recording.
  bool active = !playback_info.isPlaying() && (!note_capture_armed || controller_.isAnySessionRecording());
  if (!active) {
    clock_.stop();
    return;
  }
  auto now = now_();
  if (!clock_.isRunning()) {
    // Starting over from the top, with step 0 fired at once.
    clock_.start();
    last_tick_ = now;
    advanceToStep(clock_.currentStep());
    return;
  }
  float dt = chrono::duration<float>(now - last_tick_).count();
  last_tick_ = now;
  for (int step : clock_.advance(dt, rowDuration(song))) advanceToStep(step);
}

int
SessionPlayer::quantizedStep() const {
  auto step = clock_.currentStep();
  auto row_duration = rowDuration(controller_.getSong());
  if (row_duration > 0.0f && clock_.phase() / row_duration >= 0.5f) step++;
  return step;
}

void
SessionPlayer::advanceToStep(int step) {
  auto & song = controller_.getSong();
  auto & event_queue = controller_.getPlaybackEventQueue();
  auto buffer_name = controller_.getActiveBufferName();

  // Snapshot: the loop mutates all three maps.
  vector<int> track_ids;
  for (auto & [ track_id, unused ] : launched_) track_ids.push_back(track_id);
  for (auto & [ track_id, unused ] : queued_) {
    if (find(track_ids.begin(), track_ids.end(), track_id) == track_ids.end()) track_ids.push_back(track_id);
  }
  for (auto & [ track_id, unused ] : queued_recording_) {
    if (find(track_ids.begin(), track_ids.end(), track_id) == track_ids.end()) track_ids.push_back(track_id);
  }

  auto rows_per_bar = song.getRowsPerBar();
  if (rows_per_bar <= 0) rows_per_bar = 1;

  for (auto track_id : track_ids) {
    auto & clips = song.getClips(track_id);
    auto launched_it = launched_.find(track_id);
    if (launched_it != launched_.end() &&
        (launched_it->second.clip_index < 0 || launched_it->second.clip_index >= static_cast<int>(clips.size()))) {
      // The clip list shrank under it.
      launched_.erase(launched_it);
      launched_it = launched_.end();
    }

    // A queued launch or stop waits for the next bar - never the playing
    // clip's own length, and never immediate.
    bool bar_boundary = step % rows_per_bar == 0;
    auto queued_it = queued_.find(track_id);
    if (queued_it != queued_.end() && bar_boundary) {
      auto queued_index = queued_it->second;
      queued_.erase(queued_it);
      if (queued_index < 0) {
        // A stop releases through the voices' natural tail.
        if (launched_it != launched_.end()) {
          event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, buffer_name, track_id));
          launched_.erase(launched_it);
        }
        launched_it = launched_.end();
      } else {
        launched_it = launched_.insert_or_assign(track_id, Launched{queued_index, step}).first;
      }
    }

    auto queued_recording_it = queued_recording_.find(track_id);
    if (queued_recording_it != queued_recording_.end()) {
      if (bar_boundary) {
        auto queued_value = queued_recording_it->second;
        queued_recording_.erase(queued_recording_it);
        if (queued_value.kind == QueuedRecording::STOP) {
          // A finished take loops back on the bar it ended on.
          controller_.trimSessionRecordingClip(track_id);
          joinCompletedTakes(track_id, step);
          launched_it = launched_.find(track_id);
        } else if (queued_value.kind == QueuedRecording::FRESH_TAKE) {
          // A fresh take replaces whatever this track was playing.
          if (launched_it != launched_.end()) {
            event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, buffer_name, track_id));
            launched_.erase(launched_it);
            launched_it = launched_.end();
          }
          controller_.armSessionTrackRecording(track_id, queued_value.clip_index);
        } else if (launched_it != launched_.end() && launched_it->second.clip_index == queued_value.clip_index) {
          // The clip is still playing: record into it without restarting
          // it, the take's rows lining up with its loop.
          controller_.armSessionTrackRecording(track_id, queued_value.clip_index);
          controller_.primeSessionRecordingOrigin(track_id, launched_it->second.launch_step);
        }
      }
    }

    if (launched_it == launched_.end()) continue;
    auto clip_index = launched_it->second.clip_index;
    if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) continue;
    auto & clip = clips[static_cast<size_t>(clip_index)];
    auto relative_step = step - launched_it->second.launch_step;
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    if (!clip.isLooping() && relative_step >= length) {
      // A one-shot clip has played through once.
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, buffer_name, track_id));
      launched_.erase(track_id);
      continue;
    }
    fireClipStep(track_id, clip_index, clip, relative_step);
  }

  for (auto track_id : controller_.getSessionRecordingTrackIds()) controller_.extendSessionRecordingClipIfNeeded(track_id, step);
}

unordered_map<int, SessionPlayer::Playhead>
SessionPlayer::playheads() const {
  auto & song = controller_.getSong();
  unordered_map<int, Playhead> result;
  for (auto & [ track_id, launched ] : launched_) {
    auto & clips = song.getClips(track_id);
    if (launched.clip_index < 0 || launched.clip_index >= static_cast<int>(clips.size())) continue;
    auto & clip = clips[static_cast<size_t>(launched.clip_index)];
    auto & playhead = result[track_id];
    playhead.clip_index = launched.clip_index;
    if (clock_.isRunning()) {
      playhead.row = clipPlayheadRow(clock_.currentStep(), launched.launch_step, clip.getLength(), clip.isLooping());
    }
  }
  for (auto & [ track_id, queued ] : queued_) result[track_id].queued_clip = queued;
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
    if (armed && !has_clip) return SessionPadHighlight::ARMED_EMPTY;
  }
  if (!has_clip) return SessionPadHighlight::NONE;

  // While the transport plays, what sounds is the arrangement.
  if (playback_info.isPlaying()) {
    auto & section = song.getSection(playback_info.getPatternIndex());
    return resolveInstanceAt(song, section, track_id, playback_info.getRowIndex()).clip_index == clip_index ?
      SessionPadHighlight::PLAYING : SessionPadHighlight::NONE;
  }
  auto launched_it = launched_.find(track_id);
  if (launched_it != launched_.end() && launched_it->second.clip_index == clip_index) return SessionPadHighlight::PLAYING;
  auto queued_it = queued_.find(track_id);
  if (queued_it != queued_.end() && queued_it->second == clip_index) return SessionPadHighlight::QUEUED;
  return SessionPadHighlight::NONE;
}
