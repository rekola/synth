#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/DrumMachineTrack.h"
#include "../src/model/Group.h"
#include "../src/model/SampleTrack.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/GenericInstrument.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace std;

namespace {

string readFile(const string & path) {
  ifstream in(path);
  ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace

// A <note>/<command> element's own "track" attribute is the only thing
// that survives a save/reload round trip if it names a track's raw
// internal id instead of a textual one - that id is just a runtime
// counter, reassigned fresh every time a Track object is constructed, so
// a reference to one is unresolvable the moment the file is reopened.
// Regression coverage for both halves of that: a track with an explicit
// textual id keeps being referenced by it, and a track with none gets one
// auto-assigned (Song::addTrack()) so its notes survive too.
TEST(note_round_trips_for_a_track_with_an_explicit_textual_id) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_note_textual_id_scratch.xml").string();

  // Tuning pinned explicitly (not the ambient default): this test is about
  // track-reference round-tripping, not tuning - a fixed tuning keeps the
  // expected raw note value below meaningful regardless of what the
  // default happens to be.
  Song song(Tuning::TET12);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  track.setId("chords");
  song.addScene();
  song.getScene(0).setNote(0, track.getInternalId(), 0, Note(60, 40));
  song.save(scratch_path);

  // The saved file must actually reference the track by its own textual
  // id, not a raw number - the literal, human-inspectable symptom the
  // bug this guards against was reported as.
  auto saved = readFile(scratch_path);
  CHECK(saved.find("track=\"chords\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded(Tuning::TET12);
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("chords");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & notes = reloaded.getScene(0).getNotes(0, reloaded_track->getInternalId());
    CHECK(notes.size() == 1);
    if (notes.size() == 1) CHECK(notes[0].getValue() == 60);
  }

  fs::remove(scratch_path);
}

TEST(note_round_trips_for_a_track_with_no_explicit_id) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_note_auto_id_scratch.xml").string();

  // Tuning pinned explicitly - see the previous test's own comment.
  Song song(Tuning::TET12);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  CHECK(!track.getId().empty()); // addTrack() must have assigned one
  song.addScene();
  song.getScene(0).setNote(0, track.getInternalId(), 0, Note(60, 40));
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded(Tuning::TET12);
  CHECK(reloaded.open(scratch_path, provider));

  CHECK(reloaded.getMasterTrack().getChildren().size() == 1);
  auto & reloaded_track = *reloaded.getMasterTrack().getChildren()[0];
  CHECK(reloaded_track.getId() == track.getId());
  auto & notes = reloaded.getScene(0).getNotes(0, reloaded_track.getInternalId());
  CHECK(notes.size() == 1);
  if (notes.size() == 1) CHECK(notes[0].getValue() == 60);

  fs::remove(scratch_path);
}

// Overwriting a row's only note with Note()'s undefined value is exactly
// what pasting a blank note-column selection does
// (PatternBlockOps::pastePatternBlockNotes) - Pattern::setNote() must drop
// the row from its sparse map rather than leave it holding nothing but
// that placeholder, since an undefined note's toString() text ("···")
// isn't parseable back by Note::stringToKey() on reload.
TEST(overwriting_a_note_with_an_undefined_value_leaves_no_stale_row_entry) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_undefined_note_scratch.xml").string();

  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.addScene();
  auto & scene = song.getScene(0);
  scene.setNote(6, track.getInternalId(), 0, Note(60, 40));
  scene.setNote(6, track.getInternalId(), 0, Note());
  CHECK(scene.getNotes(6, track.getInternalId()).empty());

  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("row=\"6\"") == string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider)); // must not crash/assert on reload

  fs::remove(scratch_path);
}

TEST(command_round_trips_for_a_track_with_an_explicit_textual_id) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_command_textual_id_scratch.xml").string();

  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  track.setId("bass");
  song.addScene();
  song.getScene(0).setCommand(0, track.getInternalId(), Command("V400"));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("track=\"bass\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("bass");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & command = reloaded.getScene(0).getCommand(0, reloaded_track->getInternalId());
    CHECK(command.isDefined());
    CHECK(to_string(command) == "V400");
  }

  fs::remove(scratch_path);
}

