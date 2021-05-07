#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "Song.h"
#include "HRFT.h"
#include "InstrumentVoice.h"

#define NOTEDOMAIN ((float)1/4)

#include <memory>

class SongState {
 public:
  explicit SongState(int _outSampleRate) : outSampleRate(_outSampleRate), hrft(_outSampleRate) { }
  
  size_t getOutSampleRate() const { return outSampleRate; }
  
  size_t getSampleInterval(const Song & song) const {
    float tnote = (float)60 / song.getTempo() * NOTEDOMAIN * 2;
    return (size_t)(tnote * outSampleRate);
  }

  size_t getTickInterval(const Song & song) const {
    return getSampleInterval(song) / 12;
  }
      
  float gettime(const Song & song) const {
    return (float)(absolute_pos * getSampleInterval(song) + sample_pos) / outSampleRate;
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

  void stopNote(size_t track, size_t column) {
    auto it = voices.find(track);
    if (it != voices.end()) {
      for (auto & voice : it->second) {
	if (column == voice->getIdentifier() && voice->isPlaying()) {
	  voice->stopNote();
	}
      }
    }
  }
  
  void playNote(size_t track, size_t column, float frequency, float velocity, float detune, float delay, const Instrument & instrument) {
    auto & track_voices = voices[track];
    
    bool voice_found = false;
    for (auto & voice : track_voices) {
      if (!voice_found && !voice->isPlaying()) {
	voice->setIdentifier(column);
	voice->playNote(frequency, velocity, delay, detune);
	voice_found = true;
      } else if (column == voice->getIdentifier() && voice->isPlaying()) {
	voice->stopNote();
      }
    }
    if (!voice_found) {
      track_voices.push_back(instrument.createVoice(outSampleRate, column));
      track_voices.back()->playNote(frequency, velocity, delay, detune);
    }
  }

  std::vector<std::shared_ptr<InstrumentVoice> > & getVoices(size_t track) { return voices[track]; }

  void clearVoices() { voices.clear(); }

  size_t getVoiceCount() const {
    size_t n = 0;
    for (auto & d : voices) {
      for (auto & voice : d.second) {
	if (voice->isPlaying()) n++;
      }
    }
    return n;
  }

  size_t getAllocatedVoiceCount() const {
    size_t n = 0;
    for (auto & track_voices : voices) {
      n += track_voices.second.size();
    }
    return n;
  }

private:
  bool is_playing = false;

  size_t sample_pos = 0, track_pos = 0, pattern_pos = 0, absolute_pos = 0;
  size_t outSampleRate;

  std::unordered_map<unsigned short, std::vector<std::shared_ptr<InstrumentVoice> > > voices;

  HRFT hrft;
};

#endif
 
