#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/ArrangementOps.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/Group.h"
#include "../src/model/SampleTrack.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/GenericInstrument.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/model/SampleContent.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

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
  song.addSection();
  song.getSection(0).setNote(0, track.getInternalId(), 0, Note(60, 40));
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
    auto & notes = reloaded.getSection(0).getNotes(0, reloaded_track->getInternalId());
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
  song.addSection();
  song.getSection(0).setNote(0, track.getInternalId(), 0, Note(60, 40));
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded(Tuning::TET12);
  CHECK(reloaded.open(scratch_path, provider));

  CHECK(reloaded.getMasterTrack().getChildren().size() == 1);
  auto & reloaded_track = *reloaded.getMasterTrack().getChildren()[0];
  CHECK(reloaded_track.getId() == track.getId());
  auto & notes = reloaded.getSection(0).getNotes(0, reloaded_track.getInternalId());
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
  song.addSection();
  auto & section = song.getSection(0);
  section.setNote(6, track.getInternalId(), 0, Note(60, 40));
  section.setNote(6, track.getInternalId(), 0, Note());
  CHECK(section.getNotes(6, track.getInternalId()).empty());

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
  song.addSection();
  song.getSection(0).setCommand(0, track.getInternalId(), Command("0V40"));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("track=\"bass\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("bass");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & command = reloaded.getSection(0).getCommand(0, reloaded_track->getInternalId());
    CHECK(command.isDefined());
    CHECK(to_string(command) == "0V40");
  }

  fs::remove(scratch_path);
}

// A second command column writes an explicit column="1" attribute
// (matching <note column="...">'s own convention) - column 0 stays
// attribute-less, so every pre-existing song file (with at most one
// command per row) round-trips byte-for-byte unchanged.
TEST(command_multi_column_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_command_multi_column_scratch.xml").string();

  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  track.setId("bass");
  song.addSection();
  song.getSection(0).setCommand(0, track.getInternalId(), Command("0K05"));
  song.getSection(0).setCommand(0, track.getInternalId(), 1, Command("1V40"));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("column=\"1\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  // A reload's own internal ids aren't guaranteed to match the original
  // song's (command_round_trips_for_a_track_with_an_explicit_textual_id
  // above establishes the same "look it up by its stable textual id"
  // convention) - track.getInternalId() above is only ever valid against
  // `song`, never `reloaded`.
  auto reloaded_track = reloaded.getMasterTrack().getChildById("bass");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & reloaded_command_0 = reloaded.getSection(0).getCommand(0, reloaded_track->getInternalId(), 0);
    auto & reloaded_command_1 = reloaded.getSection(0).getCommand(0, reloaded_track->getInternalId(), 1);
    CHECK(to_string(reloaded_command_0) == "0K05");
    CHECK(to_string(reloaded_command_1) == "1V40");
  }

  fs::remove(scratch_path);
}

// A section's own length lives on Section, not Song - each one round-trips
// its own <section length="N"> (in bars) independently, and a fresh Section
// defaults to 4 bars (Section.h's own compiled default) when never given an
// explicit length of its own - there's no more song-wide fallback to
// migrate from ("patternRows" is no longer read or written at all).
TEST(section_length_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "scene_length_scratch.xml").string();

  Song song;
  auto & sized = song.addSection();
  sized.setLengthBars(7);
  auto & defaulted = song.addSection();
  CHECK(defaulted.getLengthBars() == 4);
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("length=\"7\"") != string::npos);
  CHECK(saved.find("length=\"4\"") != string::npos); // always written, even at the compiled default - a section's own length is never optional/implicit
  CHECK(saved.find("patternRows") == string::npos); // never written any more

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));
  CHECK(reloaded.getSections().size() == 2);
  CHECK(reloaded.getSections()[0].getLengthBars() == 7);
  CHECK(reloaded.getSections()[1].getLengthBars() == 4);

  fs::remove(scratch_path);
}