// Pattern length lives on Song, not per-Pattern (every pattern in a song
// shares it) - <song patternRows="N"> round-trips through save/reload,
// and a fresh Song defaults to 64 (matching the empty song
// Controller::switchToBuffer() creates for a not-yet-open buffer name).
TEST(pattern_length_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_pattern_length_scratch.xml").string();

  Song song;
  CHECK(song.getPatternLength() == 64);
  song.setPatternLength(32);
  song.addScene();
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("patternRows=\"32\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));
  CHECK(reloaded.getPatternLength() == 32);

  fs::remove(scratch_path);
}

TEST(add_track_assigns_distinct_ids_to_multiple_id_less_tracks) {
  Song song;
  auto & a = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & b = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & c = song.addTrack(make_unique<InstrumentTrack>(0));

  CHECK(!a.getId().empty());
  CHECK(!b.getId().empty());
  CHECK(!c.getId().empty());
  CHECK(a.getId() != b.getId());
  CHECK(b.getId() != c.getId());
  CHECK(a.getId() != c.getId());
}

TEST(add_track_leaves_an_explicit_id_untouched) {
  Song song;
  auto track = make_unique<InstrumentTrack>(0);
  track->setId("melody");
  auto & added = song.addTrack(move(track));
  CHECK(added.getId() == "melody");
}

// id/from/name are three independent attributes on <instrument> - id is
// Track/SongObject's own pre-existing identifier (untouched by the from/name
// split), from is the taxonomy-path resolution target, and name is an
// optional user-assigned label, distinct from both. Regression coverage for
// the one case in the real song corpus where id and a genuine (would-be)
// label question actually meet: songs/songtest11.xml's harp instrument,
// which has an id but - like every instrument migrated from the old
// overloaded-name format - no name, since none ever existed to preserve.
TEST(instrument_id_from_and_name_round_trip_independently) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_instrument_id_from_name_scratch.xml").string();

  Song song;
  auto harp = make_unique<GenericInstrument>();
  harp->setId("harp");
  harp->setFrom("string.plucked.harp");
  // name left unset - the common post-migration case: no label was ever
  // authored, so none should be invented.
  song.addInstrument(move(harp));

  auto labeled = make_unique<GenericInstrument>();
  labeled->setFrom("piano.electric.tine");
  labeled->setName("Solo instrument");
  song.addInstrument(move(labeled));

  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));
  CHECK(reloaded.getInstrumentPool().getInstruments().size() == 2);

  auto * reloaded_harp = dynamic_cast<GenericInstrument *>(reloaded.getInstrumentPool().getInstruments()[0].get());
  CHECK(reloaded_harp != nullptr);
  if (reloaded_harp) {
    CHECK(reloaded_harp->getId() == "harp");
    CHECK(reloaded_harp->getFrom() == "string.plucked.harp");
    CHECK(reloaded_harp->getName().empty());
  }

  auto * reloaded_labeled = dynamic_cast<GenericInstrument *>(reloaded.getInstrumentPool().getInstruments()[1].get());
  CHECK(reloaded_labeled != nullptr);
  if (reloaded_labeled) {
    CHECK(reloaded_labeled->getId().empty());
    CHECK(reloaded_labeled->getFrom() == "piano.electric.tine");
    CHECK(reloaded_labeled->getName() == "Solo instrument");
  }

  fs::remove(scratch_path);
}

// <generator> children - a recognized name round-trips keyed by its SF2
// generator id.
TEST(generator_override_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "generator_override_scratch.xml").string();

  Song song;
  auto instrument = make_unique<GenericInstrument>();
  instrument->setFrom("piano.acoustic.grand");
  instrument->addGeneratorOverride(SF2Generator::InitialFilterFc, 9000.0f);
  song.addInstrument(move(instrument));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("name=\"initialFilterFc\"") != string::npos);
  CHECK(saved.find("value=\"9000") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));
  CHECK(reloaded.getInstrumentPool().getInstruments().size() == 1);

  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstrumentPool().getInstruments()[0].get());
  CHECK(reloaded_instrument != nullptr);
  if (reloaded_instrument) {
    auto & overrides = reloaded_instrument->getGeneratorOverrides();
    auto it = overrides.find(SF2Generator::InitialFilterFc);
    CHECK(it != overrides.end());
    if (it != overrides.end()) CHECK_NEAR(it->second, 9000.0f, 1e-5f);
    CHECK(reloaded_instrument->getUnknownGeneratorOverrides().empty());
  }

  fs::remove(scratch_path);
}

