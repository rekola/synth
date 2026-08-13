#include "TestFramework.h"

#include "../ArpeggiatorState.h"
#include "../Arpeggiator.h"
#include "../Oscillator.h"
#include "../MemoryParameterSource.h"
#include "../ChannelConfiguration.h"
#include "../SphericalPosition.h"
#include "../SendLevels.h"
#include "../ActiveVoiceInfo.h"
#include "../NoteOrigin.h"

#include <memory>
#include <vector>
#include <algorithm>
#include <unordered_map>

// Exercises ArpeggiatorState directly through the public Track/TrackState
// API only - the same shape tests/EnvelopeFilterTests.cpp's
// makeEnvelopeFilterState() helper uses - with no Player/event queue/Song
// involved. noteOn()'s calls below pass NoteOrigin::LIVE, exercising the
// stepper the same way live audition input reaches it
// (Player::handlePlaybackControlEvent()'s PLAY_NOTE case); a handful of
// tests further down call noteOn(..., NoteOrigin::PATTERN)/endPatternRow()
// directly to exercise the pattern-driven path
// (InstrumentTrackState::render()'s pending-events loop) the same way -
// see tests/RenderTests.cpp for the full, Song-driven end-to-end version
// of that path. An Arpeggiator is now a track kind (InstrumentTrack.h),
// not an instrument-wrapping node - the instrument it steps through is a
// plain sibling object, passed into noteOn() directly, the same way
// Player.cpp passes whatever this track's own instrument_id_ resolves to.

namespace {

std::unique_ptr<Arpeggiator> makeArpeggiator(const char * mode, int note_duration, int octaves, int gate) {
  auto arp = std::make_unique<Arpeggiator>();
  MemoryParameterSource params;
  params.set("mode", std::string(mode));
  params.set("noteDuration", note_duration);
  params.set("octaves", octaves);
  params.set("gate", gate);
  arp->loadParameters(params);
  return arp;
}

// The note_value(s) currently sounding, off InstrumentTrackState's own
// public getAllActiveVoices() (ArpeggiatorState doesn't override it - its
// step voices live in the same voices_ every plain track's do) - no audio
// decoding needed for tests that only care about *which* note is
// currently active. `track_id` must match whatever the state was
// constructed with.
std::vector<int> activeNoteValues(const ArpeggiatorState & state, int track_id) {
  std::unordered_map<int, std::vector<ActiveVoiceInfo> > out;
  state.getAllActiveVoices(out);
  std::vector<int> values;
  auto it = out.find(track_id);
  if (it != out.end()) for (auto & v : it->second) values.push_back(v.note_value);
  std::sort(values.begin(), values.end());
  return values;
}

int countZeroCrossings(const float * data, int n) {
  int count = 0;
  for (int i = 1; i < n; i++) {
    if ((data[i - 1] < 0.0f) != (data[i] < 0.0f)) count++;
  }
  return count;
}

}

TEST(arpeggiator_state_is_silent_until_a_chord_is_held) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", 1, 0, 1);
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  CHECK(!state.isActive());
  auto buf = state.renderVoices(256);
  CHECK(!buf.hasChannel(Channel::Main));
  CHECK(!state.isActive());
}

// The concrete regression test for "does the stepping logic actually
// step": a held 3-note chord in UP mode visits each note in pitch order,
// wraps back to the bottom, and - since gate_ < noteDuration_ here - falls
// silent in between steps rather than running notes together.
TEST(arpeggiator_state_steps_ascending_through_held_chord_with_gaps) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", /*noteDuration=*/2, /*octaves=*/0, /*gate=*/1);
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120); // samples per row - also the gate length here
  CHECK(unit > 0);

  // Adding notes to a chord one at a time must not reset stepping - only
  // going from empty to non-empty does (see noteOn()'s own comment). All
  // three land before the first render() call, so the first step already
  // sees the full, pitch-sorted chord.
  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::LIVE);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f, NoteOrigin::LIVE);
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f, NoteOrigin::LIVE);
  CHECK(state.isActive());

  // A fresh LIVE chord onset defers its actual first trigger by the
  // chord-collect window (noteOn()'s own comment) - silent until it
  // elapses, even though held_notes_ (and isActive() above) is already
  // non-empty. Rendering exactly that many samples first leaves
  // samples_until_next_step_ at precisely 0, so every checkpoint below
  // proceeds exactly as if step 0 had triggered immediately, just shifted
  // by this fixed offset.
  state.renderVoices(state.getChordCollectWindowSamples());
  CHECK(activeNoteValues(state, 0).empty());

  // Checkpoints deliberately land strictly inside each interval, never on
  // a step/gate boundary itself (boundaries are resolved lazily, at the
  // start of the next render() call).
  state.renderVoices(unit / 2);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60}); // step 0's gate still open

  state.renderVoices(unit); // crosses the gate close (at 1*unit) - now silent
  CHECK(activeNoteValues(state, 0).empty());

  state.renderVoices(unit); // crosses the step advance (at 2*unit) into step 1's gate
  CHECK(activeNoteValues(state, 0) == std::vector<int>{64});

  state.renderVoices(unit); // crosses step 1's gate close (at 3*unit)
  CHECK(activeNoteValues(state, 0).empty());

  state.renderVoices(unit); // crosses the step advance (at 4*unit) into step 2's gate
  CHECK(activeNoteValues(state, 0) == std::vector<int>{67});

  state.renderVoices(unit); // crosses step 2's gate close (at 5*unit)
  CHECK(activeNoteValues(state, 0).empty());

  state.renderVoices(unit); // crosses the step advance (at 6*unit), wrapping back to step 0
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60});
}

