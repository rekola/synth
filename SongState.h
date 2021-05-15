#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "EventHandler.h"
#include "Song.h"
#include "HRFT.h"
#include "TrackState.h"
#include "PlaybackControlEvent.h"

#define NOTE_DOMAIN ((float)1/4)

#include <memory>

class SongState : public EventHandler {
 public:
  explicit SongState(int _outSampleRate) : outSampleRate(_outSampleRate), hrft(_outSampleRate) { }

  void handlePlaybackControlEvent(PlaybackControlEvent & ev) {
    switch (ev.getType()) {
    case PlaybackControlEvent::PLAY:
      is_playing = true;
      break;
    case PlaybackControlEvent::STOP:
      is_playing = false;
      break;
    case PlaybackControlEvent::MOVE_POSITION:
      {
	
      }
      break;
    case PlaybackControlEvent::CLEAR_VOICES:
      {
	auto it = track_states.find(ev.getParameter1());
	if (it != track_states.end()) it->second->getVoices().clear();
      }
      break;
    case PlaybackControlEvent::PLAY_NOTE:
      break;
    case PlaybackControlEvent::STOP_NOTE:
      {
	auto it = track_states.find(ev.getParameter1());
	if (it != track_states.end()) {
	  auto & track_state = it->second;
	  track_state->getVoices().stopNote(ev.getParameter2());
	}
      }
      break;
    default:
      break;
    }
  }

  size_t getSampleInterval(const Song & song) const {
    float tnote = (float)60 / song.getTempo() * NOTE_DOMAIN * 2;
    return (size_t)(tnote * getOutSampleRate());
  }

  size_t getTickInterval(const Song & song) const {
    return getSampleInterval(song) / 12;
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

  size_t samplesUntilNextRow(const Song & song) const {
    auto sinterval = getSampleInterval(song);
    return sample_pos == 0 ? sinterval : sinterval - sample_pos;    
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
  
  TrackState & getTrackState(unsigned short track_idx) {
    auto it = track_states.find(track_idx);
    if (it != track_states.end()) {
      return *(it->second);
    } else {
      auto s = std::make_unique<TrackState>(getOutSampleRate());
      auto ptr = s.get();
      track_states[track_idx] = std::move(s);
      return *ptr;
    }
  }

  size_t getVoiceCount() const {
    size_t n = 0;
    for (auto & td : track_states) {
      n += td.second->getVoices().getVoiceCount();      
    }
    return n;
  }

  size_t getAllocatedVoiceCount() const {
    size_t n = 0;
    for (auto & td : track_states) {
      n += td.second->getVoices().getAllocatedVoiceCount();
    }
    return n;
  }

  unsigned int getOutSampleRate() const { return outSampleRate; }

private:
  unsigned int outSampleRate;
  bool is_playing = true;
  size_t sample_pos = 0, track_pos = 0, pattern_pos = 0, absolute_pos = 0;

  std::unordered_map<unsigned short, std::unique_ptr<TrackState> > track_states;
  
  HRFT hrft;
};
  
#endif
 
  