// An unrecognized <generator> name is preserved, unapplied, rather than
// rejecting the file or being silently dropped - see SF2GeneratorTable.h's
// own doc comment for why.
TEST(unknown_generator_name_is_preserved_unapplied_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "unknown_generator_scratch.xml").string();

  Song song;
  auto instrument = make_unique<GenericInstrument>();
  instrument->setFrom("piano.acoustic.grand");
  instrument->addUnknownGeneratorOverride("totallyMadeUp", 5.0f);
  song.addInstrument(move(instrument));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("name=\"totallyMadeUp\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstrumentPool().getInstruments()[0].get());
  CHECK(reloaded_instrument != nullptr);
  if (reloaded_instrument) {
    CHECK(reloaded_instrument->getGeneratorOverrides().empty());
    auto & unknown = reloaded_instrument->getUnknownGeneratorOverrides();
    CHECK(unknown.size() == 1);
    if (unknown.size() == 1) {
      CHECK(unknown[0].first == "totallyMadeUp");
      CHECK_NEAR(unknown[0].second, 5.0f, 1e-5f);
    }
  }

  fs::remove(scratch_path);
}

// An <instrument> with no <generator> children at all must round-trip with
// both override containers empty - the "no override" case the whole
// mechanism has to stay bit-identical for.
TEST(no_generator_children_means_no_overrides_after_round_trip) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "no_generator_scratch.xml").string();

  Song song;
  auto instrument = make_unique<GenericInstrument>();
  instrument->setFrom("piano.acoustic.grand");
  song.addInstrument(move(instrument));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("<generator") == string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));
  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstrumentPool().getInstruments()[0].get());
  CHECK(reloaded_instrument != nullptr);
  if (reloaded_instrument) {
    CHECK(reloaded_instrument->getGeneratorOverrides().empty());
    CHECK(reloaded_instrument->getUnknownGeneratorOverrides().empty());
  }

  fs::remove(scratch_path);
}

TEST(volume_envelope_generator_overrides_round_trip_through_save_and_load) {
  // All 8 volume-envelope generator names/ids in one document - exercises
  // SF2GeneratorTable.h's lookup table end to end for every recognized
  // name beyond initialFilterFc (covered separately above).
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "volume_envelope_override_scratch.xml").string();

  const vector<pair<SF2Generator, float>> overrides = {
    { SF2Generator::DelayVolEnv, 100.0f },
    { SF2Generator::AttackVolEnv, 200.0f },
    { SF2Generator::HoldVolEnv, 300.0f },
    { SF2Generator::DecayVolEnv, 2400.0f },
    { SF2Generator::SustainVolEnv, 960.0f },
    { SF2Generator::ReleaseVolEnv, 1900.0f },
    { SF2Generator::KeynumToVolEnvHold, 80.0f },
    { SF2Generator::KeynumToVolEnvDecay, -80.0f },
  };

  Song song;
  auto instrument = make_unique<GenericInstrument>();
  instrument->setFrom("piano.electric.tine");
  for (auto & [id, value] : overrides) instrument->addGeneratorOverride(id, value);
  song.addInstrument(move(instrument));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("name=\"delayVolEnv\"") != string::npos);
  CHECK(saved.find("name=\"attackVolEnv\"") != string::npos);
  CHECK(saved.find("name=\"holdVolEnv\"") != string::npos);
  CHECK(saved.find("name=\"decayVolEnv\"") != string::npos);
  CHECK(saved.find("name=\"sustainVolEnv\"") != string::npos);
  CHECK(saved.find("name=\"releaseVolEnv\"") != string::npos);
  CHECK(saved.find("name=\"keynumToVolEnvHold\"") != string::npos);
  CHECK(saved.find("name=\"keynumToVolEnvDecay\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));
  CHECK(reloaded.getInstrumentPool().getInstruments().size() == 1);

  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstrumentPool().getInstruments()[0].get());
  CHECK(reloaded_instrument != nullptr);
  if (reloaded_instrument) {
    auto & round_tripped = reloaded_instrument->getGeneratorOverrides();
    CHECK(round_tripped.size() == overrides.size());
    for (auto & [id, value] : overrides) {
      auto it = round_tripped.find(id);
      CHECK(it != round_tripped.end());
      if (it != round_tripped.end()) CHECK_NEAR(it->second, value, 1e-5f);
    }
    CHECK(reloaded_instrument->getUnknownGeneratorOverrides().empty());
  }

  fs::remove(scratch_path);
}

