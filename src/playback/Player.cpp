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

SongState &
Player::stateFor(const string & name, const Song & song) {
  auto it = live_states_.find(name);
  if (it != live_states_.end()) return *it->second;
  auto state = std::make_unique<SongState>(channel_config_);
  state->initialize(song);

  // Eagerly builds the whole TrackState tree (Track::getState() creates a
  // track's own state - and, recursively, its descendants' - the first
  // time anything asks for it; SongState::renderBlock()'s own per-track
  // loop is the other, lazy call site, one track at a time) rather than
  // leaving that to happen incidentally on this buffer's first
  // renderBlock() call. The single global state_ this replaced never
  // needed to do this explicitly: it was already being rendered
  // continuously from Player::play()'s own startup, well before the first
  // user keystroke could possibly reach handlePlaybackControlEvent(), so
  // its tree was always already built by the time any PLAY_NOTE/
  // SET_TRACK_*/etc. event needed it. A brand-new per-buffer SongState has
  // no such head start - without this, handlePlaybackControlEvent()'s own
  // state.getChildByInternalId() lookups (a plain lookup, not a
  // lazy-create - only Track::getState() creates on demand) would
  // silently miss on this buffer's very first note/control event,
  // confirmed as a real regression (a buffer's first-ever note went
  // silent) before this loop was added. Same snapshot-then-release
  // pattern as renderBlock()'s own track_snapshot, for the same reason
  // (see its own comment) - getState() itself is cheap (plain
  // construction, no real DSP work), so nothing here needs the lock held
  // any longer than the pointer copy takes.
  std::vector<Track *> track_snapshot;
  {
    std::lock_guard<std::mutex> guard(song.getTracksMutex());
    track_snapshot.reserve(song.getTracks().size());
    for (auto & track : song.getTracks()) track_snapshot.push_back(track.get());
  }
  for (auto * track : track_snapshot) track->getState(*state);

  auto & ref = *state;
  live_states_.emplace(name, std::move(state));
  return ref;
}

