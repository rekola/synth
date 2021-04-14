#ifndef _SYNTH_H_
#define _SYNTH_H_

#include "Song.h"

#include <memory>

class SampleData;

class Synth {
 public:
  explicit Synth(int samplerate) : samplerate(samplerate) { }
  
  SampleData play(Song & song, size_t frames);

  float gettime() const {
    return (float)complete_pos / samplerate;
  }
  
  bool togglePlayback() {
    is_playing = !is_playing;
    return is_playing;
  }
  bool isPlaying() const { return is_playing; }

  const size_t getTrackPosition() const { return trkpos; }
  const size_t getCurrentPosition() const { return trkpos * PATTLEN + ptrnpos; }
  const size_t getPatternPosition() const { return ptrnpos; }
  
private:
  bool is_playing = false;

  size_t samplepos = 0, ptrnpos = 0, trkpos = 0, complete_pos = 0;
  size_t samplerate;
};

#endif
 