TEST(recognized_and_unrecognized_generator_overrides_coexist_in_one_document) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "mixed_generator_scratch.xml").string();

  Song song;
  auto instrument = make_unique<GenericInstrument>();
  instrument->setFrom("piano.electric.tine");
  instrument->addGeneratorOverride(SF2Generator::DecayVolEnv, 2400.0f);
  instrument->addUnknownGeneratorOverride("someFutureGenerator", 42.0f); // unrecognized
  song.addInstrument(move(instrument));
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstrumentPool().getInstruments()[0].get());
  CHECK(reloaded_instrument != nullptr);
  if (reloaded_instrument) {
    auto & recognized = reloaded_instrument->getGeneratorOverrides();
    CHECK(recognized.size() == 1);
    auto it = recognized.find(SF2Generator::DecayVolEnv);
    CHECK(it != recognized.end());
    if (it != recognized.end()) CHECK_NEAR(it->second, 2400.0f, 1e-5f);

    auto & unknown = reloaded_instrument->getUnknownGeneratorOverrides();
    CHECK(unknown.size() == 1);
    if (unknown.size() == 1) {
      CHECK(unknown[0].first == "someFutureGenerator");
      CHECK_NEAR(unknown[0].second, 42.0f, 1e-5f);
    }
  }

  fs::remove(scratch_path);
}

// getScene() falls back to a shared, process-wide sentinel Scene for an
// out-of-range index (deliberately, for read-only callers - see its own
// comment) - getOrCreateScene() is the write-intent counterpart that
// actually grows the song instead, so a write aimed past the last real
// Scene lands in real, persisted content rather than silently aliasing
// into that sentinel (the exact bug PatternEditor's own note/annotation
// entry and PatternMatrix's yank hit before each was moved onto this).
TEST(get_or_create_scene_grows_the_song_up_to_the_requested_index) {
  Song song;
  CHECK(song.getScenes().size() == 0);

  auto & scene = song.getOrCreateScene(2);
  CHECK(song.getScenes().size() == 3);
  scene.setNote(0, 0, 0, Note(60, 100));

  // The same index now resolves to the exact Scene just written into, not
  // a second, distinct instance.
  CHECK(song.getScene(2).getNote(0, 0, 0).getValue() == 60);
}

TEST(get_or_create_scene_does_not_regrow_an_already_large_enough_song) {
  Song song;
  song.getOrCreateScene(4);
  CHECK(song.getScenes().size() == 5);

  // Asking for an earlier index must not truncate/replace what's already
  // there.
  auto & scene = song.getOrCreateScene(1);
  scene.setNote(0, 0, 0, Note(67, 100));
  CHECK(song.getScenes().size() == 5);
  CHECK(song.getScene(1).getNote(0, 0, 0).getValue() == 67);
}

TEST(get_or_create_scene_is_a_real_distinct_scene_not_the_shared_sentinel) {
  Song song_a, song_b;
  auto & scene_a = song_a.getOrCreateScene(0);
  scene_a.setNote(0, 0, 0, Note(60, 100));

  // A second, unrelated Song's own out-of-range write must never alias
  // into the same object song_a's write just landed in - the exact
  // failure mode of the old getScene()-for-writing bug (a single shared
  // static empty_scene_ instance, process-wide, not per-Song).
  auto & scene_b = song_b.getOrCreateScene(0);
  CHECK(scene_b.getNote(0, 0, 0).getValue() != 60);
}

