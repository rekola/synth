#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace cellui {

// What a link between the application and the display sustains, best first: FULL is 60 Hz
// and up, the others the update rate that is left. A class is a measure of the link; what an
// application does at a class is its own (nothing is cut here).
enum class LinkClass { FULL, HZ_30, HZ_15, HZ_5, HZ_1 };

// "full", "30", "15", "5", "1".
const char * linkClassName(LinkClass cls);
// "Full speed", "30 Hz", ...: for a menu.
std::string linkClassLabel(LinkClass cls);
// `name` is "auto" (measure again: `pin` is left empty) or a class name; false for anything else.
bool parseLinkClass(const std::string & name, std::optional<LinkClass> & pin);

struct LinkStatus {
  LinkClass cls = LinkClass::FULL;
  // The backend's own words for its link, shown as they are: "480 kB/s, RTT 38 ms" for a
  // terminal, "60 Hz vsync, 58 fps" for a window. Empty when it has nothing to say.
  std::string description;
  // Set by the user, not measured.
  bool pinned = false;
  // Whether the backend has anything to report yet: a link is measured over seconds, so until then
  // the class is only the default and an application shows "n/a".
  bool available = false;

  bool operator==(const LinkStatus & o) const {
    return cls == o.cls && description == o.description && pinned == o.pinned && available == o.available;
  }
  bool operator!=(const LinkStatus & o) const { return !(*this == o); }
};

// The bandwidth, in bytes a second, a class needs: what its profile sends, as measured, over the
// share of the link it may use. The figures are the kitty demands of the playing UI with pictures
// deflated (about 400, 200 and 100 kB/s for full, 30 Hz and 15 Hz, and 40 kB/s of text at 5 Hz)
// over 0.7; a protocol whose pictures compress worse (Sixel) will want its own once phase 3 measures it.
struct LinkThresholds {
  double full = 570e3, hz30 = 290e3, hz15 = 145e3, hz5 = 60e3;
};

// The class a bandwidth is enough for; `margin` > 1 asks for that much more than the threshold.
LinkClass linkClassForBandwidth(const LinkThresholds & thresholds, double bytes_per_sec, double margin = 1.0);
// The class a rate of frames a second is, with the same margin.
LinkClass linkClassForFps(double fps, double margin = 1.0);
// The class a window's frame rate is: the share it achieves of what its display can show, the refresh
// rate up to 60 Hz (FULL is 60 Hz and up), so a 30 Hz monitor at 30 fps and a 144 Hz one at 60 are both
// FULL. A `refresh_hz` of 0 or less (unknown) is taken as 60. The margin applies below FULL only.
LinkClass linkClassForFrameRate(double fps, double refresh_hz, double margin = 1.0);

// The frame rate a window keeps up with, from its frame clock's ticks: each frame drawn lasts until the
// clock's next tick, drawn or not, since a frame still being drawn holds the clock back. So a window
// that draws only some of its ticks (an application capping its own rate) still measures what it could
// do, and one that draws nothing measures nothing.
class FrameRateMeter {
 public:
  // A tick at `time_us` (microseconds, the frame clock's own time); `drew` when it drew a frame.
  void tick(int64_t time_us, bool drew);
  // Frames a second over the frames that ended in the last kRecentUs, or the last two when fewer did
  // (none older than kSpanUs); nothing for fewer than two.
  std::optional<double> rate(int64_t now_us) const;
  // A frame longer than this is a clock that stopped (the window hidden), not a slow frame.
  static constexpr int64_t kLongestFrameUs = 2'000'000;
  static constexpr int64_t kRecentUs = 1'000'000;
  static constexpr int64_t kSpanUs = 3'000'000;

 private:
  struct Frame {
    int64_t end_us;
    int64_t length_us;
  };
  std::deque<Frame> frames_;
  int64_t last_us_ = 0;
  bool last_drew_ = false;
};

struct LinkClassifierConfig {
  // A reading worse than the class for this long moves it down; at once to the best the readings said.
  std::chrono::milliseconds down_after{2000};
  // A reading better than the class by `up_margin` (the margin of the reading's second class) for
  // this long moves it up one class.
  std::chrono::milliseconds up_after{15000};
  double up_margin = 1.5;
};

// Turns readings into a class that does not flap: quickly down, slowly up. A reading is the class
// it indicates, and the class it indicates with the up margin applied; the caller makes both.
class LinkClassifier {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;
  explicit LinkClassifier(LinkClassifierConfig config = {}) : config_(config) { }

  LinkClass update(TimePoint now, LinkClass indicated, LinkClass indicated_with_margin);
  // The class is `pin` whatever is read; empty to measure again.
  void pin(std::optional<LinkClass> pin) {
    pin_ = pin;
    if (pin) current_ = *pin;
    below_since_.reset();
    above_since_.reset();
  }
  // Nothing was read for a while (nothing drawn): a trend up or down so far is forgotten, so the
  // class holds and the next readings start one afresh.
  void hold() {
    below_since_.reset();
    above_since_.reset();
  }
  LinkClass current() const { return current_; }
  bool pinned() const { return pin_.has_value(); }
  const LinkClassifierConfig & config() const { return config_; }

 private:
  LinkClassifierConfig config_;
  LinkClass current_ = LinkClass::FULL;
  std::optional<LinkClass> pin_;
  std::optional<TimePoint> below_since_, above_since_;
  LinkClass below_best_ = LinkClass::HZ_1;
};

// A backend's link status: the classifier, the latest description, the pin, and the handler told
// when the class or the pin changes (not for every new description).
class LinkMonitor {
 public:
  using TimePoint = LinkClassifier::TimePoint;
  using Handler = std::function<void(const LinkStatus &)>;
  explicit LinkMonitor(LinkClassifierConfig config = {}) : classifier_(config) { }

  const LinkStatus & status() const { return status_; }
  void setHandler(Handler handler) { handler_ = std::move(handler); }
  void pin(std::optional<LinkClass> pin);
  void update(TimePoint now, LinkClass indicated, LinkClass indicated_with_margin);
  void hold() { classifier_.hold(); }
  void setDescription(std::string description) { status_.description = std::move(description); }
  // A figure to show without a class to go with it (the link is not what limits the display).
  void setAvailable(bool available);
  const LinkClassifierConfig & classifierConfig() const { return classifier_.config(); }
  // Replaces the status (a test's, or a backend that classifies elsewhere) and tells the handler.
  void set(const LinkStatus & status);

 private:
  void notifyIfChanged(const LinkStatus & before);
  LinkClassifier classifier_;
  LinkStatus status_;
  Handler handler_;
};

// A window's reading, once a second: the frame rate it keeps up with against its refresh rate, or
// nothing when too little was drawn to tell, in which case the class holds.
void updateWindowLink(LinkMonitor & link, LinkMonitor::TimePoint now, std::optional<double> fps, double refresh_hz);

}  // namespace cellui
