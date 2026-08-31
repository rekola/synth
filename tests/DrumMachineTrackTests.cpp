#include "TestFramework.h"

#include "../src/model/DrumMachineTrack.h"
#include "../src/model/Song.h"
#include "../src/model/Pattern.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/SoundFont.h"
#include "Sf2Fixture.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/state/SongState.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"

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
  track.addLane(36);
  CHECK(track.getLaneNotes().size() == 1);
}

TEST(remove_lane_deletes_the_notes_referencing_it_from_every_scene) {
  // The single most important property this data model has to guarantee:
  // step data is keyed by GM note number, never by lane index, so
  // removing a lane must never disturb any *other* lane's steps, and
  // removeLane() must fan out across every scene, not just the current
  // one.
  Song song;
  auto & track = dynamic_cast<DrumMachineTrack &>(song.addTrack(make_unique<DrumMachineTrack>()));
  track.addLane(36);
  track.addLane(38);
  track.addLane(42);
  song.setPatternLength(8);
  song.getOrCreateScene(0).setNote(0, track.getInternalId(), 0, Note(36, 100));
  song.getOrCreateScene(0).setNote(2, track.getInternalId(), 0, Note(38, 100));
  song.getOrCreateScene(1).setNote(4, track.getInternalId(), 0, Note(42, 100));
  song.getOrCreateScene(1).setNote(4, track.getInternalId(), 1, Note(38, 100));

  track.removeLane(38, song);

  CHECK(!track.hasLane(38));
  CHECK(!song.getScene(0).getNote(2, track.getInternalId(), 0).isDefined());
  CHECK(song.getScene(0).getNote(0, track.getInternalId(), 0).getValue() == 36); // untouched
  CHECK(song.getScene(1).getNote(4, track.getInternalId(), 0).getValue() == 42); // untouched
  CHECK(!song.getScene(1).getNote(4, track.getInternalId(), 1).isDefined()); // the other scene's own 38 is gone too

  vector<int> expected = { 36, 42 };
  CHECK(track.getLaneNotes() == expected);
}

TEST(remove_lane_deletes_the_notes_referencing_it_from_every_clip_too) {
  // Same guarantee as the scene-background test above, extended to a
  // clip's own leaf Pattern - step data placed there (ArrangementOps.h)
  // is just as real as a scene's own background content, and a lane
  // removal must clean it up the same way.
  Song song;
  auto & track = dynamic_cast<DrumMachineTrack &>(song.addTrack(make_unique<DrumMachineTrack>()));
  track.addLane(36);
  track.addLane(38);

  Clip clip(track.getInternalId());
  clip.getLeafPattern().setNote(0, 0, Note(36, 100));
  clip.getLeafPattern().setNote(2, 0, Note(38, 100));
  auto & added = song.addClip(move(clip));

  track.removeLane(38, song);

  CHECK(!track.hasLane(38));
  auto & pattern = added.getLeafPattern();
  CHECK(!pattern.getNote(2, 0).isDefined()); // the clip's own 38 is gone
  CHECK(pattern.getNote(0, 0).getValue() == 36); // untouched
}

TEST(removing_and_re_adding_a_lane_starts_it_with_no_notes) {
  Song song;
  auto & track = dynamic_cast<DrumMachineTrack &>(song.addTrack(make_unique<DrumMachineTrack>()));
  track.addLane(36);
  song.setPatternLength(8);
  song.getOrCreateScene(0).setNote(0, track.getInternalId(), 0, Note(36, 100));
  track.removeLane(36, song);
  track.addLane(36);
  CHECK(!song.getScene(0).getNote(0, track.getInternalId(), 0).isDefined());
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
  Song song;
  track.removeLane(35, song);
  CHECK(static_cast<int>(track.getLaneNotes().size()) == DrumMachineTrack::kMaxLanes - 1);
  track.addLane(57);
  CHECK(track.hasLane(57));
  CHECK(static_cast<int>(track.getLaneNotes().size()) == DrumMachineTrack::kMaxLanes);
}