// getTuningForTrack() is the single shared definition of "what does a
// Note::getValue() on this track actually mean" - Song.cpp's <pattern>
// reader/writer and PatternMatrix/PatternEditor's own clipboard
// cross-tuning refusal all rely on it agreeing with itself.
TEST(get_tuning_for_track_is_percussion_for_percussion_control) {
  Song song(Tuning::TET19);
  auto & track = song.addTrack(make_unique<PercussionTrack>());
  CHECK(song.getTuningForTrack(track) == Tuning::PERCUSSION);
}

TEST(get_tuning_for_track_is_percussion_for_drum_machine) {
  Song song(Tuning::TET19);
  auto & track = song.addTrack(make_unique<DrumMachineTrack>());
  CHECK(song.getTuningForTrack(track) == Tuning::PERCUSSION);
}

TEST(get_tuning_for_track_is_the_songs_own_tuning_otherwise) {
  Song song(Tuning::TET31);
  auto & track = song.addTrack(make_unique<InstrumentTrack>());
  CHECK(song.getTuningForTrack(track) == Tuning::TET31);
}

// A fresh Song always has a master track (Song::master_track_ is default-
// constructed, never null) - not something that needs to be added, and not
// one of its own children, so it never shows up among getRootTrackIds()'s
// real, artist-authored tracks by way of getMasterTrack().getChildren().
TEST(a_fresh_song_has_a_master_track) {
  Song song;
  CHECK(song.getMasterTrack().getType() == TrackType::MASTER);
  CHECK(song.getMasterTrack().getChildren().empty());
  CHECK(song.getMasterTrack().getId() == "master");
  // Matches Track::defaultCollapsed()'s own EFFECT/MASTER default - there's
  // no per-track content worth showing at full width until the artist
  // actually uses the one effect-command column.
  CHECK(song.getMasterTrack().isCollapsed());
}

// The master is never itself parsed from a discrete XML element (see
// MasterTrack.h), so its own attributes - just "collapsed" today - have to
// round-trip through <tracks>'s own attributes instead, or an explicit
// setCollapsed(false) would silently revert to the type default on reload.
TEST(master_tracks_collapsed_state_round_trips_via_the_tracks_element) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_master_collapsed_scratch.xml").string();

  Song song;
  song.getMasterTrack().setCollapsed(false);
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("collapsed=\"0\"") != string::npos || saved.find("collapsed=\"false\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));
  CHECK(!reloaded.getMasterTrack().isCollapsed());

  fs::remove(scratch_path);
}

// removeTrack()'s own comment: removeChildByInternalId() only ever erases
// from a children_ vector, and the master is never anyone's child, so this
// can structurally never remove it - no separate guard needed.
TEST(remove_track_on_the_masters_own_id_is_a_no_op) {
  Song song;
  auto master_id = song.getMasterTrack().getInternalId();
  CHECK(!song.removeTrack(master_id));
  CHECK(song.getMasterTrack().getType() == TrackType::MASTER);
}

// getPlayableTrackIds() is what a Launchpad pad addresses (not
// getRootTrackIds(), which a note has no way to land on for the master's
// own trailing column - see Song.h's own comment on both).
TEST(playable_track_ids_excludes_the_master_but_root_track_ids_includes_it) {
  Song song;
  auto & a = song.addTrack(make_unique<InstrumentTrack>(0));
  auto master_id = song.getMasterTrack().getInternalId();

  auto root_ids = song.getRootTrackIds();
  CHECK(root_ids.size() == 2);
  CHECK(root_ids.back() == master_id);

  auto playable_ids = song.getPlayableTrackIds();
  CHECK(playable_ids == vector<int>{ a.getInternalId() });
}

// addTrack()'s after_track_id parameter - a new track lands next to
// whatever the artist has selected, not always at the very end.
TEST(add_track_with_no_after_id_still_appends_at_the_end) {
  Song song;
  auto & a = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & b = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & children = song.getMasterTrack().getChildren();
  CHECK(children.size() == 2);
  CHECK(children[0].get() == &a);
  CHECK(children[1].get() == &b);
}

TEST(add_track_after_a_top_level_sibling_inserts_right_after_it) {
  Song song;
  auto & a = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & c = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & b = song.addTrack(make_unique<InstrumentTrack>(0), a.getInternalId());

  auto & children = song.getMasterTrack().getChildren();
  CHECK(children.size() == 3);
  CHECK(children[0].get() == &a);
  CHECK(children[1].get() == &b);
  CHECK(children[2].get() == &c);
}

