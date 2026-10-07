#ifndef _PLAYBACKINFO_H_
#define _PLAYBACKINFO_H_

#include "TrackInfo.h"
#include "ActiveVoiceInfo.h"
#include "LiveTrackInfo.h"
#include "../model/BarGrid.h"

#include <vector>
#include <unordered_map>

class PlaybackInfo {
 public:
  PlaybackInfo() { }

  void setIsPlaying(bool t) { is_playing_ = t; }
  void setOutSampleRate(int outSampleRate) { outSampleRate_ = outSampleRate; }
  void setSampleInterval(int sample_interval) { sample_interval_ = sample_interval; }
  void setSamplePos(int sample_pos) { sample_pos_ = sample_pos; }
  void setAbsolutePos(int absolute_pos) { absolute_pos_ = absolute_pos; }
  // See SongState::getPositionEditSeq()'s own comment - how many
  // position-editing control events (MOVE_POSITION/SET_POSITION) the audio
  // thread had actually drained by the time this snapshot was taken.
  void setPositionEditSeq(int seq) { position_edit_seq_ = seq; }
  void setVoiceCount(int voice_count) { voice_count_ = voice_count; }
  void setAllocatedVoiceCount(int allocated_voice_count) { allocated_voice_count_ = allocated_voice_count; }

  bool isPlaying() const { return is_playing_; }  
  int getAbsolutePosition() const { return absolute_pos_; }
  int getPositionEditSeq() const { return position_edit_seq_; }
  int getSamplePos() const { return sample_pos_; }
  int getSampleInterval() const { return sample_interval_; }
  // sample_interval_/outSampleRate_ are both 0 in a default-constructed
  // PlaybackInfo - Controller::playback_info starts out that way and only
  // gets overwritten once the Player thread's first PlaybackEvent reaches
  // the UI (see Controller::receivePlaybackSnapshot()), so a pad/key press that
  // lands before that first event (e.g. right at startup) can still reach
  // here with a zero divisor - guard both rather than dividing by it.
  // Zero while paused: the position inside a row is only meaningful while rows advance.
  int getCurrentDelay() const { return is_playing_ && sample_interval_ > 0 ? 256 * sample_pos_ / sample_interval_ : 0; }

  // The round-trip latency of live input - the playback queue plus the
  // capture queue, plus whatever monitored input is waiting - while
  // capture runs (recording, threshold-armed or monitoring), else -1.
  // Nominal means it couldn't be measured and is the configured buffering
  // instead.
  void setRoundTripLatency(int frames, bool nominal) { round_trip_latency_frames_ = frames; latency_is_nominal_ = nominal; }
  int getRoundTripLatencyFrames() const { return round_trip_latency_frames_; }
  bool isRoundTripLatencyNominal() const { return latency_is_nominal_; }
  // Rounded to milliseconds at the output rate, or -1 while there's none.
  int getRoundTripLatencyMs() const {
    if (round_trip_latency_frames_ < 0 || outSampleRate_ <= 0) return -1;
    return static_cast<int>((static_cast<long long>(round_trip_latency_frames_) * 1000 + outSampleRate_ / 2) / outSampleRate_);
  }

  int getVoiceCount() const { return voice_count_; }
  int getAllocatedVoiceCount() const { return allocated_voice_count_; }

  const TrackInfo & getTrackInfo(int track_id) const {
    auto it = effect_info_.find(track_id);
    return it != effect_info_.end() ? it->second : empty_effect_info_;
  }
  void setTrackInfo(std::unordered_map<int, TrackInfo> info) { effect_info_ = std::move(info); }

  const std::vector<ActiveVoiceInfo> & getActiveVoices(int track_id) const {
    auto it = active_voices_.find(track_id);
    return it != active_voices_.end() ? it->second : empty_active_voices_;
  }
  void setActiveVoices(std::unordered_map<int, std::vector<ActiveVoiceInfo> > voices) { active_voices_ = std::move(voices); }

  // Live View's launched clips (SongState::getLiveTracks()) - the
  // tracks taken over from the arrangement and what's queued for them.
  const LiveTracks & getLiveTracks() const { return live_tracks_; }
  void setLiveTracks(LiveTracks tracks) { live_tracks_ = std::move(tracks); }
  const LiveTrackInfo * getLiveTrack(int track_id) const {
    auto it = live_tracks_.find(track_id);
    return it != live_tracks_.end() ? &it->second : nullptr;
  }
  // The live clock at the current row - rows played, unaffected by
  // seeks and pattern breaks - that launched clips advance on.
  int getLiveClock() const { return live_clock_; }
  void setLiveClock(int clock) { live_clock_ = clock; }
  // The live clock when the transport last started - where a UI that
  // first sees the transport playing a few rows in picks up from.
  int getLiveStartClock() const { return live_start_clock_; }
  void setLiveStartClock(int clock) { live_start_clock_ = clock; }
  // The tempo and the running time signature the audio thread is playing
  // with (SongState::queueSceneChange() sets them), and how many scene
  // changes it had applied when this snapshot was taken - what the UI
  // mirrors into the song.
  int getTempo() const { return tempo_; }
  void setTempo(int tempo) { tempo_ = tempo; }
  const RunningBars & getRunningBars() const { return running_bars_; }
  void setRunningBars(RunningBars running) { running_bars_ = running; }
  int getSceneSeq() const { return scene_seq_; }
  void setSceneSeq(int seq) { scene_seq_ = seq; }
  // The last Live View change the audio thread had applied when this
  // snapshot was taken - see ClipPlayer's own prediction of it.
  int getLiveSeq() const { return live_seq_; }
  void setLiveSeq(int seq) { live_seq_ = seq; }

private:
  // SongState::is_playing_ (the real, audio-thread-owned state this
  // mirrors) starts false - playback is stopped at launch. Before the
  // Player thread's first PlaybackEvent reaches the UI and calls
  // setIsPlaying() (see Controller::receivePlaybackSnapshot()), this default is
  // what every isPlaying() check in the UI thread sees - defaulting to
  // true made the very first keypress after startup (before that first
  // sync) see the transport as already playing: a first note entry's own
  // "if (!info.isPlaying())" cursor-advance push got silently skipped,
  // and Controller::togglePlaying()'s own synchronous flip-and-push would
  // have sent STOP instead of PLAY on the very first Space press.
  bool is_playing_ = false;
  int outSampleRate_ = 0;
  int sample_interval_ = 0;
  int sample_pos_ = 0, absolute_pos_ = 0;
  int position_edit_seq_ = 0;
  int voice_count_ = 0, allocated_voice_count_ = 0;
  int round_trip_latency_frames_ = -1;
  bool latency_is_nominal_ = false;
  LiveTracks live_tracks_;
  int live_clock_ = 0;
  int live_start_clock_ = 0;
  int live_seq_ = 0;
  int tempo_ = 0;
  RunningBars running_bars_;
  int scene_seq_ = 0;

  std::unordered_map<int, TrackInfo> effect_info_;
  std::unordered_map<int, std::vector<ActiveVoiceInfo> > active_voices_;

  static inline TrackInfo empty_effect_info_;
  static inline std::vector<ActiveVoiceInfo> empty_active_voices_;
};

#endif