void
Player::handlePlaybackControlEvent(PlaybackControlEvent & ev) {
  switch (ev.getType()) {
  case PlaybackControlEvent::TERMINATE:
    terminate_ = true;
    return;

  case PlaybackControlEvent::MIXER_CHANGED:
    mixer_changed_ = true;
    return;

  case PlaybackControlEvent::BUFFER_KILLED:
    // Drops this buffer's own live SongState, if it had one - also stops
    // it automatically if it happened to be the playing buffer, simply by
    // no longer existing to render at all, rather than needing a separate
    // STOP first.
    live_states_.erase(ev.getBufferName());
    if (playing_buffer_name_ == ev.getBufferName()) playing_buffer_name_.clear();
    return;

  case PlaybackControlEvent::BUFFER_RENAMED:
    {
      // Rekeys (rather than drops and lazily recreates) so a still-live
      // SongState's voices/release tail survive the rename intact, the
      // same as any other buffer switch.
      auto it = live_states_.find(ev.getBufferName());
      if (it != live_states_.end()) {
	auto node = live_states_.extract(it);
	node.key() = ev.getNewBufferName();
	live_states_.insert(std::move(node));
      }
      if (playing_buffer_name_ == ev.getBufferName()) playing_buffer_name_ = ev.getNewBufferName();
    }
    return;

  default:
    break;
  }

  // Every remaining event type targets one specific buffer - resolve its
  // Song and lazily get-or-create its own live SongState (see stateFor()'s
  // own comment) before dispatching on the actual action. A buffer that's
  // since been killed (a race between this event being pushed and it
  // actually being processed) simply has no Song to resolve any more -
  // drop the event rather than act on a buffer that no longer exists.
  auto song_ptr = controller_->getSongByName(ev.getBufferName());
  if (!song_ptr) return;
  auto & song = *song_ptr;
  auto & state = stateFor(ev.getBufferName(), song);

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
	  auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(instrument_track.getInternalId()));

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

  case PlaybackControlEvent::PLAY:
    // Demotes whatever was previously the playing buffer (if a different
    // one) to audition-only rather than tearing it down - its own
    // SongState simply stops advancing its pattern position/scheduling
    // new notes (mirroring the STOP case below), exactly like
    // SongState::renderBlock()'s own isPlaying()-gated block already does
    // for any stopped buffer, so a release tail or held note there keeps
    // sounding uninterrupted, still rendered every block down in play()
    // below.
    if (!playing_buffer_name_.empty() && playing_buffer_name_ != ev.getBufferName()) {
      auto old_it = live_states_.find(playing_buffer_name_);
      if (old_it != live_states_.end()) {
	old_it->second->setIsPlaying(false);
	old_it->second->notePlaybackStopped();
      }
    }
    playing_buffer_name_ = ev.getBufferName();
    state.setIsPlaying(true);

    // Re-locks a track's own internal clock (e.g. ArpeggiatorState's step
    // timer - see TrackState::resyncPlayhead()) to the transport every time
    // playback actually (re-)starts *and* the position actually moved while
    // stopped (SongState::resyncPlayheadAfterStop()'s own comment) - not on
    // every SET_POSITION/MOVE_POSITION edit below, which also fires on
    // plain cursor navigation while stopped (see that case's own comment)
    // and would otherwise resync on every such keypress, and not on a
    // plain pause/resume at the same row either, which needs no correction.
    state.resyncPlayheadAfterStop();
    break;

  case PlaybackControlEvent::STOP:
    if (playing_buffer_name_ == ev.getBufferName()) playing_buffer_name_.clear();
    state.setIsPlaying(false);
    state.notePlaybackStopped(); // snapshot for resyncPlayheadAfterStop() above, next PLAY
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
    state.setPosition(ev.getParameter2() ?
      song.clampRowToCurrentPattern(state.getAbsolutePosition(), state.getAbsolutePosition() + ev.getParameter1()) :
      state.getAbsolutePosition() + ev.getParameter1());
    break;

  case PlaybackControlEvent::SET_POSITION:
    // See MOVE_POSITION just above - same reasoning, mirroring Controller::
    // setEditPosition().
    state.setPosition(ev.getParameter2() ?
      song.clampRowToCurrentPattern(state.getAbsolutePosition(), ev.getParameter1()) : ev.getParameter1());
    break;

  case PlaybackControlEvent::CLEAR_VOICES:
    state.removeChild(ev.getParameter1());
    break;

  case PlaybackControlEvent::STOP_NOTE:
    {
      // noteOff() is virtual for the same reason noteOn()/notePressure()
      // above are - see that case's own comment.
      auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->noteOff(ev.getParameter2());
    }
    break;

  case PlaybackControlEvent::CHANNEL_PRESSURE:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->applyRealChannelPressure(ev.getParameter2() / 127.0f);
    }
    break;

  case PlaybackControlEvent::SET_RECORDING_MUTE:
    state.setRecordingMuted(ev.getParameter1() != 0);
    break;

  case PlaybackControlEvent::SET_TRACK_MUTED:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setMuted(ev.getParameter2() != 0);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SOLO:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSolo(ev.getParameter2() != 0);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_A:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendA(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_B:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendB(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_MAIN:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendMain(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_AZIMUTH:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setAzimuth(ev.getParameter2() / 10.0f);
    }
    break;

  default:
    break; // TERMINATE/MIXER_CHANGED/BUFFER_KILLED/BUFFER_RENAMED handled above
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

  auto mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());
  // No eager SongState construction here (unlike the single-global-state_
  // design this replaced) - every buffer's own live SongState is now
  // constructed lazily by stateFor(), the first time an event actually
  // targets it (see Player.h's own comment) - there's nothing to seed
  // before the first event/block, and live_states_ starts genuinely empty.

  // Pushes one playback snapshot per currently-live buffer - the
  // multi-buffer generalization of the single `createPlaybackEvent(*song,
  // state_)` push this replaced, called at the same two points that one
  // was (right after draining queued control events, and right after
  // rendering a block) so every live buffer's own row/pattern/voice-count
  // stays fresh at the same cadence it always did.
  auto pushSnapshots = [this]() {
    for (auto & [ name, state ] : live_states_) {
      auto song_ptr = controller_->getSongByName(name);
      if (!song_ptr) continue; // shouldn't happen - BUFFER_KILLED already drops the entry synchronously; defensive only
      controller_->getUIEventQueue().push(createPlaybackEvent(name, *song_ptr, *state));
    }
  };

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
	    if (mixer_changed_) {
	      mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());
	      mixer_changed_ = false;
	    }
	    pushSnapshots();
	  } else if (i - 1 < num_playback_desc) {
	    // Every live buffer's own SongState renders and accumulates into
	    // the same shared `mixer` this block - a single mixer->reset()
	    // here, not one per renderBlock() call (reset_mixer=false below),
	    // since a later buffer's own reset would otherwise wipe out an
	    // earlier one's already-accumulated output (see SongState::
	    // renderBlock()'s own comment on the reset_mixer parameter).
	    mixer->reset();
	    if (live_states_.empty()) {
	      // No live buffer this block (nothing has ever made a sound yet,
	      // or every buffer that once did has since been killed) - still
	      // need to give the mixer *a* correctly frame-sized accumulate()
	      // call, or its own internal accumulator stays at whatever shape
	      // it last had (reset() only zeroes existing content, it never
	      // resizes - see e.g. AmbisonicStereoMixer::accumulate()'s own
	      // comment, which is what actually establishes the frame count).
	      // Left unaccumulated, encode() below would hand back a stale or
	      // (at true startup) zero-frame master - indistinguishable from
	      // AudioBlockEvent.h's own empty-buffer shutdown sentinel, which
	      // gets VisualizationThread to mistake this ordinary silent block
	      // for its own termination signal and stop consuming its queue
	      // for the rest of the process (confirmed - the exact bug
	      // SongState::renderBlock()'s own per-track accumulate() call
	      // already had to guard against unconditionally, for the
	      // identical reason, before any of this multi-buffer support
	      // existed).
	      mixer->accumulate(AudioBuffer(0, false, false, audio.getFrameCount()));
	    }

	    // The active buffer renders *first*, straight into the same
	    // shared `mixer` every other live buffer accumulates into for the
	    // real, true combined signal below - but its own raw ambisonic
	    // contribution is snapshotted right after, while it's still the
	    // only thing accumulated into `mixer` so far, purely for
	    // AudioBlockEvent's own "one buffer, every scope" contract (see
	    // its own comment) - this never touches how the real signal
	    // itself is computed. Silent (but still correctly frame-sized -
	    // see AudioBuffer's ChannelConfiguration constructor) when the
	    // active buffer has no live SongState at all yet.
	    auto active_buffer_name = controller_->getActiveBufferNameThreadSafe();
	    AudioBuffer active_raw_bus(controller_->getChannelConfiguration(), audio.getFrameCount());
	    active_raw_bus.zero();
	    AudioBuffer active_aux_a(1, audio.getFrameCount()), active_aux_b(1, audio.getFrameCount());
	    active_aux_a.zero();
	    active_aux_b.zero();
	    auto active_it = live_states_.find(active_buffer_name);
	    if (active_it != live_states_.end()) {
	      auto active_song = controller_->getSongByName(active_buffer_name);
	      if (active_song) { // defensive only - see pushSnapshots()'s own comment
		active_it->second->renderBlock(audio.getFrameCount(), *active_song, *mixer, false);
		active_raw_bus = mixer->getRawBus();
		active_aux_a = active_it->second->getAuxASum();
		active_aux_b = active_it->second->getAuxBSum();
	      }
	    }

	    for (auto & [ name, state ] : live_states_) {
	      if (name == active_buffer_name) continue; // already rendered above
	      auto song_ptr = controller_->getSongByName(name);
	      if (!song_ptr) continue; // shouldn't happen - see pushSnapshots()'s own comment
	      state->renderBlock(audio.getFrameCount(), *song_ptr, *mixer, false);
	    }
	    auto master = mixer->encode();
	    audio.play(master, logger);

	    pushSnapshots();

	    // Hand off to VisualizationThread - its own dedicated thread,
	    // decoupled from both this real-time audio thread and the UI
	    // thread (see VisualizationThread.h and AudioBlockEvent.h for
	    // what each field means and why). master is moved, not copied
	    // (it's already this block's own independently-owned AudioBuffer,
	    // straight from mixer->encode() above, and VisualizationThread
	    // only reads it as the empty-buffer shutdown sentinel now, not
	    // for any real analysis); active_raw_bus/active_aux_a/
	    // active_aux_b are moved too - each already an independently-
	    // owned local computed fresh above, not a reference into any
	    // mixer's or SongState's own persistent state.
	    controller_->getVisualizationQueue().push(make_unique<AudioBlockEvent>(
	      move(master), move(active_raw_bus), move(active_aux_a), move(active_aux_b)));
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
Player::createPlaybackEvent(const string & buffer_name, const Song & song, const SongState & state) {
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

  return make_unique<PlaybackEvent>(buffer_name, info);
}