TEST(seed_default_kit_populates_the_rock_kit) {
  DrumMachineTrack track;
  CHECK(track.getLaneNotes().empty());

  track.seedDefaultKit();
  // Rank-table order (DrumRankTable.cpp: bass drum, snare, toms low-to-
  // high, hi-hats closed-to-open, cymbals) - happens to match this
  // particular kit's own note list order, but derived via getLaneNotes(),
  // not assumed.
  vector<int> expected = { 36, 38, 45, 47, 50, 42, 46, 49 };
  CHECK(track.getLaneNotes() == expected);

  // A no-op-safe top-up, not a reset: a lane already present (from
  // wherever) stays, and a lane already at kMaxLanes elsewhere in the
  // default set is simply skipped like any other already-assigned note
  // (addLane()'s own no-op rule).
  track.seedDefaultKit();
  CHECK(track.getLaneNotes() == expected);
}

TEST(removing_every_lane_leaves_the_track_with_zero_lanes_and_no_crash) {
  Song song;
  auto & track = dynamic_cast<DrumMachineTrack &>(song.addTrack(make_unique<DrumMachineTrack>()));
  track.addLane(36);
  track.addLane(38);
  track.addLane(42);

  track.clearAllLanes(song);

  CHECK(track.getLaneNotes().empty());
  Pattern empty_pattern;
  CHECK(track.getHitNotesForRow(empty_pattern, 0, 8).empty());
  // Re-removing an already-absent lane, and removing from an
  // already-empty track, must both stay no-ops rather than misbehaving.
  track.removeLane(42, song);
  track.removeLane(99, song);
  CHECK(track.getLaneNotes().empty());
}

TEST(drum_machine_track_appears_in_get_root_track_ids) {
  // Song::getRootTrackIds() is the shared source of "which tracks are
  // columns" for the tracker view, LaunchpadManager, and UI.
  Song song;
  auto & track = song.addTrack(make_unique<DrumMachineTrack>());
  auto ids = song.getRootTrackIds();
  CHECK(ids.size() == 2);
  CHECK(ids[0] == track.getInternalId());
  CHECK(ids[1] == song.getMasterTrack().getInternalId());
}

// --- getHitNotesForRow() itself - pure, no audio engine needed ---

TEST(get_hit_notes_for_row_returns_the_lit_lanes_at_each_step) {
  DrumMachineTrack track;
  track.addLane(36);
  track.addLane(38);

  Pattern pattern;
  pattern.setNote(0, 0, Note(36, 100));
  pattern.setNote(4, 0, Note(36, 100));
  pattern.setNote(4, 1, Note(38, 100));

  CHECK(track.getHitNotesForRow(pattern, 0, 8) == (vector<int>{ 36 }));
  CHECK(track.getHitNotesForRow(pattern, 1, 8).empty());
  CHECK(track.getHitNotesForRow(pattern, 4, 8) == (vector<int>{ 36, 38 }));
  CHECK(track.getHitNotesForRow(pattern, 7, 8).empty());
}

TEST(get_hit_notes_for_row_wraps_at_the_pattern_length) {
  DrumMachineTrack track;
  track.addLane(36);

  Pattern pattern;
  pattern.setNote(0, 0, Note(36, 100)); // step 0 only

  CHECK(track.getHitNotesForRow(pattern, 0, 8) == track.getHitNotesForRow(pattern, 8, 8));
  CHECK(track.getHitNotesForRow(pattern, 0, 8) == track.getHitNotesForRow(pattern, 800, 8));
  CHECK(track.getHitNotesForRow(pattern, 1, 8).empty());
  CHECK(!track.getHitNotesForRow(pattern, 8, 8).empty());
}

TEST(get_hit_notes_for_row_is_a_pure_function_with_no_hidden_state) {
  // Calling with a huge/unrelated row value first must not affect a later
  // call with a small one - nothing here may accumulate between calls.
  DrumMachineTrack track;
  track.addLane(42);

  Pattern pattern;
  pattern.setNote(2, 0, Note(42, 100));

  auto before = track.getHitNotesForRow(pattern, 2, 8);
  track.getHitNotesForRow(pattern, 1000000, 8);
  track.getHitNotesForRow(pattern, 3, 8);
  track.getHitNotesForRow(pattern, 999, 8);
  auto after = track.getHitNotesForRow(pattern, 2, 8);
  CHECK(before == after);
  CHECK(!before.empty());
}

