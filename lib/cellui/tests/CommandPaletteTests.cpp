#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/CommandPalette.h>
#include <cellui/FuzzyMatch.h>
#include <cellui/KeyChordNames.h>
#include <cellui/Keymap.h>

using namespace cellui;

namespace {

Input key(int id, bool ctrl = false) {
  return Input(id, 0, 0, false, false, ctrl, false, Input::Kind::PRESS);
}
Input type(const char * text) { return key(text[0]); }
Input click(int y, int x) { return Input(Key::Button1, y, x, false, false, false, false, Input::Kind::RELEASE); }

std::vector<PaletteEntry> entries() {
  return {
      {"save-song", "Save the current song", "Ctrl+S"},
      {"save-song-as", "Save under a new name", ""},
      {"undo", "Undo the last change", "Ctrl+Z"},
      {"undo-redo", "Redo the last undone change", "Ctrl+Shift+Z"},
      {"toggle-view", "Switch views", "Tab"},
  };
}

struct Fixture {
  StyleProvider styles;
  CellBuffer buffer{20, 80};
  BufferPlane root{buffer, styles};
  CommandPalette palette;
  Fixture() {
    palette.open(entries());
    palette.show(root, 20, 80);
    root.refresh();
  }
  void feed(const char * text) {
    for (const char * c = text; *c; c++) palette.offerInput(type(c));
    palette.show(root, 20, 80);
    root.refresh();
  }
};

}  // namespace

TEST(fuzzy_match_finds_subsequences_case_insensitively) {
  CHECK(fuzzyMatch("TGV", "toggle-view").has_value());
  CHECK(!fuzzyMatch("vt", "toggle-view").has_value());  // the letters have to come in order
  CHECK(!fuzzyMatch("xyz", "toggle-view").has_value());
  CHECK(!fuzzyMatch("toggle-views", "toggle-view").has_value());
  CHECK(fuzzyMatch("", "anything").has_value());
  auto m = fuzzyMatch("tgv", "toggle-view");
  CHECK(m && m->positions == std::vector<int>({0, 2, 7}));
}

TEST(fuzzy_match_ranks_exact_then_prefix_then_word_starts_then_scatter) {
  auto score = [](const char * q, const char * t) { return fuzzyMatch(q, t)->score; };
  CHECK(score("undo", "undo") > score("undo", "undo-redo"));
  CHECK(score("undo", "undo-redo") > score("undo", "redo-undo"));
  CHECK(score("sa", "save-song-as") > score("sa", "toggle-scopes-a"));
  CHECK(score("ts", "toggle-solo") > score("ts", "set-song"));
}

TEST(palette_lists_everything_alphabetically_for_an_empty_query) {
  Fixture f;
  CHECK(f.palette.shownNames() == std::vector<std::string>({"save-song", "save-song-as", "toggle-view", "undo", "undo-redo"}));
}

TEST(palette_ranks_the_exact_name_first_and_drops_non_matches) {
  Fixture f;
  f.feed("undo");
  CHECK(f.palette.shownNames() == std::vector<std::string>({"undo", "undo-redo"}));
  CHECK(f.palette.chosen() && f.palette.chosen()->name == "undo");
  f.feed("-");
  CHECK(f.palette.shownNames() == std::vector<std::string>({"undo-redo"}));
}

TEST(palette_shows_bindings_the_description_and_the_query) {
  Fixture f;
  f.feed("redo");
  auto text = f.buffer.toPlainText();
  CHECK(text.find("> redo") != std::string::npos);
  CHECK(text.find("undo-redo") != std::string::npos);
  CHECK(text.find("Ctrl+Shift+Z") != std::string::npos);
  CHECK(text.find("Redo the last undone change") != std::string::npos);
  CHECK(text.find("save-song") == std::string::npos);
}

TEST(palette_says_when_nothing_matches_and_enter_chooses_nothing) {
  Fixture f;
  f.feed("zzz");
  CHECK(f.buffer.toPlainText().find("No matching command") != std::string::npos);
  CHECK(f.palette.chosen() == nullptr);
  CHECK(f.palette.offerInput(key(Key::Enter)) == CommandPalette::Result::NONE);
}