TEST(arpeggiator_state_down_mode_starts_at_the_top_and_wraps_there) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("down", /*noteDuration=*/1, /*octaves=*/0, /*gate=*/1);
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120);

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::LIVE);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f, NoteOrigin::LIVE);
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f, NoteOrigin::LIVE);

  // See arpeggiator_state_steps_ascending_through_held_chord_with_gaps's
  // own comment - waits out the LIVE chord-collect window first.
  state.renderVoices(state.getChordCollectWindowSamples());

  state.renderVoices(unit / 2);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{67}); // starts at the top, not the bottom

  state.renderVoices(unit);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{64});

  state.renderVoices(unit);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60});

  state.renderVoices(unit);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{67}); // wraps back to the top
}

TEST(arpeggiator_state_up_down_mode_pingpongs_without_repeating_endpoints) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("updown", /*noteDuration=*/1, /*octaves=*/0, /*gate=*/1);
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120);

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::LIVE);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f, NoteOrigin::LIVE);
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f, NoteOrigin::LIVE);

  // See arpeggiator_state_steps_ascending_through_held_chord_with_gaps's
  // own comment - waits out the LIVE chord-collect window first.
  state.renderVoices(state.getChordCollectWindowSamples());

  std::vector<int> observed;
  state.renderVoices(unit / 2);
  observed.push_back(activeNoteValues(state, 0).at(0));
  for (int i = 0; i < 5; i++) {
    state.renderVoices(unit);
    observed.push_back(activeNoteValues(state, 0).at(0));
  }

  std::vector<int> expected = { 60, 64, 67, 64, 60, 64 }; // up to the top, down to the bottom, no repeat at either end
  CHECK(observed == expected);
}

// held_notes_ going empty stops scheduling new steps, but a step already
// in flight (and its gate) still runs to completion - "lets the last
// note's release tail finish" (plans/arpeggiator.md) - and re-holding a
// note afterward restarts from step 0 rather than resuming mid-cycle.
TEST(arpeggiator_state_releasing_the_chord_lets_the_current_step_finish_then_goes_silent) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", /*noteDuration=*/2, /*octaves=*/0, /*gate=*/2); // legato: gate == duration
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120);

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::LIVE);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f, NoteOrigin::LIVE);

  // See arpeggiator_state_steps_ascending_through_held_chord_with_gaps's
  // own comment - waits out the LIVE chord-collect window first.
  state.renderVoices(state.getChordCollectWindowSamples());

  state.renderVoices(unit / 2);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60});

  state.noteOff(0);
  state.noteOff(1); // chord now empty - no further steps get scheduled

  state.renderVoices(unit); // still within step 0's legato gate window (2*unit)
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60});
  CHECK(state.isActive()); // held_notes_ empty, but the release tail is real

  state.renderVoices(unit); // past step 0's gate close (2*unit) - no step 1 was ever scheduled
  CHECK(activeNoteValues(state, 0).empty());
  CHECK(!state.isActive());

  // Re-holding restarts from step 0, not wherever the old cycle left off.
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f, NoteOrigin::LIVE);
  state.renderVoices(state.getChordCollectWindowSamples()); // wait out the collect window again
  state.renderVoices(unit / 2);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{67});
}