TEST(normalize_position_walks_across_sections_of_differing_lengths) {
  Song song;
  song.setRowsPerBar(1); // 1 bar = 1 row, so bars below read directly as rows
  song.addSection().setLengthBars(3); // section 0: rows 0-2
  song.addSection().setLengthBars(5); // section 1: rows 0-4 (absolute 3-7)
  song.addSection(); // section 2: the compiled default (4 bars/rows, absolute 8-11)

  CHECK(song.normalizePosition(0, 0) == (pair<int, int>{ 0, 0 }));
  CHECK(song.normalizePosition(0, 2) == (pair<int, int>{ 0, 2 })); // section 0's own last row
  CHECK(song.normalizePosition(0, 3) == (pair<int, int>{ 1, 0 })); // crosses into section 1
  CHECK(song.normalizePosition(0, 7) == (pair<int, int>{ 1, 4 })); // section 1's own last row
  CHECK(song.normalizePosition(0, 8) == (pair<int, int>{ 2, 0 })); // crosses into section 2
  CHECK(song.normalizePosition(0, 11) == (pair<int, int>{ 2, 3 }));
  CHECK(song.normalizePosition(0, 12) == (pair<int, int>{ 3, 0 })); // past every real section - a virtual continuation, same default length
}

TEST(to_absolute_row_is_the_exact_inverse_of_normalize_position) {
  Song song;
  song.setRowsPerBar(1);
  song.addSection().setLengthBars(3);
  song.addSection().setLengthBars(5);
  song.addSection().setLengthBars(2);

  for (auto [ section_idx, row ] : { pair<int,int>{0,0}, {0,2}, {1,0}, {1,4}, {2,0}, {2,1} }) {
    auto absolute = song.toAbsoluteRow(section_idx, row);
    CHECK(song.normalizePosition(0, absolute) == (pair<int, int>{ section_idx, row }));
  }
}

TEST(clamp_row_to_current_pattern_clamps_into_the_sections_own_bounds) {
  Song song;
  song.setRowsPerBar(1);
  song.addSection().setLengthBars(3);  // rows 0-2
  song.addSection().setLengthBars(5);  // rows 0-4 (absolute 3-7)

  // Inside section 1 (a non-default-length section): clamped into [3, 7].
  CHECK(song.clampRowToCurrentPattern(5, 0) == 3);
  CHECK(song.clampRowToCurrentPattern(5, 100) == 7);
  CHECK(song.clampRowToCurrentPattern(5, 4) == 4); // already inside - untouched
}

TEST(current_track_id_defaults_unset_and_round_trips_through_a_plain_set) {
  Song song;
  CHECK(song.getCurrentTrackId() == -1);
  song.setCurrentTrackId(42);
  CHECK(song.getCurrentTrackId() == 42);
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

// A pool slot's own custom description (Instrument::getDescription(),
// OutlineView's own Song > Instruments Details panel override) round-trips
// independently of every other attribute, and stays absent when never
// authored (no empty description="" invented on save).
TEST(instrument_description_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "instrument_description_scratch.xml").string();

  Song song;
  auto described = make_unique<GenericInstrument>();
  described->setFrom("piano.acoustic.grand");
  described->setDescription("My own detuned upright, warmed up for track 3.");
  song.addInstrument(move(described));

  auto undescribed = make_unique<GenericInstrument>();
  undescribed->setFrom("string.plucked.harp");
  song.addInstrument(move(undescribed));

  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("description=") != string::npos);
  CHECK(saved.find("description=") == saved.rfind("description=")); // exactly one instance, on the described slot only

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));
  CHECK(reloaded.getInstrumentPool().getInstruments().size() == 2);

  auto * reloaded_described = dynamic_cast<GenericInstrument *>(reloaded.getInstrumentPool().getInstruments()[0].get());
  CHECK(reloaded_described != nullptr);
  if (reloaded_described) CHECK(reloaded_described->getDescription() == "My own detuned upright, warmed up for track 3.");

  auto * reloaded_undescribed = dynamic_cast<GenericInstrument *>(reloaded.getInstrumentPool().getInstruments()[1].get());
  CHECK(reloaded_undescribed != nullptr);
  if (reloaded_undescribed) CHECK(reloaded_undescribed->getDescription().empty());

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

// getSection() falls back to a shared, process-wide sentinel Section for an
// out-of-range index (deliberately, for read-only callers - see its own
// comment) - getOrCreateSection() is the write-intent counterpart that
// actually grows the song instead, so a write aimed past the last real
// Section lands in real, persisted content rather than silently aliasing
// into that sentinel.
TEST(get_or_create_section_grows_the_song_up_to_the_requested_index) {
  Song song;
  CHECK(song.getSections().size() == 0);

  auto & section = song.getOrCreateSection(2);
  CHECK(song.getSections().size() == 3);
  section.setNote(0, 0, 0, Note(60, 100));

  // The same index now resolves to the exact Section just written into, not
  // a second, distinct instance.
  CHECK(song.getSection(2).getNote(0, 0, 0).getValue() == 60);
}

