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
// note-on (the pending-events loop InstrumentTrackState::render(frames,
// instruments, context) already has) is untouched and still spawns voices
// directly - see plans/arpeggiator.md's Phase 2.
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
  // non-empty does (a fresh keypress restarts the pattern from step 0).
  void noteOn(int column, const Track & instrument, float frequency, float velocity, int note_value, float start_phase) override;
  void noteOff(int column) override;

  // Updates the held note's velocity for future steps only - live
  // aftertouch on an already-sounding step voice is a documented Phase 1
  // gap (see plans/arpeggiator.md).
  void notePressure(int column, float velocity) override;

  // Normally kept current automatically, once per block, by the
  // render(frames, instruments, context) override below (from the song's
  // own tempo, via RenderContext::getBpm()) - exposed as its own setter
  // too so a test can drive render(int frames) directly without needing
  // the full instruments/RenderContext plumbing.
  void setBpm(float bpm) { bpm_ = bpm; }

  AudioBuffer render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override;
  AudioBuffer render(int frames) override;

  // True whenever the chord is non-empty (keeps getting rendered so
  // stepping continues through a mid-gate gap with no child currently
  // sounding) or a released step's tail is still ringing
  // (InstrumentTrackState::isActive()'s own voices_ scan).
  bool isActive() const override { return !held_notes_.empty() || InstrumentTrackState::isActive(); }

  void clear() override;

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
  int stepLengthSamples() const;
  int gateLengthSamples() const;

  const Arpeggiator & arp_;
  const Track * instrument_ = nullptr; // last note-on's resolved instrument - see noteOn()

  std::vector<HeldNote> held_notes_;
  std::vector<Step> step_pool_;
  std::vector<PendingGate> pending_gates_;

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