TEST(palette_moves_edits_chooses_and_cancels) {
  Fixture f;
  CHECK(f.palette.chosen()->name == "save-song");
  f.palette.offerInput(key(Key::Down));
  CHECK(f.palette.chosen()->name == "save-song-as");
  f.palette.offerInput(key('n', true));
  CHECK(f.palette.chosen()->name == "toggle-view");
  f.palette.offerInput(key('p', true));
  CHECK(f.palette.chosen()->name == "save-song-as");
  f.feed("un");
  f.palette.offerInput(key(Key::Backspace));
  f.palette.offerInput(key(Key::Backspace));
  CHECK(f.palette.query().empty());
  f.feed("undo");
  f.palette.offerInput(key('u', true));
  CHECK(f.palette.query().empty());
  CHECK(f.palette.offerInput(key(Key::Enter)) == CommandPalette::Result::CHOSEN);
  CHECK(f.palette.offerInput(key(Key::Escape)) == CommandPalette::Result::CANCELLED);
  CHECK(f.palette.offerInput(key('g', true)) == CommandPalette::Result::CANCELLED);
}

TEST(palette_ignores_releases_and_modified_letters) {
  Fixture f;
  f.palette.offerInput(Input('a', 0, 0, false, false, false, false, Input::Kind::RELEASE));
  f.palette.offerInput(key('a', true));
  CHECK(f.palette.query().empty());
}

TEST(palette_click_on_a_row_chooses_it_and_outside_cancels) {
  Fixture f;
  // The box is 80 wide at column 0 here, its list starts on row y + 3.
  int top = (20 - (12 + 6)) / 2 + 3;
  CHECK(f.palette.offerInput(click(top + 2, 10)) == CommandPalette::Result::CHOSEN);
  CHECK(f.palette.chosen()->name == "toggle-view");
  Fixture g;
  CHECK(g.palette.offerInput(click(0, 10)) == CommandPalette::Result::CANCELLED);
}

TEST(keymap_finds_the_sequences_for_a_command_per_profile) {
  using P = KeymapProfile;
  Keymap map;
  auto cx = KeyChord::pack('x', true, false, false, false);
  auto cs = KeyChord::pack('s', true, false, false, false);
  map.bindPrefixed(P::EMACS, cx, cs, "save");
  map.bind(P::DESKTOP, cs, "save");
  auto emacs = map.sequencesFor("save", P::EMACS);
  CHECK(emacs.size() == 1 && emacs[0] == std::vector<uint64_t>({cx, cs}));
  auto desktop = map.sequencesFor("save", P::DESKTOP);
  CHECK(desktop.size() == 1 && desktop[0] == std::vector<uint64_t>({cs}));
  CHECK(map.sequencesFor("nothing", P::EMACS).empty());
}

TEST(chords_are_described_per_profile) {
  using P = KeymapProfile;
  auto cx = KeyChord::pack('x', true, false, false, false);
  auto cs = KeyChord::pack('s', true, false, false, false);
  CHECK(KeyChord::describe({cx, cs}, P::EMACS) == "C-x C-s");
  CHECK(KeyChord::describe({cs}, P::DESKTOP) == "Ctrl+S");
  CHECK(KeyChord::describe({KeyChord::pack('w', false, true, false, false)}, P::EMACS) == "M-w");
  CHECK(KeyChord::describe({KeyChord::pack(Key::Up, true, false, true, false)}, P::EMACS) == "C-S-Up");
  CHECK(KeyChord::describe({KeyChord::pack('d', true, false, true, false)}, P::EMACS) == "C-S-D");
  CHECK(KeyChord::describe({KeyChord::pack('z', true, false, true, false)}, P::DESKTOP) == "Ctrl+Shift+Z");
  CHECK(KeyChord::describe({KeyChord::pack(' ', false, false, false, false)}, P::EMACS) == "SPC");
  CHECK(KeyChord::describe({KeyChord::pack(Key::F2, false, false, false, false)}, P::DESKTOP) == "F2");
}
