#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ui/tui/TerminalUI.h"

#include <cellui/term/TestTerminal.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

namespace {

using cellui::term::kModAlt;
using cellui::term::kModCtrl;

// A terminal UI over a song (a fresh one by default) on a test terminal: no
// tty, no audio device. Every key is followed by one turn of the UI's loop.
struct UIFixture {
  ChannelConfiguration config{44100, 1};
  std::shared_ptr<Controller> controller = std::make_shared<Controller>(config);
  cellui::term::TestTerminal terminal{40, 120};
  std::unique_ptr<TerminalUI> ui;

  explicit UIFixture(const std::string & song = "", UI::View view = UI::View::LIVE) {
    if (song.empty()) controller->switchToBuffer(controller->freshBufferName());
    else CHECK(controller->openSong(song));
    ui = std::make_unique<TerminalUI>(terminal);
    ui->setTheme("dark");
    ui->setInitialView(view);
    ui->initialize(controller);
    ui->step();  // the loop's first frame is a full one
  }
  void press(int key, unsigned mods = 0) {
    terminal.pushKey(key, mods);
    ui->step();
  }
  // C-<c>, M-<c>; letters with Ctrl are sent upper case, as a terminal does.
  void ctrl(char c) { press(std::toupper(c), kModCtrl); }
  void meta(char c) { press(c, kModAlt); }
  void type(const std::string & text) {
    terminal.pushText(text);
    ui->step();
  }
  // M-x <command> RET
  void execute(const std::string & command) {
    meta('x');
    type(command);
    press(cellui::Key::Enter);
  }
  // C-x o: other-window.
  void otherWindow() {
    ctrl('x');
    press('o');
  }
  std::vector<std::string> lines() const {
    std::vector<std::string> out;
    std::istringstream in(terminal.text());
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
  }
  std::string text() const { return terminal.text(); }
};

std::string fixture(const char * name) { return std::string(TESTS_FIXTURES_DIR) + "/" + name; }

// The pattern editor's visible row numbers, from the heading down.
std::vector<int> patternRows(const UIFixture & f) {
  std::vector<int> numbers;
  auto lines = f.lines();
  size_t heading = 0;
  while (heading < lines.size() && lines[heading].find("\xE2\x96\x8C\xE2\x97\x82 T0") == std::string::npos) heading++;
  for (size_t i = heading + 2; i < lines.size(); i++) {
    const std::string & line = lines[i];
    if (line.size() < 5 || line[0] != ' ' || !isxdigit(static_cast<unsigned char>(line[1])) ||
        !isxdigit(static_cast<unsigned char>(line[2])) || line.compare(3, 4, " \xE2\x94\x82") != 0) break;
    numbers.push_back(std::stoi(line.substr(1, 2), nullptr, 16));
  }
  return numbers;
}

}  // namespace

TEST(terminal_ui_presents_a_frame_with_its_focus_and_planes) {
  UIFixture f;
  CHECK(f.terminal.frameCount() >= 1);
  CHECK(f.terminal.info().focus == "clip-grid");
  CHECK(!f.terminal.info().planes.children.empty());
  CHECK(f.terminal.frame().rows() == 40);
  CHECK(f.terminal.toJson().find("\"focus\":\"clip-grid\"") != std::string::npos);
}

TEST(terminal_ui_tab_switches_view_and_moves_focus) {
  UIFixture f;
  CHECK(f.terminal.text().find("File  Edit  Track") != std::string::npos);
  size_t before = f.terminal.frameCount();
  f.press('\t');
  CHECK(f.terminal.frameCount() > before);
  CHECK(f.terminal.text().find(" master") != std::string::npos);
  CHECK(f.terminal.text().find("00 │") != std::string::npos);
  CHECK(f.terminal.info().focus == "pattern-editor");
}

TEST(terminal_ui_follows_a_resize) {
  UIFixture f;
  f.terminal.resize(30, 90);
  f.ui->step();
  CHECK(f.terminal.frame().rows() == 30);
  CHECK(f.terminal.frame().cols() == 90);
}

// The pattern editor moves its cursor down the screen without scrolling until
// it comes within the scroll margin of the bottom, then scrolls to keep it
// there.
TEST(terminal_ui_scrolls_only_within_the_margin_of_the_bottom) {
  constexpr int kMargin = 3;
  UIFixture f(fixture("center_note.xml"), UI::View::ARRANGEMENT);
  f.otherWindow();  // the pattern editor
  auto rows = patternRows(f);
  size_t visible = rows.size();
  CHECK(visible > 2 * kMargin);
  CHECK(!rows.empty() && rows[0] == 0);

  size_t steps = 0;
  while (steps < 64 && patternRows(f)[0] == 0) {
    f.press(cellui::Key::Down);
    steps++;
  }
  CHECK(steps == visible - kMargin);
  int cursor_line = static_cast<int>(steps) - patternRows(f)[0];
  CHECK(cursor_line == static_cast<int>(visible) - 1 - kMargin);
}

// kill-region on an empty clip slot removes its stop button, a second press
// has nothing left, and toggle-stop-button brings it back.
TEST(terminal_ui_clip_grid_stop_button_is_removed_and_restored) {
  UIFixture f(fixture("terminal_ui_live.xml"), UI::View::LIVE);
  // The clip grid's first slot: columns after the 30-column outline panel.
  auto has_stop = [&] {
    std::string slot;
    for (int c = 30; c < 48; c++) slot += f.terminal.frame().at(2, c).text;
    return slot.find("\xE2\x8F\xB9") != std::string::npos;
  };
  CHECK(has_stop());
  f.press(cellui::Key::Delete);
  CHECK(!has_stop());
  f.press(cellui::Key::Delete);
  CHECK(!has_stop());
  f.execute("toggle-stop-button");
  CHECK(has_stop());
}

// set-song-scale lists the scales, choosing Otonal says so on the status line,
// and saving writes it to the song file.
TEST(terminal_ui_set_song_scale_lists_scales_and_saves_the_choice) {
  namespace fs = std::filesystem;
  auto song = fs::path(TESTS_SCRATCH_DIR) / "terminal_ui_scale_scratch.xml";
  fs::copy_file(fixture("center_note.xml"), song, fs::copy_options::overwrite_existing);
  auto read = [&] {
    std::ifstream in(song);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  };
  CHECK(read().find("scale=\"") == std::string::npos);

  UIFixture f(song.string(), UI::View::ARRANGEMENT);
  f.execute("set-song-scale");
  for (const char * name : { "None (chromatic)", "Major", "Minor", "Otonal", "Utonal" }) {
    CHECK(f.text().find(name) != std::string::npos);
  }
  // The list starts on the scale in use, None.
  for (int i = 0; i < 3; i++) f.press(cellui::Key::Down);
  f.press(cellui::Key::Enter);
  CHECK(f.text().find("Scale Otonal") != std::string::npos);

  f.ctrl('x');
  f.ctrl('s');
  CHECK(read().find("scale=\"otonal\"") != std::string::npos);
  fs::remove(song);
}
