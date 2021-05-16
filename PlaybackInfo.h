#ifndef _PLAYBACKINFO_H_
#define _PLAYBACKINFO_H_

class PlaybackInfo {
 public:
  PlaybackInfo() { }
  
  bool isPlaying() const { return is_playing; }
  const size_t getAbsolutePosition() const { return absolute_pos; }
  const size_t getPatternIndex() const { return pattern_idx; }
  const size_t getRowIndex() const { return row_idx; }
  const size_t getSamplePos() const { return sample_pos; }

  float getTime() const {
    return (float)(absolute_pos * sample_interval + sample_pos) / outSampleRate;
  }

  size_t getVoiceCount() const { return voice_count; }
  size_t getAllocatedVoiceCount() const { return allocated_voice_count; }

  bool is_playing = true;
  unsigned int outSampleRate = 0;
  size_t sample_interval = 0;
  size_t sample_pos = 0, pattern_idx = 0, row_idx = 0, absolute_pos = 0;
  size_t voice_count = 0, allocated_voice_count = 0;
};

#endif
