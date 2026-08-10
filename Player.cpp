#include "Player.h"
#include "AudioAPI.h"
#include "Controller.h"
#include "Logger.h"
#include "Tuner.h"
#include "InstrumentTrack.h"

#include "LogEvent.h"
#include "PlaybackEvent.h"
#include "RecordEvent.h"
#include "PlaybackControlEvent.h"
#include "AudioBlockEvent.h"

#include "MixerFactory.h"
#include "InstrumentTrackState.h"

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
  // getSongPtr(), not getSong() - this runs on the audio thread, so a
  // plain reference into whatever current_song happens to point to right
  // now isn't safe against a concurrent UI-thread createNewSong()/
  // openSong() reassigning it - see getSongPtr()'s own comment. Kept
  // alive for this whole call via song_ptr; song itself stays a plain
  // reference so every existing song.foo() call below is unchanged.
  auto song_ptr = controller_->getSongPtr();
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
	
	if (instrument_track.getInstrumentId() < song.getInstruments().size()) {
	  auto & instrument = song.getInstrument(instrument_track.getInstrumentId());
	  auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(instrument_track.getInternalId()));

	  if (track_state) {
	    auto [ pattern_idx, row_idx ] = state_.getRelativePosition(song);
	    auto & pattern = song.getPattern(pattern_idx);

	    if (ev.getType() == PlaybackControlEvent::PLAY_NOTE) {
	      auto tuning = (track->getType() == TrackType::PERCUSSION_CONTROL || track->getType() == TrackType::DRUM_MACHINE) ? Tuning::PERCUSSION : song.getTuning();
	      Note note(midi_note, midi_velocity);
	      auto frequency = Tuner::getFrequency(tuning, note);

	      track_state->retriggerVoices(column, note.getValue());
	      // Same extent-default resolution as InstrumentTrackState::
	      // render()'s own pattern-playback note-on path - position_.extent
	      // < 0 means "not authored on this track" (InstrumentTrack::
	      // getExtent()), resolved to the assigned instrument's own family
	      // default here too, so a live-triggered note (this path) and a
	      // pattern-triggered one resolve identically instead of a live
	      // note silently falling back to a point source.
	      auto resolved_position = instrument_track.getPosition();
	      if (resolved_position.extent < 0.0f) resolved_position.extent = instrument.getDefaultExtent();
	      auto voice = instrument.playNote(state_.getChannelConfiguration(), resolved_position, frequency, 1.0f, note.getVelocityAsFloat(), 0.0f, note.getValue(), instrument_track.getSends());
	      track_state->chokeExclusiveClasses(*voice);
	      track_state->addVoice(column, move(voice));
	    } else {
	      track_state->applyAftertouch(column, midi_velocity / 127.0f);
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
    break;
    
  case PlaybackControlEvent::STOP:
    state_.setIsPlaying(false);
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
    // playback's row-by-row advance goes through SongState::render()'s
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
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->stopVoices(ev.getParameter2());
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

  audio.startRecording();

  // getSongPtr() (not getSong()/a raw pointer) - see that method's own
  // comment: it keeps whatever Song this shared_ptr copy points to alive
  // for as long as this thread holds it, regardless of a UI-thread
  // createNewSong()/openSong() reassigning Controller's own current_song
  // out from under it in the meantime.
  auto song = controller_->getSongPtr();
  state_.initialize(*song);
  auto mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());

  while ( !terminate_ ) {
    if (poll(descriptors.get(), num_descriptors, 1000) > 0) {
      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (i == 0) {
	    auto event = event_queue.pop();
	    handleEvent(*event);
	    while ( event_queue.hasEvents() ) {
	      auto event = event_queue.pop();
	      handleEvent(*event);
	    }
	    if (song_changed_) {
	      // Drops this thread's own shared_ptr copy of the old Song (see
	      // getSongPtr()'s own comment) only now, in favor of a fresh
	      // copy of whatever current_song the UI thread's
	      // createNewSong()/openSong() already reassigned - the old
	      // Song stays alive up to exactly this point, however long
	      // after the UI thread itself moved on.
	      song = controller_->getSongPtr();
	      state_.clear();
	      state_.resetPosition(); // old song's row count means nothing against the new song's patterns
	      state_.initialize(*song);
	      mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());
	      song_changed_ = false;
	    }
	    if (mixer_changed_) {
	      mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());
	      mixer_changed_ = false;
	    }
	    auto ev = createPlaybackEvent(*song, state_);
	    controller_->getUIEventQueue().push(move(ev));
	  } else if (i - 1 < num_playback_desc) {
	    state_.render(audio.getFrameCount(), *song, *mixer);
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
	    // independently-owned SampleData, straight from mixer->encode()
	    // above); the raw bus and aux sums genuinely are copied, since
	    // mixer->getRawBus()/state_.getAuxASum()/getAuxBSum() are
	    // references into persistent buffers, overwritten next block. The
	    // raw bus is copied in full (not capped to DirAC's own 4-channel
	    // need - DiracAnalyzer.cpp already caps itself internally) since
	    // the volume meter needs every channel.
	    auto & raw_bus = mixer->getRawBus();
	    SampleData raw_bus_copy(raw_bus.numberOfChannels(), raw_bus.numberOfFrames());
	    for (int c = 0; c < raw_bus.numberOfChannels(); c++) {
	      auto src = raw_bus.getChannelData(c);
	      auto dst = raw_bus_copy.getChannelData(c);
	      for (int i = 0; i < raw_bus.numberOfFrames(); i++) dst[i] = src[i];
	    }
	    controller_->getVisualizationQueue().push(make_unique<AudioBlockEvent>(
	      move(master), move(raw_bus_copy), state_.getAuxASum(), state_.getAuxBSum()));
	  } else if (i - 1 - num_playback_desc < num_capture_desc) {
	    auto data = audio.record(logger);
	    controller_->getUIEventQueue().push(make_unique<RecordEvent>(data));
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
