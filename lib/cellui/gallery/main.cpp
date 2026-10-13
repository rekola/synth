// Prints the gallery page: every text style, colour token, sub-cell glyph,
// a Markdown document and a choice list, as a terminal shows them.
//   cellui_gallery            24-bit colour escape sequences
//   cellui_gallery --plain    the text alone
//   cellui_gallery --live     full screen through the terminal backend:
//                             arrows, PgUp/PgDn and the wheel scroll, the
//                             last line shows each event decoded; q quits
#include <cellui/BufferPlane.h>
#include <cellui/Gallery.h>
#ifndef _WIN32
#include <cellui/term/TerminalSession.h>
#include <poll.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

#ifndef _WIN32
std::string colorName(cellui::term::ColorDepth d) {
  switch (d) {
    case cellui::term::ColorDepth::NONE: return "mono";
    case cellui::term::ColorDepth::C8: return "8";
    case cellui::term::ColorDepth::C256: return "256";
    case cellui::term::ColorDepth::TRUECOLOR: return "24bit";
  }
  return "";
}

std::string pixelName(cellui::term::PixelProtocol p) {
  switch (p) {
    case cellui::term::PixelProtocol::NONE: return "none";
    case cellui::term::PixelProtocol::SIXEL: return "sixel";
    case cellui::term::PixelProtocol::ITERM2: return "iterm2";
    case cellui::term::PixelProtocol::KITTY: return "kitty";
  }
  return "";
}

int live(const cellui::CellBuffer & page, int page_rows) {
  using namespace cellui::term;
  std::string error;
  auto session = TerminalSession::open(SessionOptions(), &error);
  if (!session) {
    std::fprintf(stderr, "cellui_gallery: %s\n", error.c_str());
    return 1;
  }
  const Capabilities & caps = session->capabilities();
  std::string summary = caps.terminal + (caps.version.empty() ? "" : " " + caps.version) +
                        (caps.answered ? "" : " (no reply)") + ", colours " + colorName(caps.colors) +
                        ", images " + pixelName(caps.pixels) + (caps.kitty_keyboard ? ", kitty keys" : "") +
                        (caps.sync != SyncForm::NONE ? ", sync" : "") + ", cell " +
                        std::to_string(caps.cell_px_width) + "x" + std::to_string(caps.cell_px_height) + " px, scale " +
                        std::to_string(caps.display_scale).substr(0, 4);
  std::string last = "q quits";
  int top = 0;
  bool running = true;
  while (running) {
    int rows = session->rows(), cols = session->cols();
    int view = std::max(1, rows - 2);
    top = std::max(0, std::min(top, page_rows - view));
    cellui::CellBuffer screen(rows, cols);
    for (int r = 0; r < view && top + r < page.rows(); r++) {
      for (int c = 0; c < std::min(cols, page.cols()); c++) screen.put(r, c, page.at(top + r, c));
    }
    cellui::CellBuffer::Style bar;
    bar.fg = cellui::Color(0, 0, 0);
    bar.bg = cellui::Color(0xb0, 0xb0, 0xb0);
    screen.write(rows - 2, 0, std::string(static_cast<size_t>(cols), ' '), bar);
    screen.write(rows - 2, 0, summary, bar);
    screen.write(rows - 1, 0, last, {});
    session->present(screen);

    struct pollfd fds[2] = {{session->inputFd(), POLLIN, 0}, {session->wakeFd(), POLLIN, 0}};
    int timeout = -1;
    if (auto deadline = session->nextDeadline()) {
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - std::chrono::steady_clock::now()).count();
      timeout = static_cast<int>(std::max<long long>(0, ms + 1));
    }
    ::poll(fds, 2, timeout);
    for (const TermEvent & e : session->poll()) {
      last = describe(e);
      if (e.type == TermEvent::Type::END) running = false;
      if (e.type == TermEvent::Type::KEY && e.kind != TermEvent::Kind::RELEASE) {
        if (e.key == 'q' && e.mods == 0) running = false;
        if (e.key == cellui::Key::Up) top--;
        if (e.key == cellui::Key::Down) top++;
        if (e.key == cellui::Key::PageUp) top -= view;
        if (e.key == cellui::Key::PageDown) top += view;
      }
      if (e.type == TermEvent::Type::MOUSE && e.key == cellui::Key::Button4) top -= 3;
      if (e.type == TermEvent::Type::MOUSE && e.key == cellui::Key::Button5) top += 3;
    }
  }
  return 0;
}
#endif

}  // namespace

int main(int argc, char ** argv) {
  bool plain = false, full_screen = false;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--plain") == 0) plain = true;
#ifndef _WIN32
    else if (std::strcmp(argv[i], "--live") == 0) full_screen = true;
#endif
    else {
      std::fprintf(stderr, "usage: %s [--plain | --live]\n", argv[0]);
      return 2;
    }
  }
  cellui::Theme theme = cellui::Theme::dark();
  cellui::StyleProvider styles(theme);
  cellui::CellBuffer buffer(120, 72);
  cellui::BufferPlane plane(buffer, styles);
  cellui::Gallery gallery(plane, theme);
  plane.refresh();
#ifndef _WIN32
  if (full_screen) return live(buffer, gallery.rows());
#endif
  std::cout << (plain ? buffer.toPlainText() : buffer.toAnsi(gallery.rows()));
  return 0;
}
