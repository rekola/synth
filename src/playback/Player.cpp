#include "Player.h"
#include "../audio/AudioAPI.h"
#include "../Controller.h"
#include "../util/Logger.h"
#include "../instruments/Tuner.h"

#include "LogEvent.h"
#include "PlaybackEvent.h"
#include "RecordEvent.h"
#include "RecordingLatencyEvent.h"
#include "ThresholdRecordingTriggeredEvent.h"
#include "PlaybackControlEvent.h"
#include "AudioBlockEvent.h"

#include "../ambisonic/MixerFactory.h"
#include "../state/LeafTrackState.h"
#include "../state/InstrumentTrackState.h"
#include "../state/SampleTrackState.h"
#include "../state/NoteOrigin.h"
#include "../model/NoteCoordinate.h"
#include "../model/Clip.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {

// Self-contained (not TreeNode::decibelsToGain(), only reachable from
// TreeNode<Derived> subclasses - VoiceState/TrackState, neither of which
// Player is) - the same "each file keeps its own small dB helper"
// convention Controller.cpp's own dbToLinear() already uses for an
// identical reason.
float dbToLinear(float db) { return db > -100.0f ? powf(10.0f, db * 0.05f) : 0.0f; }

}

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
    track_snapshot.reserve(song.getMasterTrack().getChildren().size());
    for (auto & track : song.getMasterTrack().getChildren()) track_snapshot.push_back(track.get());
  }
  // Return value unused - getState() attaches the built state into *state as a side effect, which is all this loop is for.
  for (auto * track : track_snapshot) track->getState(*state, state->getSongStructure());

  // Row navigation while this buffer was still stateless (see the
  // MOVE_POSITION/SET_POSITION cases in handlePlaybackControlEvent()) left
  // its target row (and how many such events contributed to it) parked
  // here instead of forcing a SongState into existence just to remember
  // it - apply it now that one genuinely exists, so a buffer that's never
  // made a sound yet still starts playback from wherever the cursor was
  // left, not row 0, with its own getPositionEditSeq() already caught up
  // to however many edits preceded it (see pending_positions_'s own
  // comment on Player.h - plain setPosition() would instead always stamp
  // exactly 1 regardless of that count).
  auto pending_it = pending_positions_.find(name);
  if (pending_it != pending_positions_.end()) {
    state->setPositionWithEditSeq(pending_it->second.row, pending_it->second.edit_seq);
    pending_positions_.erase(pending_it);
  }

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
    // STOP first. Also drops any still-pending row (see
    // pending_positions_'s own comment) - a killed buffer has no state
    // left to ever apply it to.
    live_states_.erase(ev.getBufferName());
    pending_positions_.erase(ev.getBufferName());
    if (playing_buffer_name_ == ev.getBufferName()) playing_buffer_name_.clear();
    return;

  case PlaybackControlEvent::BUFFER_RENAMED:
    {
      // Rekeys (rather than drops and lazily recreates) so a still-live
      // SongState's voices/release tail survive the rename intact, the
      // same as any other buffer switch. A still-pending row (buffer never
      // made a sound yet) needs the same rekeying, or it'd silently apply
      // to nothing once a state is eventually created under the new name.
      auto it = live_states_.find(ev.getBufferName());
      if (it != live_states_.end()) {
	auto node = live_states_.extract(it);
	node.key() = ev.getNewBufferName();
	live_states_.insert(std::move(node));
      }
      auto pending_it = pending_positions_.find(ev.getBufferName());
      if (pending_it != pending_positions_.end()) {
	auto node = pending_positions_.extract(pending_it);
	node.key() = ev.getNewBufferName();
	pending_positions_.insert(std::move(node));
      }
      if (playing_buffer_name_ == ev.getBufferName()) playing_buffer_name_ = ev.getNewBufferName();
    }
    return;

  case PlaybackControlEvent::MOVE_POSITION:
  case PlaybackControlEvent::SET_POSITION:
    {
      // Deliberately never goes through stateFor() (unlike every other
      // event type below) - row navigation while stopped (see
      // PatternEditor's "Row navigation while stopped" comment) fires on
      // essentially every cursor keystroke in the pattern editor, for
      // whichever buffer is currently active, played or not. Letting that
      // lazily construct (and thus permanently register into
      // live_states_, rendered every block forever - see live_states_'s
      // own comment) a live SongState for a buffer that's never actually
      // made a sound would give every merely-scrolled-through buffer in a
      // session its own always-on send-bus processing - real waste on the
      // audio thread, and the opposite of the per-buffer editing/
      // playback-state plan's own stated intent. If this buffer already
      // has a live SongState (it's actually made sound before), update
      // its position directly, same as always; otherwise just remember
      // the target row in pending_positions_ - stateFor() applies it the
      // moment some later, genuinely sound-producing event actually
      // constructs the state.
      auto song_ptr = controller_->getSongByName(ev.getBufferName());
      if (!song_ptr) return;
      auto & song = *song_ptr;

      auto it = live_states_.find(ev.getBufferName());
      int base;
      if (it != live_states_.end()) {
	base = it->second->getAbsolutePosition();
      } else {
	auto pending_it = pending_positions_.find(ev.getBufferName());
	base = pending_it != pending_positions_.end() ? pending_it->second.row : 0;
      }

      // Mirrors Controller::moveEditPosition()/setEditPosition()'s own
      // decision on the UI-thread side - both derive the same result
      // independently from the same (unclamped) delta_rows/absolute_row
      // rather than one side trusting a value computed by the other
      // across the thread boundary. parameter2 carries whether a
      // pattern-editor selection was open there (clamp to the current
      // pattern, via clampRowToCurrentPattern()) or not (cross pattern
      // boundaries freely - setPosition() already floors at 0, and
      // there's no upper bound either way, same as real playback's own
      // run-off-the-end).
      int new_pos = ev.getType() == PlaybackControlEvent::MOVE_POSITION ?
	(ev.getParameter2() ? song.clampRowToCurrentPattern(base, base + ev.getParameter1()) : base + ev.getParameter1()) :
	(ev.getParameter2() ? song.clampRowToCurrentPattern(base, ev.getParameter1()) : ev.getParameter1());

      if (it != live_states_.end()) {
	it->second->setPosition(new_pos);
      } else {
	// edit_seq counts this event too (see pending_positions_'s own
	// comment on Player.h), not just the row itself.
	auto & pending = pending_positions_[ev.getBufferName()];
	pending.row = new_pos;
	pending.edit_seq++;
      }
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

      auto track = song.getMasterTrack().getChildByInternalId(track_id);
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL ||
		    track->getType() == TrackType::PERCUSSION_CONTROL ||
		    track->getType() == TrackType::DRUM_MACHINE
		    )) {
	auto track_state = dynamic_cast<InstrumentTrackState*>(state.getChildByInternalId(track->getInternalId()));

	if (track_state) {
	  // getInstrumentSource() is the same per-render()-call resolution
	  // InstrumentTrackState::render() uses for pattern-driven notes -
	  // an InstrumentTrack's own instrument_id_ pool index, or (for
	  // PercussionTrackState/DrumMachineTrackState) the pool's default
	  // kit - so a live-triggered note (Kitty-keyboard entry, Launchpad
	  // NOTES/step-grid presses) resolves its instrument exactly the
	  // same way a pattern note would.
	  auto instrument = track_state->getInstrumentSource(song.getInstrumentPool());

	  if (instrument) {
	    // InstrumentTrackState::noteOn()/notePressure() (PLAY_NOTE/
	    // NOTE_PRESSURE handling, shared by Kitty-keyboard note entry and
	    // Launchpad NOTES/step-grid presses) are virtual - a plain track
	    // spawns/updates a voice directly, an ArpeggiatorState
	    // (Arpeggiator.h's own track kind, reached the same way any other
	    // InstrumentTrackState is) routes them into its stepper's held
	    // chord instead - so this call site never needs to know which
	    // kind of track it's talking to.
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
	      track_state->noteOn(column, *instrument, frequency, note.getVelocityAsFloat(), note.getValue(), NoteOrigin::LIVE,
				   NoteCoordinate(state.getSongStructure().getOrdinalFor(*track), live_note_counter_++, column));
	    } else {
	      track_state->notePressure(column, midi_velocity / 127.0f);
	    }
	  }
	}
      }
    }
    break;

  case PlaybackControlEvent::PLAY_SAMPLE_CLIP:
    {
      auto track_id = ev.getParameter1();
      auto clip_index = ev.getParameter2();
      auto & clips = song.getClips(track_id);
      if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) break;

      auto * sample_state = dynamic_cast<SampleTrackState *>(state.getChildByInternalId(track_id));
      // No scene to bound against here (Session-view triggering isn't tied
      // to one), and no block to chunk mid-render either - straight through
      // to triggerClip(), unlike SongState.h's own transport-driven path
      // (RenderContext::addPendingSampleStart()/addPendingSampleStop()).
      // LaunchpadManager::fireOrTriggerClipStep() is what re-fires this
      // fresh every lap for a looping clip (SampleTrackState::triggerClip()'s
      // own comment on why no separate loop handling belongs here at all).
      if (sample_state) sample_state->triggerClip(clips[static_cast<size_t>(clip_index)], song.getTempo());
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

  case PlaybackControlEvent::STOP_ALL_NOTES:
    {
      // stopAllVoices()'s own whole-track natural release, for a caller
      // (Launchpad Session view's "stop this track") with no single
      // column to target the way STOP_NOTE above has - dynamic_cast to the
      // shared LeafTrackState base, not InstrumentTrackState, since Session
      // view's "stop this track" reaches a SampleTrack's own clip the same
      // uniform way it reaches every other track type.
      auto track_state = dynamic_cast<LeafTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->stopAllVoices();
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

  // SET_TRACK_MUTED/SOLO/SEND_A/SEND_B/SEND_MAIN/AZIMUTH all dynamic_cast to
  // the shared LeafTrackState base, not InstrumentTrackState - every leaf
  // track type (a SampleTrack included) has its own mute/solo/sends/pan,
  // resolved from the model-layer LeafTrack the same uniform way regardless
  // of what actually produces its voices (see Controller.cpp's own
  // asLeafTrack()).
  case PlaybackControlEvent::SET_TRACK_MUTED:
    {
      auto track_state = dynamic_cast<LeafTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setMuted(ev.getParameter2() != 0);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SOLO:
    {
      auto track_state = dynamic_cast<LeafTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSolo(ev.getParameter2() != 0);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_A:
    {
      auto track_state = dynamic_cast<LeafTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendA(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_B:
    {
      auto track_state = dynamic_cast<LeafTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendB(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_MAIN:
    {
      auto track_state = dynamic_cast<LeafTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendMain(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_AZIMUTH:
    {
      auto track_state = dynamic_cast<LeafTrackState*>(state.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setAzimuth(ev.getParameter2() / 10.0f);
    }
    break;

  case PlaybackControlEvent::SET_BUS_EFFECT:
    state.setBusEffectKind(ev.getParameter1(), static_cast<BusEffectKind>(ev.getParameter2()));
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

    // Round-trip recording-latency measurement, exactly once per take -
    // right on the false -> true edge of `recording`, before anything has
    // actually been captured yet (capture's own poll events are still
    // disabled below at this exact point, so no RecordEvent for this take
    // can have reached the UI thread before the RecordingLatencyEvent
    // pushed here does - see UI::handleRecordEvent()'s own comment on why
    // that ordering matters). Only meaningful while the transport is also
    // genuinely playing (AudioAPI::getPlaybackDelayFrames()/
    // getCaptureDelayFrames()'s own doc comment) - checked directly off
    // whichever SongState this same audio thread already treats as "the
    // active buffer" a little further below, not Controller::
    // getPlaybackInfo() (a UI-thread-owned snapshot this thread has no
    // business reading without synchronization).
    if (recording && !was_recording_) {
      auto active_it = live_states_.find(controller_->getActiveBufferNameThreadSafe());
      if (active_it != live_states_.end() && active_it->second->isPlaying()) {
	auto latency_frames = audio.getPlaybackDelayFrames() + audio.getCaptureDelayFrames();
	controller_->getUIEventQueue().push(make_unique<RecordingLatencyEvent>(latency_frames));
      }
    }
    was_recording_ = recording;

    // Loudness-threshold-armed recording (Controller::isThresholdArmed(),
    // a SampleTrack's own Record Arm) - the same false->true/true->false
    // edge idiom as `recording` above. A fresh arm resets the pre-roll
    // ring buffer (an earlier, temporally-discontinuous arm cycle's own
    // leftover content must never bleed into a later one's own drain())
    // and clears the "already triggered this cycle" latch.
    bool threshold_armed = controller_->isThresholdArmed();
    if (threshold_armed && !was_threshold_armed_) {
      threshold_ring_buffer_.reset();
      threshold_triggered_this_arm_cycle_ = false;
    }
    was_threshold_armed_ = threshold_armed;

    for (size_t i = 0; i < num_capture_desc; i++) {
      descriptors[1 + num_playback_desc + i].events = (recording || threshold_armed) ? capture_events[i] : 0;
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
	    // .events was cleared to 0 above whenever neither recording nor
	    // threshold-armed, so revents can't legitimately be set here in
	    // that case - checking both again anyway keeps this branch
	    // correct on its own, without relying on that as the only guard.
	    // Feeds SampleTrackState::setInputLoudness() (same-thread, this
	    // audio thread owns both Player and live_states_) so the VU
	    // meter shows real input level while armed-and-waiting or
	    // actually recording, not just once a voice starts playing back.
	    auto updateInputLoudness = [this](const AudioBuffer & data) {
	      auto buffer_name = controller_->getActiveBufferNameThreadSafe();
	      auto state_it = live_states_.find(buffer_name);
	      if (state_it == live_states_.end()) return;
	      auto * sample_state = dynamic_cast<SampleTrackState *>(state_it->second->getChildByInternalId(controller_->getRecordingTrackId()));
	      if (sample_state) sample_state->setInputLoudness(data.calculateMainRMS());
	    };

	    if (recording) {
	      auto data = audio.record(logger);
	      updateInputLoudness(data);
	      controller_->getUIEventQueue().push(make_unique<RecordEvent>(data));
	    } else if (threshold_armed) {
	      auto data = audio.record(logger);
	      updateInputLoudness(data);
	      threshold_ring_buffer_.push(data);
	      if (!threshold_triggered_this_arm_cycle_ && dbToLinear(kThresholdRecordTriggerDB) <= data.calculateMainRMS()) {
		threshold_triggered_this_arm_cycle_ = true;
		auto preroll = threshold_ring_buffer_.drain();

		// Backdated (scene, row): the transport's own position right
		// now, minus the pre-roll's own span converted to rows -
		// resolved here, directly against this same audio thread's
		// own live SongState (Controller::getPlaybackInfo() is a
		// UI-thread-owned snapshot this thread has no business
		// reading, same reasoning as the latency measurement above),
		// never written directly into Controller (which owns this
		// position the same way Controller::armRecordingStart()
		// already does for start-sample-capture's own take - see
		// ThresholdRecordingTriggeredEvent's own comment). Falls
		// back to (0, 0) if this buffer somehow has no live state
		// yet - can't happen in practice (armThresholdRecording()'s
		// own auto-start already gave it one), stays defensive
		// rather than assuming.
		int scene = 0, row = 0;
		auto buffer_name = controller_->getActiveBufferNameThreadSafe();
		auto song_ptr = controller_->getSongByName(buffer_name);
		auto state_it = live_states_.find(buffer_name);
		if (song_ptr && state_it != live_states_.end()) {
		  auto & state = *state_it->second;
		  auto preroll_rows = channel_config_.framesToRows(preroll.numberOfFrames(), state.getTempo());
		  auto backdated = song_ptr->normalizePosition(0, std::max(0, state.getAbsolutePosition() - preroll_rows));
		  scene = backdated.first;
		  row = backdated.second;
		}
		controller_->getUIEventQueue().push(make_unique<ThresholdRecordingTriggeredEvent>(
		  controller_->getRecordingTrackId(), std::move(preroll), scene, row));
	      }
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
