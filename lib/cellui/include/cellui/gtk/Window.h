#pragma once

#include <cellui/Backend.h>

#include <memory>
#include <string>

namespace cellui::gtk {

struct WindowOptions {
  std::string title = "synth";
  // The size of the window in cells at start.
  int rows = 40, cols = 120;
  // Gives the family and size of the font that sets the cell size.
  std::string font = "Monospace 12";
};

// How long frames took to draw, for measuring.
struct FrameStats {
  size_t frames = 0;
  double draw_ms_total = 0, draw_ms_max = 0;      // the whole snapshot
  double raster_ms_total = 0, raster_ms_max = 0;  // the cells rasterized into pixels
};

// The GTK 4 backend: a GtkWindow with one custom widget that shows the
// composed frame - cells and the images over them - and turns GTK's key,
// mouse, scroll and paste events into BackendEvents. A frame is rasterized
// into one texture (CellRasterizer) when the widget draws.
//
// Single-threaded: everything but wake() belongs to the thread that opened
// the window, which runs eventLoop().
class Window : public Backend {
 public:
  // Opens a window; nullptr (with *error set) when GTK cannot open a
  // display.
  static std::unique_ptr<Window> open(const WindowOptions & options = {}, std::string * error = nullptr);
  ~Window() override;
  Window(const Window &) = delete;
  Window & operator=(const Window &) = delete;

  const DisplayInfo & display() const override;
  int rows() const override;
  int cols() const override;

  EventLoop & eventLoop() override;
  int inputFd() const override;
  int wakeFd() const override;
  std::optional<TimePoint> nextDeadline() const override { return std::nullopt; }
  std::vector<BackendEvent> poll() override;

  void present(const CellBuffer & frame, const CursorRequest & cursor = {}, const std::vector<ImagePlacement> & images = {},
               const FrameInfo * info = nullptr) override;
  void redraw() override;

  // Makes wakeFd() readable; any thread may call it.
  void wake();

  const FrameStats & stats() const;

  // The window's state, private to the implementation.
  struct Impl;

 private:
  Window();
  std::unique_ptr<Impl> impl_;
};

}  // namespace cellui::gtk
