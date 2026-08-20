#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
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

  Song song;
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
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getTrackById("chords");
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

  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  CHECK(!track.getId().empty()); // addTrack() must have assigned one
  song.addScene();
  song.getScene(0).setNote(0, track.getInternalId(), 0, Note(60, 40));
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  CHECK(reloaded.getTracks().size() == 1);
  auto & reloaded_track = *reloaded.getTracks()[0];
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

  auto reloaded_track = reloaded.getTrackById("bass");
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
  CHECK(reloaded.getInstruments().size() == 2);

  auto * reloaded_harp = dynamic_cast<GenericInstrument *>(reloaded.getInstruments()[0].get());
  CHECK(reloaded_harp != nullptr);
  if (reloaded_harp) {
    CHECK(reloaded_harp->getId() == "harp");
    CHECK(reloaded_harp->getFrom() == "string.plucked.harp");
    CHECK(reloaded_harp->getName().empty());
  }

  auto * reloaded_labeled = dynamic_cast<GenericInstrument *>(reloaded.getInstruments()[1].get());
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
  CHECK(reloaded.getInstruments().size() == 1);

  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstruments()[0].get());
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

  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstruments()[0].get());
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
  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstruments()[0].get());
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
  CHECK(reloaded.getInstruments().size() == 1);

  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstruments()[0].get());
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

  auto * reloaded_instrument = dynamic_cast<GenericInstrument *>(reloaded.getInstruments()[0].get());
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
