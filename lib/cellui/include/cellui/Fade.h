#pragma once

#include <cellui/Color.h>

#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace cellui {

// Colour fades for state changes in place: a widget asks for the colour a
// thing (a clip slot, a flag) should have this frame, by a key of its own,
// and gets the target eased in from the colour it was showing when the target
// last changed. A thing seen for the first time shows its target at once, so
// nothing fades in when a view opens.
//
// The widget calls color() for each thing it draws and sweep() once after the
// frame; things not asked for since the last sweep are forgotten. active()
// says whether a fade is still running, so the loop keeps drawing frames.
class Fades {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::milliseconds kDuration{160};

  // `animate` false (reduced motion) shows the target and keeps no fade.
  Color color(uint64_t key, const Color & target, bool animate = true, Clock::time_point now = Clock::now()) {
    auto it = entries_.find(key);
    if (it == entries_.end() || !animate) {
      // Started a full duration ago: arrived, so nothing is running.
      entries_[key] = Entry{target, target, now - kDuration, true};
      return target;
    }
    Entry & e = it->second;
    e.seen = true;
    if (!same(e.to, target)) {
      e.from = shown(e, now);
      e.to = target;
      e.start = now;
    }
    return shown(e, now);
  }

  // The same for a plain number, such as how much of a tint a thing wears.
  float value(uint64_t key, float target, bool animate = true, Clock::time_point now = Clock::now()) {
    auto it = values_.find(key);
    if (it == values_.end() || !animate) {
      values_[key] = ValueEntry{target, target, now - kDuration, true};
      return target;
    }
    ValueEntry & e = it->second;
    e.seen = true;
    if (e.to != target) {
      e.from = shownValue(e, now);
      e.to = target;
      e.start = now;
    }
    return shownValue(e, now);
  }

  // Forgets the things the last frame did not ask for.
  void sweep() {
    sweepMap(entries_);
    sweepMap(values_);
  }

  bool active(Clock::time_point now = Clock::now()) const {
    for (auto & [key, e] : entries_) {
      if (now - e.start < kDuration) return true;
    }
    for (auto & [key, e] : values_) {
      if (now - e.start < kDuration) return true;
    }
    return false;
  }

  // The same easing for a plain 0..1 progress (ease in and out).
  static float ease(float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
  }

 private:
  struct Entry {
    Color from, to;
    Clock::time_point start;
    bool seen = true;
  };

  struct ValueEntry {
    float from, to;
    Clock::time_point start;
    bool seen = true;
  };

  template <typename Map>
  static void sweepMap(Map & map) {
    for (auto it = map.begin(); it != map.end();) {
      if (!it->second.seen) it = map.erase(it);
      else {
        it->second.seen = false;
        ++it;
      }
    }
  }

  static float shownValue(const ValueEntry & e, Clock::time_point now) {
    float t = std::chrono::duration<float>(now - e.start) / std::chrono::duration<float>(kDuration);
    return t >= 1.0f ? e.to : e.from + (e.to - e.from) * ease(t);
  }

  static bool same(const Color & a, const Color & b) {
    return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
  }

  static Color shown(const Entry & e, Clock::time_point now) {
    float t = std::chrono::duration<float>(now - e.start) / std::chrono::duration<float>(kDuration);
    return t >= 1.0f ? e.to : e.from.blend(ease(t), e.to);
  }

  std::unordered_map<uint64_t, Entry> entries_;
  std::unordered_map<uint64_t, ValueEntry> values_;
};

}  // namespace cellui
