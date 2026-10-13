#pragma once

#include <cellui/CellBuffer.h>
#include <cellui/Image.h>
#include <cellui/PlaneInfo.h>
#include <cellui/term/Capabilities.h>
#include <cellui/term/FrameEncoder.h>
#include <cellui/term/TermEvent.h>

#include <chrono>
#include <optional>
#include <vector>

namespace cellui::term {

// What an application needs of a terminal: its capabilities and size, the
// events that come from it, and somewhere to put a frame. TerminalSession is
// the real one; TestBackend keeps the frames and takes its input from the
// test, so an application can be exercised without a tty.
class Terminal {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;

  virtual ~Terminal() = default;

  virtual const Capabilities & capabilities() const = 0;
  virtual int rows() const = 0;
  virtual int cols() const = 0;

  // A loop waits for either to be readable, or for nextDeadline(), then
  // calls poll().
  virtual int inputFd() const = 0;
  virtual int wakeFd() const = 0;
  virtual std::optional<TimePoint> nextDeadline() const = 0;
  // Every event available now, without blocking.
  virtual std::vector<TermEvent> poll() = 0;

  // Shows a frame. `info` is given only when wantsFrameInfo() says it is
  // used.
  virtual void present(const CellBuffer & frame, const CursorRequest & cursor = {},
                       const std::vector<ImagePlacement> & images = {}, const FrameInfo * info = nullptr) = 0;
  virtual bool wantsFrameInfo() const { return false; }
  // The next frame is drawn in full.
  virtual void redraw() = 0;
};

}  // namespace cellui::term
