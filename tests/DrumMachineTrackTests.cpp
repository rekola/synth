#include "TestFramework.h"

#include "../DrumMachineTrack.h"
#include "../Song.h"
#include "../InstrumentProvider.h"
#include "../OfflineRenderer.h"
#include "../ChannelConfiguration.h"
#include "../SongState.h"
#include "../MixerFactory.h"
#include "../MixerType.h"
#include "../Mixer.h"

#include <filesystem>
#include <cmath>
#include <algorithm>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace std;

TEST(add_lane_derives_order_from_the_rank_table_not_insertion_order) {
  DrumMachineTrack track;
  for (int note : { 49, 36, 42, 38 }) track.addLane(note);
  vector<int> expected = { 36, 38, 42, 49 };
  CHECK(track.getLaneNotes() == expected);
}

TEST(add_lane_is_a_no_op_when_the_lane_already_exists) {
  DrumMachineTrack track;
  track.addLane(36);
  track.setStep(36, 0, true);
  track.addLane(36); // must not reset the existing lane's steps
  CHECK(track.getLaneNotes().size() == 1);
  CHECK(track.getSteps(36) == 0b00000001);
}

TEST(remove_lane_deletes_only_that_lanes_step_data) {
  // The single most important property this data model has to guarantee
  // (plans/drum-machine.md's own risk list): step data is keyed by GM
  // note number, never by lane index, so removing/adding a lane must
  // never disturb any *other* lane's steps.
  DrumMachineTrack track;
  track.addLane(36);
  track.addLane(38);
  track.addLane(42);
  track.setStep(36, 0, true);
  track.setStep(38, 2, true);
  track.setStep(42, 4, true);

  track.removeLane(38);

  CHECK(!track.hasLane(38));
  CHECK(track.getSteps(38) == 0); // gone
  CHECK(track.getSteps(36) == 0b00000001); // untouched
  CHECK(track.getSteps(42) == 0b00010000); // untouched

  vector<int> expected = { 36, 42 };
  CHECK(track.getLaneNotes() == expected);
}

TEST(removing_and_re_adding_a_lane_starts_it_all_rest_again) {
  DrumMachineTrack track;
  track.addLane(36);
  track.setStep(36, 0, true);
  track.removeLane(36);
  track.addLane(36);
  CHECK(track.getSteps(36) == 0);
}

TEST(add_then_remove_a_different_lane_leaves_every_survivor_byte_identical) {
  // Phase 6's own risk: the picker adds/removes lanes one at a time in
  // response to individual pad presses, in whatever order the user
  // happens to press them - not just "add a batch, then remove one" like
  // remove_lane_deletes_only_that_lanes_step_data above. Interleave
  // several add/remove calls and check survivors after every single step,
  // not just at the end.
  DrumMachineTrack track;
  track.addLane(36);
  track.setStep(36, 0, true);
  track.addLane(42);
  track.setStep(42, 2, true);
  track.addLane(38);
  track.setStep(38, 4, true);

  track.removeLane(42); // remove the middle one first
  CHECK(track.getSteps(36) == 0b00000001);
  CHECK(track.getSteps(38) == 0b00010000);
  CHECK(!track.hasLane(42));

  track.addLane(49);
  track.setStep(49, 6, true);
  CHECK(track.getSteps(36) == 0b00000001); // still untouched
  CHECK(track.getSteps(38) == 0b00010000); // still untouched

  track.removeLane(36); // remove the first-added lane
  CHECK(!track.hasLane(36));
  CHECK(track.getSteps(38) == 0b00010000); // untouched
  CHECK(track.getSteps(49) == 0b01000000); // untouched

  vector<int> expected = { 38, 49 }; // rank order, not insertion/removal order
  CHECK(track.getLaneNotes() == expected);
}

