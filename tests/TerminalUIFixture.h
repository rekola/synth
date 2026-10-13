#pragma once

#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ui/tui/TerminalUI.h"

#include <cellui/TestBackend.h>

#include <cctype>
#include <memory>
#include <sstream>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

namespace terminal_ui_test {

using cellui::term::kModAlt;
using cellui::term::kModCtrl;

// A terminal UI over a song (a fresh one by default) on a test terminal: no
// tty, no audio device. Every key is followed by one turn of the UI's loop.
struct UIFixture {
  ChannelConfiguration config{44100, 1};
  std::shared_ptr<Controller> controller = std::make_shared<Controller>(config);
  cellui::test::TestBackend terminal{40, 120};
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
  // Screen row y as text ("" past the end).
  std::string line(size_t y) const {
    auto all = lines();
    return y < all.size() ? all[y] : std::string();
  }
  // The wheel at (y, x), one notch.
  void wheel(int y, int x, bool down = true, bool shift = false) {
    terminal.pushMouse(y, x, down ? cellui::Key::Button5 : cellui::Key::Button4, shift ? cellui::term::kModShift : 0);
    ui->step();
  }
  // The background of the cell at (y, x) as "rrggbb", "" when it has none.
  std::string bg(int y, int x) const {
    const auto & cell = terminal.frame().at(y, x);
    if (!cell.bg) return "";
    char buf[8];
    snprintf(buf, sizeof buf, "%02x%02x%02x", cell.bg->getRed() & 255, cell.bg->getGreen() & 255, cell.bg->getBlue() & 255);
    return buf;
  }
};

inline std::string fixture(const char * name) { return std::string(TESTS_FIXTURES_DIR) + "/" + name; }
// A song from the repository's songs/ directory.
inline std::string repoSong(const char * name) { return std::string(TESTS_FIXTURES_DIR) + "/../../songs/" + name; }

// The pattern editor's visible row numbers, from the heading down.
inline std::vector<int> patternRows(const UIFixture & f) {
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

}  // namespace terminal_ui_test
