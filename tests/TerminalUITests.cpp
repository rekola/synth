#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ui/tui/TerminalUI.h"

#include <cellui/term/TestTerminal.h>

#include <memory>

namespace {

// A terminal UI over a fresh song on a test terminal: no tty, no audio device.
struct UIFixture {
  ChannelConfiguration config{44100, 1};
  std::shared_ptr<Controller> controller = std::make_shared<Controller>(config);
  cellui::term::TestTerminal terminal{40, 120};
  std::unique_ptr<TerminalUI> ui;

  UIFixture() {
    controller->switchToBuffer(controller->freshBufferName());
    ui = std::make_unique<TerminalUI>(terminal);
    ui->setTheme("dark");
    ui->initialize(controller);
  }
  void press(int key, unsigned mods = 0) {
    terminal.pushKey(key, mods);
    ui->step();
  }
};

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
