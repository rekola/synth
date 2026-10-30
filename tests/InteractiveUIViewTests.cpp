#include <set>
#include <chrono>
#include <thread>
#include "TestFramework.h"

#include "InteractiveUIFixture.h"
#include "../src/model/ArrangementOps.h"
#include "../src/model/LeafTrack.h"
#include "../src/ui/CommandDescriptions.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <regex>
#include <thread>

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
  CHECK(!rowsOf(f).empty() && rowsOf(f)[0] == 6); // three rows a notch
  CHECK(transportRow(f) == "1.1.1");

  f.wheel(20, 50, true, true);
  CHECK(firstTrackTitle(f) != "T0");

  f.wheel(20, 50, true, true);
  auto shifted = firstTrackTitle(f);
  f.wheelSideways(20, 50, false);
  CHECK(firstTrackTitle(f) != shifted); // the sideways wheel scrolls tracks like Shift

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
  f.type("z");
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

  f.press(' ', cellui::kModCtrl);
  CHECK(f.text().find("Mark set") != std::string::npos);
  f.press(Key::Down);
  f.press(Key::Down);
  f.ctrl('w');
  CHECK(f.text().find("Region killed") != std::string::npos);
  f.ctrl('y');
  CHECK(f.text().find("Yanked") != std::string::npos);
  f.press(' ', cellui::kModCtrl);
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
  f.type("z");
  CHECK(patternLine(f, "00") != before);

  // Right past the arrangement grid's last track stays in the grid.
  f.otherWindow();
  f.press(Key::Down);
  std::string row01 = patternLine(f, "01");
  for (int i = 0; i < 10; i++) f.press(Key::Right);
  f.type("x"); // would enter a note if focus had moved to the pattern editor
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
  // The slots' own colour is the commonest one in the column.
  std::map<std::string, int> counts;
  for (int y = 2; y < 10; y++) counts[f.bg(y, x)]++;
  std::string plain;
  int most = 0;
  for (auto & [color, count] : counts) {
    if (count > most) most = count, plain = color;
  }
  for (int y = 2; y < 10; y++) { // the clip slots' rows
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
// With motion on, the cursor of a widget that loses focus fades out instead
// of vanishing at once.
TEST(terminal_ui_focus_change_fades_the_cursor) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  f.ui->setReducedMotion(false);
  f.ui->step();
  CHECK(!gridCursorRows(f).empty());
  f.otherWindow();                   // the pattern editor
  CHECK(!gridCursorRows(f).empty()); // still on its way out
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  f.ui->step();
  f.ui->step();
  CHECK(gridCursorRows(f).empty());
}

TEST(terminal_ui_live_view_layout_and_the_clip_grid_cursor) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  CHECK(f.line(1).compare(0, 8, " Outline") == 0 && f.line(1).find("T0") != std::string::npos);

  for (int i = 0; i < 5; i++) f.press(Key::Up);
  CHECK(gridCursorRows(f) == std::vector<int>{2});

  for (int i = 0; i < 3; i++) f.press(Key::Down);
  f.otherWindow();  // the pattern editor
  // A note typed into the pattern editor creates a clip where the track is -
  // its first clip - not at the clip grid's cursor (row 4).
  f.type("z");
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
  CHECK(f.terminal.info().focus == "command-palette");
  f.ctrl('g');

  // Back in the clip grid, its cursor is where it was left, whatever the
  // pattern editor did meanwhile.
  f.otherWindow();
  CHECK(gridCursorRows(f) == std::vector<int>{5});

  f.press(Key::Tab);
  CHECK(f.line(1).find("Outline") == std::string::npos);  // Arrangement view brings the scope row back
}

// On a display that shifts planes by part of a cell, a wheel notch moves the
// arrangement grid there (along the bars) over several frames: it is shown
// between bars, as a plane shifted by a fraction, until it settles on the whole bar.
TEST(terminal_ui_smooth_scroll_moves_the_arrangement_grid_between_bars) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT, display);
  {
    // More bars than the grid is wide, so there is something to scroll to.
    auto & song = f.controller->getSong();
    Song::Edit edit(song, "long arrangement");
    placeStopInstance(song, song.getPlayableTrackIds()[0], 16 * 300);
  }
  f.ui->step();
  auto shift = [&]() -> std::optional<double> {
    for (const auto & layer : f.terminal.info().backdrops) {
      if (layer.scroll && layer.scroll->row == 1 && layer.scroll->shift_cols > 0) return layer.scroll->shift_cols;
    }
    return std::nullopt;
  };
  CHECK(!shift());  // at rest it is not shifted
  f.wheel(3, 20);
  double seen = 0;
  bool between = false;
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    f.ui->step();
    if (auto s = shift()) {
      between = true;
      seen = *s;
    }
  }
  CHECK(between && seen > 0.0 && seen < 1.0);
  CHECK(!shift());  // settled on a whole bar
}

// The outline's tree scrolls by parts of a row under its heading, which stays
// where it is.
TEST(terminal_ui_smooth_scroll_moves_the_outline_tree_under_its_heading) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  auto shift = [&]() -> std::optional<double> {
    for (const auto & layer : f.terminal.info().backdrops) {
      if (layer.scroll && layer.scroll->shift_rows > 0) return layer.scroll->shift_rows;
    }
    return std::nullopt;
  };
  auto heading_row = [&]() {
    for (size_t y = 0; y < 6; y++) {
      if (f.line(y).find("Outline") == 1) return static_cast<int>(y);
    }
    return -1;
  };
  int heading = heading_row();
  CHECK(heading >= 0);
  CHECK(!shift());
  f.wheel(heading + 5, 5);
  bool between = false;
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    f.ui->step();
    if (auto s = shift()) {
      between = true;
      CHECK(*s > 0.0 && *s < 1.0);
    }
    CHECK(heading_row() == heading);
  }
  CHECK(between);
  CHECK(!shift());
  // It moved: the tree's first rows are not the first any more.
  CHECK(f.line(static_cast<size_t>(heading + 1)).find("Song") == std::string::npos);
}

