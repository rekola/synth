#ifndef _ARPEGGIATORSTATE_H_
#define _ARPEGGIATORSTATE_H_

#include "InstrumentTrackState.h"
#include "Arpeggiator.h"

#include <vector>

// The persistent per-track stepper behind a live-triggered (audition-mode
// - see plans/arpeggiator.md) note on an Arpeggiator track. Inherits
// InstrumentTrackState (mirroring DrumMachineTrackState's own reasoning -
// see its doc comment) rather than bare TrackState: an arpeggiated note's
// underlying voices still want the exact same voices_/addVoice()/
// stopVoices()/clearFinishedVoices() bookkeeping any other InstrumentTrack
// gets, they're just triggered on the stepper's own schedule instead of
// directly at note-on. Unlike every other note-generating Track
// (NoteMultiplier.cpp is the closest sibling among *voice* generators),
// which spawns all its children once, synchronously, at note-on, this is
// genuinely stateful across many render() calls: it tracks the whole held
// chord and decides, block by block, what to trigger next.
//
// Reached the same way any other InstrumentTrackState is - via
// state_.getChildByInternalId(track_id) - so Player.cpp never needs to
// know this class exists at all: it calls noteOn()/noteOff()/
// notePressure() on whatever InstrumentTrackState it resolved, and virtual
// dispatch (InstrumentTrackState.h's own declarations) picks this
// override for an arpeggiator track and the base behavior for a plain
// one. Those three are called from Player::handlePlaybackControlEvent()'s
// PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE handling - the live audition path
// shared by Kitty-keyboard note entry (PatternEditor.cpp) and Launchpad
// NOTES/step-grid presses (LaunchpadManager.cpp). Pattern/song-driven
// note-on (InstrumentTrackState::render(frames, instruments, context)'s own
// pending-events loop) reaches the very same noteOn() override, driving the
// stepper from authored pattern data too - see noteOn()'s own comment on
// how NoteOrigin.h tells the two callers apart.
class ArpeggiatorState : public InstrumentTrackState {
 public:
  ArpeggiatorState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, int instrument_id, const SphericalPosition & position, const SendLevels & sends, const Arpeggiator & arp)
    : InstrumentTrackState(channel_config, solo, muted, track_id, instrument_id, position, sends), arp_(arp) { }

  // `column` is the same note-column identity InstrumentTrackState::
  // voices_ is keyed by for a plain track (PlaybackControlEvent's column
  // parameter, or a pattern row's own note-column index) - a held chord's
  // several simultaneous keys/pads/pattern notes map onto distinct ids the
  // same way they already do for the non-arpeggiated path. `instrument` is
  // whatever this track's own instrument_id_ resolves to (the caller
  // already looked this up to reach this class in the first place - same
  // signature as InstrumentTrackState::noteOn()). `start_phase` is part of
  // that shared signature but unused here - a step's own start phase is
  // decided fresh by triggerNextStep() whenever it actually fires, however
  // much later that ends up being. Adding a note to an already-sounding
  // chord does not reset the step position; the chord going from empty to
  // non-empty does (a fresh keypress/pattern onset restarts the pattern
  // from step 0 - see the .cpp for how LIVE vs PATTERN `origin` shapes
  // *when*, and whether, that restart's first step actually fires - a step
  // already ringing is never cut short to make room for it).
  void noteOn(int column, const Track & instrument, float frequency, float velocity, int note_value, float start_phase, NoteOrigin origin) override;
  void noteOff(int column) override;

  // See InstrumentTrackState::endPatternRow()'s own comment for when this
  // fires. Decides, once per pattern row rather than once per column,
  // whether that row restated the *whole* held chord (resync the step
  // clock to this row's frame, unless a step is currently ringing - see
  // the .cpp) or only edited part of it while the rest kept sustaining
  // from an earlier row (leave the step clock alone).
  void endPatternRow() override;

  // Updates the held note's velocity for future steps only - live
  // aftertouch on an already-sounding step voice is a documented Phase 1
  // gap (see plans/arpeggiator.md).
  void notePressure(int column, float velocity) override;

  // Normally kept current automatically, once per block, by the
  // render(frames, instruments, context) override below (from the song's
  // own tempo, via RenderContext::getBpm()) - exposed as its own setter
  // too so a test can drive renderVoices(int frames) directly without
  // needing the full instruments/RenderContext plumbing.
  void setBpm(float bpm) { bpm_ = bpm; }

  AudioBuffer render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override;
  AudioBuffer renderVoices(int frames) override;

  // Exposed (see setBpm()'s own reasoning) so a test can build an exact
  // sample timeline around the delay a LIVE from-empty chord onset now
  // incurs before its first step (see noteOn()'s own comment) - the real
  // value, from this instance's own ChannelConfiguration, rather than a
  // magic number/formula duplicated into the test and liable to drift out
  // of sync with the real one.
  int getChordCollectWindowSamples() const { return chordCollectWindowSamples(); }

  // True whenever the chord is non-empty (keeps getting rendered so
  // stepping continues through a mid-gate gap with no child currently
  // sounding) or a released step's tail is still ringing
  // (InstrumentTrackState::isActive()'s own voices_ scan).
  bool isActive() const override { return !held_notes_.empty() || InstrumentTrackState::isActive(); }

  void clear() override;

  // See TrackState::resyncPlayhead()'s own comment - playback just
  // (re-)started (Player.cpp's PlaybackControlEvent::PLAY), so whatever the
  // step clock drifted to while stopped no longer means anything. Forces
  // the same "trigger fresh on the very next render()" state noteOn()'s
  // was_empty branch uses for PATTERN, with no chord-collect delay (that
  // delay is about human hand-timing - see noteOn() - starting playback
  // has no such ambiguity to wait out) - but, like every other resync
  // point in this class, only when nothing is currently ringing (see the
  // .cpp). held_notes_/pending_gates_ are left untouched either way: a
  // chord still held (e.g. a live take paused mid-arpeggio) keeps sounding
  // through the transition exactly as before.
  //
  // If a step *is* currently ringing, this simply does nothing (see
  // resyncIfNothingRinging()'s own comment) rather than remembering the
  // resync and applying it once it's safe - that "catch it up later"
  // behavior was tried and reverted too: it caused its own real, reported
  // drift (forcing a fresh restart at an unexpected, unrelated moment -
  // most visibly whenever endPatternRow()'s own frequent full-chord
  // restatements each finally got applied). Both this and the earlier
  // row-aware step recovery attempt (also reverted - see
  // plans/arpeggiator-timing-fixes.md) ran into the same wall: this class
  // doesn't actually know the pattern's own note-event history, only
  // whatever the most recent row told it, so any attempt to be clever
  // about *which* step to resume on ends up wrong often enough to be worse
  // than simply restarting. A correct version needs the arp to actually
  // know that history - e.g. by having song playback pre-schedule/
  // pre-create the stepper's events ahead of time, rather than reacting to
  // them one row at a time - a bigger, separate piece of work, not
  // attempted here.
  void resyncPlayhead() override;

 private:
  struct HeldNote { int id; float frequency, velocity; int note_value; };
  struct Step { float frequency, velocity; int note_value; };

  // A step's gate deadline, tracked independently of step-advance timing
  // so that gate_ >= noteDuration_ (legato/no gap) doesn't need any
  // special-casing: the outgoing step's voice simply keeps ringing, still
  // tracked here, until its own deadline elapses - even after a newer step
  // has already started.
  struct PendingGate { int voice_id; int samples_remaining; };

  void rebuildStepPool();
  void triggerNextStep();
  void advanceIndex(int pool_size);
  void closeElapsedGates();
  void resyncIfNothingRinging();
  int stepLengthSamples() const;
  int gateLengthSamples() const;
  int chordCollectWindowSamples() const;

  const Arpeggiator & arp_;
  const Track * instrument_ = nullptr; // last note-on's resolved instrument - see noteOn()

  std::vector<HeldNote> held_notes_;
  std::vector<Step> step_pool_;
  std::vector<PendingGate> pending_gates_;
  std::vector<int> touched_columns_this_row_; // PATTERN-origin column ids updated so
                                               // far in the pattern row currently being
                                               // processed - see endPatternRow().

  int step_index_ = -1; // -1: no step triggered yet, pick the first on next render()
  int direction_ = 1; // UP_DOWN ping-pong direction
  int samples_until_next_step_ = 0;
  int next_voice_id_ = 0; // fresh id per triggered step, not a shared/fixed one - so
                           // stopVoices(id) at a gate's own deadline (closeElapsedGates())
                           // targets exactly that step's voice, never a newer step's, and
                           // an outgoing step whose gate hasn't closed yet (gate_ >=
                           // noteDuration_ - legato) keeps rendering alongside the new step
                           // instead of being reached by the wrong id.
  float bpm_ = 0.0f;
};

#endif
