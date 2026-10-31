// Prints the gallery page: every text style, colour token, sub-cell glyph,
// a Markdown document and a choice list, as a terminal shows them.
//   cellui_gallery            24-bit colour escape sequences
//   cellui_gallery --plain    the text alone
//   cellui_gallery --live     full screen through the terminal backend:
//                             arrows, PgUp/PgDn and the wheel scroll, the
//                             last line shows each event decoded; q quits.
//                             The charts are pictures where the terminal
//                             shows them.
//   cellui_gallery --gtk      the same in a GTK window (when built with it)
#include <cellui/Backend.h>
#include <cellui/BufferPlane.h>
#include <cellui/Gallery.h>
#ifdef CELLUI_HAS_GTK
#include <cellui/gtk/Window.h>
#endif
#ifndef _WIN32
#include <cellui/term/TerminalSession.h>
#include <poll.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>

namespace {

// The gallery on a page of its own, for a display: the charts are the ones
// chartsFor() picks and the page knows the cell size in pixels. A default
// DisplayInfo (no pictures) gives the text version.
struct Page {
  cellui::Theme theme = cellui::Theme::dark();
  cellui::StyleProvider styles{theme};
  cellui::CellBuffer buffer{120, 72};
  cellui::BufferPlane plane{buffer, styles};
  std::unique_ptr<cellui::Gallery> gallery;
  // The proportional text a display sets itself; cells mark the run they belong to.
  std::vector<cellui::TextRun> text_runs;

  explicit Page(const cellui::DisplayInfo & display) {
    plane.setProportionalText(display.proportional_text);
    if (display.pictures && display.cell_px_height > 0 && display.cell_px_width > 0) {
      plane.setCellPixelSize(display.cell_px_height, display.cell_px_width);
      plane.setDisplayScale(display.display_scale);
    }
    gallery = std::make_unique<cellui::Gallery>(plane, theme, display);
    plane.refresh();
    text_runs = plane.takeTextRuns();
  }
};

// Shows the page from row `top`, a status bar and a last line under it.
[[maybe_unused]] void showPage(cellui::Backend & backend, const Page & page, int top, const std::string & summary, const std::string & last) {
  int rows = backend.rows(), cols = backend.cols();
  int view = std::max(1, rows - 2);
  cellui::CellBuffer screen(rows, cols);
  for (int r = 0; r < view && top + r < page.buffer.rows(); r++) {
    for (int c = 0; c < std::min(cols, page.buffer.cols()); c++) screen.put(r, c, page.buffer.at(top + r, c));
  }
  cellui::CellBuffer::Style bar;
  bar.fg = cellui::Color(0, 0, 0);
  bar.bg = cellui::Color(0xb0, 0xb0, 0xb0);
  screen.write(rows - 2, 0, std::string(static_cast<size_t>(cols), ' '), bar);
  screen.write(rows - 2, 0, summary, bar);
  screen.write(rows - 1, 0, last, {});
  // The cells carry their picture's index, so the pictures keep their order
  // and only move up with the scroll.
  std::vector<cellui::ImagePlacement> images = page.plane.images();
  for (auto & placement : images) placement.row -= top;
  cellui::FrameInfo info;
  info.text_runs = page.text_runs;
  backend.present(screen, {}, images, &info);
}

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

int live() {
  using namespace cellui::term;
  std::string error;
  auto session = TerminalSession::open(SessionOptions(), &error);
  if (!session) {
    std::fprintf(stderr, "cellui_gallery: %s\n", error.c_str());
    return 1;
  }
  const Capabilities & caps = session->capabilities();
  Page page(caps.display);
  int page_rows = page.gallery->rows();
  std::string summary = caps.terminal + (caps.version.empty() ? "" : " " + caps.version) +
                        (caps.answered ? "" : " (no reply)") + ", colours " + colorName(caps.colors) +
                        ", images " + pixelName(caps.pixels) + (caps.kitty_keyboard ? ", kitty keys" : "") +
                        (caps.sync != SyncForm::NONE ? ", sync" : "") + ", cell " +
                        std::to_string(caps.display.cell_px_width) + "x" + std::to_string(caps.display.cell_px_height) + " px, scale " +
                        std::to_string(caps.display.display_scale).substr(0, 4);
  std::string last = "q quits";
  int top = 0;
  bool running = true;
  while (running) {
    int view = std::max(1, session->rows() - 2);
    top = std::max(0, std::min(top, page_rows - view));
    showPage(*session, page, top, summary, last);

    struct pollfd fds[2] = {{session->inputFd(), POLLIN, 0}, {session->wakeFd(), POLLIN, 0}};
    int timeout = -1;
    if (auto deadline = session->nextDeadline()) {
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - std::chrono::steady_clock::now()).count();
      timeout = static_cast<int>(std::max<long long>(0, ms + 1));
    }
    ::poll(fds, 2, timeout);
    for (const TermEvent & e : session->pollTerm()) {
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

#ifdef CELLUI_HAS_GTK
int liveWindow() {
  std::string error;
  auto window = cellui::gtk::Window::open({}, &error);
  if (!window) {
    std::fprintf(stderr, "cellui_gallery: %s\n", error.c_str());
    return 1;
  }
  Page page(window->display());
  int page_rows = page.gallery->rows();
  std::string summary = "GTK window, cell " + std::to_string(window->display().cell_px_width) + "x" +
                        std::to_string(window->display().cell_px_height) + " px, scale " +
                        std::to_string(window->display().display_scale).substr(0, 4);
  int top = 0;
  auto & loop = window->eventLoop();
  auto draw = [&](int64_t) {
    int view = std::max(1, window->rows() - 2);
    top = std::max(0, std::min(top, page_rows - view));
    showPage(*window, page, top, summary, "q quits");
  };
  window->setFrameHandler(draw);
  loop.addSource({window->inputFd(), window->wakeFd()}, [&] {
    for (const cellui::BackendEvent & e : window->poll()) {
      if (e.type == cellui::BackendEvent::Type::CLOSE) loop.quit();
      if (e.type == cellui::BackendEvent::Type::INPUT && e.input.getKind() != cellui::Input::Kind::RELEASE) {
        int key = e.input.getId();
        if (key == 'q') loop.quit();
        if (key == cellui::Key::Up) top--;
        if (key == cellui::Key::Down) top++;
        if (key == cellui::Key::PageUp) top -= std::max(1, window->rows() - 2);
        if (key == cellui::Key::PageDown) top += std::max(1, window->rows() - 2);
        if (key == cellui::Key::Button4) top -= 3;
        if (key == cellui::Key::Button5) top += 3;
      }
    }
    window->requestFrame();
  });
  window->requestFrame();
  loop.run();
  return 0;
}
#endif

}  // namespace

int main(int argc, char ** argv) {
  bool plain = false, full_screen = false, window = false;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--plain") == 0) plain = true;
#ifndef _WIN32
    else if (std::strcmp(argv[i], "--live") == 0) full_screen = true;
#endif
#ifdef CELLUI_HAS_GTK
    else if (std::strcmp(argv[i], "--gtk") == 0) window = true;
#endif
    else {
      std::fprintf(stderr, "usage: %s [--plain | --live | --gtk]\n", argv[0]);
      return 2;
    }
  }
#ifdef CELLUI_HAS_GTK
  if (window) return liveWindow();
#endif
  (void)window;
#ifndef _WIN32
  if (full_screen) return live();
#endif
  (void)full_screen;
  Page page{cellui::DisplayInfo{}};
  std::cout << (plain ? page.buffer.toPlainText() : page.buffer.toAnsi(page.gallery->rows()));
  return 0;
}