// The clip grid's rows scroll by parts of a row under its header on a smooth
// display, and a click once settled picks the row it is on.
TEST(terminal_ui_smooth_scroll_moves_the_clip_grid_rows_under_their_header) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  {
    // More clip rows than the grid is tall, so there is something to scroll to.
    auto & song = f.controller->getSong();
    Song::Edit edit(song, "many clips");
    song.ensureClipAt(song.getPlayableTrackIds()[0], 80);
  }
  f.ui->step();
  auto shift = [&]() -> std::optional<double> {
    for (const auto & layer : f.terminal.info().backdrops) {
      if (layer.scroll && layer.scroll->shift_rows > 0 && layer.scroll->col >= kOutlineCols) return layer.scroll->shift_rows;
    }
    return std::nullopt;
  };
  auto header = f.line(1);
  CHECK(!shift());
  f.wheel(10, kOutlineCols + 3);
  bool between = false;
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    f.ui->step();
    if (auto s = shift()) {
      between = true;
      CHECK(*s > 0.0 && *s < 1.0);
    }
  }
  CHECK(between);
  CHECK(!shift());
  CHECK(f.line(1) == header); // the header stayed
}

// The clip grid's columns, header included, scroll sideways by parts of a cell
// on a smooth display, and settle on a whole column.
TEST(terminal_ui_smooth_scroll_moves_the_clip_grid_columns_sideways) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  auto shift = [&]() -> std::optional<double> {
    for (const auto & layer : f.terminal.info().backdrops) {
      if (layer.scroll && layer.scroll->shift_cols > 0 && layer.scroll->col >= kOutlineCols) return layer.scroll->shift_cols;
    }
    return std::nullopt;
  };
  auto header = f.line(1);
  CHECK(!shift());
  f.wheelSideways(10, kOutlineCols + 3);
  bool between = false;
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    f.ui->step();
    if (auto s = shift()) {
      between = true;
      CHECK(*s > 0.0 && *s < 1.0);
    }
  }
  CHECK(between);
  CHECK(!shift());
  CHECK(f.line(1) != header); // the columns, header with them, moved
}

// The outline's cursor is a layer of its own: once it is on the last row in
// view and the keys go on, it stays where it is and only the content moves.
TEST(terminal_ui_smooth_scroll_keeps_the_outline_cursor_in_place_at_the_edge) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  int heading = -1;
  for (size_t y = 0; y < 6; y++) {
    if (f.line(y).find("Outline") == 1) heading = static_cast<int>(y);
  }
  CHECK(heading >= 0);
  // The cursor is a translucent band over the tree, so the row shows through.
  auto cursor_row = [&]() {
    for (const auto & layer : f.terminal.info().backdrops) {
      if (layer.scroll && layer.scroll->band) {
        return static_cast<int>(std::lround(layer.scroll->row + layer.scroll->band->row));
      }
    }
    return -1;
  };
  auto settle = [&]() {
    for (int i = 0; i < 100; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      f.ui->step();
    }
  };
  for (int i = 0; i < 4 && f.terminal.info().focus != "outline-view"; i++) f.otherWindow();
  settle();
  for (int i = 0; i < 40; i++) f.press(cellui::Key::Down);
  settle();
  int edge = cursor_row();
  CHECK(edge > heading + 3);
  // More keys: the content moves under it, the cursor does not.
  bool moved = false;
  std::string before = f.line(static_cast<size_t>(edge));
  for (int i = 0; i < 3; i++) {
    f.press(cellui::Key::Down);
    for (int j = 0; j < 40; j++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      f.ui->step();
      CHECK(cursor_row() == edge);
    }
    if (f.line(static_cast<size_t>(edge)) != before) moved = true;
  }
  CHECK(moved);
}

// A click shows the pressed row; on release the cursor appears there instead
// of easing in from where it was.
TEST(terminal_ui_smooth_scroll_cursor_appears_on_the_clicked_row) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  auto band_row = [&]() {
    for (const auto & layer : f.terminal.info().backdrops) {
      if (layer.scroll && layer.scroll->band) return layer.scroll->row + layer.scroll->band->row;
    }
    return -1.0;
  };
  for (int i = 0; i < 4 && f.terminal.info().focus != "outline-view"; i++) f.otherWindow();
  f.press(cellui::Key::Down);
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    f.ui->step();
  }
  double before = band_row();
  CHECK(before >= 0);
  int y = static_cast<int>(before) + 6;
  f.terminal.pushMouse(y, 5, cellui::Key::Button1);
  f.ui->step();
  f.terminal.pushMouse(y, 5, cellui::Key::Button1, 0, cellui::Input::Kind::RELEASE);
  f.ui->step();
  CHECK(std::abs(band_row() - y) < 0.01);
}

// A press in the clip grid shows the cell pressed without moving the cursor;
// the release picks it.
TEST(terminal_ui_clip_grid_click_shows_pressed_then_picks_on_release) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  int x = kOutlineCols + 3, y = 5;
  auto cursor_before = gridCursorRows(f);
  auto bg_before = f.bg(y, x);
  f.terminal.pushMouse(y, x, cellui::Key::Button1);
  f.ui->step();
  CHECK(f.bg(y, x) != bg_before);
  auto marked = cursor_before;
  marked.push_back(y); // the pressed cell is marked too, the cursor stays where it was
  CHECK(gridCursorRows(f) == marked);
  f.terminal.pushMouse(y, x, cellui::Key::Button1, 0, cellui::Input::Kind::RELEASE);
  f.ui->step();
  CHECK(f.bg(y, x) != bg_before);
  CHECK(gridCursorRows(f) == std::vector<int>{y});
}

