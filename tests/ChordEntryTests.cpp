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
            Gbv = 27,
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

TEST(chord_cursor_widens_to_six_columns_and_the_track_follows) {
  ChordFixture f;
  CHECK(f.columns() == 1);
  f.widen(2);
  CHECK(f.shows("Cursor width: 3"));
  CHECK(f.columns() == 3);
  f.widen(1);
  CHECK(f.columns() == 4);
  f.widen(1);
  CHECK(f.shows("Cursor width: 5"));
  CHECK(f.columns() == 5);
  f.widen(1);
  CHECK(f.shows("Cursor width: 6"));
  CHECK(f.columns() == 6);
  f.widen(1); // no wider than six
  CHECK(f.shows("at its widest"));
  CHECK(f.columns() == 6);
  // Narrowing removes no column.
  for (int i = 0; i < 5; i++) f.execute("narrow-cursor");
  CHECK(f.shows("Cursor width: 1"));
  CHECK(f.columns() == 6);
  f.execute("narrow-cursor");
  CHECK(f.shows("one column wide"));
  // The empty columns go when the cursor leaves the track.
  f.press(cellui::Key::Right, kModCtrl);
  CHECK(f.columns() == 1);
}

TEST(chord_cursor_leaves_the_columns_in_use_when_it_leaves_the_track) {
  ChordFixture f;
  f.widen(3);
  f.down('q');
  f.down('w');
  f.wait();
  f.up('q');
  f.up('w');
  CHECK(f.row(0) == (V{C, Ds, E, Asu}));
  f.widen(0);
  f.controller->ensureNoteColumns(f.track_id, 6);
  f.press(cellui::Key::Right, kModCtrl);
  CHECK(f.columns() == 4);
}

TEST(chord_entry_completes_one_key_to_a_chord_as_wide_as_the_cursor) {
  ChordFixture f;
  f.widen(3);
  f.down('q');
  // Not at the key press: more keys may follow.
  CHECK(f.row(0) == (V{C}));
  f.wait();
  CHECK(f.row(0) == (V{C, E, G, Asu}));
  CHECK(f.shows("C harmonic seventh"));
  f.up('q');
  // The cursor has stepped on and kept its width.
  f.down('r');
  f.wait();
  f.up('r');
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
  f.down('q');
  f.up('q');
  CHECK(f.row(0) == (V{C, E, G}));
  CHECK(f.shows("C major"));
  f.down('y');
  f.up('y');
  CHECK(f.row(1) == (V{A, 53 + C, 53 + Ds}));
}

TEST(chord_entry_completes_several_keys_from_the_chords_that_contain_them) {
  {
    ChordFixture f;
    f.widen(3);
    f.down('q');
    f.down('w');
    CHECK(f.row(0) == (V{C, Ds})); // no other chord on the way
    f.wait();
    CHECK(f.row(0) == (V{C, Ds, E, Asu}));
    f.up('w');
    f.up('q');
    CHECK(f.row(0) == (V{C, Ds, E, Asu}));
  }
  {
    ChordFixture f;
    f.widen(2);
    f.down('q');
    f.down('u');
    f.wait();
    CHECK(f.row(0) == (V{C, E, Asu}));
  }
  {
    ChordFixture f;
    f.widen(3);
    f.down('u');
    f.down('e');
    f.up('u');
    f.up('e');
    CHECK(f.row(0) == (V{E, G, Asu, 53 + C}));
    CHECK(f.shows("C harmonic seventh / E"));
  }
}

TEST(chord_entry_works_the_chord_out_again_when_a_key_joins_the_held_ones) {
  ChordFixture f;
  f.widen(3);
  f.down('q');
  f.wait();
  CHECK(f.row(0) == (V{C, E, G, Asu}));
  f.down('w');
  CHECK(f.row(0) == (V{C, Ds})); // what the first completion added is gone
  f.wait();
  CHECK(f.row(0) == (V{C, Ds, E, Asu}));
  f.up('q');
  f.up('w');
  f.execute("undo");
  CHECK(f.row(0).empty());
}

