#include "TestFramework.h"

#include <cellui/win32/Win32EventLoop.h>
#include <cellui/win32/Window.h>

#include <windows.h>

using namespace cellui;

TEST(win32_loop_runs_the_handler_of_a_signalled_handle) {
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  win32::Win32EventLoop loop;
  int runs = 0;
  loop.addHandles({ event }, [&] {
    runs++;
    ResetEvent(event);
  });
  CHECK(!loop.iterate(false));
  CHECK(runs == 0);
  SetEvent(event);
  CHECK(loop.iterate(false));
  CHECK(runs == 1);
  // Reset by the handler, so the next turn finds nothing.
  CHECK(!loop.iterate(false));
  CloseHandle(event);
}

TEST(win32_loop_run_ends_on_quit_and_waits_no_longer_than_before_says) {
  win32::Win32EventLoop loop;
  int turns = 0;
  loop.setBefore([] { return 10; });
  loop.setAfter([&](bool) {
    if (++turns == 3) loop.quit();
  });
  loop.run();
  CHECK(turns == 3);
}

namespace {

// Without a desktop (a service session) the window cannot open; the test
// then says it was skipped.
win32::Window * theWindow() {
  static std::unique_ptr<win32::Window> window;
  static bool tried = false;
  if (!tried) {
    tried = true;
    win32::WindowOptions options;
    options.title = "cellui-test";
    options.rows = 20;
    options.cols = 60;
    std::string error;
    window = win32::Window::open(options, &error);
    if (!window) std::printf("SKIP: no display (%s)\n", error.c_str());
  }
  return window.get();
}

}  // namespace

TEST(win32_window_reports_its_cells_and_a_cell_size) {
  win32::Window * w = theWindow();
  if (!w) return;
  CHECK(w->display().cell_px_width > 0);
  CHECK(w->display().cell_px_height > 0);
  CHECK(w->display().pictures);
  CHECK(w->display().keymap_profile == KeymapProfile::DESKTOP);
  CHECK(w->rows() > 0);
  CHECK(w->cols() > 0);
}

TEST(win32_window_turns_key_messages_into_input) {
  win32::Window * w = theWindow();
  if (!w) return;
  w->poll();
  HWND hwnd = static_cast<HWND>(w->nativeHandle());
  SendMessageW(hwnd, WM_KEYDOWN, VK_UP, 1);
  SendMessageW(hwnd, WM_KEYUP, VK_UP, 1 | (1 << 30) | (1u << 31));
  std::vector<BackendEvent> events = w->poll();
  CHECK(events.size() == 2);
  if (events.size() == 2) {
    CHECK(events[0].type == BackendEvent::Type::INPUT);
    CHECK(events[0].input.getId() == Key::Up);
    CHECK(events[0].input.getKind() == Input::Kind::PRESS);
    CHECK(events[1].input.getKind() == Input::Kind::RELEASE);
  }
}

TEST(win32_window_close_is_an_event_not_the_end_of_the_window) {
  win32::Window * w = theWindow();
  if (!w) return;
  w->poll();
  SendMessageW(static_cast<HWND>(w->nativeHandle()), WM_CLOSE, 0, 0);
  std::vector<BackendEvent> events = w->poll();
  CHECK(events.size() == 1);
  if (!events.empty()) CHECK(events[0].type == BackendEvent::Type::CLOSE);
  CHECK(IsWindow(static_cast<HWND>(w->nativeHandle())));
}

TEST(win32_window_wake_raises_its_handle) {
  win32::Window * w = theWindow();
  if (!w) return;
  w->poll();
  CHECK(WaitForSingleObject(w->wakeHandle(), 0) == WAIT_TIMEOUT);
  w->wake();
  CHECK(WaitForSingleObject(w->wakeHandle(), 0) == WAIT_OBJECT_0);
  w->poll();
  CHECK(WaitForSingleObject(w->wakeHandle(), 0) == WAIT_TIMEOUT);
}

TEST(win32_window_presents_a_frame_without_failing) {
  win32::Window * w = theWindow();
  if (!w) return;
  CellBuffer frame(w->rows(), w->cols());
  CellBuffer::Style style;
  style.fg = Color(255, 200, 0);
  style.bg = Color(0, 0, 80);
  frame.write(1, 2, "hello", style);
  w->present(frame, CursorRequest{ true, 1, 2 });
  w->eventLoop().iterate(false);
  CHECK(IsWindow(static_cast<HWND>(w->nativeHandle())));
}
