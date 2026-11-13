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
            Fsu = 26,
            G = 31,
            A = 39,
            Asu = 43 };

// The pattern editor of a fresh song in the arrangement, on its first track.
struct ChordFixture : UIFixture {
  explicit ChordFixture(Scale scale = Scale::OTONAL, std::optional<cellui::KeymapProfile> keymap = {})
      : UIFixture("", UI::View::ARRANGEMENT, {}, keymap) {
    // By name: the key for it is the terminal keymap's.
    for (int i = 0; i < 4 && terminal.info().focus != "pattern-editor"; i++) {
      controller->sendCommand("other-window");
      ui->step();
    }
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

// The palette key after an entry acts on that entry and steps through the
// degree's chords, each written in place.
TEST(chord_palette_steps_through_the_chords_of_the_entry_just_made) {
  ChordFixture f;
  f.widen(2);
  f.down('z');
  f.up('z');
  CHECK(f.row(0) == (V{C, E, G}));
  f.execute("next-chord");
  CHECK(f.row(0) == (V{C, Ds, F}));
  CHECK(f.shows("C narrow major"));
  CHECK(f.shows("Chord: 3 notes"));
  f.meta('n');
  CHECK(f.row(0) == (V{C, Ds, Fsu}));
  CHECK(f.shows("C mirror diminished"));
  f.meta('n');
  CHECK(f.row(0) == (V{C, E, F}));
  f.meta('n');
  CHECK(f.row(0) == (V{C, Ds, E}));
  // Round to the first, with no chord twice.
  f.meta('n');
  CHECK(f.row(0) == (V{C, E, G}));
  f.meta('p');
  CHECK(f.row(0) == (V{C, Ds, E}));
  f.press(cellui::Key::Enter);
  CHECK(!f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, Ds, E}));
  // The whole session is one undo step, the entry another.
  f.execute("undo");
  CHECK(f.row(0) == (V{C, E, G}));
  f.execute("undo");
  CHECK(f.row(0).empty());
}

TEST(chord_palette_completes_notes_played_with_a_cursor_of_one_column) {
  ChordFixture f;
  f.down('z');
  f.down('x');
  f.up('z');
  f.up('x');
  CHECK(f.row(0) == (V{C, Ds}));
  // Four notes the first time; the notes played are the given ones.
  f.execute("next-chord");
  CHECK(f.shows("Chord: 4 notes"));
  CHECK(f.row(0) == (V{C, Ds, F, A}));
  CHECK(f.shows("C D♯ F♯↑ A♯↑") && f.shows("C D♯ E A♯↑"));
  f.meta('n');
  CHECK(f.row(0) == (V{C, Ds, Fsu, Asu}));
  f.meta('n');
  CHECK(f.row(0) == (V{C, Ds, E, Asu}));
  // Cancelling puts back what was there.
  f.ctrl('g');
  CHECK(f.row(0) == (V{C, Ds}));
  CHECK(!f.shows("Chord: 4 notes"));
}

TEST(chord_palette_size_changes_without_the_cursor) {
  ChordFixture f;
  f.down('z');
  f.up('z');
  f.execute("next-chord");
  CHECK(f.row(0) == (V{C, E, G, Asu}));
  f.press(cellui::Key::Left, kModCtrl | cellui::kModShift);
  CHECK(f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, E, G}));
  f.execute("narrow-cursor");
  CHECK(f.row(0) == (V{C, E}));
  f.execute("narrow-cursor"); // no smaller than two
  CHECK(f.shows("Chord: 2 notes"));
  f.execute("widen-cursor");
  CHECK(f.row(0) == (V{C, E, G}));
  f.press(cellui::Key::Enter);
  // The cursor is still one column wide, and the size is remembered.
  f.down('v');
  f.up('v');
  CHECK(f.row(1) == (V{F}));
  f.execute("next-chord");
  CHECK(f.shows("Chord: 3 notes"));
  CHECK(f.row(1) == (V{F, A, 53 + C}));
}

TEST(chord_palette_writes_a_short_chord_and_empties_the_rest) {
  ChordFixture f;
  f.widen(3);
  f.down('n');
  f.up('n');
  CHECK(f.row(0) == (V{A, 53 + C, 53 + Ds, 53 + F}));
  f.execute("next-chord");
  CHECK(f.shows("A minor"));
  CHECK(f.row(0) == (V{A, 53 + C, 53 + E}));
  f.press(cellui::Key::Enter);
  CHECK(f.row(0) == (V{A, 53 + C, 53 + E}));
}

// Away from the entry the palette acts at the cursor, on the note in its first column.
TEST(chord_palette_opens_on_a_chord_entered_earlier) {
  ChordFixture f;
  f.widen(2);
  f.down('z');
  f.up('z');
  f.down('v');
  f.up('v');
  f.press(cellui::Key::Up);
  f.press(cellui::Key::Up);
  CHECK(f.row(0) == (V{C, E, G}));
  f.meta('n');
  // The present chord is the first line, so the palette is on the second.
  CHECK(f.row(0) == (V{C, Ds, F}));
  f.meta('p');
  CHECK(f.row(0) == (V{C, E, G}));
  f.ctrl('g');
  CHECK(f.row(1) == (V{F, A, 53 + C}));
}

