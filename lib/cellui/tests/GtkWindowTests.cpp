#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/gtk/GlibEventLoop.h>
#include <cellui/gtk/Window.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>
#include <poll.h>
#include <unistd.h>

using namespace cellui;

namespace {

// GTK opens one display per process, so the tests share a window; none
// without a display (the test then says it was skipped).
gtk::Window * theWindow() {
  static std::unique_ptr<gtk::Window> window;
  static bool tried = false;
  if (!tried) {
    tried = true;
    gtk::WindowOptions options;
    options.title = "cellui-test";
    options.rows = 20;
    options.cols = 60;
    std::string error;
    window = gtk::Window::open(options, &error);
    if (!window) std::printf("SKIP: no display (%s)\n", error.c_str());
  }
  return window.get();
}

// Turns of the window's loop until `done` or the time is up.
bool pumpUntil(gtk::Window & w, const std::function<bool()> & done, int ms = 3000) {
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    w.eventLoop().iterate(false);
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return done();
}

bool haveXdotool() { return std::system("command -v xdotool > /dev/null 2>&1") == 0; }

void xdotool(const std::string & args) {
  int r = std::system(("xdotool " + args + " > /dev/null 2>&1").c_str());
  (void)r;
}

}  // namespace

TEST(gtk_window_reports_its_display_and_size) {
  gtk::Window * w = theWindow();
  if (!w) return;
  CHECK(w->rows() == 20 && w->cols() == 60);
  const DisplayInfo & d = w->display();
  CHECK(d.pictures);
  CHECK(d.cell_px_height >= 8 && d.cell_px_width >= 4);
  CHECK(d.default_fg && d.default_bg);
  CHECK(d.display_scale == 1.0);
  CHECK(!w->nextDeadline());
  CHECK(w->inputFd() >= 0 && w->wakeFd() >= 0 && w->inputFd() != w->wakeFd());
}

TEST(gtk_window_draws_a_presented_frame) {
  gtk::Window * w = theWindow();
  if (!w) return;
  StyleProvider styles;
  CellBuffer screen(w->rows(), w->cols());
  BufferPlane root(screen, styles);
  root.putstr(1, 2, "hello window");
  root.refresh();
  size_t before = w->stats().frames;
  w->present(screen, { true, 1, 2 }, root.images());
  CHECK(pumpUntil(*w, [&] { return w->stats().frames > before; }));
  CHECK(w->stats().raster_ms_total > 0);
  // No more frames while nothing is presented.
  size_t drawn = w->stats().frames;
  pumpUntil(*w, [] { return false; }, 200);
  CHECK(w->stats().frames == drawn);
}

TEST(gtk_window_draws_a_frame_over_a_backdrop_layer) {
  gtk::Window * w = theWindow();
  if (!w) return;
  StyleProvider styles;
  CellBuffer screen(w->rows(), w->cols());
  BufferPlane root(screen, styles);
  root.putstr(1, 2, "below the dialog");
  auto scrim = root.createChild();
  scrim->setBackdrop(Backdrop{ 2, 0.3f });
  for (int r = 0; r < w->rows(); r++) scrim->putstr(r, 0, std::string(static_cast<size_t>(w->cols()), ' '));
  auto dialog = root.createChild();
  dialog->resize(3, 20);
  dialog->move(4, 5);
  dialog->setBgColor(40, 40, 60);
  dialog->putstr(1, 2, "dialog");
  root.refresh();
  FrameInfo info;
  info.backdrops = root.takeBackdropLayers();
  CHECK(info.backdrops.size() == 1);
  size_t before = w->stats().frames;
  w->present(screen, {}, root.images(), &info);
  CHECK(pumpUntil(*w, [&] { return w->stats().frames > before; }));
  // The layer's texture is kept: the same frame again draws without
  // rasterizing anything.
  double raster = w->stats().raster_ms_total;
  before = w->stats().frames;
  w->present(screen, {}, root.images(), &info);
  CHECK(pumpUntil(*w, [&] { return w->stats().frames > before; }));
  CHECK(w->stats().raster_ms_total == raster);
  // A frame without layers after it.
  w->present(screen, {}, root.images());
  before = w->stats().frames;
  CHECK(pumpUntil(*w, [&] { return w->stats().frames > before; }));
}

TEST(gtk_window_wake_is_its_own_fd) {
  gtk::Window * w = theWindow();
  if (!w) return;
  w->poll();
  struct pollfd p = { w->wakeFd(), POLLIN, 0 };
  CHECK(::poll(&p, 1, 0) == 0);
  w->wake();
  p = { w->wakeFd(), POLLIN, 0 };
  CHECK(::poll(&p, 1, 0) == 1);
  struct pollfd q = { w->inputFd(), POLLIN, 0 };
  CHECK(::poll(&q, 1, 0) == 0);
  w->poll();
  p = { w->wakeFd(), POLLIN, 0 };
  CHECK(::poll(&p, 1, 0) == 0);
}

TEST(gtk_loop_runs_ready_sources_and_hooks) {
  if (!theWindow()) return;
  gtk::GlibEventLoop loop;
  int fds[2];
  CHECK(::pipe(fds) == 0);
  int ran = 0;
  std::string order;
  loop.addSource({ fds[0] }, [&] {
    ran++;
    char c;
    CHECK(::read(fds[0], &c, 1) == 1);
  });
  loop.setBefore([&] { order += "b"; return 10; });
  loop.setAfter([&](bool ready) { order += ready ? "R" : "t"; });
  CHECK(!loop.iterate(true));  // the time limit ends the wait
  CHECK(::write(fds[1], "x", 1) == 1);
  CHECK(loop.iterate(true));
  CHECK(ran == 1);
  CHECK(order == "btbR");
  ::close(fds[0]);
  ::close(fds[1]);
}