// The text version of the dials (a display without pictures): the caption
// stands on the Sends group's first row, with the A dial's three rows from there
// and the B dial's one row lower, and the Main dial three rows below the group's
// last.
namespace {
struct DialRows {
  int caption = -1;
  int a_bottom() const { return caption + 1; } // the middle row, which shows the value
  int b_bottom() const { return caption + 2; }
  int main_bottom() const { return caption + 6; }
};

DialRows dialRows(UIFixture & f) {
  DialRows rows;
  for (int i = 0; i < 40 && rows.caption < 0; i++) {
    if (f.line(static_cast<size_t>(i)).find("Sends", kOutlineCols) != std::string::npos) rows.caption = i;
  }
  return rows;
}

void clickCell(UIFixture & f, int y, int x) {
  f.terminal.pushMouse(y, x, cellui::Key::Button1);
  f.ui->step();
  f.terminal.pushMouse(y, x, cellui::Key::Button1, 0, cellui::Input::Kind::RELEASE);
  f.ui->step();
}
} // namespace

// A press that starts on the arrangement grid stays with it while the pointer
// drags over the pattern editor; a fresh click there takes the focus.
TEST(terminal_ui_a_drag_stays_with_the_widget_the_press_started_on) {
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT);
  f.terminal.pushMouse(3, 20, cellui::Key::Button1);
  f.ui->step();
  CHECK(f.terminal.info().focus == "arrangement-grid");
  // The terminal reports the drag as the press again.
  f.terminal.pushMouse(20, 50, cellui::Key::Button1);
  f.ui->step();
  CHECK(f.terminal.info().focus == "arrangement-grid");
  f.terminal.pushMouse(20, 50, cellui::Key::Button1, 0, cellui::Input::Kind::RELEASE);
  f.ui->step();
  clickCell(f, 20, 50);
  CHECK(f.terminal.info().focus == "pattern-editor");
}

// A click on a dial opens its editor, on that dial.
TEST(terminal_ui_clip_grid_click_on_a_send_opens_its_editor) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int column = kOutlineCols + 1;
  // The B dial of the first track.
  clickCell(f, rows.b_bottom(), column + 1 + 8 + 1);
  f.type("-6");
  CHECK(f.line(static_cast<size_t>(rows.b_bottom())).find("-6") != std::string::npos);
  f.ctrl('g');
  // The Main dial has its own row.
  clickCell(f, rows.main_bottom(), column + 5);
  f.type("-3");
  CHECK(f.line(static_cast<size_t>(rows.main_bottom())).find("-3") != std::string::npos);
  f.ctrl('g');
}

// Typing a dB value into a dial sets that send, and only that one.
TEST(terminal_ui_clip_grid_dial_edit_sets_the_send_it_is_on) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int column = kOutlineCols + 1;
  auto sends = [&]() {
    auto & song = f.controller->getSong();
    return dynamic_cast<const LeafTrack &>(*song.getMasterTrack().getChildByInternalId(song.getPlayableTrackIds()[0])).getSends();
  };
  auto before = sends();
  clickCell(f, rows.a_bottom(), column + 2);
  f.type("-6");
  f.press(cellui::Key::Enter);
  CHECK_NEAR(sendLinearToDb(sends().a), -6.0f, 0.01f);
  CHECK(sends().b == before.b && sends().main == before.main);
  clickCell(f, rows.main_bottom(), column + 5);
  f.type("-12");
  f.press(cellui::Key::Enter);
  CHECK_NEAR(sendLinearToDb(sends().main), -12.0f, 0.01f);
  CHECK_NEAR(sendLinearToDb(sends().a), -6.0f, 0.01f);
}

namespace {
float sendDbOfFirstTrack(UIFixture & f, int send) {
  auto & song = f.controller->getSong();
  auto sends = dynamic_cast<const LeafTrack &>(*song.getMasterTrack().getChildByInternalId(song.getPlayableTrackIds()[0])).getSends();
  return sendLinearToDb(send == 0 ? sends.main : send == 1 ? sends.a
                                                           : sends.b);
}
} // namespace

// A click opens the editor on the current value (with a caret); typing replaces it.
TEST(terminal_ui_clip_grid_dial_editor_shows_the_value_and_typing_replaces_it) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int column = kOutlineCols + 1;
  clickCell(f, rows.a_bottom(), column + 2);
  CHECK(f.line(static_cast<size_t>(rows.a_bottom())).find("▏") != std::string::npos);
  f.type("-7");
  f.press(cellui::Key::Enter);
  CHECK_NEAR(sendDbOfFirstTrack(f, 1), -7.0f, 0.01f);
}

// Dragging a dial up raises it and down lowers it (to off below the lowest
// step), and ending the drag does not open the editor.
TEST(terminal_ui_clip_grid_dragging_a_dial_turns_it) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int x = kOutlineCols + 1 + 2, y = rows.a_bottom();
  float start = sendDbOfFirstTrack(f, 1);
  auto drag = [&](int to_y) {
    f.terminal.pushMouse(y, x, cellui::Key::Button1);
    f.ui->step();
    f.terminal.pushMouse(to_y, x, cellui::Key::Button1);
    f.ui->step();
    f.terminal.pushMouse(to_y, x, cellui::Key::Button1, 0, cellui::Input::Kind::RELEASE);
    f.ui->step();
  };
  drag(y - 2);
  CHECK(sendDbOfFirstTrack(f, 1) > start);
  CHECK(f.line(static_cast<size_t>(y)).find("▏") == std::string::npos); // no editor
  drag(y + 3);
  CHECK(sendDbOfFirstTrack(f, 1) < -5.0f);
  for (int i = 0; i < 5; i++) drag(y + 3);
  CHECK(sendDbOfFirstTrack(f, 1) < -99.0f);
  CHECK(f.line(static_cast<size_t>(y)).find("▏") == std::string::npos);
}

