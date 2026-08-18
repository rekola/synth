#include "Player.h"
#include "../audio/AudioAPI.h"
#include "../Controller.h"
#include "../util/Logger.h"
#include "../instruments/Tuner.h"
#include "../model/InstrumentTrack.h"

#include "LogEvent.h"
#include "PlaybackEvent.h"
#include "RecordEvent.h"
#include "PlaybackControlEvent.h"
#include "AudioBlockEvent.h"

#include "../ambisonic/MixerFactory.h"
#include "../state/InstrumentTrackState.h"
#include "../state/NoteOrigin.h"
#include "../model/NoteCoordinate.h"

using namespace std;

class EventLogger : public Logger {
 public:
  EventLogger(EventQueue * _event_queue) : event_queue(_event_queue) { }

  void log(std::string s) override {
    event_queue->push(make_unique<LogEvent>(std::move(s)));
  }

private:
  EventQueue * event_queue;
};

void
Player::handlePlaybackControlEvent(PlaybackControlEvent & ev) {
  // getCurrentSong(), not getSong() - this runs on the audio thread, so a
  // plain reference into whatever the active buffer happens to be right
  // now isn't safe against a concurrent UI-thread openSong()/
  // switchToBuffer() reassigning it - see getCurrentSong()'s own comment.
  // Kept alive for this whole call via song_ptr; song itself stays a
  // plain reference so every existing song.foo() call below is unchanged.
  auto song_ptr = controller_->getCurrentSong();
  auto & song = *song_ptr;

  switch (ev.getType()) {
  case PlaybackControlEvent::PLAY_NOTE:
  case PlaybackControlEvent::NOTE_PRESSURE:
    {
      auto track_id = ev.getParameter1();
      auto column = ev.getParameter2();
      auto midi_note = ev.getParameter3();
      auto midi_velocity = ev.getParameter4();
      
      auto track = song.getTrackByInternalId(track_id);
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL ||
		    track->getType() == TrackType::PERCUSSION_CONTROL ||
		    track->getType() == TrackType::DRUM_MACHINE
		    )) {
	auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
	
	if (instrument_track.getInstrumentId() >= 0 && instrument_track.getInstrumentId() < static_cast<int>(song.getInstruments().size())) {
	  auto & instrument = song.getInstrument(instrument_track.getInstrumentId());
	  auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(instrument_track.getInternalId()));

	  if (track_state) {
	    // InstrumentTrackState::noteOn()/notePressure() (PLAY_NOTE/
	    // NOTE_PRESSURE handling, shared by Kitty-keyboard note entry and
	    // Launchpad NOTES/step-grid presses) are virtual - a plain track
	    // spawns/updates a voice directly, an ArpeggiatorState
	    // (Arpeggiator.h's own track kind, reached the same way any other
	    // InstrumentTrackState is) routes them into its stepper's held
	    // chord instead (plans/arpeggiator.md) - so this call site never
	    // needs to know which kind of track it's talking to.
	    if (ev.getType() == PlaybackControlEvent::PLAY_NOTE) {
	      auto tuning = (track->getType() == TrackType::PERCUSSION_CONTROL || track->getType() == TrackType::DRUM_MACHINE) ? Tuning::PERCUSSION : song.getTuning();
	      Note note(midi_note, midi_velocity);
	      auto frequency = Tuner::getFrequency(tuning, note);

	      // A live note has no authored (scene, row) position to build a
	      // real NoteCoordinate from - live_note_counter_ (this Player's
	      // own, advanced once per live note-on) stands in for
	      // absolute_row instead, so InstrumentVoice can still derive a
	      // decorrelated start phase for it the same way a pattern note's
	      // real coordinate does (see Player.h's own comment on why this
	      // counter's monotonic growth is fine here, unlike everywhere
	      // else this migration cares about reproducibility).
	      track_state->noteOn(column, instrument, frequency, note.getVelocityAsFloat(), note.getValue(), NoteOrigin::LIVE,
				   NoteCoordinate(instrument_track.getInternalId(), live_note_counter_++, column));
	    } else {
	      track_state->notePressure(column, midi_velocity / 127.0f);
	    }
	  }
	}
      }
    }
    break;
    
  case PlaybackControlEvent::TERMINATE:
    terminate_ = true;
    break;

  case PlaybackControlEvent::PLAY:
    state_.setIsPlaying(true);

    // Re-locks a track's own internal clock (e.g. ArpeggiatorState's step
    // timer - see TrackState::resyncPlayhead()) to the transport every time
    // playback actually (re-)starts *and* the position actually moved while
    // stopped (SongState::resyncPlayheadAfterStop()'s own comment) - not on
    // every SET_POSITION/MOVE_POSITION edit below, which also fires on
    // plain cursor navigation while stopped (see that case's own comment)
    // and would otherwise resync on every such keypress, and not on a
    // plain pause/resume at the same row either, which needs no correction.
    state_.resyncPlayheadAfterStop();
    break;

  case PlaybackControlEvent::STOP:
    state_.setIsPlaying(false);
    state_.notePlaybackStopped(); // snapshot for resyncPlayheadAfterStop() above, next PLAY
    break;
    
  case PlaybackControlEvent::MOVE_POSITION:
    // Mirrors Controller::moveEditPosition()'s own decision on the
    // UI-thread side - both derive the same result independently from the
    // same (unclamped) delta_rows rather than one side trusting a value
    // computed by the other across the thread boundary. parameter2 carries
    // whether a pattern-editor selection was open there (clamp to the
    // current pattern, via clampRowToCurrentPattern()) or not (cross
    // pattern boundaries freely - setPosition() already floors at 0, and
    // there's no upper bound either way, same as real playback's own
    // run-off-the-end). Only ever reaches here while stopped (see
    // PatternEditor's "Row navigation while stopped" comment); real
    // playback's row-by-row advance goes through SongState::renderBlock()'s
    // own movePosition()/jumpToPatternBreak() calls, never this event.
    state_.setPosition(ev.getParameter2() ?
      song.clampRowToCurrentPattern(state_.getAbsolutePosition(), state_.getAbsolutePosition() + ev.getParameter1()) :
      state_.getAbsolutePosition() + ev.getParameter1());
    break;

  case PlaybackControlEvent::SET_POSITION:
    // See MOVE_POSITION just above - same reasoning, mirroring Controller::
    // setEditPosition().
    state_.setPosition(ev.getParameter2() ?
      song.clampRowToCurrentPattern(state_.getAbsolutePosition(), ev.getParameter1()) : ev.getParameter1());
    break;

  case PlaybackControlEvent::CLEAR_VOICES:
    state_.removeChild(ev.getParameter1());
    break;
    
  case PlaybackControlEvent::STOP_NOTE:
    {
      // noteOff() is virtual for the same reason noteOn()/notePressure()
      // above are - see that case's own comment.
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->noteOff(ev.getParameter2());
    }
    break;

  case PlaybackControlEvent::CHANNEL_PRESSURE:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->applyRealChannelPressure(ev.getParameter2() / 127.0f);
    }
    break;

  case PlaybackControlEvent::SET_RECORDING_MUTE:
    state_.setRecordingMuted(ev.getParameter1() != 0);
    break;

  case PlaybackControlEvent::SET_TRACK_MUTED:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setMuted(ev.getParameter2() != 0);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SOLO:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSolo(ev.getParameter2() != 0);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_A:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendA(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_B:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendB(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_MAIN:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendMain(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_AZIMUTH:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setAzimuth(ev.getParameter2() / 10.0f);
    }
    break;

  case PlaybackControlEvent::SONG_CHANGED:
    song_changed_ = true;
    break;

  case PlaybackControlEvent::MIXER_CHANGED:
    mixer_changed_ = true;
    break;
  }
}