TEST(add_lane_stops_at_kMaxLanes_since_the_step_grid_has_exactly_that_many_rows) {
  // The picker itself has no cap of its own - it just calls addLane()/
  // removeLane() (LaunchpadManager::handleDrumPickerPadEvent) - so the
  // limit has to live here, matching the step grid's fixed 8-row display
  // (LaunchpadManager.cpp's step-grid rendering branch).
  DrumMachineTrack track;
  vector<int> notes = { 35, 36, 38, 40, 42, 46, 49, 51, 57 }; // 9 distinct GM percussion notes
  CHECK(static_cast<int>(notes.size()) > DrumMachineTrack::kMaxLanes);
  for (int note : notes) track.addLane(note);

  CHECK(static_cast<int>(track.getLaneNotes().size()) == DrumMachineTrack::kMaxLanes);
  CHECK(!track.hasLane(57)); // the 9th, over the cap, never got added

  // Still a silent no-op, not a crash or a partial mutation, when pressed
  // again while full.
  track.addLane(57);
  CHECK(static_cast<int>(track.getLaneNotes().size()) == DrumMachineTrack::kMaxLanes);

  // Freeing a slot lets exactly one more back in.
  track.removeLane(35);
  CHECK(static_cast<int>(track.getLaneNotes().size()) == DrumMachineTrack::kMaxLanes - 1);
  track.addLane(57);
  CHECK(track.hasLane(57));
  CHECK(static_cast<int>(track.getLaneNotes().size()) == DrumMachineTrack::kMaxLanes);
}

TEST(seed_default_kit_populates_the_rock_kit_all_rest) {
  DrumMachineTrack track;
  CHECK(track.getLaneNotes().empty());

  track.seedDefaultKit();
  // Rank-table order (DrumRankTable.cpp: bass drum, snare, toms low-to-
  // high, hi-hats closed-to-open, cymbals) - happens to match this
  // particular kit's own note list order, but derived via getLaneNotes(),
  // not assumed.
  vector<int> expected = { 36, 38, 45, 47, 50, 42, 46, 49 };
  CHECK(track.getLaneNotes() == expected);
  for (int note : expected) CHECK(track.getSteps(note) == 0); // all-rest

  // A no-op-safe top-up, not a reset: existing lanes (and any step data
  // already programmed into them) are untouched, and a lane already at
  // kMaxLanes elsewhere in the default set is simply skipped like any
  // other already-assigned note (addLane()'s own no-op rule).
  track.setStep(36, 0, true);
  track.seedDefaultKit();
  CHECK(track.getSteps(36) == 0b00000001);
  CHECK(track.getLaneNotes() == expected);
}

TEST(removing_every_lane_leaves_the_track_with_zero_lanes_and_no_crash) {
  DrumMachineTrack track;
  track.addLane(36);
  track.addLane(38);
  track.addLane(42);
  track.setStep(36, 0, true);
  track.setStep(38, 0, true);
  track.setStep(42, 0, true);

  track.removeLane(38);
  track.removeLane(36);
  track.removeLane(42); // removing the last remaining lane

  CHECK(track.getLaneNotes().empty());
  CHECK(track.getHitNotesForRow(0).empty());
  // Re-removing an already-absent lane, and removing from an
  // already-empty track, must both stay no-ops rather than misbehaving.
  track.removeLane(42);
  track.removeLane(99);
  CHECK(track.getLaneNotes().empty());
}

TEST(set_step_and_set_steps_only_affect_lanes_that_exist) {
  DrumMachineTrack track;
  track.setStep(36, 0, true); // no lane yet - must not create phantom step data
  CHECK(track.getSteps(36) == 0);
  CHECK(!track.hasLane(36));

  track.addLane(36);
  track.setStep(36, 0, true);
  track.setStep(36, 7, true);
  CHECK(track.getSteps(36) == 0b10000001);
  track.setStep(36, 0, false);
  CHECK(track.getSteps(36) == 0b10000000);
}

namespace {

InstrumentProvider makeProvider() { return InstrumentProvider(); }

// Mirrors the production steps-string convention exactly (leftmost
// character = step 0, chronological reading order - see
// Song.cpp's loadDrumMachineData/storeDrumMachineData), so test
// expectations are written against the same source-of-truth strings the
// fixture file uses instead of hand-transcribed binary literals (easy to
// get backwards - see git history of this file).
uint8_t stepsFromString(const char * s) {
  uint8_t steps = 0;
  for (int i = 0; s[i] != 0 && i < 8; i++) {
    if (s[i] == '1') steps = static_cast<uint8_t>(steps | (1u << i));
  }
  return steps;
}

} // namespace