// Matches "add it under the parent of the current track" - a track
// selected inside a Group gets its new sibling inside that same Group,
// not promoted to a top-level child of the master.
TEST(add_track_after_a_track_nested_in_a_group_inserts_inside_that_group) {
  Song song;
  auto & group = song.addTrack(make_unique<Group>());
  auto & inner = group.addChild(make_unique<InstrumentTrack>(0));

  auto & sibling = song.addTrack(make_unique<InstrumentTrack>(0), inner.getInternalId());

  CHECK(song.getMasterTrack().getChildren().size() == 1); // still just the group
  CHECK(group.getChildren().size() == 2);
  CHECK(group.getChildren()[0].get() == &inner);
  CHECK(group.getChildren()[1].get() == &sibling);
}

// A stale/unresolvable after_track_id (here, one that was never added at
// all) falls back to a plain append rather than silently dropping the
// new track.
TEST(add_track_after_an_unresolvable_id_falls_back_to_appending) {
  Song song;
  auto & a = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & b = song.addTrack(make_unique<InstrumentTrack>(0), 999999);

  auto & children = song.getMasterTrack().getChildren();
  CHECK(children.size() == 2);
  CHECK(children[0].get() == &a);
  CHECK(children[1].get() == &b);
}

TEST(add_track_after_a_sample_track_inserts_right_after_it) {
  Song song;
  auto & sample = song.addTrack(make_unique<SampleTrack>(nullptr));
  auto & sibling = song.addTrack(make_unique<InstrumentTrack>(0), sample.getInternalId());

  auto & children = song.getMasterTrack().getChildren();
  CHECK(children.size() == 2);
  CHECK(children[0].get() == &sample);
  CHECK(children[1].get() == &sibling);
}

// The master is never itself parsed from a discrete XML element (see
// MasterTrack.h's own comment) - a saved song has no <master> element at
// all, and reloading rebuilds exactly one master with its real children
// underneath, the same as a freshly constructed Song would.
TEST(master_track_is_not_a_discrete_xml_element_and_survives_reload) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_master_track_scratch.xml").string();

  Song song;
  song.addTrack(make_unique<InstrumentTrack>(0));
  song.addTrack(make_unique<PercussionTrack>());
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("<master") == string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  CHECK(reloaded.getMasterTrack().getType() == TrackType::MASTER);
  CHECK(reloaded.getMasterTrack().getChildren().size() == 2);

  fs::remove(scratch_path);
}

// Master's own effect-command Pattern is addressed by its reserved "master"
// id, same as any other track's textual id (trackReferenceText()/
// resolveTrackReference(), Song.cpp) - a <pattern track="master"> entry
// must resolve back to the same object after a reload.
TEST(master_tracks_own_command_round_trips_by_its_reserved_id) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_master_command_scratch.xml").string();

  Song song;
  song.addScene();
  song.getScene(0).setCommand(0, song.getMasterTrack().getInternalId(), Command("V400"));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("track=\"master\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto & command = reloaded.getScene(0).getCommand(0, reloaded.getMasterTrack().getInternalId());
  CHECK(command.isDefined());

  fs::remove(scratch_path);
}

TEST(a_fresh_song_has_no_clips_for_any_track) {
  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  CHECK(song.getClips(track.getInternalId()).empty());
  CHECK(song.getClips(-1).empty()); // an id that resolves to nothing at all
}

TEST(added_clips_are_grouped_by_track_in_the_order_added) {
  Song song;
  auto & drums = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & bass = song.addTrack(make_unique<InstrumentTrack>(1));

  Pattern fill;
  fill.setNote(0, 0, Note(36, 100));
  song.addClip(drums.getInternalId(), fill).setName("fill");

  Pattern groove;
  song.addClip(drums.getInternalId(), groove).setName("groove");

  Pattern walk;
  song.addClip(bass.getInternalId(), walk).setName("walk");

  auto & drum_clips = song.getClips(drums.getInternalId());
  CHECK(drum_clips.size() == 2);
  if (drum_clips.size() == 2) {
    CHECK(drum_clips[0].getName() == "fill");
    CHECK(drum_clips[1].getName() == "groove");
  }

  auto & bass_clips = song.getClips(bass.getInternalId());
  CHECK(bass_clips.size() == 1);
  if (bass_clips.size() == 1) CHECK(bass_clips[0].getName() == "walk");
}

