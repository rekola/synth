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
      {"save-buffer", "Save the current song", "Ctrl+S"},
      {"save-buffer-as", "Save under a new name", ""},
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
  CHECK(score("sa", "save-buffer-as") > score("sa", "toggle-scopes-a"));
  CHECK(score("ts", "toggle-solo") > score("ts", "set-song"));
}

TEST(palette_lists_everything_alphabetically_for_an_empty_query) {
  Fixture f;
  CHECK(f.palette.shownNames() == std::vector<std::string>({"save-buffer", "save-buffer-as", "toggle-view", "undo", "undo-redo"}));
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
  CHECK(text.find("M-x redo") != std::string::npos);
  CHECK(text.find("undo-redo") != std::string::npos);
  CHECK(text.find("Ctrl+Shift+Z") != std::string::npos);
  CHECK(text.find("Redo the last undone change") != std::string::npos);
  CHECK(text.find("save-buffer") == std::string::npos);
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
  CHECK(f.palette.chosen()->name == "save-buffer");
  f.palette.offerInput(key(Key::Down));
  CHECK(f.palette.chosen()->name == "save-buffer-as");
  f.palette.offerInput(key('n', true));
  CHECK(f.palette.chosen()->name == "toggle-view");
  f.palette.offerInput(key('p', true));
  CHECK(f.palette.chosen()->name == "save-buffer-as");
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
  // Five commands: the list is on rows 13-17, the description and the query
  // below it.
  CHECK(f.palette.offerInput(click(15, 10)) == CommandPalette::Result::CHOSEN);
  CHECK(f.palette.chosen()->name == "toggle-view");
  Fixture g;
  CHECK(g.palette.offerInput(click(0, 10)) == CommandPalette::Result::CANCELLED);
}

TEST(keymap_finds_the_sequences_for_a_command_per_profile) {
  using P = KeymapProfile;
  Keymap map;
  auto cx = KeyChord::pack('x', true, false, false, false);
  auto cs = KeyChord::pack('s', true, false, false, false);
  map.bindPrefixed(P::TERMINAL, cx, cs, "save");
  map.bind(P::DESKTOP, cs, "save");
  auto emacs = map.sequencesFor("save", P::TERMINAL);
  CHECK(emacs.size() == 1 && emacs[0] == std::vector<uint64_t>({cx, cs}));
  auto desktop = map.sequencesFor("save", P::DESKTOP);
  CHECK(desktop.size() == 1 && desktop[0] == std::vector<uint64_t>({cs}));
  CHECK(map.sequencesFor("nothing", P::TERMINAL).empty());
}

TEST(chords_are_described_per_profile) {
  using P = KeymapProfile;
  auto cx = KeyChord::pack('x', true, false, false, false);
  auto cs = KeyChord::pack('s', true, false, false, false);
  CHECK(KeyChord::describe({cx, cs}, P::TERMINAL) == "C-x C-s");
  CHECK(KeyChord::describe({cs}, P::DESKTOP) == "Ctrl+S");
  CHECK(KeyChord::describe({KeyChord::pack('w', false, true, false, false)}, P::TERMINAL) == "M-w");
  CHECK(KeyChord::describe({KeyChord::pack(Key::Up, true, false, true, false)}, P::TERMINAL) == "C-S-Up");
  CHECK(KeyChord::describe({KeyChord::pack('d', true, false, true, false)}, P::TERMINAL) == "C-S-D");
  CHECK(KeyChord::describe({KeyChord::pack('z', true, false, true, false)}, P::DESKTOP) == "Ctrl+Shift+Z");
  CHECK(KeyChord::describe({KeyChord::pack(' ', false, false, false, false)}, P::TERMINAL) == "SPC");
  CHECK(KeyChord::describe({KeyChord::pack(Key::F2, false, false, false, false)}, P::DESKTOP) == "F2");
}

TEST(palette_tab_completes_to_the_longest_shared_prefix) {
  Fixture f;
  f.feed("sa");
  f.palette.offerInput(key(Key::Tab));
  CHECK(f.palette.query() == "save-buffer");  // save-buffer and save-buffer-as agree up to here
  f.palette.offerInput(key(Key::Tab));
  CHECK(f.palette.query() == "save-buffer");  // nothing more to add
  CHECK(f.palette.note().empty());
  f.feed("-");
  f.palette.offerInput(key(Key::Tab));
  CHECK(f.palette.query() == "save-buffer-as");
  f.palette.offerInput(key(Key::Tab));
  CHECK(f.palette.note() == "[Sole completion]");
  f.palette.offerInput(key(Key::Backspace));
  CHECK(f.palette.note().empty());
}

TEST(palette_tab_with_no_command_starting_with_the_query_says_so) {
  Fixture f;
  f.feed("view");  // matches toggle-view by fuzzy, but no name starts with it
  f.palette.offerInput(key(Key::Tab));
  CHECK(f.palette.query() == "view");
  CHECK(f.palette.note() == "[No match]");
  f.palette.show(f.root, 20, 80);
  f.root.refresh();
  CHECK(f.buffer.toPlainText().find("[No match]") != std::string::npos);
}

TEST(palette_tab_completion_ignores_case_and_runs_nothing) {
  Fixture f;
  f.feed("UN");
  CHECK(f.palette.offerInput(key(Key::Tab)) == CommandPalette::Result::NONE);
  CHECK(f.palette.query() == "undo");
}

TEST(palette_sits_on_the_bottom_rows_with_the_query_on_the_last) {
  Fixture f;
  f.feed("undo");
  auto text = f.buffer.toPlainText();
  std::vector<std::string> lines;
  size_t from = 0;
  for (size_t at; (at = text.find('\n', from)) != std::string::npos; from = at + 1) lines.push_back(text.substr(from, at - from));
  lines.push_back(text.substr(from));
  // Two matches: their rows, the description and the query are the last four
  // of the twenty rows.
  CHECK(lines.size() >= 20);
  CHECK(lines[16].find("undo") == 1);
  CHECK(lines[17].find("undo-redo") == 1);
  CHECK(lines[18].find("Undo the last change") == 1);
  CHECK(lines[19].find("M-x undo") == 1);
  CHECK(lines[15].find("undo") == std::string::npos);  // nothing above the list
}

namespace {
bool sameColor(const Color & a, const Color & b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}
}  // namespace

TEST(palette_list_is_a_raised_panel_but_the_description_and_query_use_the_display_background) {
  Fixture f;
  f.feed("undo");
  // Rows 16 and 17 list the two matches, 18 is the description, 19 the query.
  auto panel = f.styles.picker_bg_color;
  // The selected row (16) takes the cursor colour, the other the panel's.
  CHECK(f.buffer.at(16, 10).bg && sameColor(*f.buffer.at(16, 10).bg, f.styles.highlight_bg_color));
  CHECK(f.buffer.at(17, 10).bg && sameColor(*f.buffer.at(17, 10).bg, panel));
  for (int row : {18, 19}) {
    for (int col : {0, 1, 10, 79}) CHECK(!f.buffer.at(row, col).bg);
  }
  // The description is quiet and italic, so it does not read as a command.
  auto & description = f.buffer.at(18, 3);
  CHECK(description.italic);
  CHECK(description.fg && sameColor(*description.fg, f.styles.theme.text_secondary));
  CHECK(!f.buffer.at(16, 3).italic);
  CHECK(f.buffer.at(16, 3).fg && !sameColor(*f.buffer.at(16, 3).fg, *description.fg));
}
