#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "Song.h"
#include "TrackState.h"
#include "Tuner.h"
#include "RenderContext.h"
#include "Mixer.h"
#include "bus/SendBusProcessor.h"

#include <memory>

class SongState : public TrackState {
 public:
  explicit SongState(ChannelConfiguration channel_config) : TrackState(channel_config), render_context_(channel_config), send_bus_(channel_config) { }

  void initialize(const Song & song) {
    tempo_ = song.getTempo();
    render_context_.setBpm(tempo_);
    send_bus_.setReverbParameters(song.getReverbSize(), song.getReverbDecay(), song.getReverbDamping(), song.getReverbPreDelay(), song.getReverbWet());
  }
  
  void render(int frames, const Song & song, Mixer & mixer) {
    mixer.reset();
  
    if (isPlaying()) {
      for (int i = 0; i < frames; i++) {
	if (getSamplePos() == 0) {
	  auto [ pattern_idx, row_idx ] = getRelativePosition(song);
	  auto & pattern = song.getPattern(pattern_idx);
	  auto & notes = pattern.getNotes(row_idx);

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
		  frequency = Tuner::getFrequency(tuning, note);
		  velocity = note.getVelocityAsFloat() * (1 + song.getRandomizationFactor() * getRandF());
		}
		float delay = note.getDelayAsFloat() + song.getRandomizationFactor() * getRandF();
		auto delay_samples = int(delay * getChannelConfiguration().getSampleInterval(tempo_));
		int note_value = (note.isAftertouch() || note.isOff()) ? -1 : note.getValue();
		render_context_.addPendingEvent(track_id, i + delay_samples, int(j), frequency, velocity, note_value);
	      }
	    }
	  }
	  auto & commands = pattern.getCommands(row_idx);
	  for (auto & [ track_id, command ] : commands) {
	    // render_context_.addPendingEvent(col, i, command);
	  }
	}
	
	auto remaining = samplesUntilNextRow();
	if (i + remaining <= frames) {
	  i += remaining;
	  movePosition(1);
	} else {
	  // The next row boundary doesn't fall within this block - advance by
	  // however many samples are actually left in it (frames - i), not by
	  // a full `frames` again. Reusing `frames` here double-counted the
	  // `i` samples already consumed earlier in this same loop (e.g. by a
	  // previous row transition partway through the block), advancing
	  // sample_pos_ too far and drifting note timing later in the song -
	  // by design a row's sample-length is essentially never an exact
	  // multiple of the block size, so this fires on nearly every block
	  // that contains (or follows) a row transition.
	  moveForwardSamples(frames - i);
	  break;
	}
      }
    }
    
    if (!song.getInstruments().empty()) {
      if (send_a_sum_.numberOfFrames() != frames) send_a_sum_ = SampleData(1, frames);
      if (send_b_sum_.numberOfFrames() != frames) send_b_sum_ = SampleData(1, frames);
      send_a_sum_.zero();
      send_b_sum_.zero();

      for (auto & track : song.getTracks()) {
	auto data = track->getState(*this).render(frames, song.getInstruments(), render_context_);
	mixer.accumulate(data);

	if (auto * a = data.getChannel(Channel::SendA)) {
	  auto dst = send_a_sum_.getChannelData(0);
	  for (int i = 0; i < frames; i++) dst[i] += a[i];
	  send_a_sum_.setNonZero();
	}
	if (auto * b = data.getChannel(Channel::SendB)) {
	  auto dst = send_b_sum_.getChannelData(0);
	  for (int i = 0; i < frames; i++) dst[i] += b[i];
	  send_b_sum_.setNonZero();
	}
      }

      // The send bus's own output is always ambisonic-shaped (see
      // SendBusProcessor) and the top-level mixer is guaranteed to be one
      // too now (BasicMixer is retired) - so this is a plain, unconditional
      // accumulate, no decode step. The isAmbisonic() guard isn't really a
      // conceptual "is this truly ambisonic" question - it exists solely so
      // the one synthetic top-level MONO config a Compressor regression
      // test constructs directly (bypassing MixerFactory) skips the send
      // bus entirely, rather than handing a genuine 1-channel
      // ambisonic_channels_ buffer to encodeStereoAsPoints(), which asserts
      // out.numberOfChannels() >= 2 on shape alone.
      if (getChannelConfiguration().isAmbisonic()) {
	// Always processed, even when both sums are silent, so the shared
	// reverb tail/chorus modulation stay continuous across blocks (see
	// SendBusProcessor.h).
	send_bus_.process(send_a_sum_, send_b_sum_, frames);
	mixer.accumulate(send_bus_.getBusAmbisonic());
      }
    }

    render_context_.updateFrameOffset(-frames);
  }

#if 0
  int getTickInterval() const {
    return getSampleInterval() / 12;
  }
#endif
  
  bool isPlaying() const { return is_playing_; }
  void setIsPlaying(bool b) { is_playing_ = b; }

  // The playback position is a raw row count accumulated across however
  // long the previous song played; it means nothing against a different
  // song's (likely much shorter) pattern list, so it must be reset whenever
  // the underlying Song is swapped out from under this state.
  void resetPosition() { sample_pos_ = 0; absolute_pos_ = 0; }

  int getAbsolutePosition() const { return absolute_pos_; }
  int getSamplePos() const { return sample_pos_; }
    
  void moveForwardSamples(int n = 1) {
    auto sinterval = getChannelConfiguration().getSampleInterval(tempo_);

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

  int samplesUntilNextRow() const {
    auto sinterval = getChannelConfiguration().getSampleInterval(tempo_);
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
  
  int getTempo() const { return tempo_; }

  // Raw, pre-send-bus-processing per-block sums (mono) - used by the UI's
  // raw-channel volume meter to show SendA/SendB levels before they're
  // folded into the shared reverb/chorus wet signal.
  const SampleData & getSendASum() const { return send_a_sum_; }
  const SampleData & getSendBSum() const { return send_b_sum_; }

private:
  int tempo_ = 0;
  bool is_playing_ = false;
  int sample_pos_ = 0, absolute_pos_ = 0;
  RenderContext render_context_;
  SendBusProcessor send_bus_;
  SampleData send_a_sum_, send_b_sum_;
};
  
#endif
