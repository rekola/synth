#ifndef _SYNTH_H_
#define _SYNTH_H_

#include "Song.h"

#include <memory>

class SampleData;

class Synth {
 public:
  explicit Synth(int samplerate, unsigned char *track);
  
  SampleData play(size_t frames);
  float gettime() const {
    return (float)samplepos / srate;
  }

  bool togglePlayback() {
    is_playing = !is_playing;
    return is_playing;
  }
  bool isPlaying() const { return is_playing; }

  const size_t getTrackPosition() const { return trkpos; }
  const size_t getCurrentPosition() const { return trkpos * PATTLEN + ptrnpos; }
  const size_t getPatternPosition() const { return ptrnpos; }

  Song & getSong() { return song; }
  
private:
  bool is_playing = false;
  
  // unsigned char trkcnt;
  int sinterval, samplepos = 0, ptrnpos = 0, srate;
  size_t trkpos = 0, trkmaxlen = 0;
  
  // global delay parameters
  int delay1, delay2;
  float fd1, fd2, delaymix1, delaymix2;

  Song song;
};

#endif
 
