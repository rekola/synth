#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "Song.h"
#include "TrackState.h"
#include "Tuner.h"
#include "TrackEventQueue.h"
#include "Mixer.h"

#include <memory>

class SongState : public TrackState {
 public:
  explicit SongState(ChannelConfiguration channel_config) : TrackState(channel_config) { }

  void render(int frames, const Song & song, Mixer & mixer) {
    mixer.reset();

    auto & track_events = getEventQueue();
  
    if (isPlaying()) {
      for (size_t i = 0; i < frames; i++) {
	if (getSamplePos() == 0) {
	  auto [ pattern_idx, row_idx ] = getRelativePosition(song);
	  auto & pattern = song.getPattern(pattern_idx);
	  auto & notes = pattern.getNotes(row_idx);
	  
	  if (song.getKey() >= 0) {
	    getTuner().tune(song.getTuning(), song.getKey(), notes);
	  }
	  
	  for (auto & [ track_id, notes ] : notes) {
	    for (size_t j = 0; j < notes.size(); j++) {
	      if (notes[j].isDefined()) {
		auto & note = notes[j];
		float frequency = 0.0f, velocity = 0.0f;
		float delay = 0;
		if (note.isAftertouch()) {
		  velocity = note.getVelocityAsFloat();
		} else if (!note.isOff()) {
		  frequency = getTuner().getFrequency(song.getTuning(), song.getKey(), note);
		  velocity = note.getVelocityAsFloat() * (1 + song.getRandomizationFactor() * rand() / RAND_MAX);
		  delay = note.getDelayAsFloat();
		}
		delay += song.getRandomizationFactor() * rand() / RAND_MAX;
		auto delay_samples = int(delay * song.getSampleInterval(getChannelConfiguration().getAudioOutSampleRate()));
		track_events.addPendingEvent(track_id, i + delay_samples, int(j), frequency, velocity);
	      }
	    }
	  }
	  auto & commands = pattern.getCommands(row_idx);
	  for (auto & [ track_id, command ] : commands) {
	    // track_events.addPendingEvent(col, i, command);
	  }
	}
	
	auto remaining = samplesUntilNextRow(song);
	if (i + remaining <= frames) {
	  i += remaining;
	  movePosition(1);
	} else {
	  i += frames;
	  moveForwardSamples(song, frames);
	}
      }
    }
    
    if (!song.getInstruments().empty()) {
      for (auto & track : song.getTracks()) {
	auto data = track->render(frames, *this, song.getInstruments(), track_events);
	if (!track->isMuted()) {
	  mixer.accumulate(data, track->getVolume());
	}
      }
    }
    
    track_events.updateFrameOffset(-frames);
  }

#if 0
  int getTickInterval(const Song & song) const {
    return song.getSampleInterval(channel_config.getAudioOutSampleRate()) / 12;
  }
#endif
  
  bool isPlaying() const override { return is_playing; }
  void setIsPlaying(bool b) { is_playing = b; }

  int getAbsolutePosition() const { return absolute_pos; }
  int getSamplePos() const { return sample_pos; }
    
  void moveForwardSamples(const Song & song, int n = 1) {
    auto sinterval = song.getSampleInterval(getChannelConfiguration().getAudioOutSampleRate());

    for (int i = 0; i < n; i++) {
      sample_pos++;
      
      if (sample_pos == sinterval) {
	movePosition(1);
      }
    }
  }

  std::pair<int, int> getRelativePosition(const Song & song) const {
    std::pair<int, int> rv(0, absolute_pos);
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

  int samplesUntilNextRow(const Song & song) const {
    auto sinterval = song.getSampleInterval(getChannelConfiguration().getAudioOutSampleRate());
    return sample_pos == 0 ? sinterval : sinterval - sample_pos;    
  }
  
  void movePosition(int n_rows) {
    sample_pos = 0;
    if (n_rows >= 0 || absolute_pos + n_rows >= 0) {
      absolute_pos += n_rows;
    } else {
      absolute_pos = 0;
    }
  }
  
  int getVoiceCount() const {
    int n = 0;
    for (auto & td : track_states) {
      n += td.second->getVoiceCount();
    }
    return n;
  }

  int getAllocatedVoiceCount() const {
    int n = 0;
    for (auto & td : track_states) {
      n += td.second->getAllocatedVoiceCount();
    }
    return n;
  }

  TrackState & getTrackState(const Track & track) {
    auto it = track_states.find(track.getId());
    if (it != track_states.end()) return *(it->second);
    auto state = track.createState(getChannelConfiguration());
    auto ptr = state.get();
    track_states[track.getId()] = std::move(state);
    return *ptr;
  }

  TrackState * getTrackState(int id) {
    auto it = track_states.find(id);
    if (it != track_states.end()) return it->second.get();
    return nullptr;    
  }

  const std::unordered_map<int, std::unique_ptr<TrackState> > & getTrackStates() const { return track_states; }
  
  Tuner & getTuner() { return tuner; }
  TrackEventQueue & getEventQueue() { return track_events; }

private:
  bool is_playing = false;
  int sample_pos = 0, absolute_pos = 0;
  Tuner tuner;
  TrackEventQueue track_events;
  
  std::unordered_map<int, std::unique_ptr<TrackState> > track_states;
};
  
#endif
