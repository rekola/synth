#ifndef _PLAYBACKINFO_H_
#define _PLAYBACKINFO_H_

#include "TrackInfo.h"
#include "ActiveVoiceInfo.h"

#include <vector>
#include <unordered_map>

class PlaybackInfo {
 public:
  PlaybackInfo() { }

  void setIsPlaying(bool t) { is_playing_ = t; }
  void setOutSampleRate(int outSampleRate) { outSampleRate_ = outSampleRate; }
  void setSampleInterval(int sample_interval) { sample_interval_ = sample_interval; }
  void setSamplePos(int sample_pos) { sample_pos_ = sample_pos; }
  void setPatternIdx(int pattern_idx) { pattern_idx_ = pattern_idx; }
  void setRowIdx(int row_idx) { row_idx_ = row_idx; }
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
  int getPatternIndex() const { return pattern_idx_; }
  int getRowIndex() const { return row_idx_; }
  int getSamplePos() const { return sample_pos_; }
  // sample_interval_/outSampleRate_ are both 0 in a default-constructed
  // PlaybackInfo - Controller::playback_info starts out that way and only
  // gets overwritten once the Player thread's first PlaybackEvent reaches
  // the UI (see Controller::receivePlaybackSnapshot()), so a pad/key press that
  // lands before that first event (e.g. right at startup) can still reach
  // here with a zero divisor - guard both rather than dividing by it.
  int getCurrentDelay() const { return sample_interval_ > 0 ? 256 * sample_pos_ / sample_interval_ : 0; }

  float getTime() const {
    return outSampleRate_ > 0 ? (float)(absolute_pos_ * sample_interval_ + sample_pos_) / outSampleRate_ : 0.0f;
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
  int sample_pos_ = 0, pattern_idx_ = 0, row_idx_ = 0, absolute_pos_ = 0;
  int position_edit_seq_ = 0;
  int voice_count_ = 0, allocated_voice_count_ = 0;

  std::unordered_map<int, TrackInfo> effect_info_;
  std::unordered_map<int, std::vector<ActiveVoiceInfo> > active_voices_;

  static inline TrackInfo empty_effect_info_;
  static inline std::vector<ActiveVoiceInfo> empty_active_voices_;
};

#endif