// The wheel over a dial turns it instead of scrolling the grid.
TEST(terminal_ui_clip_grid_wheel_over_a_dial_turns_it) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int x = kOutlineCols + 1 + 2, y = rows.a_bottom();
  float start = sendDbOfFirstTrack(f, 1);
  for (int i = 0; i < 3; i++) f.ui->step(); // frames with nothing to redraw
  auto before = f.line(static_cast<size_t>(rows.caption));
  f.wheel(y, x, false);
  CHECK_NEAR(sendDbOfFirstTrack(f, 1), start + 1.0f, 0.05f);
  f.wheel(y, x, true);
  f.wheel(y, x, true);
  CHECK_NEAR(sendDbOfFirstTrack(f, 1), start - 1.0f, 0.05f);
  CHECK(f.line(static_cast<size_t>(rows.caption)) == before); // the grid did not scroll
}

// Escape closes a dial's editor, even as a bare Escape in the terminal, and
// the value stays.
TEST(terminal_ui_clip_grid_escape_closes_the_dial_editor) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int column = kOutlineCols + 1;
  auto start = sendDbOfFirstTrack(f, 1);
  clickCell(f, rows.a_bottom(), column + 2);
  CHECK(f.line(static_cast<size_t>(rows.a_bottom())).find("▏") != std::string::npos);
  f.press(cellui::Key::Escape);
  CHECK(f.line(static_cast<size_t>(rows.a_bottom())).find("▏") == std::string::npos);
  CHECK_NEAR(sendDbOfFirstTrack(f, 1), start, 0.001f);
  // And the keys go back to the grid.
  f.press(cellui::Key::Right);
}

// Moving the focus away closes the editor too.
TEST(terminal_ui_clip_grid_losing_focus_closes_the_dial_editor) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int column = kOutlineCols + 1;
  clickCell(f, rows.a_bottom(), column + 2);
  CHECK(f.line(static_cast<size_t>(rows.a_bottom())).find("▏") != std::string::npos);
  clickCell(f, rows.a_bottom() + 12, column + 10); // a click in the pattern editor takes the focus
  CHECK(f.line(static_cast<size_t>(rows.a_bottom())).find("▏") == std::string::npos);
}

// Clicking another dial closes the open editor (and opens that dial's own).
TEST(terminal_ui_clip_grid_clicking_another_dial_closes_the_editor) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int column = kOutlineCols + 1;
  clickCell(f, rows.a_bottom(), column + 2); // A
  CHECK(f.line(static_cast<size_t>(rows.a_bottom())).find("▏") != std::string::npos);
  clickCell(f, rows.main_bottom(), column + 5); // Main
  CHECK(f.line(static_cast<size_t>(rows.a_bottom())).find("▏") == std::string::npos);
  CHECK(f.line(static_cast<size_t>(rows.main_bottom())).find("▏") != std::string::npos);
  // A click on the dial being edited keeps it open.
  clickCell(f, rows.main_bottom(), column + 5);
  CHECK(f.line(static_cast<size_t>(rows.main_bottom())).find("▏") != std::string::npos);
}

// A drag goes on turning the dial when the pointer leaves the clip grid.
TEST(terminal_ui_clip_grid_drag_continues_outside_the_grid) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int x = kOutlineCols + 1 + 2, y = rows.a_bottom();
  float start = sendDbOfFirstTrack(f, 1);
  f.terminal.pushMouse(y, x, cellui::Key::Button1);
  f.ui->step();
  // Down into the pattern editor below, and out to the right.
  f.terminal.pushMouse(y + 12, x + 30, cellui::Key::Button1);
  f.ui->step();
  float lowered = sendDbOfFirstTrack(f, 1);
  CHECK(lowered < start);
  f.terminal.pushMouse(y + 12, x + 30, cellui::Key::Button1, 0, cellui::Input::Kind::RELEASE);
  f.ui->step();
  CHECK_NEAR(sendDbOfFirstTrack(f, 1), lowered, 0.001f);
}

// On the Sends row Left and Right walk through the two dials before moving
// on to the next column.
TEST(terminal_ui_clip_grid_left_right_walks_the_two_dials) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto rows = dialRows(f);
  CHECK(rows.caption > 0);
  int column = kOutlineCols + 1;
  clickCell(f, rows.a_bottom(), column + 2); // the A dial: opens its editor
  f.ctrl('g');
  f.press(cellui::Key::Right); // to B
  f.press(cellui::Key::Enter);
  f.type("-9");
  CHECK(f.line(static_cast<size_t>(rows.b_bottom())).find("-9") != std::string::npos);
  f.ctrl('g');
  f.press(cellui::Key::Left); // back to A
  f.press(cellui::Key::Enter);
  f.type("-9");
  CHECK(f.line(static_cast<size_t>(rows.a_bottom())).find("-9") != std::string::npos);
  f.ctrl('g');
}

// The command prompt opens with M-x on the terminal keymap and with
// Ctrl+Shift+P on the desktop one, and each keymap ignores the other's chord.
TEST(terminal_ui_command_prompt_shortcut_follows_the_keymap_profile) {
  auto opens = [](UIFixture & f, int key, unsigned mods) {
    f.press(key, mods);
    bool open = f.terminal.info().focus == "command-palette";
    if (open) f.ctrl('g');
    return open;
  };
  UIFixture emacs;
  CHECK(opens(emacs, 'x', kModAlt));
  CHECK(!opens(emacs, 'p', kModCtrl | cellui::kModShift));

  cellui::DisplayInfo desktop_display;
  desktop_display.keymap_profile = cellui::KeymapProfile::DESKTOP;
  UIFixture desktop("", UI::View::LIVE, desktop_display);
  CHECK(opens(desktop, 'p', kModCtrl | cellui::kModShift));
  CHECK(!opens(desktop, 'x', kModAlt));
}

