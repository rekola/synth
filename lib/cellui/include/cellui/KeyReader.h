#pragma once

#include <cellui/EscapeCoalescer.h>
#include <cellui/Input.h>

#include <chrono>
#include <deque>
#include <functional>
#include <optional>

namespace cellui {

// What widgets see of the keyboard and mouse: the terminal's inputs, with
// Escape as Meta (EscapeCoalescer.h). A bare Escape press is held until the
// next input shows whether it began an Alt chord: if it did, only the chord
// is delivered; if not, the Escape goes first and then that input. Releases
// (Escape's own included) and mouse events don't decide it.
//
// Something on screen may want a bare Escape at once rather than when the
// next key arrives, to close a popup: takeEscape() is asked on every bare
// Escape press, and when it takes it the Escape is never delivered again,
// though it can still become an Alt chord's prefix.
class KeyReader {
 public:
  using Clock = std::chrono::steady_clock;
  // How long a pending Escape waits before an "ESC-" indicator is worth
  // showing: long enough that an ordinary Alt chord never flashes it.
  static constexpr std::chrono::milliseconds kIndicatorDelay{300};

  KeyReader() : coalescer_(source_) { }
  KeyReader(const KeyReader &) = delete;
  KeyReader & operator=(const KeyReader &) = delete;

  // Asked on each bare Escape press; returns whether it took the Escape.
  std::function<bool(const Input &)> takeEscape;

  // Queues an input from the terminal.
  void push(const Input & input) { source_.queue.push_back(input); }
  // Text arriving as a paste is data: an Escape in it is no prefix.
  void setInPaste(bool in_paste) { coalescer_.setInPaste(in_paste); }

  // The next input for the widgets, or false when there is none yet.
  bool next(Input * input);

  // Whether a bare Escape is waiting for its next key, and since when.
  bool escapePending() const { return coalescer_.escapePending(); }
  std::optional<Clock::time_point> escapePendingSince() const { return pending_since_; }
  // When the "ESC-" indicator is due, while an Escape is pending.
  std::optional<Clock::time_point> indicatorDue() const {
    if (!pending_since_) return std::nullopt;
    return *pending_since_ + kIndicatorDelay;
  }

 private:
  struct Queue : InputEventSource {
    std::deque<Input> queue;
    bool next(Input * input) override {
      if (queue.empty()) return false;
      *input = queue.front();
      queue.pop_front();
      return true;
    }
  };

  Queue source_;
  EscapeSequenceCoalescer coalescer_;
  std::deque<Input> out_;
  std::optional<Input> held_;  // the bare Escape waiting for its next input
  bool taken_ = false;         // takeEscape() took the held Escape
  std::optional<Clock::time_point> pending_since_;
};

}  // namespace cellui