void
Player::play(AudioAPI & audio) {
  EventLogger logger(&(controller_->getUIEventQueue()));
  
  auto & event_queue = controller_->getPlaybackEventQueue();
      
  size_t num_playback_desc = audio.getPlaybackDescriptors().size();
  size_t num_capture_desc = audio.getCaptureDescriptors().size();
  
  size_t num_descriptors = 1 + num_playback_desc + num_capture_desc;

  auto descriptors = std::make_unique<pollfd[]>(num_descriptors);

  descriptors[0].fd = event_queue.getPollFd();
  descriptors[0].events = POLLIN;
  
  for (size_t i = 0; i < num_playback_desc; i++) {
    descriptors[1 + i] = audio.getPlaybackDescriptors()[i];
  }

  for (size_t i = 0; i < num_capture_desc; i++) {
    descriptors[1 + num_playback_desc + i] = audio.getCaptureDescriptors()[i];
  }

  // Capture's own negotiated .events (POLLIN) - stashed so it can be
  // restored below. The capture descriptors otherwise stay in the poll set
  // with .events cleared to 0 (poll() then never reports on them, so they
  // never contribute a spurious wakeup) until the user actually activates
  // recording (Controller::isRecording(), set by PatternEditor's Ctrl+R) -
  // recording is no longer engaged automatically just because playback
  // started. Clearing .events rather than dropping the capture fds from
  // the array entirely also sidesteps a busy-loop risk: an ALSA capture
  // stream that's open but never snd_pcm_start()ed (see
  // AlsaAudio::startRecording()) sits with a frozen hw pointer, so its
  // avail-derived "ready" condition would otherwise stay permanently true
  // and poll() would never actually block on it.
  auto capture_events = std::make_unique<short[]>(num_capture_desc);
  for (size_t i = 0; i < num_capture_desc; i++) {
    capture_events[i] = descriptors[1 + num_playback_desc + i].events;
    descriptors[1 + num_playback_desc + i].events = 0;
  }

  // getCurrentSong() (not getSong()/a raw pointer) - see that method's own
  // comment: it keeps whatever Song this shared_ptr copy points to alive
  // for as long as this thread holds it, regardless of a UI-thread
  // openSong()/switchToBuffer() reassigning Controller's own active buffer
  // out from under it in the meantime.
  auto song = controller_->getCurrentSong();
  state_.initialize(*song);
  auto mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());

  while ( !terminate_ ) {
    bool recording = controller_->isRecording();
    for (size_t i = 0; i < num_capture_desc; i++) {
      descriptors[1 + num_playback_desc + i].events = recording ? capture_events[i] : 0;
    }

    if (poll(descriptors.get(), num_descriptors, 1000) > 0) {
      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (i == 0) {
	    auto event = event_queue.pop();
	    handleEvent(*event);
	    while ( event_queue.hasEvents() ) {
	      auto next_event = event_queue.pop();
	      handleEvent(*next_event);
	    }
	    if (song_changed_) {
	      // Drops this thread's own shared_ptr copy of the old Song (see
	      // getCurrentSong()'s own comment) only now, in favor of a fresh
	      // copy of whatever active buffer the UI thread's own openSong()/
	      // switchToBuffer() already reassigned - the old Song stays
	      // alive up to exactly this point, however long after the UI
	      // thread itself moved on.
	      song = controller_->getCurrentSong();
	      state_.clear();
	      state_.resetPosition(); // old song's row count means nothing against the new song's patterns
	      state_.initialize(*song);
	      // No mixer rebuild here (unlike MIXER_CHANGED, below) - the
	      // mixer only depends on channel_config/mixer_type/legacy-binaural
	      // (process-wide device/decoder settings), never on which song is
	      // active, so recreating it on every buffer switch was pure
	      // waste - and, with the default binaural decoder in particular,
	      // expensive enough (it re-solves MagLS against the full measured
	      // HRTF grid at construction time - see AmbisonicMagLSDecoder's
	      // own header comment) to blow the real-time deadline on this
	      // thread and XRUN on every single switch.
	      song_changed_ = false;
	    }
	    if (mixer_changed_) {
	      mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());
	      mixer_changed_ = false;
	    }
	    auto ev = createPlaybackEvent(*song, state_);
	    controller_->getUIEventQueue().push(move(ev));
	  } else if (i - 1 < num_playback_desc) {
	    state_.renderBlock(audio.getFrameCount(), *song, *mixer);
	    auto master = mixer->encode();
	    audio.play(master, logger);

	    auto ev = createPlaybackEvent(*song, state_);
	    controller_->getUIEventQueue().push(move(ev));

	    // Hand master, the full raw ambisonic bus, and the two aux sums
	    // off to VisualizationThread - its own dedicated thread, decoupled
	    // from both this real-time audio thread and the UI thread (see
	    // VisualizationThread.h) - for spectrum-FFT, DirAC directional
	    // analysis, and the raw-channel volume meter's loudness scan
	    // respectively (see AudioBlockEvent.h and
	    // VisualizationThread::handleAudioBlockEvent()); none of that
	    // DSP-shaped work belongs on this real-time thread. master is
	    // moved, not copied (it's already this block's own
	    // independently-owned AudioBuffer, straight from mixer->encode()
	    // above); the raw bus and aux sums genuinely are copied, since
	    // mixer->getRawBus()/state_.getAuxASum()/getAuxBSum() are
	    // references into persistent buffers, overwritten next block. The
	    // raw bus is copied in full (not capped to DirAC's own 4-channel
	    // need - DiracAnalyzer.cpp already caps itself internally) since
	    // the volume meter needs every channel.
	    auto & raw_bus = mixer->getRawBus();
	    AudioBuffer raw_bus_copy(raw_bus.numberOfChannels(), raw_bus.numberOfFrames());
	    for (int c = 0; c < raw_bus.numberOfChannels(); c++) {
	      auto src = raw_bus.getChannelData(c);
	      auto dst = raw_bus_copy.getChannelData(c);
	      for (int frame = 0; frame < raw_bus.numberOfFrames(); frame++) dst[frame] = src[frame];
	    }
	    controller_->getVisualizationQueue().push(make_unique<AudioBlockEvent>(
	      move(master), move(raw_bus_copy), state_.getAuxASum(), state_.getAuxBSum()));
	  } else if (i - 1 - num_playback_desc < num_capture_desc) {
	    // .events was cleared to 0 above whenever recording is inactive, so
	    // revents can't legitimately be set here in that case - checking
	    // `recording` again anyway keeps this branch correct on its own,
	    // without relying on that as the only guard.
	    if (recording) {
	      auto data = audio.record(logger);
	      controller_->getUIEventQueue().push(make_unique<RecordEvent>(data));
	    }
	  }
	}
      }
    }
  }
}

std::unique_ptr<PlaybackEvent>
Player::createPlaybackEvent(const Song & song, const SongState & state) {
  auto [ pattern_idx, row_idx ] = state.getRelativePosition(song);

  PlaybackInfo info;
  info.setIsPlaying(state.isPlaying());
  info.setOutSampleRate(state.getChannelConfiguration().getAudioOutSampleRate());
  info.setSampleInterval(state.getChannelConfiguration().getSampleInterval(state.getTempo()));
  info.setSamplePos(state.getSamplePos());
  info.setPatternIdx(pattern_idx);
  info.setRowIdx(row_idx);
  info.setAbsolutePos(state.getAbsolutePosition());
  info.setPositionEditSeq(state.getPositionEditSeq());
  info.setVoiceCount(state.getVoiceCount());
  info.setAllocatedVoiceCount(state.getAllocatedVoiceCount());

  std::unordered_map<int, TrackInfo> effect_info;
  state.getAllTrackInfo(effect_info);
  info.setTrackInfo(move(effect_info));

  std::unordered_map<int, std::vector<ActiveVoiceInfo> > active_voices;
  state.getAllActiveVoices(active_voices);
  info.setActiveVoices(move(active_voices));

  return make_unique<PlaybackEvent>(info);
}
