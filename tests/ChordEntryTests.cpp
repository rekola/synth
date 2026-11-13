#include "TestFramework.h"

#include "InteractiveUIFixture.h"

#include "../src/model/ArrangementOps.h"
#include "../src/model/LeafTrack.h"

#include <chrono>
#include <thread>

using namespace terminal_ui_test;

namespace {

const int kC = 5 * 53; // C-4 in 53-EDO
// Steps of 53-EDO above C; the otonal keys are z C, x D♯, c E, v F, b G, n A, m A♯↑.
enum Step { C = 0,
            Ds = 12,
            E = 17,
            F = 22,
            G = 31,
            A = 39,
            Asu = 43 };

// The pattern editor of a fresh song in the arrangement, on its first track.
struct ChordFixture : UIFixture {
  explicit ChordFixture(Scale scale = Scale::OTONAL, std::optional<cellui::KeymapProfile> keymap = {})
      : UIFixture("", UI::View::ARRANGEMENT, {}, keymap) {
    for (int i = 0; i < 4 && terminal.info().focus != "pattern-editor"; i++) otherWindow();
    CHECK(terminal.info().focus == "pattern-editor");
    controller->getSong().setScale(scale);
    ui->step();
    track_id = controller->getSong().getCurrentTrackId();
  }
  int track_id = -1;

  void down(char key) {
    terminal.pushKey(key, 0, cellui::Input::Kind::PRESS);
    ui->step();
  }
  void up(char key) {
    terminal.pushKey(key, 0, cellui::Input::Kind::RELEASE);
    ui->step();
  }
  // Longer than the interval that groups keys into one entry.
  void wait() {
    std::this_thread::sleep_for(std::chrono::milliseconds(130));
    ui->step();
  }
  void widen(int times) {
    for (int i = 0; i < times; i++) execute("widen-cursor");
  }
  // The notes of a row of the first track, as steps above C-4 (-1 for an empty column).
  std::vector<int> row(int r) {
    auto target = resolveEditTarget(controller->getSong(), track_id, r);
    std::vector<int> out;
    for (auto & note : target.pattern->getNotes(target.effective_row)) out.push_back(note.isDefined() ? note.getValue() - kC : -1);
    return out;
  }
  int columns() {
    auto track = dynamic_cast<const LeafTrack *>(controller->getSong().getMasterTrack().getChildByInternalId(track_id));
    return track ? track->getMinNoteColumns() : 0;
  }
  bool shows(const char * text) const { return this->text().find(text) != std::string::npos; }
};

using V = std::vector<int>;

} // namespace

TEST(chord_cursor_widens_to_four_columns_and_the_track_follows) {
  ChordFixture f;
  CHECK(f.columns() == 1);
  f.widen(2);
  CHECK(f.shows("Cursor width: 3"));
  CHECK(f.columns() == 3);
  f.widen(1);
  CHECK(f.columns() == 4);
  f.widen(1); // no wider than four
  CHECK(f.shows("at its widest"));
  CHECK(f.columns() == 4);
  // Narrowing removes no column.
  for (int i = 0; i < 3; i++) f.execute("narrow-cursor");
  CHECK(f.shows("Cursor width: 1"));
  CHECK(f.columns() == 4);
  f.execute("narrow-cursor");
  CHECK(f.shows("one column wide"));
  // The empty columns go when the cursor leaves the track.
  f.press(cellui::Key::Right, kModCtrl);
  CHECK(f.columns() == 1);
}

TEST(chord_cursor_leaves_the_columns_in_use_when_it_leaves_the_track) {
  ChordFixture f;
  f.widen(3);
  f.down('z');
  f.down('x');
  f.wait();
  f.up('z');
  f.up('x');
  CHECK(f.row(0) == (V{C, Ds, F, A}));
  f.widen(0);
  f.controller->ensureNoteColumns(f.track_id, 6);
  f.press(cellui::Key::Right, kModCtrl);
  CHECK(f.columns() == 4);
}

TEST(chord_entry_completes_one_key_to_a_chord_as_wide_as_the_cursor) {
  ChordFixture f;
  f.widen(3);
  f.down('z');
  // Not at the key press: more keys may follow.
  CHECK(f.row(0) == (V{C}));
  f.wait();
  CHECK(f.row(0) == (V{C, E, G, Asu}));
  CHECK(f.shows("C harmonic seventh"));
  f.up('z');
  // The cursor has stepped on and kept its width.
  f.down('v');
  f.wait();
  f.up('v');
  CHECK(f.row(0) == (V{C, E, G, Asu}));
  CHECK(f.row(1) == (V{F, A, 53 + C, 53 + Ds}));
  // One undo removes one chord.
  f.execute("undo");
  CHECK(f.row(1).empty());
  CHECK(f.row(0) == (V{C, E, G, Asu}));
  f.execute("undo");
  CHECK(f.row(0).empty());
}

TEST(chord_entry_at_three_columns_names_the_triad) {
  ChordFixture f;
  f.widen(2);
  // A quick tap, released before the interval ends, still writes the chord.
  f.down('z');
  f.up('z');
  CHECK(f.row(0) == (V{C, E, G}));
  CHECK(f.shows("C major"));
  f.down('n');
  f.up('n');
  CHECK(f.row(1) == (V{A, 53 + C, 53 + Ds}));
}

