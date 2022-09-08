#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "Song.h"
#include "TrackState.h"
#include "Tuner.h"
#include "RenderContext.h"
#include "Mixer.h"

#include <memory>

class SongState : public TrackState {
 public:
  explicit SongState(ChannelConfiguration channel_config) : TrackState(channel_config), render_context_(channel_config) { }

  void render(int frames, const Song & song, Mixer & mixer) {
    mixer.reset();
  
    if (isPlaying()) {
      for (int i = 0; i < frames; i++) {
	if (getSamplePos() == 0) {
	  auto [ pattern_idx, row_idx ] = getRelativePosition(song);
	  auto & pattern = song.getPattern(pattern_idx);
	  auto & notes = pattern.getNotes(row_idx);
	  
	  if (song.getKey() >= 0) {
	    getTuner().tune(song.getTuning(), song.getKey(), notes);
	  }
	  
	  for (auto & [ track_id, notes ] : notes) {
	    auto track = song.getTrackByInternalId(track_id);
	    auto tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();

	    for (size_t j = 0; j < notes.size(); j++) {
	      if (notes[j].isDefined()) {
		auto & note = notes[j];
		float frequency = 0.0f, velocity = 0.0f;
		if (note.isAftertouch()) {
		  velocity = note.getVelocityAsFloat();
		} else if (!note.isOff()) {
		  frequency = getTuner().getFrequency(tuning, song.getKey(), note);
		  velocity = note.getVelocityAsFloat() * (1 + song.getRandomizationFactor() * getRandF());
		}
		float delay = note.getDelayAsFloat() + song.getRandomizationFactor() * getRandF();
		auto delay_samples = int(delay * song.getSampleInterval(getChannelConfiguration().getAudioOutSampleRate()));
		render_context_.addPendingEvent(track_id, i + delay_samples, int(j), frequency, velocity);
	      }
	    }
	  }
	  auto & commands = pattern.getCommands(row_idx);
	  for (auto & [ track_id, command ] : commands) {
	    // render_context_.addPendingEvent(col, i, command);
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
	auto data = getChildState(*track).render(frames, song.getInstruments(), render_context_);
	mixer.accumulate(data);
      }
    }
    
    render_context_.updateFrameOffset(-frames);
  }

#if 0
  int getTickInterval(const Song & song) const {
    return song.getSampleInterval(channel_config.getAudioOutSampleRate()) / 12;
  }
#endif
  
  bool isPlaying() const override { return is_playing_; }
  void setIsPlaying(bool b) { is_playing_ = b; }

  int getAbsolutePosition() const { return absolute_pos_; }
  int getSamplePos() const { return sample_pos_; }
    
  void moveForwardSamples(const Song & song, int n = 1) {
    auto sinterval = song.getSampleInterval(getChannelConfiguration().getAudioOutSampleRate());

    for (int i = 0; i < n; i++) {
      sample_pos_++;
      
      if (sample_pos_ == sinterval) {
	movePosition(1);
      }
    }
  }

  std::pair<int, int> getRelativePosition(const Song & song) const {
    std::pair<int, int> rv(0, absolute_pos_);
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
    return sample_pos_ == 0 ? sinterval : sinterval - sample_pos_;
  }
  
  void movePosition(int n_rows) {
    sample_pos_ = 0;
    if (n_rows >= 0 || absolute_pos_ + n_rows >= 0) {
      absolute_pos_ += n_rows;
    } else {
      absolute_pos_ = 0;
    }
  }
  
  Tuner & getTuner() { return tuner_; }

private:
  bool is_playing_ = false;
  int sample_pos_ = 0, absolute_pos_ = 0;
  Tuner tuner_;
  RenderContext render_context_;
};
  
#endif