TEST(get_hit_notes_for_row_is_empty_with_no_lanes_or_no_matching_notes) {
  DrumMachineTrack track;
  Pattern pattern;
  pattern.setNote(0, 0, Note(36, 100));
  CHECK(track.getHitNotesForRow(pattern, 0, 8).empty()); // no lanes at all

  track.addLane(38); // a lane that isn't the note actually present
  CHECK(track.getHitNotesForRow(pattern, 0, 8).empty());
}

TEST(get_hit_notes_for_row_ignores_a_note_left_over_from_a_removed_lane) {
  // A note whose value doesn't match any current lane_notes_ (e.g. pasted
  // in from a track with a different kit, or left behind by a since-
  // removed lane) stays silently inert rather than firing.
  DrumMachineTrack track;
  track.addLane(36);

  Pattern pattern;
  pattern.setNote(0, 0, Note(36, 100));
  pattern.setNote(0, 1, Note(38, 100)); // not one of this track's lanes

  CHECK(track.getHitNotesForRow(pattern, 0, 8) == (vector<int>{ 36 }));
}

// --- SongState wiring - real audio, via a fixture ---

namespace {

// Renders exactly one row's worth of samples and returns the peak absolute
// sample value across every output channel - "was anything audible in
// this row" without needing bit-exact waveform comparison (which would be
// fragile here: playNote()'s start_phase argument is randomized per note-on,
// see InstrumentTrackState.h).
float renderRowPeak(SongState & state, const Song & song, Mixer & mixer, int row_samples) {
  state.renderBlock(row_samples, song, mixer);
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

// A DrumMachineTrack (like PercussionTrack) has no per-track instrument_id_
// any more - every note plays through the pool's one default kit
// (InstrumentPool::getDefaultKitInstrument(), resolved from `provider` via
// the song's <instruments from="..."> - see Song::open()). None of these
// fixtures set that attribute, so it defaults to path "kit" - registering
// a fast-decaying synthetic SF2 preset directly under that literal key
// (registerPath(), same call InstrumentProvider::resolvePath() ends up
// walking to) makes song.open() resolve it exactly the same way a real
// kit.* SoundFont path would, without needing an actual GM font on disk.
// SF2, not a plain Oscillator or an <envelope> wrapper: registerPath()
// only accepts a shared_ptr<Instrument>, and only SoundFontInstrument (via
// SoundFont::createInstrument()) gives volume-envelope decay without
// needing an Effect wrapper. Needed only by the tests below that count
// audible rows over time (decay matters); every other fixture in this
// file just checks whether a note fires at all, which doesn't depend on
// how it decays.
void registerFastDecayKit(InstrumentProvider & provider) {
  using namespace sf2fixture;
  // Timecents = 1200*log2(seconds) - GeneratorOverrideTests.cpp's own
  // DecayVolEnv=2400.0f/"4s base decay time" data point confirms the
  // scale (2^(2400/1200) = 4). ~1ms attack, ~5ms hold, ~5ms decay to
  // silence, comfortably inside one row - fast enough that a hit row is
  // loud and every row after it is back below kSilentPeak.
  std::vector<PresetSpec> presets = {
    { "FastDecayKit", 0, {
      GenSpec{ 34, -12000 }, // AttackVolEnv ~ 1ms
      GenSpec{ 35, -9200 },  // HoldVolEnv ~ 5ms
      GenSpec{ 36, -9200 },  // DecayVolEnv ~ 5ms
      GenSpec{ 37, 1000 },   // SustainVolEnv ~ -100dB, effectively silent
      GenSpec{ 38, -9200 },  // ReleaseVolEnv ~ 5ms - never actually reached (no note-off)
    } },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "drum_machine_fast_decay_kit_fixture.sf2").string();
  writeMinimalSf2(path, presets);
  // SoundFont sf(path) itself need not outlive this call - the returned
  // Instrument holds its own shared_ptr<SoundFontFile> (SoundFontInstrument's
  // own ctor argument, SoundFont.cpp's createInstrument()), independent of
  // the SoundFont wrapper object's own lifetime.
  SoundFont sf(path);
  provider.registerPath("kit", sf.createInstrument(0));
}

} // namespace

TEST(drum_machine_track_seek_directly_to_a_later_repetition_still_triggers_the_right_hits) {
  // The actual risk this whole feature is about: seeking straight to a row
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
    // Row 24 (24 % 8 == 0): BD and CH both hit per the fixture's pattern.
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
  // Pattern length 8 inside a 20-row scene, one lane hit only at step 4 -
  // repetitions land at rows 4 and 12; a would-be third repetition at row
  // 20 never happens because the scene ends at row 19. Exactly 2 audible
  // onsets, not 3.
  InstrumentProvider provider;
  registerFastDecayKit(provider);
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

TEST(drum_machine_track_switches_pattern_content_at_each_scene_boundary) {
  // Two scenes, each 5 rows (patternRows="5"), with genuinely different
  // content: scene 0 hits BD at row 0, scene 1 hits SD at row 1 - proving
  // both that scene 1's own Pattern actually takes over (not a leftover
  // copy of scene 0's) and that row indexing resets to scene-relative 0
  // at the boundary (absolute row 5 = scene 1's own row 0).
  InstrumentProvider provider;
  registerFastDecayKit(provider);
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_two_patterns.xml", provider));

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int hits = 0;
  for (int row = 0; row < 7; row++) { // scene 0's 5 rows + scene 1's first 2
    if (renderRowPeak(state, song, *mixer, row_samples) > kAudiblePeak) hits++;
  }
  CHECK(hits == 2); // absolute row 0 (scene 0's BD) and absolute row 6 (scene 1's SD)
}

TEST(drum_machine_track_retrigger_chokes_the_previous_hit_instead_of_stacking) {
  // Lane BD hits at rows 0 and 8 (pattern length 8, one step lit), with a
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

  for (int row = 0; row < 8; row++) state.renderBlock(row_samples, song, *mixer); // rows 0..7
  // getVoiceCount() counts the whole active node tree (this track's own
  // state node plus each voice's envelope/oscillator wrapper chain), not
  // "number of notes" - so the right baseline to compare against is
  // whatever one ringing voice's own tree measures as, not a hardcoded
  // constant tied to this fixture's specific instrument chain depth.
  int one_voice_worth = state.getVoiceCount();
  CHECK(one_voice_worth > 0);

  state.renderBlock(row_samples, song, *mixer); // row 8 - second hit, must choke voice #1
  // A little more than the ~10ms fast-release window, comfortably less
  // than one row's own 125ms.
  state.renderBlock(static_cast<int>(0.05f * config.getAudioOutSampleRate()), song, *mixer);

  // Back to exactly one voice's worth - voice #1 was choked and cleaned
  // up, not left ringing forever alongside voice #2 (which is what an
  // unbounded doubling towards two voices' worth would mean here).
  CHECK(state.getVoiceCount() == one_voice_worth);
}

TEST(drum_machine_track_removed_down_to_zero_lanes_renders_silence_without_crashing) {
  // The lane picker can remove lanes from a live track (e.g. every pad
  // pressed off, one at a time) all the way down to none. Playback stays
  // fully generic (SongState.h's per-track loop doesn't special-case
  // DrumMachineTrack at all) - removing every lane just deletes every
  // note referencing it, so this must degrade to "emits nothing," not
  // crash.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/drum_machine_track_32rows.xml", provider));

  Track * raw_track = song.getMasterTrack().getChildById("0");
  CHECK(raw_track != nullptr);
  auto & track = static_cast<DrumMachineTrack &>(*raw_track);
  for (int note : vector<int>(track.getLaneNotes())) track.removeLane(note, song);
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