TEST(chord_entry_completes_several_keys_from_the_chords_that_contain_them) {
  {
    ChordFixture f;
    f.widen(3);
    f.down('z');
    f.down('x');
    CHECK(f.row(0) == (V{C, Ds})); // no other chord on the way
    f.wait();
    CHECK(f.row(0) == (V{C, Ds, F, A}));
    f.up('x');
    f.up('z');
    CHECK(f.row(0) == (V{C, Ds, F, A}));
  }
  {
    ChordFixture f;
    f.widen(2);
    f.down('z');
    f.down('m');
    f.wait();
    CHECK(f.row(0) == (V{C, E, Asu}));
  }
  {
    ChordFixture f;
    f.widen(3);
    f.down('m');
    f.down('c');
    f.up('m');
    f.up('c');
    CHECK(f.row(0) == (V{E, G, Asu, 53 + C}));
    CHECK(f.shows("C harmonic seventh / E"));
  }
}

TEST(chord_entry_works_the_chord_out_again_when_a_key_joins_the_held_ones) {
  ChordFixture f;
  f.widen(3);
  f.down('z');
  f.wait();
  CHECK(f.row(0) == (V{C, E, G, Asu}));
  f.down('x');
  CHECK(f.row(0) == (V{C, Ds})); // what the first completion added is gone
  f.wait();
  CHECK(f.row(0) == (V{C, Ds, F, A}));
  f.up('z');
  f.up('x');
  f.execute("undo");
  CHECK(f.row(0).empty());
}

TEST(chord_entry_adds_nothing_without_a_chord_to_add) {
  {
    // As many keys as the cursor is wide, or more: all are written and the width stays.
    ChordFixture f;
    f.widen(1);
    for (char key : std::string("zcb")) f.down(key);
    f.wait();
    for (char key : std::string("zcb")) f.up(key);
    CHECK(f.row(0) == (V{C, E, G}));
    CHECK(f.columns() >= 3);
    f.down('z');
    f.up('z');
    CHECK(f.row(1) == (V{C, E}));
  }
  {
    // No chord holds C, G and A.
    ChordFixture f;
    f.widen(3);
    for (char key : std::string("zbn")) f.down(key);
    f.wait();
    CHECK(f.row(0) == (V{C, G, A}));
    CHECK(f.shows("No chord of the scale contains these notes"));
  }
  {
    // A cursor of one column.
    ChordFixture f;
    f.down('z');
    f.wait();
    f.up('z');
    CHECK(f.row(0) == (V{C}));
  }
  {
    // A note that is no degree of the scale: C in D otonal.
    ChordFixture f;
    f.controller->getSong().setKey(9);
    f.widen(3);
    f.down('z');
    f.wait();
    f.up('z');
    CHECK(f.row(0) == (V{C}));
  }
  {
    // A scale without chords.
    ChordFixture f(Scale::MAJOR);
    f.widen(3);
    f.down('z');
    f.down('c');
    f.wait();
    f.up('z');
    f.up('c');
    CHECK(f.row(0) == (V{C, E}));
  }
}

TEST(chord_entry_leaves_an_active_selection_alone) {
  ChordFixture f;
  f.widen(2);
  f.execute("set-mark");
  f.press(cellui::Key::Down);
  f.down('z');
  f.up('z');
  CHECK(f.row(1) == (V{C, E, G}));
  // The mark is still there to exchange with.
  f.execute("exchange-point-and-mark");
  CHECK(!f.shows("No mark"));
  f.down('v');
  f.up('v');
  CHECK(f.row(0) == (V{F, A, 53 + C}));
}

// Without key releases the keys arriving together are the entry; it is
// completed when the interval closes, and the cursor steps on then.
TEST(chord_entry_without_key_releases_completes_when_the_interval_closes) {
  ChordFixture f;
  f.widen(3);
  f.type("z");
  f.type("x");
  CHECK(f.row(0) == (V{C, Ds}));
  f.wait();
  CHECK(f.row(0) == (V{C, Ds, F, A}));
  f.type("z");
  f.wait();
  CHECK(f.row(1) == (V{C, E, G, Asu}));
  f.execute("undo");
  CHECK(f.row(1).empty());
  CHECK(f.row(0) == (V{C, Ds, F, A}));
}

TEST(saving_removes_the_empty_note_columns_from_the_file_only) {
  ChordFixture f;
  f.widen(3);
  auto path = std::string(TESTS_SCRATCH_DIR) + "/chord_columns_scratch.xml";
  f.controller->saveSongAs(path);
  CHECK(f.columns() == 4); // the cursor is still in the track

  UIFixture loaded(path, UI::View::ARRANGEMENT);
  auto & song = loaded.controller->getSong();
  auto track = dynamic_cast<const LeafTrack *>(song.getMasterTrack().getChildByInternalId(song.getRootTrackIds()[0]));
  CHECK(track && track->getMinNoteColumns() == 1);
}
