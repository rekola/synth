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

  const size_t getCurrentPosition() const { return section_pos * PATTLEN + track_pos; }
  const size_t getSectionPosition() const { return section_pos; }
  const size_t getTrackPosition() const { return track_pos; }

  void moveForwardSample(const Song & song) {
    auto sinterval = getSampleInterval(song);
    if (samplepos + 1 < sinterval || track_pos + 1 < PATTLEN || section_pos + 1 < song.getSections().size()) {
      samplepos++;
      
      if (samplepos == sinterval) {
	moveForward(song);
      }
    }
  }
  
  void moveForward(const Song & song) {
    samplepos = 0;
    if (track_pos + 1 < PATTLEN) {
      track_pos++;
      absolute_pos++;
    } else if (section_pos + 1 < song.getSections().size()) {
      section_pos++;
      track_pos = 0;
      absolute_pos++;      
    }
  }

  void moveBackwards(const Song & song) {
    samplepos = 0;
    if (track_pos > 0) {
      track_pos--;
      absolute_pos--;
    } else if (section_pos > 0) {
      section_pos--;
      track_pos = PATTLEN - 1;
      absolute_pos--;
    }
  }
  
private:
  bool is_playing = false;

  size_t samplepos = 0, track_pos = 0, section_pos = 0, absolute_pos = 0;
  size_t samplerate;
};

#endif
 
