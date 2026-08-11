#include "TestFramework.h"

#include "../ArpeggiatorState.h"
#include "../Arpeggiator.h"
#include "../Oscillator.h"
#include "../MemoryParameterSource.h"
#include "../ChannelConfiguration.h"
#include "../SphericalPosition.h"
#include "../SendLevels.h"
#include "../ActiveVoiceInfo.h"

#include <memory>
#include <vector>
#include <algorithm>
#include <unordered_map>

// Exercises ArpeggiatorState directly through the public Track/TrackState
// API only - the same shape tests/EnvelopeFilterTests.cpp's
// makeEnvelopeFilterState() helper uses - with no Player/event queue/Song
// involved, since Phase 1 (plans/arpeggiator.md) makes the stepper
// reachable from live audition input alone, never pattern data. An
// Arpeggiator is now a track kind (InstrumentTrack.h), not an
// instrument-wrapping node - the instrument it steps through is a plain
// sibling object, passed into noteOn() directly, the same way Player.cpp
// passes whatever this track's own instrument_id_ resolves to.

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
  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f);
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f);
  CHECK(state.isActive());

  // Checkpoints deliberately land strictly inside each interval, never on
  // a step/gate boundary itself (see plans/arpeggiator.md's ArpeggiatorState
  // design for why boundaries are resolved lazily, at the start of the
  // next render() call).
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

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f);
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f);

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

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f);
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f);

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

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f);
  state.noteOn(1, instrument, 550.0f, 0.8f, 64, 0.0f);

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
  state.noteOn(2, instrument, 660.0f, 0.8f, 67, 0.0f);
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

  state.noteOn(0, instrument, 440.0f, 0.8f, 60, 0.0f);

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