TEST(gtk_loop_idle_wait_sleeps_through_the_time_limit) {
  if (!theWindow()) return;
  gtk::GlibEventLoop loop;
  int turns = 0;
  loop.setBefore([&] { return 100; });
  loop.setAfter([&](bool) { turns++; });
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) loop.iterate(true);
  // A wait that signalled its own context would return at once, every time.
  CHECK(turns < 20);
}

TEST(gtk_loop_runs_nest_and_quit_ends_the_innermost) {
  if (!theWindow()) return;
  gtk::GlibEventLoop loop;
  int outer[2], inner[2];
  CHECK(::pipe(outer) == 0 && ::pipe(inner) == 0);
  std::string log;
  int inner_depth = 0;
  loop.addSource({ outer[0] }, [&] {
    char c;
    CHECK(::read(outer[0], &c, 1) == 1);
    log += "o";
    CHECK(::write(inner[1], "x", 1) == 1);
    loop.run();  // a modal wait inside a handler
    log += "O";
    loop.quit();
  });
  loop.addSource({ inner[0] }, [&] {
    char c;
    CHECK(::read(inner[0], &c, 1) == 1);
    log += "i";
    inner_depth = loop.depth();
    loop.quit();
  });
  CHECK(::write(outer[1], "x", 1) == 1);
  loop.run();
  CHECK(log == "oiO");
  CHECK(inner_depth == 2);
  CHECK(loop.depth() == 0);
  for (int fd : { outer[0], outer[1], inner[0], inner[1] }) ::close(fd);
}

TEST(gtk_window_input_from_the_display) {
  gtk::Window * w = theWindow();
  if (!w || !haveXdotool()) return;
  pumpUntil(*w, [] { return false; }, 500);  // mapped
  w->poll();
  xdotool("search --name cellui-test windowfocus");
  pumpUntil(*w, [] { return false; }, 200);

  std::vector<BackendEvent> got;
  auto gather = [&](size_t count) {
    return pumpUntil(*w, [&] {
      for (auto & e : w->poll()) got.push_back(e);
      return got.size() >= count;
    });
  };

  // A key: press, then release.
  xdotool("key a");
  CHECK(gather(2));
  if (got.size() >= 2) {
    CHECK(got[0].type == BackendEvent::Type::INPUT && got[0].input.getId() == 'a' && got[0].input.getKind() == Input::Kind::PRESS);
    CHECK(got[1].input.getId() == 'a' && got[1].input.getKind() == Input::Kind::RELEASE);
  }

  // Ctrl and Shift come with the key, and Shift+letter is the letter with Shift.
  got.clear();
  xdotool("key ctrl+space shift+a Up");
  CHECK(gather(6));
  if (got.size() >= 6) {
    CHECK(got[0].input.getId() == ' ' && got[0].input.hasCtrl());
    CHECK(got[2].input.getId() == 'a' && got[2].input.hasShift() && got[2].input.getTrueCaseId() == 'A');
    CHECK(got[4].input.getId() == Key::Up);
  }

  // Alt comes with the key as the Alt modifier, for the M-x chord.
  got.clear();
  xdotool("key alt+x");
  CHECK(gather(2));
  if (got.size() >= 2) {
    bool x_alt = false;
    for (auto & e : got) x_alt |= e.input.getId() == 'x' && e.input.hasAlt();
    CHECK(x_alt);
  }

  // A click: the cell under the pointer, press and release.
  got.clear();
  int cw = w->display().cell_px_width, ch = w->display().cell_px_height;
  xdotool("search --name cellui-test mousemove --window %1 " + std::to_string(cw * 5 + 2) + " " + std::to_string(ch * 3 + 2) + " click 1");
  CHECK(gather(2));
  if (got.size() >= 2) {
    CHECK(got[0].input.getId() == Key::Button1 && got[0].input.getKind() == Input::Kind::PRESS);
    CHECK(got[0].input.getY() == 3 && got[0].input.getX() == 5);
    CHECK(got[1].input.getKind() == Input::Kind::RELEASE);
  }

  // The wheel is Button4 (up) and Button5 (down) at the pointer.
  got.clear();
  xdotool("click 5 click 4");
  CHECK(gather(2));
  if (got.size() >= 2) {
    CHECK(got[0].input.getId() == Key::Button5 && got[0].input.getY() == 3 && got[0].input.getX() == 5);
    CHECK(got[1].input.getId() == Key::Button4);
  }

  // A new window size is a resize in cells.
  got.clear();
  xdotool("search --name cellui-test windowsize " + std::to_string(cw * 40) + " " + std::to_string(ch * 10));
  CHECK(pumpUntil(*w, [&] {
    for (auto & e : w->poll()) got.push_back(e);
    return w->cols() == 40 && w->rows() == 10;
  }));
  bool resized = false;
  for (auto & e : got) resized |= e.type == BackendEvent::Type::RESIZE && e.cols == 40 && e.rows == 10;
  CHECK(resized);
}
