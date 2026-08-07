#include "TestFramework.h"

#include "../Song.h"
#include "../InstrumentTrack.h"
#include "../InstrumentProvider.h"

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
  song.addPattern(8);
  song.getPattern(0).setNote(0, track.getInternalId(), 0, Note(60, 40));
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
    auto & notes = reloaded.getPattern(0).getNotes(0, reloaded_track->getInternalId());
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
  song.addPattern(8);
  song.getPattern(0).setNote(0, track.getInternalId(), 0, Note(60, 40));
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  CHECK(reloaded.getTracks().size() == 1);
  auto & reloaded_track = *reloaded.getTracks()[0];
  CHECK(reloaded_track.getId() == track.getId());
  auto & notes = reloaded.getPattern(0).getNotes(0, reloaded_track.getInternalId());
  CHECK(notes.size() == 1);
  if (notes.size() == 1) CHECK(notes[0].getValue() == 60);

  fs::remove(scratch_path);
}

TEST(command_round_trips_for_a_track_with_an_explicit_textual_id) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "song_command_textual_id_scratch.xml").string();

  Song song;
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  track.setId("bass");
  song.addPattern(8);
  song.getPattern(0).setCommand(0, track.getInternalId(), Command("V400"));
  song.save(scratch_path);

  auto saved = readFile(scratch_path);
  CHECK(saved.find("track=\"bass\"") != string::npos);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  auto reloaded_track = reloaded.getTrackById("bass");
  CHECK(reloaded_track != nullptr);
  if (reloaded_track) {
    auto & command = reloaded.getPattern(0).getCommand(0, reloaded_track->getInternalId());
    CHECK(command.isDefined());
    CHECK(to_string(command) == "V400");
  }

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