TEST(chord_palette_says_why_it_does_not_open) {
  {
    ChordFixture f(Scale::MAJOR);
    f.down('z');
    f.up('z');
    f.execute("next-chord");
    CHECK(f.shows("The song's scale has no chords"));
    CHECK(f.row(0) == (V{C}));
  }
  {
    ChordFixture f;
    f.execute("next-chord");
    CHECK(f.shows("No note under the cursor"));
  }
  {
    ChordFixture f;
    f.controller->getSong().setKey(9);
    f.down('z');
    f.up('z');
    f.execute("next-chord");
    CHECK(f.shows("is not a note of the song's scale"));
    CHECK(f.row(0) == (V{C}));
  }
}

// Any other key accepts the chord and does what it does.
TEST(chord_palette_is_accepted_by_the_next_note) {
  ChordFixture f;
  f.widen(2);
  f.down('z');
  f.up('z');
  f.execute("previous-chord");
  CHECK(f.row(0) == (V{C, Ds, E}));
  f.down('v');
  f.up('v');
  CHECK(!f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, Ds, E}));
  CHECK(f.row(1) == (V{F, A, 53 + C}));
}

// The desktop keymap has its own keys, and Escape cancels there.
TEST(chord_palette_on_the_desktop_keymap) {
  ChordFixture f(Scale::OTONAL, cellui::KeymapProfile::DESKTOP);
  f.press(cellui::Key::Right, kModCtrl | cellui::kModShift);
  f.press(cellui::Key::Right, kModCtrl | cellui::kModShift);
  CHECK(f.shows("Cursor width: 3"));
  f.down('z');
  f.up('z');
  CHECK(f.row(0) == (V{C, E, G}));
  f.press('.', kModCtrl);
  CHECK(f.row(0) == (V{C, Ds, F}));
  f.press(',', kModCtrl);
  CHECK(f.row(0) == (V{C, E, G}));
  f.press(',', kModCtrl);
  CHECK(f.row(0) == (V{C, Ds, E}));
  f.press(cellui::Key::Escape);
  CHECK(!f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, E, G}));
}

// An arrow is no palette key: it accepts the chord and moves the cursor.
TEST(chord_palette_is_accepted_by_an_arrow) {
  ChordFixture f;
  f.widen(2);
  f.down('z');
  f.up('z');
  f.meta('n');
  CHECK(f.row(0) == (V{C, Ds, F}));
  f.press(cellui::Key::Down);
  CHECK(!f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, Ds, F}));
}

namespace {

// The same fixture, for the cursor's height: the edit step.
struct HeightFixture : ChordFixture {
  int cursorRow() const { return controller->getPlaybackInfo().getAbsolutePosition(); }
  // Notes in the first `columns` note columns of rows 0 to `rows` - 1.
  void fill(int rows, int columns) {
    auto & song = controller->getSong();
    {
      Song::Edit edit(song, "fill");
      for (int r = 0; r < rows; r++) {
        auto target = resolveEditTarget(song, track_id, r);
        for (int c = 0; c < columns; c++) target.pattern->setNote(target.effective_row, c, Note(kC + c, 0x40, 0));
      }
    }
    controller->ensureNoteColumns(track_id, columns);
    ui->step();
  }
};

} // namespace

TEST(cursor_height_is_the_edit_step_from_one_row_to_a_bar) {
  HeightFixture f;
  f.execute("edit-step-decrease");
  CHECK(f.shows("one row high"));
  f.execute("set-edit-step-4");
  CHECK(f.shows("Edit step: 4"));
  f.execute("edit-step-increase");
  CHECK(f.shows("Edit step: 5"));
  // A bar of 4/4 is 16 rows.
  for (int i = 0; i < 11; i++) f.execute("edit-step-increase");
  CHECK(f.shows("Edit step: 16"));
  f.execute("edit-step-increase");
  CHECK(f.shows("one bar, 16 rows"));
  // A shorter bar reduces it.
  {
    auto & song = f.controller->getSong();
    Song::Edit edit(song, "signature");
    song.setTimeSignature(TimeSignature{3, 4});
  }
  f.ui->step();
  f.execute("edit-step-increase");
  CHECK(f.shows("one bar, 12 rows"));
  f.execute("edit-step-decrease");
  CHECK(f.shows("Edit step: 11"));
}

TEST(cursor_moves_down_by_its_height_after_an_entry_that_writes_its_first_row) {
  HeightFixture f;
  f.execute("set-edit-step-4");
  f.down('z');
  f.up('z');
  CHECK(f.row(0) == (V{C}));
  CHECK(f.cursorRow() == 4);
  f.down('c');
  f.up('c');
  for (int r : {1, 2, 3, 5}) CHECK(f.row(r).empty());
  CHECK(f.row(4) == (V{E}));
  CHECK(f.cursorRow() == 8);
}

TEST(delete_and_backspace_empty_every_cell_under_the_cursor) {
  HeightFixture f;
  f.fill(8, 4);
  f.widen(2); // three columns
  f.execute("set-edit-step-4");
  f.press(cellui::Key::Delete);
  // The fourth column and the rows below the cursor stay.
  for (int r = 0; r < 4; r++) CHECK(f.row(r) == (V{-1, -1, -1, 3}));
  for (int r = 4; r < 8; r++) CHECK(f.row(r) == (V{0, 1, 2, 3}));
  CHECK(f.cursorRow() == 0);
  // Backspace empties the cursor's cells and steps back by its height.
  for (int i = 0; i < 6; i++) f.press(cellui::Key::Down);
  f.execute("set-edit-step-2");
  f.press(cellui::Key::Backspace);
  for (int r : {4, 5}) CHECK(f.row(r) == (V{0, 1, 2, 3}));
  for (int r : {6, 7}) CHECK(f.row(r) == (V{-1, -1, -1, 3}));
  CHECK(f.cursorRow() == 4);
}
