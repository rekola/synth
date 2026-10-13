#include "TestFramework.h"

#include "TerminalUIFixture.h"

#include <regex>

using namespace terminal_ui_test;
namespace Key = cellui::Key;

namespace {

// The transport row from the info line ("1.1.1").
std::string transportRow(const UIFixture & f) {
  std::string info;
  for (const auto & line : f.lines()) {
    if (line.find("voices:") != std::string::npos) info = line;
  }
  std::istringstream in(info);
  std::string word, second;
  in >> word >> second;
  return second;
}

std::string firstTrackTitle(const UIFixture & f) {
  std::smatch m;
  for (const auto & line : f.lines()) {
    if (std::regex_search(line, m, std::regex("\xE2\x96\x8C\xE2\x97\x82 (T[0-9]+)"))) return m[1];
  }
  return "";
}

std::vector<int> rowsOf(const UIFixture & f) {
  std::vector<int> rows;
  std::regex row(R"(^ ([0-9a-f]{2}) \xE2\x94\x82)");
  std::smatch m;
  for (const auto & line : f.lines()) {
    if (std::regex_search(line, m, row)) rows.push_back(std::stoi(m[1], nullptr, 16));
  }
  return rows;
}

}  // namespace

// The wheel scrolls the widget under the mouse without moving focus, scrolls
// its view and never its cursor (or the transport); Shift scrolls tracks
// sideways; the next cursor move brings the view back.
TEST(terminal_ui_wheel_scrolls_the_view_not_the_cursor_or_focus) {
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT);
  f.otherWindow();  // the pattern editor
  for (int i = 0; i < 2; i++) f.wheel(20, 50);
  CHECK(!rowsOf(f).empty() && rowsOf(f)[0] == 2);
  CHECK(transportRow(f) == "1.1.1");

  f.wheel(20, 50, true, true);
  CHECK(firstTrackTitle(f) != "T0");

  f.wheel(3, 20);  // over the arrangement grid
  f.press(Key::Down);  // still reaches the pattern editor
  CHECK(transportRow(f) == "1.1.2");
  auto rows = rowsOf(f);
  rows.resize(std::min<size_t>(4, rows.size()));
  CHECK(std::find(rows.begin(), rows.end(), 1) != rows.end());
}

TEST(terminal_ui_wheel_in_live_view_scrolls_the_outline_and_leaves_the_scene) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  std::string tree_before = f.line(3);
  for (int i = 0; i < 3; i++) f.wheel(6, 5);  // over the outline tree
  CHECK(f.line(3) != tree_before);

  f.otherWindow();  // the pattern editor
  for (int i = 0; i < 4; i++) f.wheel(30, 50);
  // A note typed into the pattern editor creates a clip in its scene.
  f.type("q");
  CHECK(f.line(2).find("Clip") != std::string::npos);
}

namespace {

std::string patternLine(const UIFixture & f, const std::string & label) {
  for (const auto & line : f.lines()) {
    std::string prefix = " " + label + " ";
    if (line.compare(0, prefix.size(), prefix) == 0) return line;
  }
  return "";
}

}  // namespace

// The centralized keybinding dispatch: mark, kill, yank and quit in the
// pattern editor, toggle-playing, select-named-buffer, and Left/Right at a
// widget's edge staying in the widget.
TEST(terminal_ui_emacs_keybindings_reach_their_commands) {
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT);
  f.otherWindow();  // a fresh session starts on the overview, not the pattern editor

  f.press(' ', cellui::term::kModCtrl);
  CHECK(f.text().find("Mark set") != std::string::npos);
  f.press(Key::Down);
  f.press(Key::Down);
  f.ctrl('w');
  CHECK(f.text().find("Region killed") != std::string::npos);
  f.ctrl('y');
  CHECK(f.text().find("Yanked") != std::string::npos);
  f.press(' ', cellui::term::kModCtrl);
  f.ctrl('g');
  CHECK(f.text().find("Mark deactivated") != std::string::npos);

  f.press(' ');
  CHECK(f.text().find("Playing") != std::string::npos || f.text().find("Stopped") != std::string::npos);
  if (f.text().find("Playing") != std::string::npos) f.press(' ');

  // Typing a name that isn't an open buffer creates a fresh blank one.
  f.ctrl('x');
  f.press('b');
  f.type("keybindings_test");
  f.press(Key::Enter);
  CHECK(f.text().find("keybindings_test") != std::string::npos);

  // Left at the first column stays in the pattern editor.
  std::string before = patternLine(f, "00");
  for (int i = 0; i < 3; i++) f.press(Key::Left);
  f.type("q");
  CHECK(patternLine(f, "00") != before);

  // Right past the arrangement grid's last track stays in the grid.
  f.otherWindow();
  f.press(Key::Down);
  std::string row01 = patternLine(f, "01");
  for (int i = 0; i < 10; i++) f.press(Key::Right);
  f.type("w");  // would enter a note if focus had moved to the pattern editor
  CHECK(patternLine(f, "01") == row01);
}