TEST(chord_entry_adds_nothing_without_a_chord_to_add) {
  {
    // As many keys as the cursor is wide, or more: all are written and the width stays.
    ChordFixture f;
    f.widen(1);
    for (char key : std::string("qet")) f.down(key);
    f.wait();
    for (char key : std::string("qet")) f.up(key);
    CHECK(f.row(0) == (V{C, E, G}));
    CHECK(f.columns() >= 3);
    f.down('q');
    f.up('q');
    CHECK(f.row(1) == (V{C, E}));
  }
  {
    // No chord holds C, G and A.
    ChordFixture f;
    f.widen(3);
    for (char key : std::string("qty")) f.down(key);
    f.wait();
    CHECK(f.row(0) == (V{C, G, A}));
    CHECK(f.shows("No chord of the scale contains these notes"));
  }
  {
    // A cursor of one column.
    ChordFixture f;
    f.down('q');
    f.wait();
    f.up('q');
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
  f.down('q');
  f.up('q');
  CHECK(f.row(1) == (V{C, E, G}));
  // The mark is still there to exchange with.
  f.execute("exchange-point-and-mark");
  CHECK(!f.shows("No mark"));
  f.down('r');
  f.up('r');
  CHECK(f.row(0) == (V{F, A, 53 + C}));
}

// Without key releases the keys arriving together are the entry; it is
// completed when the interval closes, and the cursor steps on then.
TEST(chord_entry_without_key_releases_completes_when_the_interval_closes) {
  ChordFixture f;
  f.widen(3);
  f.type("q");
  f.type("w");
  CHECK(f.row(0) == (V{C, Ds}));
  f.wait();
  CHECK(f.row(0) == (V{C, Ds, E, Asu}));
  f.type("q");
  f.wait();
  CHECK(f.row(1) == (V{C, E, G, Asu}));
  f.execute("undo");
  CHECK(f.row(1).empty());
  CHECK(f.row(0) == (V{C, Ds, E, Asu}));
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

// After an entry the palette acts on that entry: it opens on the chord
// written, and Up and Down step through the degree's chords, each written in place.
TEST(chord_palette_steps_through_the_chords_of_the_entry_just_made) {
  ChordFixture f;
  f.widen(2);
  f.down('q');
  f.up('q');
  CHECK(f.row(0) == (V{C, E, G}));
  f.meta('/');
  CHECK(f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, E, G}));
  // The chords C names come first: its 7♯9, cut to three notes.
  f.press(cellui::Key::Down);
  CHECK(f.row(0) == (V{C, Ds, E}));
  f.press(cellui::Key::Down);
  CHECK(f.row(0) == (V{C, Ds, F}));
  CHECK(f.shows("C narrow major"));
  f.press(cellui::Key::Down);
  // The mirror seventh A♯↑ hangs from an octave above the played C, so it is cut to its top three
  // notes, which fall from the first column.
  CHECK(f.row(0) == (V{53 + C, Asu, Fsu}));
  CHECK(f.shows("A♯↑ mirror seventh / C (3 notes)"));
  f.press(cellui::Key::Down);
  CHECK(f.row(0) == (V{C, E, F}));
  // Round to the first, with no chord twice.
  f.press(cellui::Key::Down);
  CHECK(f.row(0) == (V{C, E, G}));
  f.press(cellui::Key::Up);
  CHECK(f.row(0) == (V{C, E, F}));
  f.press(cellui::Key::Enter);
  CHECK(!f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, E, F}));
  // The whole session is one undo step, the entry another.
  f.execute("undo");
  CHECK(f.row(0) == (V{C, E, G}));
  f.execute("undo");
  CHECK(f.row(0).empty());
}

TEST(chord_palette_completes_notes_played_with_a_cursor_of_one_column) {
  ChordFixture f;
  f.down('q');
  f.down('w');
  f.up('q');
  f.up('w');
  CHECK(f.row(0) == (V{C, Ds}));
  // Four notes the first time; the notes played are the given ones, and
  // with no chord there yet the first is written.
  f.meta('/');
  CHECK(f.shows("Chord: 4 notes"));
  CHECK(f.row(0) == (V{C, Ds, E, Asu}));
  CHECK(f.shows("C D♯ F A") && f.shows("C D♯ E A♯↑"));
  f.press(cellui::Key::Down);
  CHECK(f.row(0) == (V{C, Ds, F, A}));
  f.press(cellui::Key::Down);
  // The mirror seventh is found from the highest played note, D♯, and hangs from it.
  CHECK(f.row(0) == (V{Ds, C, Asu - 53, Fsu - 53}));
  // Cancelling puts back what was there.
  f.ctrl('g');
  CHECK(f.row(0) == (V{C, Ds}));
  CHECK(!f.shows("Chord: 4 notes"));
}

