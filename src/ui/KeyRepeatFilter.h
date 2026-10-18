#ifndef _KEYREPEATFILTER_H_
#define _KEYREPEATFILTER_H_

#include <chrono>

// Spots a held key's auto-repeat on a terminal that reports no key release,
// where a repeat looks like another press of the same key. The first repeat
// comes after the terminal's start-up delay, so it can't be told from a
// deliberate second press; the one after it follows within a few
// milliseconds, which no second press does. That event is a repeat, and so
// was the one before it.
class KeyRepeatFilter {
 public:
  using Clock = std::chrono::steady_clock;

  // Between repeats, always shorter than a person can press a key twice.
  static constexpr std::chrono::milliseconds kRepeatInterval{90};
  // How long after the press the first repeat can come.
  static constexpr std::chrono::milliseconds kMinStartDelay{150};
  static constexpr std::chrono::milliseconds kMaxStartDelay{1000};

  enum class Verdict {
    PRESS,        // a new press: act on it
    REPEAT,       // auto-repeat: ignore it
    FIRST_REPEAT, // ignore it, and the previous event was the first repeat: take that one back
  };

  Verdict observe(int key, Clock::time_point when) {
    if (!has_last_ || key != key_) {
      begin(key, when);
      return Verdict::PRESS;
    }
    auto gap = when - last_;
    if (streaming_) {
      if (gap <= kRepeatInterval) {
        last_ = when;
        return Verdict::REPEAT;
      }
      begin(key, when); // released and pressed again
      return Verdict::PRESS;
    }
    if (gap <= kRepeatInterval && has_prev_) {
      auto delay = last_ - prev_;
      if (delay >= kMinStartDelay && delay <= kMaxStartDelay) {
        streaming_ = true;
        last_ = when;
        return Verdict::FIRST_REPEAT;
      }
    }
    prev_ = last_;
    has_prev_ = true;
    last_ = when;
    return Verdict::PRESS;
  }

  void reset() { has_last_ = false; }

 private:
  void begin(int key, Clock::time_point when) {
    has_last_ = true;
    has_prev_ = false;
    streaming_ = false;
    key_ = key;
    last_ = when;
  }

  bool has_last_ = false, has_prev_ = false, streaming_ = false;
  int key_ = 0;
  Clock::time_point last_, prev_;
};

#endif
