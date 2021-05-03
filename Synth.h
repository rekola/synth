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

  size_t getSampleRate() const { return samplerate; }
  
  size_t getSampleInterval(const Song & song) const {
    float tnote = (float)60 / song.getTempo() * NOTEDOMAIN * 2;
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

  const size_t getAbsolutePosition() const { return absolute_pos; }
  const size_t getPatternPosition() const { return pattern_pos; }
  const size_t getTrackPosition() const { return track_pos; }

  void moveForwardSample(const Song & song) {
    auto sinterval = getSampleInterval(song);
    if (samplepos + 1 < sinterval || track_pos + 1 < song.getPattern(pattern_pos).getNumRows() || pattern_pos + 1 < song.getPatterns().size()) {
      samplepos++;
      
      if (samplepos == sinterval) {
	moveForward(song);
      }
    }
  }
  
  void moveForward(const Song & song) {
    samplepos = 0;
    if (track_pos + 1 < song.getPattern(pattern_pos).getNumRows()) {
      track_pos++;
      absolute_pos++;
    } else if (pattern_pos + 1 < song.getPatterns().size()) {
      pattern_pos++;
      track_pos = 0;
      absolute_pos++;      
    }
  }

  void moveBackwards(const Song & song) {
    samplepos = 0;
    if (track_pos > 0) {
      track_pos--;
      absolute_pos--;
    } else if (pattern_pos > 0) {
      pattern_pos--;
      track_pos = song.getPattern(pattern_pos).getNumRows() - 1;
      absolute_pos--;
    }
  }

  void moveForward(const Song & song, size_t rows) {
    for (size_t i = 0; i < rows; i++) moveForward(song);
  }

  void moveBackwards(const Song & song, size_t rows) {
    for (size_t i = 0; i < rows; i++) moveBackwards(song);
  }

private:
  bool is_playing = false;

  size_t samplepos = 0, track_pos = 0, pattern_pos = 0, absolute_pos = 0;
  size_t samplerate;
};

#endif
 