TEST(chord_palette_size_changes_without_the_cursor) {
  ChordFixture f;
  f.down('q');
  f.up('q');
  f.meta('/');
  CHECK(f.row(0) == (V{C, E, G, Asu}));
  f.press(cellui::Key::Left, kModCtrl | cellui::kModShift);
  CHECK(f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, E, G}));
  f.execute("narrow-cursor");
  CHECK(f.row(0) == (V{C, E}));
  f.execute("narrow-cursor"); // no smaller than two
  CHECK(f.shows("Chord: 2 notes"));
  f.press(cellui::Key::Right, kModCtrl | cellui::kModShift);
  CHECK(f.row(0) == (V{C, E, G}));
  f.press(cellui::Key::Enter);
  // The cursor is still one column wide, and the size is remembered.
  f.down('r');
  f.up('r');
  CHECK(f.row(1) == (V{F}));
  f.meta('/');
  CHECK(f.shows("Chord: 3 notes"));
  CHECK(f.row(1) == (V{F, A, 53 + C}));
}

TEST(chord_palette_writes_a_short_chord_and_empties_the_rest) {
  ChordFixture f;
  f.widen(3);
  f.down('y');
  f.up('y');
  CHECK(f.row(0) == (V{A, 53 + C, 53 + Ds, 53 + F}));
  f.meta('/');
  f.press(cellui::Key::Down);
  CHECK(f.shows("A minor"));
  CHECK(f.row(0) == (V{A, 53 + C, 53 + E})); // the minor triad rises from its lowest note
  f.press(cellui::Key::Enter);
  CHECK(f.row(0) == (V{A, 53 + C, 53 + E}));
}

// Away from the entry the palette acts at the cursor, on the note in its first column.
TEST(chord_palette_opens_on_a_chord_entered_earlier) {
  ChordFixture f;
  f.widen(2);
  f.down('q');
  f.up('q');
  f.down('r');
  f.up('r');
  f.press(cellui::Key::Up);
  f.press(cellui::Key::Up);
  CHECK(f.row(0) == (V{C, E, G}));
  // The present chord is the selected line: the first, so Up goes round to the last.
  f.meta('/');
  CHECK(f.row(0) == (V{C, E, G}));
  f.press(cellui::Key::Up);
  CHECK(f.row(0) == (V{C, E, F}));
  f.press(cellui::Key::Down);
  f.press(cellui::Key::Down);
  CHECK(f.row(0) == (V{C, Ds, E}));
  f.ctrl('g');
  CHECK(f.row(0) == (V{C, E, G}));
  CHECK(f.row(1) == (V{F, A, 53 + C}));
}

TEST(chord_palette_says_why_it_does_not_open) {
  {
    ChordFixture f(Scale::MAJOR);
    f.down('z');
    f.up('z');
    f.execute("chord-palette");
    CHECK(f.shows("The song's scale has no chords"));
    CHECK(f.row(0) == (V{C}));
  }
  {
    // Without a note the palette still opens, with its check box and no chords; accepting writes nothing.
    ChordFixture f;
    f.execute("chord-palette");
    CHECK(f.shows("Borrowed chords"));
    CHECK(f.shows("No note to build a chord on"));
    f.press(cellui::Key::Enter);
    CHECK(!f.shows("Borrowed chords"));
    for (auto v : f.row(0)) CHECK(v == -1);
  }
  {
    // C in D otonal: no key gives it, so it is written by hand.
    ChordFixture f;
    auto & song = f.controller->getSong();
    song.setKey(9);
    {
      Song::Edit edit(song, "note");
      auto target = resolveEditTarget(song, f.track_id, 0);
      target.pattern->setNote(target.effective_row, 0, Note(kC, 0x40, 0));
    }
    f.controller->ensureNoteColumns(f.track_id, 1);
    f.ui->step();
    f.execute("chord-palette");
    CHECK(f.shows("is not a note of the song's scale"));
    CHECK(f.row(0) == (V{C}));
  }
}

// Any other key accepts the chord and does what it does; so does the palette's own key.
TEST(chord_palette_is_accepted_by_the_next_note) {
  ChordFixture f;
  f.widen(2);
  f.down('q');
  f.up('q');
  f.meta('/');
  f.press(cellui::Key::Up);
  CHECK(f.row(0) == (V{C, E, F}));
  f.down('r');
  f.up('r');
  CHECK(!f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, E, F}));
  CHECK(f.row(1) == (V{F, A, 53 + C}));
  f.meta('/');
  CHECK(f.shows("Chord: 3 notes"));
  f.meta('/');
  CHECK(!f.shows("Chord: 3 notes"));
  CHECK(f.row(1) == (V{F, A, 53 + C}));
}

