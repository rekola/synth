#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace cellui {

// What a backend with no clock of its own does for Backend::requestFrame():
// remembers that a frame is wanted and hands it out no sooner than a minimum
// interval after the last one. Frames are coalesced: any number of requests
// before the next one make one call.
class FrameGate {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Handler = std::function<void(int64_t)>;

  explicit FrameGate(std::chrono::microseconds min_interval = {}) : min_interval_(min_interval) { }

  void request() { dirty_ = true; }
  void setHandler(Handler handler) { handler_ = std::move(handler); }
  bool dirty() const { return dirty_; }

  // When the next frame may start, if one is wanted.
  std::optional<TimePoint> deadline() const {
    if (!dirty_) return std::nullopt;
    return last_ ? *last_ + min_interval_ : TimePoint{};
  }

  // Calls the handler if a frame is wanted and due. A request the handler
  // makes (an animation going on) is for the next frame.
  void run(TimePoint now) {
    if (!dirty_ || !handler_) return;
    if (last_ && now < *last_ + min_interval_) return;
    dirty_ = false;
    last_ = now;
    handler_(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
  }

 private:
  std::chrono::microseconds min_interval_;
  Handler handler_;
  bool dirty_ = false;
  std::optional<TimePoint> last_;
};

}  // namespace cellui