// octaves_ widens the step pool with frequency-doubled copies of each held
// note (note_value passed through unchanged - see ArpeggiatorState.h) - so
// two consecutive steps of a single held note actually differ in pitch.
// Checked on the rendered audio itself (zero-crossing rate), since
// note_value alone can't distinguish an octave-shifted step from the
// original.
TEST(arpeggiator_state_octaves_widen_the_pool_to_a_higher_pitch) {
  ChannelConfiguration config(44100); // ambisonic_order 0 = mono/W-only, simplest to analyze
  auto arp = makeArpeggiator("up", /*noteDuration=*/4, /*octaves=*/1, /*gate=*/4); // legato: no gap to work around
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120);
  int step_samples = 4 * unit;

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::LIVE);

  // See arpeggiator_state_steps_ascending_through_held_chord_with_gaps's
  // own comment - waits out the LIVE chord-collect window first, so `low`
  // below starts exactly at step 0's own trigger, not partway through a
  // silent lead-in.
  state.renderVoices(state.getChordCollectWindowSamples());

  auto low = state.renderVoices(step_samples); // whole of step 0 (440Hz) - the transition is resolved
                                          // lazily, at the start of the *next* render() call, so
                                          // this entire chunk is clean.
  auto high = state.renderVoices(step_samples / 2); // triggers step 1 (880Hz) right at this call's own
                                               // start, so this whole chunk is clean too.

  CHECK(low.hasChannel(Channel::Main));
  CHECK(high.hasChannel(Channel::Main));

  float low_rate = (float)countZeroCrossings(low.getChannel(Channel::Main), low.numberOfFrames()) / low.numberOfFrames();
  float high_rate = (float)countZeroCrossings(high.getChannel(Channel::Main), high.numberOfFrames()) / high.numberOfFrames();

  // high should be roughly double low's crossing rate (one octave up) -
  // loose bounds since this is a plain zero-crossing estimate, not a real
  // pitch detector.
  CHECK(high_rate > low_rate * 1.5f);
  CHECK(high_rate < low_rate * 2.5f);
}

// The concrete regression test for the reported "if the other note than
// the root hits first, the arpeggio starts from that one" - a hand-rolled
// chord's top note lands first (the was_empty transition, exactly the
// scenario that used to trigger an immediate step on just that one note),
// with the rest following shortly after but still well inside the
// chord-collect window (see noteOn()'s own comment). The eventual first
// step is the pool's true lowest note (UP mode's own starting point),
// never the first-pressed one.
TEST(arpeggiator_state_live_chord_collect_window_starts_on_the_lowest_note_regardless_of_press_order) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", /*noteDuration=*/2, /*octaves=*/0, /*gate=*/1);
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int window = state.getChordCollectWindowSamples();
  CHECK(window > 4);

  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f, NoteOrigin::LIVE); // top note first
  state.renderVoices(window / 4); // still well inside the collect window

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::LIVE); // the rest follow shortly after
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f, NoteOrigin::LIVE);
  CHECK(activeNoteValues(state, 0).empty()); // still silent - the deadline hasn't moved

  state.renderVoices(window); // comfortably past the (unmoved) deadline
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60}); // the lowest note, not the first-pressed one
}

// The window has a deadline, not an open-ended "wait for the chord to
// finish" - a note landing after it has already closed (and step 0
// already triggered on whatever came before) just joins the pool for
// whatever step comes *next*, the same as any other mid-cycle addition.
TEST(arpeggiator_state_live_note_arriving_after_the_collect_window_closes_does_not_join_the_already_triggered_step) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", /*noteDuration=*/4, /*octaves=*/0, /*gate=*/4); // legato: generous, uninterrupted step 0
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int window = state.getChordCollectWindowSamples();

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::LIVE);
  // +1: rendering exactly `window` samples only counts samples_until_next_step_
  // down to precisely 0 (the trigger check itself only runs at the *top*
  // of each internal chunk - see renderVoices()'s own comment on why it's
  // samples_until_next_step_ alone, not step_index_ < 0, that gates it) -
  // one more sample is what actually crosses into the next chunk and
  // fires it.
  state.renderVoices(window + 1); // waits the window out - step 0 triggers on just this one note
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60});

  // A second, lower note lands well after the window has already closed
  // and step 0 has already been chosen and triggered.
  state.noteOn(1, instrument, 220.0f, 0.8f, 48, 0.0f, NoteOrigin::LIVE);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60}); // unaffected
}