TEST(get_or_create_section_does_not_regrow_an_already_large_enough_song) {
  Song song;
  song.getOrCreateSection(4);
  CHECK(song.getSections().size() == 5);

  // Asking for an earlier index must not truncate/replace what's already
  // there.
  auto & section = song.getOrCreateSection(1);
  section.setNote(0, 0, 0, Note(67, 100));
  CHECK(song.getSections().size() == 5);
  CHECK(song.getSection(1).getNote(0, 0, 0).getValue() == 67);
}

TEST(get_or_create_section_is_a_real_distinct_section_not_the_shared_sentinel) {
  Song song_a, song_b;
  auto & scene_a = song_a.getOrCreateSection(0);
  scene_a.setNote(0, 0, 0, Note(60, 100));

  // A second, unrelated Song's own out-of-range write must never alias
  // into the same object song_a's write just landed in - the exact
  // failure mode of the old getSection()-for-writing bug (a single shared
  // static empty_scene_ instance, process-wide, not per-Song).
  auto & scene_b = song_b.getOrCreateSection(0);
  CHECK(scene_b.getNote(0, 0, 0).getValue() != 60);
}

// getTuningForTrack() is the single shared definition of "what does a
// Note::getValue() on this track actually mean" - Song.cpp's <pattern>
// reader/writer and PatternEditor's own clipboard cross-tuning refusal
// both rely on it agreeing with itself.
TEST(get_tuning_for_track_is_percussion_for_percussion_control) {
  Song song(Tuning::TET19);
  auto & track = song.addTrack(make_unique<PercussionTrack>());
  CHECK(song.getTuningForTrack(track) == Tuning::PERCUSSION);
}

TEST(get_tuning_for_track_is_percussion_for_drum_machine) {
  // Percussion tuning regardless of lane count - a step-sequenced
  // PercussionTrack here, unlike the lane-less one just above.
  Song song(Tuning::TET19);
  auto & track = dynamic_cast<PercussionTrack &>(song.addTrack(make_unique<PercussionTrack>()));
  track.addLane(36);
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
  auto & sample = song.addTrack(make_unique<SampleTrack>());
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
  song.addSection();
  song.getSection(0).setCommand(0, song.getMasterTrack().getInternalId(), Command("0V40"));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("track=\"master\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto & command = reloaded.getSection(0).getCommand(0, reloaded.getMasterTrack().getInternalId());
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

  Clip fill(drums.getInternalId());
  fill.getLeafPattern().setNote(0, 0, Note(36, 100));
  song.addClip(std::move(fill)).setName("fill");

  Clip groove(drums.getInternalId());
  song.addClip(std::move(groove)).setName("groove");

  Clip walk(bass.getInternalId());
  song.addClip(std::move(walk)).setName("walk");

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
  song.addSection();
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

  Clip fill(track.getInternalId());
  fill.getLeafPattern().setNote(0, 0, Note(60, 100));
  fill.getLeafPattern().setNote(4, 0, Note(64, 90));
  fill.getLeafPattern().setCommand(2, Command("ZB04"));
  fill.setName("fill");
  fill.setLength(8);
  song.addClip(std::move(fill));
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

  Clip p(track.getInternalId());
  p.getLeafPattern().setNote(0, 0, Note(60, 100));
  song.addClip(std::move(p));
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

// A clip is unconnected to any section - it must not leak into, or be
// confused with, a section's own inline Pattern for the same track.
TEST(clips_are_independent_of_a_sections_own_inline_pattern) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_clip_vs_scene_scratch.xml").string();

  // Tuning pinned explicitly - see note_round_trips_for_a_track_with_an_
  // explicit_textual_id's own comment.
  Song song(Tuning::TET12);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  track.setId("drums");
  song.addSection();
  song.getSection(0).setNote(0, track.getInternalId(), 0, Note(48, 100));

  Clip clip(track.getInternalId());
  clip.getLeafPattern().setNote(0, 0, Note(60, 100));
  song.addClip(std::move(clip)).setName("fill");
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded(Tuning::TET12);
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("drums");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    // The section's own note is untouched by the clip.
    auto & scene_notes = reloaded.getSection(0).getNotes(0, reloaded_track->getInternalId());
    CHECK(scene_notes.size() == 1);
    if (scene_notes.size() == 1) CHECK(scene_notes[0].getValue() == 48);

    // The clip is untouched by the section's own note.
    auto & clips = reloaded.getClips(reloaded_track->getInternalId());
    CHECK(clips.size() == 1);
    if (clips.size() == 1) CHECK(clips[0].getLeafPattern().getNote(0, 0).getValue() == 60);
  }

  fs::remove(scratch_path);
}

