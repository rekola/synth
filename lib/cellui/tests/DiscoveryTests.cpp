#include "TestFramework.h"

#include <cellui/term/Discovery.h>
#include <cellui/term/InputParser.h>

#include <fstream>
#include <sstream>

using namespace cellui::term;

namespace {

std::string readFixture(const std::string & name) {
  std::ifstream in(std::string(CELLUI_TEST_DIR) + "/fixtures/startup/" + name + ".bin", std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

Environment xtermEnv() {
  Environment env;
  env.term = "xterm-256color";
  return env;
}

// Feeds a recorded terminal's replies to discovery, in chunks of `chunk`
// bytes, as they would arrive.
Capabilities discover(const std::string & replies, Environment env = xtermEnv(), size_t chunk = 4096) {
  Discovery d(env);
  InputParser p;
  p.expectCursorReport();
  for (size_t i = 0; i < replies.size(); i += chunk) {
    p.feed(replies.substr(i, chunk), InputParser::TimePoint{});
    while (p.hasEvents()) d.ingest(p.pop());
  }
  return d.result();
}

}  // namespace

TEST(discovery_queries_end_with_the_sentinel) {
  Discovery d(xtermEnv());
  std::string q = d.queries();
  CHECK(q.compare(0, 4, "\x1b[6n") == 0);
  CHECK(q.size() >= 3 && q.compare(q.size() - 3, 3, "\x1b[c") == 0);
  CHECK(q.find("\x1b[=27u") != std::string::npos);
  CHECK(q.find("\x1b[16t") != std::string::npos);
  Environment console = xtermEnv();
  console.linux_console = true;
  std::string cq = Discovery(console).queries();
  CHECK(cq.find("\x1b[>0q") == std::string::npos);
  CHECK(cq.find("\x1b]11;?") == std::string::npos);
}

TEST(discovery_xterm) {
  Capabilities c = discover(readFixture("xterm"));
  CHECK(c.answered);
  CHECK(c.terminal == "xterm" && c.version == "390");
  CHECK(c.colors == ColorDepth::C256);
  CHECK(c.pixels == PixelProtocol::SIXEL);
  CHECK(c.sixel_registers == 1024 && c.sixel_max_width == 1000 && c.sixel_max_height == 1000);
  CHECK(c.sixel_setup == "\x1b[?80l\x1b[?8452h");
  CHECK(c.pixel_mouse);
  CHECK(c.sync == SyncForm::NONE);
  CHECK(c.cell_px_height == 20 && c.cell_px_width == 10);
  CHECK(c.rows == 24 && c.cols == 80);
  CHECK(!c.kitty_keyboard && !c.sextants);
  CHECK(c.default_bg && c.default_bg->getRed() == 255);
}

TEST(discovery_kitty) {
  Capabilities c = discover(readFixture("kitty"));
  CHECK(c.terminal == "kitty" && c.version == "0.31.0");
  CHECK(c.colors == ColorDepth::TRUECOLOR);
  CHECK(c.kitty_keyboard);
  CHECK(c.pixels == PixelProtocol::KITTY && c.kitty_graphics_animation);
  CHECK(c.sync == SyncForm::NONE);  // reported, but not used on kitty
  CHECK(c.sextants && c.quadrants && c.distinct_default_bg);
}

TEST(discovery_foot) {
  Capabilities c = discover(readFixture("foot"));
  CHECK(c.terminal == "foot" && c.version == "1.16.2");
  CHECK(c.colors == ColorDepth::TRUECOLOR);
  CHECK(c.kitty_keyboard);
  CHECK(c.pixels == PixelProtocol::SIXEL);
  CHECK(c.sync == SyncForm::MODE);
  CHECK(c.default_bg && c.default_bg->getRed() == 0x11);
}

TEST(discovery_wezterm) {
  Capabilities c = discover(readFixture("wezterm"));
  CHECK(c.terminal == "wezterm" && c.version == "20240203-110809-5046fc22");
  CHECK(c.colors == ColorDepth::TRUECOLOR);
  CHECK(c.sextants && c.undercurl);
  CHECK(c.pixels == PixelProtocol::KITTY && !c.kitty_graphics_animation);
  CHECK(c.sync == SyncForm::MODE);
}

TEST(discovery_vte) {
  Capabilities c = discover(readFixture("vte"));
  CHECK(c.terminal == "vte");
  CHECK(c.quadrants && c.sextants);
  CHECK(c.colors == ColorDepth::C256);
  CHECK(c.pixels == PixelProtocol::NONE);
}

TEST(discovery_tmux) {
  Capabilities c = discover(readFixture("tmux"));
  CHECK(c.terminal == "tmux" && c.version == "3.4");
  CHECK(c.colors == ColorDepth::C256);
  CHECK(c.pixels == PixelProtocol::NONE);
}

TEST(discovery_minimal_and_silent_terminals) {
  Capabilities m = discover(readFixture("minimal"));
  CHECK(m.answered);
  CHECK(m.terminal == "xterm-256color");
  CHECK(m.colors == ColorDepth::C256 && m.pixels == PixelProtocol::NONE && !m.kitty_keyboard);
  Capabilities silent = discover("");
  CHECK(!silent.answered);
  CHECK(silent.colors == ColorDepth::C256 && silent.pixels == PixelProtocol::NONE && silent.sync == SyncForm::NONE);
  CHECK(silent.half_blocks && silent.braille && !silent.quadrants && !silent.sextants);
}

TEST(discovery_replies_split_anywhere) {
  std::string replies = readFixture("foot");
  Capabilities whole = discover(replies);
  Capabilities split = discover(replies, xtermEnv(), 3);
  CHECK(split.terminal == whole.terminal && split.version == whole.version);
  CHECK(split.pixels == whole.pixels && split.sync == whole.sync && split.kitty_keyboard == whole.kitty_keyboard);
}

TEST(discovery_divergences) {
  // A mode reported as set (1) or permanently set (3) is supported too.
  CHECK(discover("\x1b[?2026;1$y\x1b[?62c").sync == SyncForm::MODE);
  CHECK(discover("\x1b[?2026;3$y\x1b[?62c").sync == SyncForm::MODE);
  CHECK(discover("\x1b[?2026;0$y\x1b[?62c").sync == SyncForm::NONE);
  // A kitty graphics error is not support.
  CHECK(discover("\x1b_Gi=31;ENOTSUPPORTED:no\x1b\\\x1b[4;480;800t\x1b[8;24;80t\x1b[?62c").pixels == PixelProtocol::NONE);
  // Sixel needs the attribute in DA1, whatever XTSMGRAPHICS said.
  CHECK(discover("\x1b[?1;0;256S\x1b[6;20;10t\x1b[?62;22c").pixels == PixelProtocol::NONE);
  // The cell size report wins over the text area divided by the size.
  Capabilities c = discover("\x1b[4;485;805t\x1b[6;20;10t\x1b[8;24;80t\x1b[?62c");
  CHECK(c.cell_px_height == 20 && c.cell_px_width == 10);
  // An unknown XTGETTCAP answer changes nothing and produces no keys.
  InputParser p;
  p.feed("\x1bP1+r7878=31\x1b\\", InputParser::TimePoint{});
  CHECK(p.hasEvents() && p.pop().type == TermEvent::Type::REPLY && !p.hasEvents());
}

TEST(discovery_environment) {
  Environment env = xtermEnv();
  env.colorterm = "truecolor";
  CHECK(discover("\x1b[?62c", env).colors == ColorDepth::TRUECOLOR);
  env.colors_override = "8";
  CHECK(discover("\x1b[?62c", env).colors == ColorDepth::C8);
  Environment vt = xtermEnv();
  vt.term = "vt100";
  CHECK(discover("", vt).colors == ColorDepth::NONE);
  CHECK(!discover("", vt).column_move);
  Environment over = xtermEnv();
  over.pixels_override = "none";
  CHECK(discover(readFixture("kitty"), over).pixels == PixelProtocol::NONE);
  Environment ssh = xtermEnv();
  ssh.ssh = true;
  CHECK(Discovery(ssh).timeout() == std::chrono::milliseconds(2000));
}