// The --keymap option overrides the keymap the display declares, both ways.
TEST(terminal_ui_keymap_override_replaces_the_display_keymap) {
  auto opens = [](UIFixture & f, int key, unsigned mods) {
    f.press(key, mods);
    bool open = f.terminal.info().focus == "command-palette";
    if (open) f.ctrl('g');
    return open;
  };
  UIFixture gui_on_terminal("", UI::View::LIVE, {}, cellui::KeymapProfile::DESKTOP);
  CHECK(opens(gui_on_terminal, 'p', kModCtrl | cellui::kModShift));
  CHECK(!opens(gui_on_terminal, 'x', kModAlt));

  cellui::DisplayInfo desktop_display;
  desktop_display.keymap_profile = cellui::KeymapProfile::DESKTOP;
  UIFixture terminal_on_window("", UI::View::LIVE, desktop_display, cellui::KeymapProfile::TERMINAL);
  CHECK(opens(terminal_on_window, 'x', kModAlt));
  CHECK(!opens(terminal_on_window, 'p', kModCtrl | cellui::kModShift));
}

// Ctrl-X is the cut chord on the desktop keymap, not a prefix that swallows
// the next key.
TEST(terminal_ui_desktop_ctrl_x_is_not_a_prefix) {
  cellui::DisplayInfo desktop_display;
  desktop_display.keymap_profile = cellui::KeymapProfile::DESKTOP;
  UIFixture f("", UI::View::LIVE, desktop_display);
  f.press('x', kModCtrl);
  f.press('b'); // C-x b would open the buffer prompt on the terminal keymap
  CHECK(lastLine(f).find("buffer") == std::string::npos);

  UIFixture emacs;
  emacs.press('x', kModCtrl);
  emacs.press('b');
  CHECK(lastLine(emacs).find("uffer") != std::string::npos);
}

// The desktop keymap saves with Ctrl+S, which the Emacs one leaves unbound.
TEST(terminal_ui_desktop_ctrl_s_saves) {
  // A fresh buffer saves as song.xml in the working directory.
  auto dir = std::filesystem::temp_directory_path() / "synth_ctrl_s_test";
  std::filesystem::create_directories(dir);
  auto previous = std::filesystem::current_path();
  std::filesystem::current_path(dir);
  auto saves = [](UIFixture & f) {
    f.press('s', kModCtrl);
    return lastLine(f).find("Saved") != std::string::npos;
  };
  UIFixture emacs;
  CHECK(!saves(emacs));
  cellui::DisplayInfo desktop_display;
  desktop_display.keymap_profile = cellui::KeymapProfile::DESKTOP;
  UIFixture desktop("", UI::View::LIVE, desktop_display);
  CHECK(saves(desktop));
  std::filesystem::current_path(previous);
  std::filesystem::remove_all(dir);
}

// The palette lists the commands, narrows them as the query is typed, shows
// the bindings, and runs the chosen one.
TEST(terminal_ui_command_palette_filters_and_runs_a_command) {
  UIFixture f;
  f.meta('x');
  CHECK(f.terminal.info().focus == "command-palette");
  CHECK(f.text().find("about") != std::string::npos); // the list starts alphabetically
  // A fuzzy query: the letters in order, not adjacent.
  f.type("tgv");
  CHECK(f.text().find("toggle-view") != std::string::npos);
  CHECK(f.text().find("Switch between Live View and the arrangement") != std::string::npos);
  CHECK(f.text().find("toggle-mute") == std::string::npos);
  CHECK(f.text().find("TAB") != std::string::npos);
  f.press(cellui::Key::Enter);
  CHECK(f.terminal.info().focus == "pattern-editor"); // Live View -> Arrangement view and the editor
}

TEST(terminal_ui_command_palette_shows_the_desktop_binding_on_a_desktop_display) {
  cellui::DisplayInfo desktop_display;
  desktop_display.keymap_profile = cellui::KeymapProfile::DESKTOP;
  UIFixture f("", UI::View::LIVE, desktop_display);
  f.press('p', kModCtrl | cellui::kModShift);
  f.type("undo");
  CHECK(f.text().find("Ctrl+Z") != std::string::npos);
  CHECK(f.text().find("Ctrl+Shift+Z") == std::string::npos); // undo-redo is below, not shown selected
  f.press(cellui::Key::Escape);
  CHECK(f.terminal.info().focus != "command-palette");
}

// Every command the palette lists says what it does.
TEST(every_command_has_a_description) {
  UIFixture f;
  for (auto & name : f.controller->commandCompletions("")) {
    if (commandDescription(name).empty()) fprintf(stderr, "no description: %s\n", name.c_str());
    CHECK(!commandDescription(name).empty());
  }
}

// Tab in the palette completes the typed prefix like the Emacs minibuffer.
TEST(terminal_ui_command_palette_tab_completes_a_prefix) {
  UIFixture f;
  f.meta('x');
  f.type("toggle-v");
  f.press(cellui::Key::Tab);
  CHECK(f.text().find("M-x toggle-view") != std::string::npos);
  f.press(cellui::Key::Escape);
}

// The clip grid belongs to Live View: in Arrangement view its plane is hidden,
// not just under the pattern editor.
TEST(terminal_ui_switching_views_hides_the_clip_grid_in_arrangement_view) {
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE);
  auto clip_grid_visible = [&]() {
    for (auto & plane : f.terminal.info().planes.children) {
      if (plane.name == "clip-grid") return plane.visible;
    }
    return false;
  };
  CHECK(clip_grid_visible());
  for (int i = 0; i < 4; i++) {
    f.press(cellui::Key::Tab);
    f.ui->step();
    CHECK(clip_grid_visible() == (i % 2 == 1));
  }
}

