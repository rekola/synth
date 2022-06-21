#ifndef _PLAYBACKINFO_H_
#define _PLAYBACKINFO_H_

#include "TrackInfo.h"

class PlaybackInfo {
 public:
  PlaybackInfo() { }
  
  bool isPlaying() const { return is_playing; }
  const int getAbsolutePosition() const { return absolute_pos; }
  const int getPatternIndex() const { return pattern_idx; }
  const int getRowIndex() const { return row_idx; }
  const int getSamplePos() const { return sample_pos; }

  float getTime() const {
    return (float)(absolute_pos * sample_interval + sample_pos) / outSampleRate;
  }

  int getVoiceCount() const { return voice_count; }
  int getAllocatedVoiceCount() const { return allocated_voice_count; }

  const TrackInfo & getTrackInfo(int track_id) const {
    auto it = effect_info.find(track_id);
    return it != effect_info.end() ? it->second : empty_effect_info;
  }
  void setTrackInfo(int track_id, TrackInfo info) { effect_info[track_id] = info; }
  
  bool is_playing = true;
  int outSampleRate = 0;
  int sample_interval = 0;
  int sample_pos = 0, pattern_idx = 0, row_idx = 0, absolute_pos = 0;
  int voice_count = 0, allocated_voice_count = 0;

private:
  std::unordered_map<int, TrackInfo> effect_info;
  TrackInfo empty_effect_info;
};

#endif