// The concrete regression test for the reported song-playback drift/lag: a
// pattern row that refreshes *every* column of the currently-held chord
// resyncs the step clock to that row's own frame immediately, rather than
// waiting for whatever's left of the previous chord's own already-
// scheduled step to elapse.
TEST(arpeggiator_state_pattern_chord_replace_resyncs_the_step_clock_mid_cycle) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", /*noteDuration=*/4, /*octaves=*/0, /*gate=*/1); // a real gap between steps
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120);

  // Two-note chord, PATTERN-origin - triggers immediately, no
  // chord-collect delay (see noteOn()'s own comment).
  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::PATTERN);
  state.noteOn(1, instrument, 660.0f, 0.8f, 67, 0.0f, NoteOrigin::PATTERN);
  state.endPatternRow();
  state.renderVoices(1);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60}); // step 0 = the lower note

  // Well into the gap between steps - long past step 0's own 1-row gate,
  // well before its 4-row natural advance to step 1.
  state.renderVoices(2 * unit - 1);
  CHECK(activeNoteValues(state, 0).empty());

  // A new pattern row replaces *both* columns with a new chord - a full
  // restatement of the held chord.
  state.noteOn(0, instrument, 880.0f, 0.8f, 79, 0.0f, NoteOrigin::PATTERN);
  state.noteOn(1, instrument, 1320.0f, 0.8f, 86, 0.0f, NoteOrigin::PATTERN);
  state.endPatternRow();

  // Resyncs immediately, at this exact frame, instead of waiting out
  // however much of step 0's own 4-row cycle was left - the new chord's
  // own step 0 (the lower of the two new notes) sounds right away.
  state.renderVoices(1);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{79});
}

// The concrete regression test for the reported "I hear the root note
// played twice almost simultaneously": a full-chord-replace resync (as
// above) whose new chord happens to share a note with the old one (a
// changed voicing over the same root) must not leave the *old* chord's
// still-open step ringing alongside a freshly-triggered new one at the
// same pitch. resyncIfNothingRinging() does nothing (beyond a timing
// clamp) instead of forcing an immediate retrigger when a step is still
// ringing (see its own comment - cutting the old voice short to make room
// was tried and reverted, since a note that's already playing must never
// be cut). Legato (gate == noteDuration) so step 0's voice is deliberately
// still open, not yet past its own natural gate, when the second row hits.
TEST(arpeggiator_state_pattern_chord_replace_does_not_double_a_note_shared_with_the_old_chord) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", /*noteDuration=*/4, /*octaves=*/0, /*gate=*/4);
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120);

  // Old chord - root (60) plus a fifth above it (67).
  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::PATTERN);
  state.noteOn(1, instrument, 660.0f, 0.8f, 67, 0.0f, NoteOrigin::PATTERN);
  state.endPatternRow();
  state.renderVoices(1);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60}); // step 0 = the root

  state.renderVoices(unit); // still comfortably inside step 0's own 4-row legato window

  // A new row restates the whole chord - same root (60), a different note
  // above it (64 instead of 67).
  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::PATTERN);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f, NoteOrigin::PATTERN);
  state.endPatternRow();

  // Step 0's own voice is still ringing (its legato gate hasn't closed
  // yet), so this must not retrigger anything - the root keeps sounding
  // exactly as before, not doubled by a second, freshly-triggered voice at
  // the same pitch (activeNoteValues() doesn't deduplicate - a still-open
  // old voice plus a new one at the same pitch would show up as
  // {60, 60} here).
  state.renderVoices(1);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60});

  // Once step 0's own natural boundary actually arrives, the stepper
  // advances on its own unbroken schedule (step_index_ was never touched
  // by the resync above - see resyncIfNothingRinging()'s own comment on
  // why a deferred resync is no longer remembered/applied later either),
  // landing on index 1 of the *new* pool - 64, not the old chord's 67 -
  // so the chord update itself was never lost, just not applied
  // instantaneously. The old root's voice has closed by this point too
  // (legato: its own gate coincides with this same boundary), so nothing
  // lingers alongside the new note either.
  state.renderVoices(4 * unit);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{64});
}