// The arrangement layer's own instance events - a real clip reference and
// an explicit stop, round-tripped through the same <section> a <pattern>/
// <annotation> already lives in.
TEST(instance_events_round_trip_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_instance_events_scratch.xml").string();

  Song song(Tuning::TET12);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  track.setId("drums");
  Clip clip(track.getInternalId());
  clip.getLeafPattern(); // the mutable overload creates it - see Clip.h's own comment on why every real clip needs one
  auto clip_id = song.addClip(move(clip)).getId();
  auto & section = song.addSection();
  section.setInstance(track.getInternalId(), 0, clip_id);
  section.setInstance(track.getInternalId(), 16, "OFF");
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("<arrangement") != string::npos);
  CHECK(saved.find("track=\"drums\"") != string::npos);
  CHECK(saved.find(">OFF<") != string::npos);
  CHECK(saved.find(">" + clip_id + "<") != string::npos);

  InstrumentProvider provider;
  Song reloaded(Tuning::TET12);
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("drums");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & reloaded_scene = reloaded.getSection(0);
    CHECK(reloaded_scene.getInstance(reloaded_track->getInternalId(), 0) == clip_id);
    CHECK(reloaded_scene.getInstance(reloaded_track->getInternalId(), 16) == "OFF");
    // The clip's own id survived the round trip too, so the instance
    // above still resolves to a real clip, not a dangling reference.
    CHECK(resolveInstanceAt(reloaded, reloaded_scene, reloaded_track->getInternalId(), 0).clip_index == 0);
  }

  fs::remove(scratch_path);
}

// A SampleTrack clip's own audio round-trips through save/load as a
// sidecar .wav plus a <sample file="..."> reference - Song::save()
// writes the buffer out and points the XML at it; Song::open() reads it
// back via loadMonoSample(), never resampling either way (Clip's own
// getNativeSampleRate() is what's actually checked, not just that the
// frame count matches).
TEST(sample_clip_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_sample_clip_scratch.xml").string();
  auto scratch_samples_dir = fs::path(TESTS_SCRATCH_DIR) / "song_sample_clip_scratch.samples";

  Song song(Tuning::TET12);
  auto & track = song.addTrack(make_unique<SampleTrack>());
  track.setId("vox");

  constexpr int kFrames = 8;
  auto buffer = make_shared<AudioBuffer>(1, kFrames);
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < kFrames; i++) data[i] = static_cast<float>(i) / kFrames - 0.5f;

  Clip clip(track.getInternalId());
  auto & content = clip.getSampleContent();
  content.setBuffer(buffer);
  content.setNativeSampleRate(48000);
  content.setInPoint(0.01f);
  content.setOutPoint(0.02f);
  content.setOriginalTempo(120);
  clip.setName("Take 1");
  auto clip_id = song.addClip(move(clip)).getId();
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("<sample ") != string::npos);
  CHECK(saved.find("file=\"") != string::npos);
  CHECK(saved.find("<pattern") == string::npos); // a sample clip has no note content to write
  CHECK(fs::is_directory(scratch_samples_dir));
  CHECK(fs::exists(scratch_samples_dir / (clip_id + ".wav")));

  InstrumentProvider provider;
  Song reloaded(Tuning::TET12);
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("vox");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & clips = reloaded.getClips(reloaded_track->getInternalId());
    CHECK(clips.size() == 1);
    if (clips.size() == 1) {
      auto & reloaded_content = clips[0].getSampleContent();
      CHECK(reloaded_content.getBuffer() != nullptr);
      CHECK(reloaded_content.getNativeSampleRate() == 48000);
      CHECK_NEAR(reloaded_content.getInPoint(), 0.01f, 1e-5f);
      CHECK_NEAR(reloaded_content.getOutPoint(), 0.02f, 1e-5f);
      CHECK(reloaded_content.getOriginalTempo() == 120);
      if (reloaded_content.getBuffer()) {
        CHECK(reloaded_content.getBuffer()->numberOfFrames() == kFrames);
        auto reloaded_data = reloaded_content.getBuffer()->getChannelData(0);
        for (int i = 0; i < kFrames; i++) CHECK_NEAR(reloaded_data[i], data[i], 1e-5f);
      }
    }
  }

  fs::remove(scratch_path);
  fs::remove_all(scratch_samples_dir);
}

