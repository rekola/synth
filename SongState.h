#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "EventHandler.h"
#include "Song.h"
#include "VoicePool.h"
#include "PlaybackControlEvent.h"
#include "TrackState.h"
#include "Tuner.h"
#include "TrackEventQueue.h"

#include <memory>

class SongState : public EventHandler {
 public:
  explicit SongState(int _outSampleRate) : outSampleRate(_outSampleRate) { }

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
	if (ev.getParameter1() > 0) moveForward(ev.getParameter1());
	else if (ev.getParameter1() < 0) moveBackwards(-ev.getParameter1());
      }
      break;
    case PlaybackControlEvent::CLEAR_VOICES:
      {
	auto it = track_voices.find(ev.getParameter1());
	if (it != track_voices.end()) it->second->clear();
      }
      break;
    case PlaybackControlEvent::STOP_NOTE:
      {
	auto it = track_voices.find(ev.getParameter1());
	if (it != track_voices.end()) {
	  it->second->stopVoices(ev.getParameter2());
	}
      }
      break;      
    default:
      break;
    }
  }

  size_t getTickInterval(const Song & song) const {
    return song.getSampleInterval(getOutSampleRate()) / 12;
  }
  
  bool isPlaying() const { return is_playing; }

  const size_t getAbsolutePosition() const { return absolute_pos; }
  const size_t getSamplePos() const { return sample_pos; }
    
  void moveForwardSamples(const Song & song, size_t n = 1) {
    auto sinterval = song.getSampleInterval(getOutSampleRate());

    for (size_t i = 0; i < n; i++) {
      sample_pos++;
      
      if (sample_pos == sinterval) {
	moveForward();
      }
    }
  }

  std::pair<size_t, size_t> getRelativePosition(const Song & song) const {
    std::pair<size_t, size_t> rv(0, absolute_pos);
    for (auto & pattern : song.getPatterns()) {
      if (rv.second >= pattern.getNumRows()) {
	rv.second -= pattern.getNumRows();
	rv.first++;
      } else {
	break;
      }
    }
    return rv;
  }

  size_t samplesUntilNextRow(const Song & song) const {
    auto sinterval = song.getSampleInterval(getOutSampleRate());
    return sample_pos == 0 ? sinterval : sinterval - sample_pos;    
  }
  
  void moveForward(size_t rows = 1) {
    sample_pos = 0;
    absolute_pos += rows;
  }

  void moveBackwards(size_t rows = 1) {
    sample_pos = 0;
    if (absolute_pos > rows) {
      absolute_pos -= rows;
    } else {
      absolute_pos = 0;
    }
  }
  
  VoicePool & getTrackVoices(int track_id) {
    auto it = track_voices.find(track_id);
    if (it != track_voices.end()) {
      return *(it->second);
    } else {
      auto s = std::make_unique<VoicePool>(getOutSampleRate());
      auto ptr = s.get();
      track_voices[track_id] = std::move(s);
      return *ptr;
    }
  }

  size_t getVoiceCount() const {
    size_t n = 0;
    for (auto & td : track_voices) {
      n += td.second->getVoiceCount();
    }
    return n;
  }

  size_t getAllocatedVoiceCount() const {
    size_t n = 0;
    for (auto & td : track_voices) {
      n += td.second->getAllocatedVoiceCount();
    }
    return n;
  }

  unsigned int getOutSampleRate() const { return outSampleRate; }

  TrackState & getTrackState(const Track & track) {
    auto it = track_states.find(track.getId());
    if (it != track_states.end()) return *(it->second);
    auto state = track.createState(outSampleRate);
    auto ptr = state.get();
    track_states[track.getId()] = move(state);
    return *ptr;
  }

  const std::unordered_map<int, std::unique_ptr<TrackState> > & getTrackStates() const { return track_states; }
  
  Tuner & getTuner() { return tuner; }
  TrackEventQueue & getEventQueue() { return track_events; }
  
private:
  unsigned int outSampleRate;
  bool is_playing = false;
  size_t sample_pos = 0, absolute_pos = 0;
  Tuner tuner;
  TrackEventQueue track_events;
  
  std::unordered_map<int, std::unique_ptr<VoicePool> > track_voices;
  std::unordered_map<int, std::unique_ptr<TrackState> > track_states;
};
  
#endif
