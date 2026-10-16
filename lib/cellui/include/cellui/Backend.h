#pragma once

#include <cellui/BackendEvent.h>
#include <cellui/CellBuffer.h>
#include <cellui/DisplayInfo.h>
#include <cellui/EventLoop.h>
#include <cellui/Image.h>
#include <cellui/PlaneInfo.h>

#include <chrono>
#include <optional>
#include <vector>

namespace cellui {

// What an application needs of the thing it is shown on: the display and its
// size, the events that come from it, and somewhere to put a frame.
// term::TerminalSession is the terminal's, test::TestBackend keeps the
// frames and takes its input from the test, so an application can be
// exercised without a tty.
class Backend {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;

  virtual ~Backend() = default;

  virtual const DisplayInfo & display() const = 0;
  virtual int rows() const = 0;
  virtual int cols() const = 0;

  // The loop the application runs on: it waits for either of these to be
  // readable, or for nextDeadline(), then calls poll().
  virtual EventLoop & eventLoop() = 0;
  virtual int inputFd() const = 0;
  virtual int wakeFd() const = 0;
  virtual std::optional<TimePoint> nextDeadline() const = 0;
  // Every event available now, without blocking.
  virtual std::vector<BackendEvent> poll() = 0;

  // Shows a frame. `info` has the backdrops' layers, which the backend
  // applies; its planes and focus are given only when wantsFrameInfo() says
  // they are used.
  virtual void present(const CellBuffer & frame, const CursorRequest & cursor = {},
                       const std::vector<ImagePlacement> & images = {}, const FrameInfo * info = nullptr) = 0;
  virtual bool wantsFrameInfo() const { return false; }
  // The next frame is drawn in full.
  virtual void redraw() = 0;
  // Whether the user selects text with the mouse instead of the application
  // getting the clicks. Default no-op.
  virtual void setNativeSelection(bool) { }
};

}  // namespace cellui