// The write side omits <clips> entirely when the song has no clips (the
// same "default/empty state stores nothing" rule storeBusConfig() already
// follows) - most songs never use this feature, so a spurious empty
// element on every one of them would just be diff noise.
TEST(save_omits_the_clips_element_entirely_when_there_are_no_clips) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_no_clips_scratch.xml").string();

  Song song;
  song.addTrack(make_unique<InstrumentTrack>(0));
  song.addScene();
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("<clips") == string::npos);

  fs::remove(scratch_path);
}

TEST(clip_round_trips_its_name_length_notes_and_command_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_clip_scratch.xml").string();

  // Tuning pinned explicitly - see note_round_trips_for_a_track_with_an_
  // explicit_textual_id's own comment.
  Song song(Tuning::TET12);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  track.setId("drums");

  Pattern fill;
  fill.setNote(0, 0, Note(60, 100));
  fill.setNote(4, 0, Note(64, 90));
  fill.setCommand(2, Command("ZB04"));
  auto & new_clip = song.addClip(track.getInternalId(), fill);
  new_clip.setName("fill");
  new_clip.setLength(8);
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("<clips>") != string::npos);
  CHECK(saved.find("track=\"drums\"") != string::npos);
  CHECK(saved.find("name=\"fill\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded(Tuning::TET12);
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("drums");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & clips = reloaded.getClips(reloaded_track->getInternalId());
    CHECK(clips.size() == 1);
    if (clips.size() == 1) {
      auto & clip = clips[0];
      CHECK(clip.getName() == "fill");
      CHECK(clip.getLength() == 8);
      CHECK(clip.getLeafPattern().getNote(0, 0).getValue() == 60);
      CHECK(clip.getLeafPattern().getNote(4, 0).getValue() == 64);
      CHECK(clip.getLeafPattern().getCommand(2).isDefined());
    }
  }

  fs::remove(scratch_path);
}

// A clip with no name at all (the attribute itself omitted, not just
// empty) must still round-trip cleanly rather than erroring or picking up
// a stray value from a neighboring attribute.
TEST(a_clip_with_no_name_round_trips_with_an_empty_one) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_clip_no_name_scratch.xml").string();

  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  track.setId("drums");

  Pattern p;
  p.setNote(0, 0, Note(60, 100));
  song.addClip(track.getInternalId(), p);
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("drums");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & clips = reloaded.getClips(reloaded_track->getInternalId());
    CHECK(clips.size() == 1);
    if (clips.size() == 1) CHECK(clips[0].getName().empty());
  }

  fs::remove(scratch_path);
}

// A clip is unconnected to any scene - it must not leak into, or be
// confused with, a scene's own inline Pattern for the same track.
TEST(clips_are_independent_of_a_scenes_own_inline_pattern) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_clip_vs_scene_scratch.xml").string();

  // Tuning pinned explicitly - see note_round_trips_for_a_track_with_an_
  // explicit_textual_id's own comment.
  Song song(Tuning::TET12);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  track.setId("drums");
  song.addScene();
  song.getScene(0).setNote(0, track.getInternalId(), 0, Note(48, 100));

  Pattern clip_pattern;
  clip_pattern.setNote(0, 0, Note(60, 100));
  song.addClip(track.getInternalId(), clip_pattern).setName("fill");
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded(Tuning::TET12);
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("drums");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    // The scene's own note is untouched by the clip.
    auto & scene_notes = reloaded.getScene(0).getNotes(0, reloaded_track->getInternalId());
    CHECK(scene_notes.size() == 1);
    if (scene_notes.size() == 1) CHECK(scene_notes[0].getValue() == 48);

    // The clip is untouched by the scene's own note.
    auto & clips = reloaded.getClips(reloaded_track->getInternalId());
    CHECK(clips.size() == 1);
    if (clips.size() == 1) CHECK(clips[0].getLeafPattern().getNote(0, 0).getValue() == 60);
  }

  fs::remove(scratch_path);
}
