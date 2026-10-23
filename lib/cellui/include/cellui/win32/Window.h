#pragma once

#include <cellui/Backend.h>

#include <memory>
#include <string>

namespace cellui::win32 {

struct WindowOptions {
  std::string title = "synth";
  // The size of the window in cells at start.
  int rows = 40, cols = 120;
  // The font that sets the cell size, and its size in points.
  std::string font = "Consolas";
  double font_points = 11;
};

// The Windows backend: a Win32 window whose client area shows the composed
// frame - cells, the images over them and the backdrops' dimming - drawn
// with Direct2D and DirectWrite, and which turns the window's key, mouse,
// wheel and paste messages into BackendEvents. Per-monitor DPI aware: sizes
// are pixels and display().display_scale follows the monitor.
//
// Single-threaded: everything but wake() belongs to the thread that opened
// the window, which runs eventLoop().
//
// Windows has no file descriptors: inputFd() and wakeFd() are -1, and the
// loop waits on inputHandle() and wakeHandle() (events) instead.
class Window : public Backend {
 public:
  // Opens a window; nullptr (with *error set) when Windows or Direct2D
  // cannot give one.
  static std::unique_ptr<Window> open(const WindowOptions & options = {}, std::string * error = nullptr);
  ~Window() override;
  Window(const Window &) = delete;
  Window & operator=(const Window &) = delete;

  const DisplayInfo & display() const override;
  int rows() const override;
  int cols() const override;

  EventLoop & eventLoop() override;
  int inputFd() const override { return -1; }
  int wakeFd() const override { return -1; }
  std::optional<TimePoint> nextDeadline() const override { return std::nullopt; }
  std::vector<BackendEvent> poll() override;

  void present(const CellBuffer & frame, const CursorRequest & cursor = {}, const std::vector<ImagePlacement> & images = {},
               const FrameInfo * info = nullptr) override;
  void redraw() override;

  // Manual-reset events (HANDLEs), signalled while poll() has events to
  // give and after wake(). poll() resets both.
  std::vector<void *> inputHandles() const override { return { inputHandle(), wakeHandle() }; }
  void * inputHandle() const;
  void * wakeHandle() const;
  // Signals wakeHandle(); any thread may call it.
  void wake();

  // The window (an HWND), for tests and for dialogs owned by it.
  void * nativeHandle() const;

  // The window's state, private to the implementation.
  struct Impl;

 private:
  Window();
  std::unique_ptr<Impl> impl_;
};

}  // namespace cellui::win32