// The counterpart to the resync test above: a pattern row that only
// refreshes *some* of the currently-held chord's columns (dropping one
// note and replacing it with another while the rest keep sustaining from
// an earlier row) leaves the step clock alone - resyncing over one voice's
// own change would restart the whole cycle audibly and wrongly.
TEST(arpeggiator_state_pattern_partial_chord_edit_does_not_resync_the_step_clock) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", /*noteDuration=*/2, /*octaves=*/0, /*gate=*/1);
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120);

  // Three-note chord, all three columns touched by the same row.
  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::PATTERN);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f, NoteOrigin::PATTERN);
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f, NoteOrigin::PATTERN);
  state.endPatternRow();

  state.renderVoices(unit / 2);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60}); // step 0

  state.renderVoices(unit); // crosses the gate close (1*unit)
  CHECK(activeNoteValues(state, 0).empty());

  state.renderVoices(unit); // crosses the step advance (2*unit) into step 1's gate
  CHECK(activeNoteValues(state, 0) == std::vector<int>{64});

  // A later pattern row drops column 2's note and replaces it with a
  // different one (kept above column 1's pitch, so the pool's sort order
  // is otherwise unchanged) while columns 0/1 keep sustaining untouched
  // from the very first row - a partial edit, not a full chord
  // restatement.
  state.noteOn(2, instrument, 770.0f, 0.8f, 70, 0.0f, NoteOrigin::PATTERN);
  state.endPatternRow();

  // The step clock is untouched - the rest of the cycle proceeds exactly
  // as if the edit had never happened, just with the edited note's new
  // pitch/note_value once its own turn comes round.
  state.renderVoices(unit); // crosses step 1's gate close (3*unit)
  CHECK(activeNoteValues(state, 0).empty());

  state.renderVoices(unit); // crosses the step advance (4*unit) into step 2's gate
  CHECK(activeNoteValues(state, 0) == std::vector<int>{70}); // the edited note - not a resync back to step 0
}

// The concrete regression test for the reported seek-then-restart desync:
// resyncPlayhead() (TrackState.h) re-locks the step clock to "trigger
// fresh on the very next render()" without touching held_notes_ - a chord
// still held through the stop (e.g. a live take paused mid-arpeggio) keeps
// sounding, but the next step starts the cycle over rather than resuming
// wherever the old, now-irrelevant schedule would have landed.
TEST(arpeggiator_state_resync_playhead_realigns_the_step_clock_without_dropping_the_held_chord) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", /*noteDuration=*/2, /*octaves=*/0, /*gate=*/1);
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120);

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::PATTERN);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f, NoteOrigin::PATTERN);
  state.endPatternRow();

  state.renderVoices(unit / 2);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60}); // step 0

  state.renderVoices(unit); // gate closes (1*unit) - silent, well before step 1's own advance (2*unit)
  CHECK(activeNoteValues(state, 0).empty());

  // Playback (re-)starts (Player.cpp's PlaybackControlEvent::PLAY) mid-cycle.
  state.resyncPlayhead();
  CHECK(state.isActive()); // held_notes_ untouched - the chord is still held

  // The very next render re-triggers immediately, back at step 0 - not
  // wherever the old, now-irrelevant cycle would otherwise have continued
  // from (step 1, at the original 2*unit boundary).
  state.renderVoices(1);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60});
}

// resyncPlayhead() arriving while a step is still ringing (legato, unlike
// the gapped case above) must not cut that note - and, per
// resyncIfNothingRinging()'s own comment (an earlier "remember it and
// catch up later" version was tried and reverted - real, reported drift),
// does not force a restart once the stepper does eventually advance
// either: the sequence just continues exactly as if the resync had never
// been requested.
TEST(arpeggiator_state_resync_playhead_while_ringing_does_not_disturb_the_running_sequence) {
  ChannelConfiguration config(44100);
  auto arp = makeArpeggiator("up", /*noteDuration=*/2, /*octaves=*/0, /*gate=*/2); // legato: no gap to defer into
  Oscillator instrument(WaveformType::SINE);
  ArpeggiatorState state(config, false, false, 0, 0, SphericalPosition{}, SendLevels{}, *arp);
  state.setBpm(120.0f);

  int unit = config.getSampleInterval(120);

  // Three-note chord: A (lowest), B, C (highest).
  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f, NoteOrigin::PATTERN);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f, NoteOrigin::PATTERN);
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f, NoteOrigin::PATTERN);
  state.endPatternRow();
  state.renderVoices(1);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{60}); // step 0 = A

  state.renderVoices(2 * unit); // crosses the step advance (2*unit) into step 1 (B) - still legato, no gap
  CHECK(activeNoteValues(state, 0) == std::vector<int>{64});

  // The transport seeks mid-step-1's own still-open legato gate.
  state.resyncPlayhead();

  // Step 1's own voice keeps ringing, unmangled - the seek must not cut it.
  state.renderVoices(1);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{64});

  // Once step 1's own natural boundary arrives, the sequence just
  // continues where it was already heading - step 2 (C) - not reset to a
  // fresh step 0.
  state.renderVoices(2 * unit);
  CHECK(activeNoteValues(state, 0) == std::vector<int>{67});
}