// The desktop keymap has its own key for the palette, and Escape cancels there.
TEST(chord_palette_on_the_desktop_keymap) {
  ChordFixture f(Scale::OTONAL, cellui::KeymapProfile::DESKTOP);
  f.press(cellui::Key::Right, kModCtrl | cellui::kModShift);
  f.press(cellui::Key::Right, kModCtrl | cellui::kModShift);
  CHECK(f.shows("Cursor width: 3"));
  f.down('q');
  f.up('q');
  CHECK(f.row(0) == (V{C, E, G}));
  f.press(' ', kModCtrl);
  CHECK(f.shows("Chord: 3 notes"));
  f.press(cellui::Key::Down);
  CHECK(f.row(0) == (V{C, Ds, E}));
  f.press(cellui::Key::Escape);
  CHECK(!f.shows("Chord: 3 notes"));
  CHECK(f.row(0) == (V{C, E, G}));
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
  f.down('q');
  f.up('q');
  CHECK(f.row(0) == (V{C}));
  CHECK(f.cursorRow() == 4);
  f.down('e');
  f.up('e');
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

TEST(the_fx_column_accepts_the_coarse_correction_symbols_when_typed) {
  for (auto [text, cents] : {std::pair{std::string(">1F"), 31.0}, std::pair{std::string("<10"), -16.0}}) {
    HeightFixture f;
    f.fill(2, 1);
    for (int i = 0; i < 3; i++) f.press(cellui::Key::Right); // note, velocity, delay, then fx
    f.type(text);
    auto target = resolveEditTarget(f.controller->getSong(), f.track_id, 0);
    auto note = target.pattern->getNotes(target.effective_row)[0];
    CHECK(std::string(note.getFx()) == text);
    CHECK(note.hasCoarseTuningCorrection());
    CHECK(note.getTuningCorrectionCents() == cents);
    CHECK(f.shows(text.c_str()));
  }
}

TEST(chord_entry_at_five_columns_writes_five_note_chords) {
  {
    ChordFixture f;
    f.widen(4);
    f.down('r');
    f.wait();
    f.up('r');
    // The harmonic run 4:5:6:7:8 from F.
    CHECK(f.row(0) == (V{F, A, 53 + C, 53 + Ds, 53 + F}));
    f.meta('/');
    CHECK(f.shows("Chord: 5 notes"));
    f.press(cellui::Key::Down);
    CHECK(f.row(0).size() == 5);
  }
  {
    ChordFixture f;
    f.widen(4);
    f.down('q');
    f.wait();
    f.up('q');
    CHECK(f.row(0) == (V{C, E, G, Asu, 53 + C})); // the run 4:5:6:7:8 from C
  }
  {
    // F♯↑ has no run: a seventh chord that contains it, with its first note repeated an octave on.
    ChordFixture f;
    f.widen(4);
    f.down('5');
    f.wait();
    f.up('5');
    auto row = f.row(0);
    CHECK(row.size() == 5);
    CHECK(row[4] == row[0] + 53);
    CHECK(f.shows("7♯9"));
    CHECK(!f.shows("with octave"));
  }
  {
    // D𝄫 has no five-note chord: its seventh chord with the first note an octave on.
    ChordFixture f;
    f.widen(4);
    f.down('1');
    f.wait();
    f.up('1');
    auto row = f.row(0);
    CHECK(row.size() >= 4);
    CHECK(row[0] == 2);
    for (std::size_t i = 1; i < 4; i++) CHECK(row[i] > 2);
    CHECK(row.size() == 5 && row[4] == 2 + 53);
  }
  {
    // C and B together.
    ChordFixture f;
    f.widen(4);
    f.down('q');
    f.down('k');
    f.wait();
    f.up('q');
    f.up('k');
    auto row = f.row(0);
    CHECK(row.size() == 5);
    CHECK(row[0] == C);
    CHECK(f.shows("C major seventh"));
  }
  {
    // At four columns no five-note chord is offered.
    ChordFixture f;
    f.widen(3);
    f.down('r');
    f.wait();
    f.up('r');
    CHECK(f.row(0) == (V{F, A, 53 + C, 53 + Ds}));
    CHECK(!f.shows("ninth"));
  }
}

TEST(chord_palette_is_five_notes_at_most) {
  ChordFixture f;
  f.down('r');
  f.up('r');
  f.meta('/');
  CHECK(f.shows("Chord: 4 notes"));
  f.execute("widen-cursor");
  CHECK(f.shows("Chord: 5 notes"));
  CHECK(f.row(0) == (V{F, A, 53 + C, 53 + Ds, 53 + F}));
  f.execute("widen-cursor");
  CHECK(f.shows("Chord: 6 notes"));
  f.execute("widen-cursor"); // no larger than six
  CHECK(f.shows("2 to 6 notes"));
  CHECK(f.shows("Chord: 6 notes"));
}

TEST(off_key_releases_every_column_under_the_cursor) {
  for (auto scale : {Scale::OTONAL, Scale::MAJOR}) {
    ChordFixture f(scale);
    // Notes in the first two rows, then OFF over three columns of the first.
    for (int r = 0; r < 2; r++) {
      f.down('z');
      f.up('z');
    }
    f.press(cellui::Key::Up);
    f.press(cellui::Key::Up);
    f.widen(2);
    f.controller->getSong().breakTypingRun();
    f.down('a');
    f.up('a');
    auto offs = [&](int r) {
      auto target = resolveEditTarget(f.controller->getSong(), f.track_id, r);
      int n = 0;
      for (auto & note : target.pattern->getNotes(target.effective_row)) n += note.isOff() ? 1 : 0;
      return n;
    };
    CHECK(offs(0) == 3);
    CHECK(offs(1) == 0);
    f.execute("undo");
    CHECK(offs(0) == 0);
  }
}

// The keys give ratios of the gamut above the tonic, so chord entry in a key other
// than C gives the chords of C moved to that key (the keyboard's tonic is the song's).
TEST(chord_entry_in_another_key_gives_the_chords_of_c_moved) {
  ChordFixture f;
  f.controller->getSong().setKey(A);
  f.widen(3);
  f.down('q');
  f.wait();
  f.up('q');
  CHECK(f.row(0) == (V{A, A + E, A + G, A + Asu}));
  CHECK(f.shows("A harmonic seventh"));
  f.down('r'); // 4/3
  f.wait();
  f.up('r');
  CHECK(f.row(1) == (V{A + F, A + F + E, A + F + G, A + F + Asu}));
}

// Q is the tonic and the keys rise from it: the octave is counted from the tonic.
TEST(chord_entry_in_another_key_follows_the_octave_from_the_tonic) {
  ChordFixture f;
  f.controller->getSong().setKey(A);
  f.controller->setGlobalOctave(5);
  f.widen(3);
  f.down('q');
  f.wait();
  f.up('q');
  CHECK(f.row(0) == (V{53 + A, 53 + A + E, 53 + A + G, 53 + A + Asu}));
}

TEST(chord_entry_in_a_diamond_song_uses_the_diamond_keys) {
  ChordFixture f(Scale::DIAMOND);
  f.widen(3);
  f.down('7'); // A♭
  f.wait();
  f.up('7');
  // The harmonic seventh A♭ names comes first, A♭ C E♭ F♯↑ (it takes notes from both scales); the keys
  // are the diamond's, so every note is one of the 13.
  CHECK(f.row(0) == (V{36, 53, 53 + 14, 53 + 26}));
  // A key of no diamond note writes nothing.
  ChordFixture g(Scale::DIAMOND);
  g.down('4');
  g.up('4');
  CHECK(g.row(0) == (V{-1}) || g.row(0).empty());
}

// A mirror chord is built downward from the played note: nothing is written above it.
TEST(a_mirror_chord_is_entered_by_its_top_note) {
  {
    ChordFixture f(Scale::UTONAL);
    f.widen(3);
    f.down('q'); // C
    f.wait();
    f.up('q');
    CHECK(f.row(0) == (V{53, 36, 22, 10})); // C5 A♭4 F4 E𝄫4, falling
    CHECK(f.shows("C mirror seventh"));
  }
  {
    ChordFixture f(Scale::UTONAL);
    f.widen(3);
    f.down('t'); // G
    f.wait();
    f.up('t');
    CHECK(f.row(0) == (V{84, 67, 53, 41})); // G5 E♭5 C5 B𝄫4
  }
  {
    // Cut to the width from the first column: the played note and the ratios below it.
    ChordFixture f(Scale::UTONAL);
    f.widen(2);
    f.down('q');
    f.wait();
    f.up('q');
    CHECK(f.row(0) == (V{53, 36, 22}));
  }
  {
    ChordFixture f(Scale::UTONAL);
    f.widen(1);
    f.down('q');
    f.wait();
    f.up('q');
    CHECK(f.row(0) == (V{53, 36}));
  }
  {
    // An upward chord is as before.
    ChordFixture f(Scale::OTONAL);
    f.widen(3);
    f.down('q');
    f.wait();
    f.up('q');
    CHECK(f.row(0) == (V{C, E, G, Asu}));
  }
}

// The palette lists the same candidates and writes a mirror chord the same way.
TEST(the_palette_writes_a_mirror_chord_from_the_played_note) {
  ChordFixture f(Scale::UTONAL);
  f.widen(3);
  f.down('q');
  f.up('q');
  CHECK(f.row(0) == (V{53, 36, 22, 10}));
  f.meta('/');
  CHECK(f.row(0) == (V{53, 36, 22, 10}));
  f.press(cellui::Key::Down);
  CHECK(f.shows("G mirror seventh / C"));
  CHECK(f.row(0) == (V{53, 41, 31, 14})); // C5 B𝄫4 G4 E♭4
  f.press(cellui::Key::Down);
  CHECK(f.row(0) == (V{0, 10, 27, 41})); // E𝄫 harmonic seventh read upward from C, as before
}

namespace {
// The notes started since the queue was last read, as their note values.
std::vector<int> startedNotes(ChordFixture & f) {
  std::vector<int> out;
  auto & queue = f.controller->getPlaybackEventQueue();
  while (queue.hasEvents()) {
    auto event = queue.pop();
    if (event && event->getType() == PlaybackControlEvent::PLAY_NOTE) out.push_back(event->getParameter3());
  }
  return out;
}
} // namespace

// Phase 2: a line of single notes in utonal follows the melody by the top notes of its chords.
TEST(a_line_of_single_notes_in_utonal_gives_chords_whose_top_notes_are_the_notes) {
  ChordFixture f(Scale::UTONAL);
  f.widen(3);
  for (char key : std::string("qut")) { // C, B𝄫, G
    f.down(key);
    f.wait();
    f.up(key);
  }
  auto top = [&](int row) { return f.row(row).front() % 53; };
  CHECK(f.row(0).size() == 4 && f.row(1).size() == 4 && f.row(2).size() == 4);
  CHECK(top(0) == 0);  // C
  CHECK(top(1) == 41); // B𝄫
  CHECK(top(2) == 31); // G
}

// F and the C above it together write E𝄫 F A♭ C with that C on top, keeping their octaves.
TEST(two_played_notes_complete_a_mirror_chord_from_the_highest) {
  ChordFixture f(Scale::UTONAL);
  f.widen(3);
  f.down('r'); // F
  f.down('i'); // C an octave up
  f.wait();
  f.up('r');
  f.up('i');
  CHECK(f.row(0) == (V{53, 36, 22, 10}));
  CHECK(f.shows("C mirror seventh"));
}

// The chord the key's note is heard as, with a four-column cursor: the keys of otonal and diamond.
TEST(a_key_writes_the_chord_its_note_names) {
  {
    ChordFixture f(Scale::OTONAL);
    f.widen(3);
    f.down('u'); // A♯↑
    f.wait();
    f.up('u');
    CHECK(f.row(0) == (V{53 + 43, 53 + 26, 53 + 12, 53}));
  }
  {
    ChordFixture f(Scale::OTONAL);
    f.widen(3);
    f.down('k'); // B
    f.wait();
    f.up('k');
    CHECK(f.row(0) == (V{101, 84, 70, 58}));
  }
  {
    // E is in no seventh chord it names: C harmonic seventh read from E, as a seventh chord at four columns.
    ChordFixture f(Scale::OTONAL);
    f.widen(3);
    f.down('e');
    f.wait();
    f.up('e');
    CHECK(f.row(0) == (V{E, G, Asu, 53 + C}));
  }
  {
    // Diamond E (the diamond layout puts 5/4 on e): its mirror seventh G♭↓ A C E.
    ChordFixture f(Scale::DIAMOND);
    f.widen(3);
    f.down('e');
    f.wait();
    f.up('e');
    CHECK(f.row(0) == (V{70, 53, 39, 27}));
  }
  {
    ChordFixture f(Scale::DIAMOND);
    f.widen(2);
    f.down('e');
    f.wait();
    f.up('e');
    CHECK(f.row(0) == (V{70, 53, 39}));
  }
}

namespace {
struct Sound {
  bool play;
  int column;
  int value; // as steps above C-4; unused for a stop
};

// The sound of pressing the keys together and holding them until the completion has come.
std::vector<Sound> soundOf(ChordFixture & f, std::string keys) {
  startedNotes(f); // empty the queue
  std::vector<Sound> out;
  auto drain = [&] {
    auto & queue = f.controller->getPlaybackEventQueue();
    while (queue.hasEvents()) {
      auto event = queue.pop();
      if (!event) continue;
      if (event->getType() == PlaybackControlEvent::PLAY_NOTE) out.push_back({true, event->getParameter2(), event->getParameter3() - kC});
      if (event->getType() == PlaybackControlEvent::STOP_NOTE) out.push_back({false, event->getParameter2(), 0});
    }
  };
  for (char key : keys) f.down(key);
  drain();
  for (int i = 0; i < 20; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    f.ui->step();
    drain();
  }
  return out;
}

// The pitches still sounding after the events.
std::vector<int> soundingAfter(const std::vector<Sound> & events) {
  std::map<int, int> by_column;
  for (auto & e : events) {
    if (e.play)
      by_column[e.column] = e.value;
    else
      by_column.erase(e.column);
  }
  std::vector<int> out;
  for (auto & [column, value] : by_column) out.push_back(value);
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<int> playedValues(const std::vector<Sound> & events) {
  std::vector<int> out;
  for (auto & e : events) {
    if (e.play) out.push_back(e.value);
  }
  return out;
}

std::vector<int> sorted(std::vector<int> v) {
  std::sort(v.begin(), v.end());
  return v;
}
} // namespace

// The key sounds its own pitch at once; when the completion is ready, what sounds is made to be what is
// written, by exact pitch: a sounding note that is not written stops, a written one that is not sounding
// follows from the lowest to the highest, one already sounding carries on.
TEST(chord_entry_sounds_the_written_notes) {
  {
    // F: F4 sounds, then stops; B♭↓4, D♭↓5 and F5 follow.
    ChordFixture f(Scale::UTONAL);
    f.widen(2);
    auto events = soundOf(f, "r");
    CHECK(events.size() >= 5);
    CHECK(events[0].play && events[0].value == F);
    CHECK(f.row(0) == (V{53 + F, 53 + 5, 44}));
    CHECK(playedValues(events) == (V{F, 44, 53 + 5, 53 + F}));
    CHECK(soundingAfter(events) == sorted(f.row(0)));
  }
  {
    // C with five columns: C4 carries on as the lowest note, E𝄫4 F4 A♭4 C5 follow.
    ChordFixture f(Scale::UTONAL);
    f.widen(4);
    auto events = soundOf(f, "q");
    CHECK(playedValues(events) == (V{C, 10, 22, 36, 53}));
    CHECK(soundingAfter(events) == (V{C, 10, 22, 36, 53}));
    CHECK(soundingAfter(events) == sorted(f.row(0)));
  }
  {
    // F4 and C5 together: both carry on, E𝄫4 and A♭4 follow.
    ChordFixture f(Scale::UTONAL);
    f.widen(3);
    auto events = soundOf(f, "ri");
    CHECK(f.row(0) == (V{53, 36, 22, 10}));
    CHECK(playedValues(events) == (V{22, 53, 10, 36}) || playedValues(events) == (V{53, 22, 10, 36}));
    CHECK(soundingAfter(events) == (V{10, 22, 36, 53}));
  }
  {
    // Six columns: the note below the played one sounds first among those that follow.
    ChordFixture f(Scale::UTONAL);
    f.widen(5);
    auto events = soundOf(f, "q");
    CHECK(playedValues(events) == (V{C, -9, 10, 22, 36, 53}));
    CHECK(soundingAfter(events) == sorted(f.row(0)));
  }
  {
    // With a one-column cursor the note is written and sounded as played.
    ChordFixture f(Scale::UTONAL);
    auto events = soundOf(f, "r");
    CHECK(f.row(0) == (V{F}));
    CHECK(playedValues(events) == (V{F}));
    CHECK(soundingAfter(events) == (V{F}));
  }
  // After any entry the notes sounding are the notes written.
  for (auto scale : {Scale::OTONAL, Scale::UTONAL, Scale::DIAMOND}) {
    for (int widen = 1; widen <= 5; widen++) {
      for (char key : std::string("qwertyuiop")) {
        ChordFixture f(scale);
        f.widen(widen);
        auto events = soundOf(f, std::string(1, key));
        auto row = f.row(0);
        row.erase(std::remove(row.begin(), row.end(), -1), row.end());
        CHECK(soundingAfter(events) == sorted(row));
      }
    }
  }
}

// Chords as runs of consecutive ratios from the played note, in the order of the columns.
TEST(a_chord_is_a_run_of_ratios_from_the_played_note) {
  auto row = [](Scale scale, int widen, char key) {
    ChordFixture f(scale);
    f.widen(widen);
    f.down(key);
    f.wait();
    f.up(key);
    return f.row(0);
  };
  CHECK(row(Scale::OTONAL, 3, 'q') == (V{C, E, G, Asu}));
  CHECK(row(Scale::OTONAL, 5, 'q') == (V{C, E, G, Asu, 53 + C, 53 + 9}));
  CHECK(row(Scale::UTONAL, 2, 'q') == (V{53, 36, 22}));
  CHECK(row(Scale::UTONAL, 3, 'q') == (V{53, 36, 22, 10}));
  CHECK(row(Scale::UTONAL, 5, 'q') == (V{53, 36, 22, 10, 0, -9}));
  CHECK(row(Scale::OTONAL, 2, 'e') == (V{70, 53, 39})); // at three columns the run E C A
}

// The setting "borrowed chords" is switched in the chord palette: Tab moves the focus to its check box and
// Space toggles it, and the candidates change at once.
TEST(borrowed_chords_are_switched_in_the_chord_palette) {
  ChordFixture f; // otonal
  f.widen(3);
  f.down('e');
  f.wait();
  f.up('e');
  auto before = f.row(0);
  CHECK(before == (V{E, G, Asu, 53 + C})); // E: C harmonic seventh / E, as ever
  f.meta('/');
  CHECK(f.shows("☐ Borrowed chords"));
  {
    // The check box is on the palette's last row, under the size line and at its left edge.
    auto text = f.text();
    std::vector<std::string> lines;
    for (std::size_t start = 0; start <= text.size();) {
      auto end = text.find('\n', start);
      if (end == std::string::npos) end = text.size();
      lines.push_back(text.substr(start, end - start));
      start = end + 1;
    }
    auto column = [](const std::string & line, const std::string & what) {
      auto at = line.find(what);
      return at == std::string::npos ? -1 : static_cast<int>(std::count_if(line.begin(), line.begin() + static_cast<std::ptrdiff_t>(at), [](char c) { return (c & 0xC0) != 0x80; }));
    };
    int title = -1, box = -1;
    for (std::size_t i = 0; i < lines.size(); i++) {
      if (column(lines[i], "Chord: ") >= 0) title = static_cast<int>(i);
      if (column(lines[i], "☐ Borrowed chords") >= 0) box = static_cast<int>(i);
    }
    CHECK(title >= 0 && box == title + 1);
    if (title >= 0 && box >= 0) CHECK(column(lines[static_cast<std::size_t>(box)], "☐") == column(lines[static_cast<std::size_t>(title)], "Chord: "));
  }
  CHECK(!f.shows("(borrowed)"));
  f.press(cellui::Key::Tab);
  f.press(' ');
  CHECK(f.shows("☑ Borrowed chords"));
  CHECK(f.controller->getSong().getBorrowedChords());
  // The first candidate is now E's own chord with G♭↓, marked.
  CHECK(f.row(0) == (V{53 + E, 53 + C, A, Gbv}));
  CHECK(f.shows("(borrowed)"));
  f.press(' ');
  CHECK(f.shows("☐ Borrowed chords"));
  CHECK(f.row(0) == before);
  f.press(' ');
  f.press(cellui::Key::Enter); // toggles on the check box
  f.press(cellui::Key::Down);  // back to the list
  f.press(cellui::Key::Enter);
  CHECK(!f.shows("Borrowed chords"));
  // The setting is kept when the palette is opened again.
  CHECK(f.controller->getSong().getBorrowedChords() == false);
  f.controller->getSong().setBorrowedChords(true);
  f.meta('/');
  CHECK(f.shows("☑ Borrowed chords"));
  f.press(cellui::Key::Enter);
}

// Off, no key writes a note outside the scale; on, a key writes the chord with the borrowed note.
TEST(borrowed_chords_in_chord_entry) {
  {
    ChordFixture f;
    f.widen(3);
    f.controller->getSong().setBorrowedChords(true);
    f.down('e');
    f.wait();
    f.up('e');
    CHECK(f.row(0) == (V{53 + E, 53 + C, A, Gbv}));
    CHECK(f.shows("(borrowed)"));
  }
  {
    ChordFixture f;
    f.widen(3);
    f.down('e');
    f.wait();
    f.up('e');
    CHECK(f.row(0) == (V{E, G, Asu, 53 + C}));
    CHECK(!f.shows("(borrowed)"));
  }
}
