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
#include "DrumMachineTrack.h"
#include "constants.h"

#include <algorithm>
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

    // Floor-reflection parameters (ChannelConfiguration.h) - pushed into
    // this already-constructed instance's own stored copy the same way
    // main.cpp's setAudioOutSampleRate()/setAmbisonicOrder() calls
    // finalize a fresh one, since every playNote() call already threads
    // a ChannelConfiguration all the way down to voice construction.
    auto & mutable_config = getMutableChannelConfiguration();
    mutable_config.setEarHeight(song.getEarHeight());
    mutable_config.setFloorReflectionEnabled(song.getFloorReflectionEnabled());
    mutable_config.setFloorReflectionStrength(song.getFloorReflectionStrength());
    mutable_config.setGroundAbsorption(song.getGroundAbsorption());

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
		  velocity = note.getVelocityAsFloat();
		}
		auto delay_samples = int(note.getDelayAsFloat() * getChannelConfiguration().getSampleInterval(tempo_));
		int note_value = (note.isAftertouch() || note.isOff()) ? -1 : note.getValue();
		render_context_.addPendingEvent(track_id, i + delay_samples, int(j), frequency, velocity, note_value);
	      }
	    }
	  }
	  auto & commands = pattern.getCommands(row_idx);
	  for (auto & [ track_id, command ] : commands) {
	    // render_context_.addPendingEvent(col, i, command);
	    if (command.isPatternBreak()) {
	      pending_break_ = true;
	      pending_break_row_ = command.getBreakDestinationRow();
	    } else if (command.isAzimuthSlide()) {
	      scheduleAzimuthSlide(track_id, i, command.getAzimuthSlidePerTick());
	    }
	  }

	  // A DrumMachineTrack never has Pattern rows of its own (see
	  // DrumMachineTrack.h) - its notes are computed here directly from
	  // its own step data instead of read from `notes` above, in
	  // addition to (never instead of) the pattern lookup, so there's no
	  // double-triggering risk. getHitNotesForRow() is a pure function of
	  // (row_idx, this track's own loop length + steps), so this survives
	  // an arbitrary seek exactly like the pattern lookup above already
	  // does. The GM note number itself doubles as the pending-event
	  // "column" key (retriggerVoices()/chokeExclusiveClasses() only need
	  // it to be stable and unique per lane within this one track, and
	  // note-keying is this whole class's own convention - see
	  // DrumMachineTrack.h) and as note_value, matching how a percussion
	  // Pattern note's own getValue() already is its raw GM note number.
	  for (auto track_id : song.getRootTrackIds()) {
	    auto track = song.getTrackByInternalId(track_id);
	    if (!track || track->getType() != TrackType::DRUM_MACHINE) continue;
	    auto & drum_track = static_cast<const DrumMachineTrack &>(*track);

	    for (int note : drum_track.getHitNotesForRow(row_idx)) {
	      float frequency = Tuner::getFrequency(Tuning::PERCUSSION, note);
	      float velocity = constants::DEFAULT_VELOCITY / 127.0f;
	      render_context_.addPendingEvent(track_id, i, note, frequency, velocity, note);
	    }
	  }
	}
	
	auto remaining = samplesUntilNextRow();
	if (i + remaining <= frames) {
	  i += remaining;
	  if (pending_break_) {
	    pending_break_ = false;
	    jumpToPatternBreak(song, pending_break_row_);
	  } else {
	    movePosition(1);
	  }
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

      // Snapshotting the raw Track* pointers under Song::getTracksMutex()
      // rather than holding it for this whole loop - see that mutex's own
      // comment on why one is needed at all - keeps the lock held only as
      // long as a quick pointer copy takes, not for however long actually
      // rendering every track takes; a track added by the UI thread after
      // the snapshot is taken just isn't heard until next block, same as
      // a track added between two blocks outright. Safe against a track
      // added *during* iteration below reusing/reallocating one of these
      // pointers out from under it too, since track deletion doesn't
      // exist yet - every Track this snapshot points to lives at a fixed
      // address for the rest of the process once addTrack() returns.
      std::vector<Track *> track_snapshot;
      {
	std::lock_guard<std::mutex> guard(song.getTracksMutex());
	track_snapshot.reserve(song.getTracks().size());
	for (auto & track : song.getTracks()) track_snapshot.push_back(track.get());
      }

      for (auto * track : track_snapshot) {
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
    return song.normalizePosition(0, absolute_pos_);
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
    position_edit_seq_++;
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
    position_edit_seq_++;
  }

  // ZBxx (Command::isPatternBreak()) landing spot: row `dest_row` of the
  // pattern *after* whichever one `absolute_pos_` currently falls in -
  // used in place of movePosition(1) at the one place a row ever
  // completes (render()'s own row-boundary check above), so the rest of
  // the current pattern is simply never reached. Landing past the last
  // pattern behaves exactly like normal end-of-song run-off - nothing
  // special-cased.
  void jumpToPatternBreak(const Song & song, int dest_row) {
    auto len = song.getPatternLength();
    if (len <= 0) { movePosition(1); return; }
    auto pattern_idx = absolute_pos_ / len;
    auto row = dest_row < 0 ? 0 : (dest_row >= len ? len - 1 : dest_row);
    setPosition((pattern_idx + 1) * len + row);
  }

  // 2Lxx/2Rxx (Command::isAzimuthSlide()) - spreads constants::TICKS_PER_ROW
  // evenly-spaced nudges of `delta_per_tick` degrees across the row
  // currently starting at block-relative sample offset `row_start` (the
  // same block-relative numbering render()'s own note scheduling just
  // above already uses for its i+delay_samples offsets), each consumed by
  // InstrumentTrackState::render()'s chunked loop via RenderContext's
  // pending-azimuth-tick timeline. A tick landing beyond this block's own
  // remaining frames is simply carried forward by updateFrameOffset()
  // below, same as a note event scheduled near a block boundary already
  // is - so a command near the end of a block still applies all its
  // ticks, just spread across this call and the next.
  void scheduleAzimuthSlide(int track_id, int row_start, float delta_per_tick) {
    int row_samples = getChannelConfiguration().getSampleInterval(tempo_);
    int tick_interval = std::max(1, row_samples / constants::TICKS_PER_ROW);
    for (int tick = 1; tick <= constants::TICKS_PER_ROW; tick++) {
      int offset = tick * tick_interval;
      if (offset >= row_samples) break;
      render_context_.addPendingAzimuthTick(track_id, row_start + offset, delta_per_tick);
    }
  }

  // Bumped by every movePosition()/setPosition() call - lets a PlaybackInfo
  // snapshot (see Player::createPlaybackEvent()) declare how many
  // position-editing control events it reflects. Controller::
  // moveEditPosition()/setEditPosition() (the sole callers that ever push
  // MOVE_POSITION/SET_POSITION - see their own comments) bump a matching
  // local counter and compare it against this value on every incoming
  // snapshot, so a snapshot generated by the audio thread's own periodic
  // per-block render (state_.render() runs on every audio callback
  // regardless of play state) *before* it got around to draining a
  // just-pushed control event can't clobber a more recent local prediction
  // with a stale one - without this, that stale snapshot briefly winning
  // the race (arriving at the UI thread after the local update) made the
  // pattern-editor cursor visibly jump back to the old row and then jump
  // forward again once a caught-up snapshot arrived.
  int getPositionEditSeq() const { return position_edit_seq_; }
  
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
  int position_edit_seq_ = 0;
  bool pending_break_ = false; // ZBxx seen on the row currently completing
  int pending_break_row_ = 0;
  RenderContext render_context_;
  SendBusProcessor send_bus_;
  SampleData aux_a_sum_, aux_b_sum_;
};
  
#endif