TEST(drum_machine_track_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "drum_machine_round_trip_scratch.xml").string();

  Song song;
  auto & track = dynamic_cast<DrumMachineTrack &>(song.addTrack(make_unique<DrumMachineTrack>()));
  track.setSequenceId("my_sequence");
  for (int note : { 49, 36, 42, 38 }) track.addLane(note);
  track.setSteps(36, 0b10001000);
  track.setSteps(38, 0b00001000);
  track.setSteps(42, 0b10101010);
  // 49 stays all-rest (0), exercising the all-zero-steps case on save/load.

  song.setPatternLength(8);
  song.addPattern();
  song.save(scratch_path);

  auto provider = makeProvider();
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  CHECK(reloaded.getTracks().size() == 1);
  auto & reloaded_track = dynamic_cast<DrumMachineTrack &>(*reloaded.getTracks()[0]);

  CHECK(reloaded_track.getElementName() == std::string("drumMachineTrack"));
  CHECK(reloaded_track.getSequenceId() == "my_sequence");
  CHECK(reloaded_track.getLoopLength() == 8);

  vector<int> expected_order = { 36, 38, 42, 49 };
  CHECK(reloaded_track.getLaneNotes() == expected_order);
  CHECK(reloaded_track.getSteps(36) == 0b10001000);
  CHECK(reloaded_track.getSteps(38) == 0b00001000);
  CHECK(reloaded_track.getSteps(42) == 0b10101010);
  CHECK(reloaded_track.getSteps(49) == 0);

  fs::remove(scratch_path);
}

TEST(drum_machine_track_lane_order_on_load_is_derived_not_stored) {
  // The fixture's <lane> elements are deliberately out of rank order
  // (49, 36, 42, 38) - loading must still produce DrumRankTable order,
  // proving lane order is re-derived at load time rather than trusted
  // from whatever sequence the file happens to list lanes in.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_track.xml", provider));

  CHECK(song.getTracks().size() == 1);
  auto & track = dynamic_cast<DrumMachineTrack &>(*song.getTracks()[0]);

  vector<int> expected_order = { 36, 38, 42, 49 };
  CHECK(track.getLaneNotes() == expected_order);
  // Matches drum_machine_track.xml's own steps="..." attribute strings
  // exactly (leftmost character = step 0 - see stepsFromString above).
  CHECK(track.getSteps(36) == stepsFromString("10001000"));
  CHECK(track.getSteps(38) == stepsFromString("00001000"));
  CHECK(track.getSteps(42) == stepsFromString("10101010"));
  CHECK(track.getSteps(49) == stepsFromString("00000000"));
}

TEST(drum_machine_track_with_no_sequence_element_at_all_loads_the_default_kit) {
  // A hand-authored <drumMachineTrack id="0" instrument="0"/> with no
  // <drumMachine> child at all - the file never says anything about
  // lanes, so it should get the same default rock kit the interactive
  // "add-drum-machine-track" command would (seedDefaultKit()), not a
  // silent, lane-less track.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_track_no_sequence_element.xml", provider));

  CHECK(song.getTracks().size() == 1);
  auto & track = dynamic_cast<DrumMachineTrack &>(*song.getTracks()[0]);

  vector<int> expected = { 36, 38, 45, 47, 50, 42, 46, 49 };
  CHECK(track.getLaneNotes() == expected);
  for (int note : expected) CHECK(track.getSteps(note) == 0); // all-rest
}

TEST(drum_machine_track_with_an_explicit_but_empty_sequence_element_stays_empty) {
  // Contrast with the no-element case above: a <drumMachine> element that
  // IS present, just with zero <lane> children, means the file is being
  // explicit that this kit has no lanes - the default rock kit must not
  // fill in behind it.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_track_explicit_empty_sequence.xml", provider));

  CHECK(song.getTracks().size() == 1);
  auto & track = dynamic_cast<DrumMachineTrack &>(*song.getTracks()[0]);
  CHECK(track.getLaneNotes().empty());
}

TEST(drum_machine_track_appears_in_get_root_track_ids) {
  // Song::getRootTrackIds() is the shared source of "which tracks are
  // columns" for the tracker view, LaunchpadManager, and UI - without a
  // DRUM_MACHINE branch here, a DrumMachineTrack would never appear as a
  // column at all, making the tracker-view placeholder rendering
  // (PatternEditor.cpp's SAMPLE/DRUM_MACHINE branches) unreachable.
  Song song;
  auto & track = song.addTrack(make_unique<DrumMachineTrack>());
  auto ids = song.getRootTrackIds();
  CHECK(ids.size() == 1);
  CHECK(ids[0] == track.getInternalId());
}