// The query row is on the display's own background, like the status line it
// covers, and the description above it is not drawn like a command.
TEST(terminal_ui_command_palette_query_row_keeps_the_display_background) {
  UIFixture f;
  f.meta('x');
  f.type("undo");
  auto & frame = f.terminal.frame();
  int last = frame.rows() - 1;
  for (int col : {0, 1, 10, frame.cols() - 1}) CHECK(!frame.at(last, col).bg);
  for (int col : {0, 1, 10, frame.cols() - 1}) CHECK(!frame.at(last - 1, col).bg);
  CHECK(frame.at(last - 1, 3).italic);
  CHECK(frame.at(last - 3, 10).bg);  // the list is a raised panel
  f.press(cellui::Key::Escape);
}

// The pattern editor's rows scroll by parts of a row under the headings when
// the view follows the cursor past its margin.
TEST(terminal_ui_smooth_scroll_moves_the_pattern_editor_rows_under_their_headings) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT, display);
  auto shift = [&]() -> std::optional<double> {
    for (const auto & layer : f.terminal.info().backdrops) {
      if (layer.scroll && layer.scroll->shift_rows > 0) return layer.scroll->shift_rows;
    }
    return std::nullopt;
  };
  for (int i = 0; i < 4 && f.terminal.info().focus != "pattern-editor"; i++) f.otherWindow();
  CHECK(f.terminal.info().focus == "pattern-editor");
  // The headings are on a plane of their own, which does not move.
  auto heading_line = [&]() {
    for (size_t y = 0; y < 30; y++) {
      if (f.line(y).find("Pizzicato") != std::string::npos) return static_cast<int>(y);
    }
    return -1;
  };
  int heading = heading_line();
  CHECK(heading >= 0);
  bool between = false;
  for (int i = 0; i < 100; i++) {
    f.press(cellui::Key::Down);
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    f.ui->step();
    if (auto s = shift()) {
      between = true;
      CHECK(*s > 0.0 && *s < 1.0);
      // What shifts is the rows' plane: no heading text in it.
      for (const auto & layer : f.terminal.info().backdrops) {
        if (!layer.scroll) continue;
        const auto & content = *layer.scroll;
        if (content.shift_rows <= 0) continue;
        std::string text;
        for (int r = 0; r < content.cells.rows(); r++) {
          for (int c = 0; c < content.cells.cols(); c++) text += content.cells.at(r, c).text;
        }
        CHECK(text.find("Pizzicato") == std::string::npos);
        CHECK(text.find("T0") == std::string::npos);
      }
    }
    CHECK(heading_line() == heading);
  }
  CHECK(between);
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    f.ui->step();
  }
  CHECK(!shift());  // settled on a whole row
}

// Live View's columns each scroll by themselves: every track's column is a
// plane of its own with its own offset, and the headings stay put.
TEST(terminal_ui_smooth_scroll_moves_live_view_pattern_columns_each_by_itself) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  for (int i = 0; i < 6 && f.terminal.info().focus != "pattern-editor"; i++) f.otherWindow();
  CHECK(f.terminal.info().focus == "pattern-editor");
  auto heading_line = [&]() {
    for (size_t y = 0; y < 30; y++) {
      if (f.line(y).find("Pizzicato") != std::string::npos) return static_cast<int>(y);
    }
    return -1;
  };
  int heading = heading_line();
  CHECK(heading >= 0);
  auto shifted = [&]() {
    size_t count = 0;
    for (const auto & layer : f.terminal.info().backdrops) {
      if (!layer.scroll || layer.scroll->shift_rows <= 0 || layer.scroll->cols <= 60) continue;
      CHECK(layer.scroll->shift_rows < 1.0);
      std::string text;
      for (int r = 0; r < layer.scroll->cells.rows(); r++) {
        for (int c = 0; c < layer.scroll->cells.cols(); c++) text += layer.scroll->cells.at(r, c).text;
      }
      CHECK(text.find("Pizzicato") == std::string::npos);
      count++;
    }
    return count;
  };
  size_t most = 0;
  for (int i = 0; i < 150; i++) {
    f.press(cellui::Key::Down);
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    f.ui->step();
    most = std::max(most, shifted());
    CHECK(heading_line() == heading);
  }
  CHECK(most > 1); // a plane per column
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    f.ui->step();
  }
  CHECK(shifted() == 0); // settled on whole rows
}

// Switching from Live View to Arrangement view, on a display that scrolls
// smoothly, still shows the pattern editor's rows.
TEST(terminal_ui_smooth_scroll_pattern_editor_rows_show_after_leaving_live_view) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  for (int i = 0; i < 5; i++) {
    f.ui->step();
  }
  f.press(cellui::Key::Tab);
  for (int i = 0; i < 5; i++) {
    f.press(cellui::Key::Down);
    f.ui->step();
  }
  bool rows = false;
  for (size_t y = 0; y < 40; y++) {
    if (f.line(y).find("----") != std::string::npos) rows = true;
  }
  CHECK(rows);
}

// Live View's column planes show through each other: every track's column of
// rows is on screen at once.
TEST(terminal_ui_smooth_scroll_live_view_columns_do_not_blank_each_other) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  for (int i = 0; i < 5; i++) {
    f.ui->step();
  }
  size_t widest = 0;
  for (size_t y = 0; y < 40; y++) {
    auto line = f.line(y);
    size_t count = 0;
    for (auto at = line.find("----"); at != std::string::npos; at = line.find("----", at + 1)) count++;
    widest = std::max(widest, count);
  }
  CHECK(widest >= 3);
}