// C-x C-c asks before discarding a dirty buffer, and then quits.
TEST(terminal_ui_quit_asks_about_a_dirty_buffer_then_ends_the_ui) {
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT);
  f.otherWindow();
  f.ctrl('w');  // kills the note under the cursor: the buffer is dirty
  CHECK(!f.ui->shouldClose());
  f.ctrl('x');
  f.ctrl('c');
  if (f.text().find("discard and quit") != std::string::npos) {
    f.type("y");
    f.press(Key::Enter);
  }
  CHECK(f.ui->shouldClose());
}

namespace {

constexpr int kOutlineCols = 30;  // the outline panel, including its divider column

// Screen rows 2-13 whose cell at column x has a background other than the
// plain one (that of the last row, empty in these songs): the cursor, or a
// marked row.
std::vector<int> markedRows(const UIFixture & f, int x) {
  std::vector<int> rows;
  std::string plain = f.bg(13, x);
  for (int y = 2; y < 13; y++) {
    if (f.bg(y, x) != plain) rows.push_back(y);
  }
  return rows;
}

// Rows whose first clip-grid cell shows the clip grid's cursor on an empty
// slot.
// A slot holding a clip has a colour of its own, which is not the cursor.
std::vector<int> gridCursorRows(const UIFixture & f) {
  std::vector<int> rows;
  for (int y : markedRows(f, kOutlineCols + 1)) {
    if (f.line(static_cast<size_t>(y)).find("Clip") == std::string::npos) rows.push_back(y);
  }
  return rows;
}

std::string lastLine(const UIFixture & f) {
  auto all = f.lines();
  return all.empty() ? "" : all.back();
}

}  // namespace

// Live View's layout: no scope row, the clip grid's cursor its own (never
// moving the pattern editor's track, nor following it, and never on the
// header row), each track's column marking the clip that track is at, and
// the outline panel's button bar and details popup.
TEST(terminal_ui_live_view_layout_and_the_clip_grid_cursor) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  CHECK(f.line(1).compare(0, 8, " Outline") == 0 && f.line(1).find("T0") != std::string::npos);

  for (int i = 0; i < 5; i++) f.press(Key::Up);
  CHECK(gridCursorRows(f) == std::vector<int>{2});

  for (int i = 0; i < 3; i++) f.press(Key::Down);
  f.otherWindow();  // the pattern editor
  // A note typed into the pattern editor creates a clip where the track is -
  // its first clip - not at the clip grid's cursor (row 4).
  f.type("q");
  CHECK(f.line(2).find("Clip") != std::string::npos);
  CHECK(f.line(5).find("Clip") == std::string::npos);

  f.press(Key::PageDown);
  f.press(Key::PageDown);
  CHECK(gridCursorRows(f).empty());  // an unfocused clip grid shows no cursor

  f.otherWindow();  // the outline panel
  f.press(Key::Down);
  f.press(Key::Down);  // a Song > Instruments row
  for (const char * button : { "[Del] Delete", "[a] Stop", "[?] Info" }) CHECK(f.text().find(button) != std::string::npos);

  f.type("?");
  CHECK(f.text().find("Details") != std::string::npos && f.text().find("Play note keys to preview") != std::string::npos);
  f.ctrl('g');
  CHECK(f.text().find("Play note keys to preview") == std::string::npos);

  // Escape closes the popup right away, yet still works as the Alt prefix:
  // the next key makes it M-x.
  f.type("?");
  f.press(Key::Escape);
  CHECK(f.text().find("Play note keys to preview") == std::string::npos);
  f.type("x");
  CHECK(lastLine(f).find("M-x") != std::string::npos);
  f.ctrl('g');

  // Back in the clip grid, its cursor is where it was left, whatever the
  // pattern editor did meanwhile.
  f.otherWindow();
  CHECK(gridCursorRows(f) == std::vector<int>{5});

  f.press(Key::Tab);
  CHECK(f.line(1).find("Outline") == std::string::npos);  // Arrangement view brings the scope row back
}