// --- Phase 4: getHitNotesForRow() itself - pure, no audio engine needed ---

TEST(get_hit_notes_for_row_returns_the_lit_lanes_at_each_step) {
  DrumMachineTrack track;
  track.addLane(36);
  track.addLane(38);
  track.setSteps(36, stepsFromString("10001000"));
  track.setSteps(38, stepsFromString("00001000"));

  CHECK(track.getHitNotesForRow(0) == (vector<int>{ 36 }));
  CHECK(track.getHitNotesForRow(1).empty());
  CHECK(track.getHitNotesForRow(4) == (vector<int>{ 36, 38 }));
  CHECK(track.getHitNotesForRow(7).empty());
}

TEST(get_hit_notes_for_row_wraps_at_the_loop_length) {
  DrumMachineTrack track;
  track.addLane(36);
  track.setSteps(36, stepsFromString("10000000")); // step 0 only

  CHECK(track.getHitNotesForRow(0) == track.getHitNotesForRow(8));
  CHECK(track.getHitNotesForRow(0) == track.getHitNotesForRow(800));
  CHECK(track.getHitNotesForRow(1).empty());
  CHECK(!track.getHitNotesForRow(8).empty());
}

TEST(get_hit_notes_for_row_is_a_pure_function_with_no_hidden_state) {
  // Calling with a huge/unrelated row value first must not affect a later
  // call with a small one - this is the entire seek-correctness invariant
  // (plans/drum-machine.md): nothing here may accumulate between calls.
  DrumMachineTrack track;
  track.addLane(42);
  track.setSteps(42, stepsFromString("00100000"));

  auto before = track.getHitNotesForRow(2);
  track.getHitNotesForRow(1000000);
  track.getHitNotesForRow(3);
  track.getHitNotesForRow(999);
  auto after = track.getHitNotesForRow(2);
  CHECK(before == after);
  CHECK(!before.empty());
}

TEST(get_hit_notes_for_row_is_empty_with_no_lanes_or_no_steps_lit) {
  DrumMachineTrack track;
  CHECK(track.getHitNotesForRow(0).empty());

  track.addLane(36);
  CHECK(track.getHitNotesForRow(0).empty()); // lane exists but starts all-rest
}

TEST(get_hit_notes_for_row_is_empty_when_loop_length_is_non_positive) {
  DrumMachineTrack track;
  track.addLane(36);
  track.setSteps(36, 0xFF);
  track.setLoopLength(0);
  CHECK(track.getHitNotesForRow(0).empty());
  track.setLoopLength(-1);
  CHECK(track.getHitNotesForRow(0).empty());
}

// --- Phase 4: SongState wiring - real audio, via a fixture ---

namespace {

// Renders exactly one row's worth of samples and returns the peak absolute
// sample value across every output channel - "was anything audible in
// this row" without needing bit-exact waveform comparison (which would be
// fragile here: playNote()'s start_phase argument is randomized per note-on,
// see InstrumentTrackState.h).
float renderRowPeak(SongState & state, const Song & song, Mixer & mixer, int row_samples) {
  state.render(row_samples, song, mixer);
  auto master = mixer.encode();
  float peak = 0.0f;
  for (int c = 0; c < mixer.getOutChannels(); c++) {
    auto data = master.getChannelData(c);
    for (int i = 0; i < row_samples; i++) peak = std::max(peak, std::fabs(data[i]));
  }
  return peak;
}

constexpr float kAudiblePeak = 1e-3f;
constexpr float kSilentPeak = 1e-4f;

} // namespace