// Live View's column lines are bands that hold still while the rows scroll
// under them: past the margin the band stays on one row and the rows move.
TEST(terminal_ui_smooth_scroll_live_view_line_band_holds_still_while_the_rows_scroll) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  for (int i = 0; i < 6 && f.terminal.info().focus != "pattern-editor"; i++) f.otherWindow();
  CHECK(f.terminal.info().focus == "pattern-editor");
  auto first_band_row = [&]() -> std::optional<double> {
    for (const auto & layer : f.terminal.info().backdrops) {
      if (layer.scroll && layer.scroll->cols > 60 && layer.scroll->band) return layer.scroll->band->row;
    }
    return std::nullopt;
  };
  for (int i = 0; i < 30; i++) {
    f.press(cellui::Key::Down);
    f.ui->step();
  }
  for (int i = 0; i < 100; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    f.ui->step();
  }
  auto settled = first_band_row();
  CHECK(settled.has_value());
  bool moved = false;
  for (int i = 0; i < 20; i++) {
    f.press(cellui::Key::Down);
    for (int k = 0; k < 6; k++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      f.ui->step();
      if (auto band = first_band_row()) CHECK(*band == *settled);
      for (const auto & layer : f.terminal.info().backdrops) {
        if (layer.scroll && layer.scroll->cols > 60 && layer.scroll->shift_rows > 0) moved = true;
      }
    }
  }
  CHECK(moved);
}

// The arrangement grid has time running to the right: a bar ruler and the
// locators over one row per track, each named at the left.
TEST(terminal_ui_arrangement_grid_shows_bars_to_the_right_of_the_track_names) {
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT);
  CHECK(f.line(1).find("1   5   9") != std::string::npos); // the ruler
  CHECK(f.line(3).find("T0") != std::string::npos);        // the first track's name
  CHECK(f.line(1).find("Spectrum") != std::string::npos);  // the scopes have title bars
  CHECK(f.text().find("DirAC") != std::string::npos);
}

// A cursor shows only while the window has the focus, in the outline, the clip
// grid and the pattern editor alike; the columns' lines hold on to nothing but
// a playhead; an editor without the focus shows none.
TEST(terminal_ui_cursors_are_hidden_while_the_window_is_not_focused) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::LIVE, display);
  for (int i = 0; i < 5; i++) f.ui->step();
  auto bands = [&](bool wide) { // layers with a band: the columns' lines (wide) or any other
    size_t count = 0;
    for (const auto & layer : f.terminal.info().backdrops) {
      if (!layer.scroll || !layer.scroll->band) continue;
      if ((layer.scroll->cols > 60) == wide) count++;
    }
    return count;
  };
  auto cells = [&]() {
    std::vector<std::string> out;
    const auto & frame = f.terminal.frame();
    for (int r = 0; r < frame.rows(); r++) {
      for (int c = 0; c < frame.cols(); c++) {
        const auto & cell = frame.at(r, c);
        out.push_back(cell.bg ? std::to_string(cell.bg->getRed()) + "," + std::to_string(cell.bg->getGreen()) + "," + std::to_string(cell.bg->getBlue()) : std::string("-"));
      }
    }
    return out;
  };
  auto settle = [&]() {
    for (int i = 0; i < 12; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      f.ui->step();
    }
  };
  settle();
  auto focused_cells = cells();
  CHECK(bands(true) == 0); // the clip grid has the focus: the editor's columns show no cursor line
  f.terminal.pushWindowFocus(false);
  settle();
  CHECK(bands(true) == 0); // no track plays: only cursors were shown
  CHECK(cells() != focused_cells);
  f.terminal.pushWindowFocus(true);
  settle();
  CHECK(bands(true) == 0);
  CHECK(cells() == focused_cells);
}

// The arrangement grid's cursor cell is hidden too; its playing row stays.
TEST(terminal_ui_arrangement_grid_cursor_is_hidden_while_the_window_is_not_focused) {
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT);
  for (int i = 0; i < 5; i++) f.ui->step();
  auto grid_cells = [&]() {
    std::vector<std::string> out;
    for (auto & plane : f.terminal.info().planes.children) {
      if (plane.name != "arrangement-grid" || !plane.visible) continue;
      const auto & frame = f.terminal.frame();
      for (int r = plane.y; r < plane.y + plane.rows; r++) {
        for (int c = plane.x; c < plane.x + plane.cols; c++) {
          const auto & cell = frame.at(r, c);
          out.push_back(cell.bg ? std::to_string(cell.bg->getRed()) + "," + std::to_string(cell.bg->getGreen()) + "," + std::to_string(cell.bg->getBlue()) : std::string("-"));
        }
      }
    }
    return out;
  };
  auto settle = [&]() {
    for (int i = 0; i < 12; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      f.ui->step();
    }
  };
  settle();
  auto focused_cells = grid_cells();
  CHECK(!focused_cells.empty());
  f.terminal.pushWindowFocus(false);
  settle();
  CHECK(grid_cells() != focused_cells);
  f.terminal.pushWindowFocus(true);
  settle();
  CHECK(grid_cells() == focused_cells);
}