// A SampleTrack's own background bed (Section::getOrCreateSampleBackgroundContent(),
// written by mergeClipToBackground()) round-trips through save/load the
// same way a real clip's own audio does - a sidecar .wav plus a
// <sampleBackground file="..."> reference, this time nested inside the
// <section> that owns it. The section gets a real id of its own the moment
// the merge creates the background bed (Song::generateUniqueSectionId()),
// so the sidecar filename survives independent of the section's own
// ordinal position.
TEST(sample_background_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_sample_background_scratch.xml").string();
  auto scratch_samples_dir = fs::path(TESTS_SCRATCH_DIR) / "song_sample_background_scratch.samples";

  Song song;
  song.setRowsPerBar(4);
  auto & track = song.addTrack(make_unique<SampleTrack>());
  track.setId("bed");

  constexpr int kSourceFrames = 50;
  auto buffer = make_shared<AudioBuffer>(1, kSourceFrames);
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < kSourceFrames; i++) data[i] = static_cast<float>(i) / kSourceFrames - 0.5f;

  Clip sample_clip(track.getInternalId());
  sample_clip.setLength(2);
  sample_clip.setLooping(false);
  sample_clip.getSampleContent().setBuffer(buffer);
  sample_clip.getSampleContent().setNativeSampleRate(44100); // real clips always have one by the time they're playable
  song.addClip(move(sample_clip)); // index 0

  auto & section = song.addSection();
  section.setLengthBars(1); // 4 rows total
  placeClipInstance(song, section, track.getInternalId(), 0, 0);

  ChannelConfiguration channel_config;
  CHECK(mergeClipToBackground(song, section, track.getInternalId(), 0, channel_config) == true);
  auto scene_id = section.getId();
  CHECK(!scene_id.empty());

  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("<sampleBackground ") != string::npos);
  CHECK(fs::is_directory(scratch_samples_dir));

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getMasterTrack().getChildById("bed");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & reloaded_scene = reloaded.getSection(0);
    CHECK(reloaded_scene.getId() == scene_id); // survived the round trip, not regenerated
    auto * reloaded_content = reloaded_scene.getSampleBackgroundContent(reloaded_track->getInternalId());
    CHECK(reloaded_content != nullptr);
    if (reloaded_content) {
      CHECK(reloaded_content->getBuffer() != nullptr);
      if (reloaded_content->getBuffer()) {
        for (int i = 0; i < kSourceFrames; i++) CHECK_NEAR(reloaded_content->getBuffer()->getChannelData(0)[i], data[i], 1e-5f);
      }
    }
  }

  fs::remove(scratch_path);
  fs::remove_all(scratch_samples_dir);
}

// SampleContent::storeParameters()'s in/out trim points use the same
// omit-when-default ParameterSource::set() convention as everything else
// here - a clip nobody ever trimmed shouldn't accumulate in="0" out="0"
// noise, and a genuinely trimmed one must still round-trip exactly.
TEST(sample_clip_trim_points_are_omitted_from_xml_when_left_at_their_default) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_sample_clip_default_trim_scratch.xml").string();
  auto scratch_samples_dir = fs::path(TESTS_SCRATCH_DIR) / "song_sample_clip_default_trim_scratch.samples";

  Song song(Tuning::TET12);
  auto & track = song.addTrack(make_unique<SampleTrack>());
  auto buffer = make_shared<AudioBuffer>(1, 4);
  Clip clip(track.getInternalId());
  clip.getSampleContent().setBuffer(buffer); // in/out left at their 0.0f defaults
  song.addClip(move(clip));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("<sample ") != string::npos);
  CHECK(saved.find("in=\"") == string::npos);
  CHECK(saved.find("out=\"") == string::npos);

  fs::remove(scratch_path);
  fs::remove_all(scratch_samples_dir);
}

