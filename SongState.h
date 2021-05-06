#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "Song.h"
#include "HRFT.h"

#define NOTEDOMAIN ((float)1/4)

#include <memory>

class SongState {
 public:
  explicit SongState(int samplerate) : samplerate(samplerate) { }
  
  size_t getSampleRate() const { return samplerate; }
  
  size_t getSampleInterval(const Song & song) const {
    float tnote = (float)60 / song.getTempo() * NOTEDOMAIN * 2;
    return (size_t)(tnote * samplerate);
  }

  size_t getTickInterval(const Song & song) const {
    return getSampleInterval(song) / 12;
  }
      
  float gettime(const Song & song) const {
    return (float)(absolute_pos * getSampleInterval(song) + sample_pos) / samplerate;
  }
  
  bool togglePlayback() {
    is_playing = !is_playing;
    return is_playing;
  }
  bool isPlaying() const { return is_playing; }

  const size_t getAbsolutePosition() const { return absolute_pos; }
  const size_t getPatternPosition() const { return pattern_pos; }
  const size_t getTrackPosition() const { return track_pos; }
  const size_t getSamplePos() const { return sample_pos; }
    
  void moveForwardSample(const Song & song) {
    auto sinterval = getSampleInterval(song);
    if (sample_pos + 1 < sinterval || track_pos + 1 < song.getPattern(pattern_pos).getNumRows() || pattern_pos + 1 < song.getPatterns().size()) {
      sample_pos++;
      
      if (sample_pos == sinterval) {
	moveForward(song);
      }
    }
  }
  
  void moveForward(const Song & song) {
    sample_pos = 0;
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
    sample_pos = 0;
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

  Mixer & getMixer() { return hrft; }

private:
  bool is_playing = false;

  size_t sample_pos = 0, track_pos = 0, pattern_pos = 0, absolute_pos = 0;
  size_t samplerate;

  HRFT hrft;
};

#endif
 