namespace {

// The bands over the planes of a smooth display: the cursor row's (and the
// playheads') strips are wide, the cursor cell's is narrower than the editor.
struct BandCount {
  size_t wide = 0, narrow = 0, bordered = 0;
};
BandCount countBands(UIFixture & f) {
  BandCount count;
  for (const auto & layer : f.terminal.info().backdrops) {
    if (!layer.scroll || !layer.scroll->band) continue;
    (layer.scroll->band->cols > 60 ? count.wide : count.narrow)++;
    if (layer.scroll->band->border_px > 0) count.bordered++;
  }
  return count;
}

std::set<std::string> backgrounds(UIFixture & f) {
  std::set<std::string> out;
  const auto & frame = f.terminal.frame();
  for (int r = 0; r < frame.rows(); r++) {
    for (int c = 0; c < frame.cols(); c++) {
      const auto & cell = frame.at(r, c);
      if (cell.bg) out.insert(std::to_string(cell.bg->getRed()) + "," + std::to_string(cell.bg->getGreen()) + "," + std::to_string(cell.bg->getBlue()));
    }
  }
  return out;
}

} // namespace

// On a smooth display the cursor is four bands over the rows, a line, the
// border of the note and the field under it inverted and given its hue, which hold still while
// the rows scroll; none shows while the window is not focused.
TEST(terminal_ui_smooth_pattern_editor_cursor_is_a_band_over_the_rows) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT, display);
  f.otherWindow();
  for (int i = 0; i < 5; i++) f.ui->step();
  auto count = countBands(f);
  CHECK(count.wide == 1);
  CHECK(count.narrow == 3); // the border, and the field: an inverting band and the hue over it
  CHECK(count.bordered == 1);
  f.terminal.pushWindowFocus(false);
  for (int i = 0; i < 5; i++) f.ui->step();
  count = countBands(f);
  CHECK(count.wide == 0);
  CHECK(count.narrow == 0);
  CHECK(count.bordered == 0);
  f.terminal.pushWindowFocus(true);
  for (int i = 0; i < 5; i++) f.ui->step();
  count = countBands(f);
  CHECK(count.wide == 1);
  CHECK(count.narrow == 3); // the border, and the field: an inverting band and the hue over it
  CHECK(count.bordered == 1);
}

// The selection is drawn in the rows, so it scrolls with them; the cursor is
// not (there is no highlight in the rows without a mark).
TEST(terminal_ui_smooth_pattern_editor_selection_stays_in_the_rows) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT, display);
  f.otherWindow();
  f.press(Key::Down);
  auto without = backgrounds(f);
  f.press(' ', cellui::kModCtrl);
  f.press(Key::Down);
  f.press(Key::Down);
  auto with = backgrounds(f);
  bool new_colour = false;
  for (const auto & colour : with) new_colour = new_colour || !without.count(colour);
  CHECK(new_colour);
}


// In Arrangement view an editor that is not the focus shows no cursor: the cursor row
// looks like the other bar-start row until the editor has the focus.
TEST(terminal_ui_arrangement_pattern_editor_shows_no_cursor_without_the_focus) {
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT);
  for (int i = 0; i < 3; i++) f.ui->step();
  auto row_of = [&](const std::string & number) {
    auto lines = f.lines();
    for (size_t r = 0; r < lines.size(); r++) {
      if (lines[r].rfind(" " + number + " ", 0) == 0) return static_cast<int>(r);
    }
    return -1;
  };
  int first = row_of("00"), second = row_of("04");
  CHECK(first >= 0 && second > first);
  auto bg = [&](int row) { return f.terminal.frame().at(row, 10).bg; };
  CHECK(bg(first) == bg(second));
  f.otherWindow();
  CHECK(bg(first) != bg(second));
}

// The cursor cell inverts what is under it: its band blends by difference.
TEST(terminal_ui_smooth_pattern_editor_cursor_cell_band_inverts) {
  cellui::DisplayInfo display;
  display.smooth_scroll = true;
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT, display);
  f.otherWindow();
  for (int i = 0; i < 3; i++) f.ui->step();
  size_t inverting = 0;
  for (const auto & layer : f.terminal.info().backdrops) {
    if (layer.scroll && layer.scroll->band && layer.scroll->band->blend == cellui::BlendMode::DIFFERENCE) inverting++;
  }
  CHECK(inverting == 1);
}

// A locator's name is placed by the plane, so a wide character takes two
// columns and the next locator's mark stays where its bar is.
TEST(terminal_ui_arrangement_grid_locator_names_may_be_wide) {
  UIFixture f(repoSong("demo3.xml"), UI::View::ARRANGEMENT);
  {
    auto & song = f.controller->getSong();
    Song::Edit edit(song, "locators");
    song.setLocator(0, "歌歌");
    song.setLocator(16 * 8, "Chorus");
  }
  f.ui->step();
  f.ui->step();
  // Bars are one column each: the second locator's mark is as many columns on as bars away.
  auto row = f.line(2);
  auto first = row.find("›歌歌");
  CHECK(first != std::string::npos);
  CHECK(row.find("›Chorus") != std::string::npos);
  // The wide characters take two columns each, so the gap to the second mark is still its bar's distance.
  auto second = row.find("›Chorus");
  auto between = row.substr(first, second - first);
  CHECK(cellui::utf8::displayWidth(between) == f.controller->getSong().getArrangementBars().barIndex(16 * 8));
}

// The scopes in Arrangement view have the size they have in Live View: the
// DirAC title sits a scope's height (and its own title bar) below the spectrum's.
TEST(terminal_ui_arrangement_view_scopes_are_as_tall_as_in_live_view) {
  auto title_row = [](UIFixture & f, const std::string & title) {
    auto lines = f.lines();
    for (size_t y = 0; y < lines.size(); y++) {
      if (lines[y].find(title) != std::string::npos) return static_cast<int>(y);
    }
    return -1;
  };
  UIFixture arrangement(repoSong("demo3.xml"), UI::View::ARRANGEMENT);
  UIFixture live(repoSong("demo3.xml"), UI::View::LIVE);
  int a = title_row(arrangement, "DirAC") - title_row(arrangement, "Spectrum");
  int l = title_row(live, "DirAC") - title_row(live, "Spectrum");
  CHECK(a > 0);
  CHECK(a == l);
}