// Deleting a sample clip is purely in-memory (deleteClip()'s own
// contract - nothing on disk changes as a side effect of an edit); its
// sidecar .wav only actually disappears once the song is saved again,
// the same moment every other edit here is already expected to wait for
// before touching disk. Song::save() sweeps it as an orphan then, since
// it no longer corresponds to any live clip.
TEST(deleting_a_sample_clip_only_removes_its_sidecar_file_on_next_save) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_sample_clip_delete_scratch.xml").string();
  auto scratch_samples_dir = fs::path(TESTS_SCRATCH_DIR) / "song_sample_clip_delete_scratch.samples";

  Song song(Tuning::TET12);
  auto & track = song.addTrack(make_unique<SampleTrack>());
  auto buffer = make_shared<AudioBuffer>(1, 4);
  Clip clip(track.getInternalId());
  clip.getSampleContent().setBuffer(buffer);
  auto clip_id = song.addClip(move(clip)).getId();
  song.save(scratch_path);
  auto sidecar_path = scratch_samples_dir / (clip_id + ".wav");
  CHECK(fs::exists(sidecar_path));

  deleteClip(song, track.getInternalId(), 0);
  // A fresh, id-less filler in place (Song::ensureClipAt()'s own "hole"
  // state) - not removed outright, since holes are allowed and every
  // other track's own scene rows are indexed against this same list.
  CHECK(song.getClips(track.getInternalId()).size() == 1);
  CHECK(song.getClips(track.getInternalId())[0].isEmpty());
  CHECK(fs::exists(sidecar_path)); // still there - deleteClip() never touches disk

  song.save(scratch_path);
  CHECK(!fs::exists(sidecar_path)); // swept as an orphan on the next save

  fs::remove(scratch_path);
  fs::remove_all(scratch_samples_dir);
}

// The write side omits <arrangement> entirely when a section has no
// instance events - same "default/empty state stores nothing" rule
// storeBusConfig()/the clip pool already follow.
TEST(save_omits_the_arrangement_element_when_a_section_has_no_instances) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_no_instances_scratch.xml").string();

  Song song;
  song.addTrack(make_unique<InstrumentTrack>(0));
  song.addSection();
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("<arrangement") == string::npos);

  fs::remove(scratch_path);
}

// Song::removeInstrument() (OutlineView.cpp's own Delete action on a pool
// Instruments row) - erasing a pool slot shifts every later slot's own
// index down by one, so every InstrumentTrack::instrument_id_ in the tree
// has to be reindexed against that shift, not just the pool vector
// itself. Three tracks pointing at three different pool slots (below,
// at, and above the one being removed) exercise all three outcomes at
// once.
TEST(remove_instrument_reindexes_every_instrument_track_in_the_tree) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));   // index 0
  song.addInstrument(make_unique<Oscillator>(WaveformType::SQUARE)); // index 1 - the one that gets removed
  song.addInstrument(make_unique<Oscillator>(WaveformType::SAW));    // index 2

  auto & below = song.addTrack(make_unique<InstrumentTrack>(0));
  auto & at = song.addTrack(make_unique<InstrumentTrack>(1));
  auto & above = song.addTrack(make_unique<InstrumentTrack>(2));

  song.removeInstrument(1);

  CHECK(song.getInstrumentPool().getInstruments().size() == 2);
  // Untouched - it never pointed past the removed slot.
  CHECK(dynamic_cast<InstrumentTrack &>(below).getInstrumentId() == 0);
  // Was pointing exactly at the removed slot - invalidated (getByIndex()'s
  // own "nothing authored" sentinel), not left dangling at a now-different
  // instrument.
  CHECK(dynamic_cast<InstrumentTrack &>(at).getInstrumentId() == -1);
  // Was pointing past the removed slot - decremented by one, so it still
  // resolves to the same real instrument (formerly index 2, now index 1).
  CHECK(dynamic_cast<InstrumentTrack &>(above).getInstrumentId() == 1);
}

// A nested track (inside a Group) must be reindexed too - the walk isn't
// scoped to root tracks only the way removeTrack()'s own id-based lookup
// can be.
TEST(remove_instrument_reindexes_a_nested_instrument_track_too) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));  // index 0 - removed
  song.addInstrument(make_unique<Oscillator>(WaveformType::SQUARE)); // index 1

  auto group = make_unique<Group>();
  auto & nested = group->addChild(make_unique<InstrumentTrack>(1));
  song.addTrack(move(group));

  song.removeInstrument(0);

  CHECK(song.getInstrumentPool().getInstruments().size() == 1);
  CHECK(dynamic_cast<InstrumentTrack &>(nested).getInstrumentId() == 0);
}

// An out-of-range index is a no-op, not a crash or a silent erase of the
// wrong slot.
TEST(remove_instrument_with_an_out_of_range_index_is_a_no_op) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));

  song.removeInstrument(5);
  song.removeInstrument(-1);

  CHECK(song.getInstrumentPool().getInstruments().size() == 1);
}
