#ifndef _PLAYBACKINFO_H_
#define _PLAYBACKINFO_H_

#include "TrackInfo.h"

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
  void setVoiceCount(int voice_count) { voice_count_ = voice_count; }
  void setAllocatedVoiceCount(int allocated_voice_count) { allocated_voice_count_ = allocated_voice_count; }

  bool isPlaying() const { return is_playing_; }  
  int getAbsolutePosition() const { return absolute_pos_; }
  int getPatternIndex() const { return pattern_idx_; }
  int getRowIndex() const { return row_idx_; }
  int getSamplePos() const { return sample_pos_; }
  int getCurrentDelay() const { return 256 * sample_pos_ / sample_interval_; }
      
  float getTime() const {
    return (float)(absolute_pos_ * sample_interval_ + sample_pos_) / outSampleRate_;
  }

  int getVoiceCount() const { return voice_count_; }
  int getAllocatedVoiceCount() const { return allocated_voice_count_; }

  const TrackInfo & getTrackInfo(int track_id) const {
    auto it = effect_info_.find(track_id);
    return it != effect_info_.end() ? it->second : empty_effect_info_;
  }
  void setTrackInfo(int track_id, TrackInfo info) { effect_info_[track_id] = std::move(info); }
    
private:
  bool is_playing_ = true;
  int outSampleRate_ = 0;
  int sample_interval_ = 0;
  int sample_pos_ = 0, pattern_idx_ = 0, row_idx_ = 0, absolute_pos_ = 0;
  int voice_count_ = 0, allocated_voice_count_ = 0;

  std::unordered_map<int, TrackInfo> effect_info_;

  static inline TrackInfo empty_effect_info_;
};

#endif