TEST(drum_machine_track_seek_directly_to_a_later_repetition_still_triggers_the_right_hits) {
  // The actual risk this whole phase is about: seeking straight to a row
  // deep into a loop - never having rendered any row before it - must
  // still compute the correct hits for that row. Two entirely independent
  // SongState instances, neither of which ever renders row 0..23, so
  // there is no possible "warm-up" history to lean on.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_track_32rows.xml", provider));

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());

  {
    // Row 24 (24 % 8 == 0): lanes 36 and 42 both hit per the fixture's steps.
    auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    state.setPosition(24);
    CHECK(renderRowPeak(state, song, *mixer, row_samples) > kAudiblePeak);
  }
  {
    // Row 25 (25 % 8 == 1): no lane has a step lit at index 1.
    auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    state.setPosition(25);
    CHECK(renderRowPeak(state, song, *mixer, row_samples) < kSilentPeak);
  }
}

TEST(drum_machine_track_loop_truncates_at_the_end_of_a_short_pattern) {
  // 20 rows, one lane hit only at step 4 - repetitions land at rows 4 and
  // 12; a would-be third repetition at row 20 never happens because the
  // pattern ends at row 19. Exactly 2 audible onsets, not 3.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_track_20rows.xml", provider));

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int hits = 0;
  for (int row = 0; row < 20; row++) {
    if (renderRowPeak(state, song, *mixer, row_samples) > kAudiblePeak) hits++;
  }
  CHECK(hits == 2);
}

TEST(drum_machine_track_loop_phase_resets_at_each_pattern_boundary) {
  // Every pattern is 5 rows (patternRows="5" - all patterns in a song
  // share one length now), loop_length=8 - the lane hits only at step 0.
  // If phase correctly resets to pattern-relative row 0 at each boundary,
  // pattern 2's own first row hits again immediately (absolute row 5); if
  // it wrongly continued counting from the absolute row instead, the next
  // hit wouldn't land until absolute row 8.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_two_patterns.xml", provider));

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int hits = 0;
  for (int row = 0; row < 7; row++) { // pattern 1's 5 rows + pattern 2's first 2
    if (renderRowPeak(state, song, *mixer, row_samples) > kAudiblePeak) hits++;
  }
  CHECK(hits == 2);
}

TEST(drum_machine_track_retrigger_chokes_the_previous_hit_instead_of_stacking) {
  // Lane 36 hits at rows 0 and 8 (loop_length 8, step 0 only), with a
  // long (2s) release and nonzero sustain - a hit only note-on's, never
  // note-off's, so voice #1 would sit audibly at its sustain level
  // forever if retriggerVoices() didn't force a fast release on it when
  // the second hit lands.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_retrigger.xml", provider));

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  for (int row = 0; row < 8; row++) state.render(row_samples, song, *mixer); // rows 0..7
  // getVoiceCount() counts the whole active node tree (this track's own
  // state node plus each voice's envelope/oscillator wrapper chain), not
  // "number of notes" - so the right baseline to compare against is
  // whatever one ringing voice's own tree measures as, not a hardcoded
  // constant tied to this fixture's specific instrument chain depth.
  int one_voice_worth = state.getVoiceCount();
  CHECK(one_voice_worth > 0);

  state.render(row_samples, song, *mixer); // row 8 - second hit, must choke voice #1
  // A little more than the ~10ms fast-release window, comfortably less
  // than one row's own 125ms.
  state.render(static_cast<int>(0.05f * config.getAudioOutSampleRate()), song, *mixer);

  // Back to exactly one voice's worth - voice #1 was choked and cleaned
  // up, not left ringing forever alongside voice #2 (which is what an
  // unbounded doubling towards two voices' worth would mean here).
  CHECK(state.getVoiceCount() == one_voice_worth);
}

TEST(drum_machine_track_removed_down_to_zero_lanes_renders_silence_without_crashing) {
  // Phase 6's picker can remove lanes from a live track (e.g. every pad
  // pressed off, one at a time) all the way down to none. SongState's
  // per-row loop (SongState.h) calls getHitNotesForRow() on whatever
  // DrumMachineTrack it finds via getRootTrackIds() every row regardless
  // of lane count - this must degrade to "emits nothing", not crash.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_track_32rows.xml", provider));

  Track * raw_track = song.getTrackById("0");
  CHECK(raw_track != nullptr);
  auto & track = static_cast<DrumMachineTrack &>(*raw_track);
  for (int note : { 36, 38, 42, 49 }) track.removeLane(note);
  CHECK(track.getLaneNotes().empty());

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  for (int row = 0; row < 32; row++) {
    CHECK(renderRowPeak(state, song, *mixer, row_samples) < kSilentPeak);
  }
}
