#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "Song.h"
#include "TrackState.h"
#include "Tuner.h"
#include "RenderContext.h"
#include "Mixer.h"
#include "bus/SendBusProcessor.h"
#include "bus/BusEffectRegistry.h"
#include "MemoryParameterSource.h"

#include <memory>

class SongState : public TrackState {
 public:
  explicit SongState(ChannelConfiguration channel_config) : TrackState(channel_config), render_context_(channel_config), send_bus_(channel_config) { }

  // Load-time-only slot instantiation (see the bus-slot project-file
  // plan): for each of Song's two slots (Song.h's getBusSlot()/
  // getBusSlotKind(), placeholder-sample-rate instances that exist only
  // to own their own parameters), construct a *fresh*, correctly-
  // sample-rated BusEffect via the registry and round-trip the Song
  // slot's parameters into it through a MemoryParameterSource -
  // deviation-only storeParameters() writes only what differs from that
  // type's own construction defaults, and loadParameters() falls back to
  // the (identical, since both were built from the same registry factory)
  // construction default for anything not written, so the round-trip is
  // exact without needing per-type dispatch here. Installed into
  // send_bus_ once via setSlotEffect() - never reconfigured again for the
  // lifetime of this SongState (no runtime slot swapping - out of scope
  // per the plan).
  void initialize(const Song & song) {
    tempo_ = song.getTempo();
    render_context_.setBpm(tempo_);

    int real_sample_rate = getChannelConfiguration().getAudioOutSampleRate();
    float row_duration = getChannelConfiguration().getRowDuration(tempo_);

    for (int slot = 0; slot < 2; slot++) {
      auto & descriptor = findBusEffectDescriptor(song.getBusSlotKind(slot));
      auto effect = descriptor.factory(real_sample_rate);

      MemoryParameterSource params;
      song.getBusSlot(slot).storeParameters(params);
      effect->loadParameters(params);

      effect->setRowDuration(row_duration); // no-op except for MultiTapDelay

      send_bus_.setSlotEffect(slot, std::move(effect));
    }
  }
  
  void render(int frames, const Song & song, Mixer & mixer) {
    mixer.reset();
  
    if (isPlaying()) {
      for (int i = 0; i < frames; i++) {
	// recording_muted_: a live-hold recording session (Launchpad/
	// keyboard auto-play-while-held - see LaunchpadManager::
	// onRowAdvanced()/PatternEditor::onRowAdvanced() and their own
	// SET_RECORDING_MUTE push) still needs the transport genuinely
	// advancing rows in real time underneath (that's what makes the
	// whole-row-clear feature's timing correct), but must not let the
	// song's own already-recorded pattern content spawn new voices
	// while doing so - otherwise old, not-yet-cleared notes at rows
	// the playhead sweeps through get audibly triggered before the
	// UI thread's own (necessarily reactive, and thus slightly
	// delayed) clear catches up. Skipping just this scheduling step
	// leaves the position-advance logic below completely untouched,
	// and doesn't touch the entirely separate PLAY_NOTE/STOP_NOTE/
	// NOTE_PRESSURE path the live performance itself is heard
	// through - so recording mute is inaudible for anything the
	// player is actually doing, only for the song's own old content.
	if (getSamplePos() == 0 && !recording_muted_) {
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
      if (aux_a_sum_.numberOfFrames() != frames) aux_a_sum_ = SampleData(1, frames);
      if (aux_b_sum_.numberOfFrames() != frames) aux_b_sum_ = SampleData(1, frames);
      aux_a_sum_.zero();
      aux_b_sum_.zero();

      for (auto & track : song.getTracks()) {
	auto data = track->getState(*this).render(frames, song.getInstruments(), render_context_);
	mixer.accumulate(data);

	if (auto * a = data.getChannel(Channel::AuxA)) {
	  auto dst = aux_a_sum_.getChannelData(0);
	  for (int i = 0; i < frames; i++) dst[i] += a[i];
	}
	if (auto * b = data.getChannel(Channel::AuxB)) {
	  auto dst = aux_b_sum_.getChannelData(0);
	  for (int i = 0; i < frames; i++) dst[i] += b[i];
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
	send_bus_.process(aux_a_sum_, aux_b_sum_, frames);
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

  // See render()'s own comment on recording_muted_'s use - set/cleared by
  // PlaybackControlEvent::SET_RECORDING_MUTE, pushed by LaunchpadManager/
  // PatternEditor alongside their own auto-play-while-held engage/
  // disengage (Controller::togglePlaying()) calls.
  void setRecordingMuted(bool b) { recording_muted_ = b; }

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

  // Absolute counterpart to movePosition() above - jumps to an exact row
  // (PlaybackInfo::getAbsolutePosition()'s own units) regardless of
  // whatever absolute_pos_ has drifted to by the time this actually
  // processes. Needed anywhere a UI-thread snapshot decides "land one row
  // past what I just saw": that snapshot is already stale by some
  // unknown amount by the time this event reaches the audio thread (this
  // one keeps rendering in real time the whole time such an event is in
  // flight), so a *relative* movePosition(1) issued from the UI thread
  // moves relative to whatever position has since drifted to, not
  // relative to the row the UI thread actually saw - occasionally
  // landing a row or more further than intended. An absolute target
  // sidesteps that entirely. See LaunchpadManager/PatternEditor's own
  // auto-stop-after-recording code for the concrete case this fixed.
  void setPosition(int absolute_row) {
    sample_pos_ = 0;
    absolute_pos_ = absolute_row < 0 ? 0 : absolute_row;
  }
  
  int getTempo() const { return tempo_; }

  // Raw, pre-send-bus-processing per-block sums (mono) - used by the UI's
  // raw-channel volume meter to show AuxA/AuxB levels before they're
  // folded into the shared reverb/chorus wet signal.
  const SampleData & getAuxASum() const { return aux_a_sum_; }
  const SampleData & getAuxBSum() const { return aux_b_sum_; }

private:
  int tempo_ = 0;
  bool is_playing_ = false;
  bool recording_muted_ = false;
  int sample_pos_ = 0, absolute_pos_ = 0;
  RenderContext render_context_;
  SendBusProcessor send_bus_;
  SampleData aux_a_sum_, aux_b_sum_;
};
  
#endif
