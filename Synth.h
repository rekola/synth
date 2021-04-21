#ifndef _SYNTH_H_
#define _SYNTH_H_

#include "Song.h"

#define NOTEDOMAIN ((float)1/4)

#include <memory>

class SampleData;

class Synth {
 public:
  explicit Synth(int samplerate) : samplerate(samplerate) { }
  
  SampleData play(Song & song, size_t frames);

  size_t getSampleInterval(const Song & song) const {
    float tnote = (float)60 / song.bpm * NOTEDOMAIN * 2;
    return (size_t)(tnote * samplerate);
  }
      
  float gettime(const Song & song) const {
    return (float)(absolute_pos * getSampleInterval(song) + samplepos) / samplerate;
  }
  
  bool togglePlayback() {
    is_playing = !is_playing;
    return is_playing;
  }
  bool isPlaying() const { return is_playing; }

  const size_t getTrackPosition() const { return trkpos; }
  const size_t getCurrentPosition() const { return trkpos * PATTLEN + ptrnpos; }
  const size_t getPatternPosition() const { return ptrnpos; }

  void moveForwardSample(const Song & song) {
    auto sinterval = getSampleInterval(song);
    if (samplepos + 1 < sinterval || ptrnpos + 1 < PATTLEN || trkpos + 1 < song.getSections().size()) {
      samplepos++;
      
      if (samplepos == sinterval) {
	moveForward(song);
      }
    }
  }
  
  void moveForward(const Song & song) {
    samplepos = 0;
    if (ptrnpos + 1 < PATTLEN) {
      ptrnpos++;
      absolute_pos++;
    } else if (trkpos + 1 < song.getSections().size()) {
      trkpos++;
      ptrnpos = 0;
      absolute_pos++;      
    }
  }

  void moveBackwards(const Song & song) {
    samplepos = 0;
    if (ptrnpos > 0) {
      ptrnpos--;
      absolute_pos--;
    } else if (trkpos > 0) {
      trkpos--;
      ptrnpos = PATTLEN - 1;
      absolute_pos--;
    }
  }
  
private:
  bool is_playing = false;

  size_t samplepos = 0, ptrnpos = 0, trkpos = 0, absolute_pos = 0;
  size_t samplerate;
};

#endif
 
