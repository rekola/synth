#ifndef _PERFORMANCEPROFILE_H_
#define _PERFORMANCEPROFILE_H_

#include <cellui/LinkStatus.h>

#include <algorithm>
#include <chrono>

// What the app draws and how often, by the class of the link to the display.
// The link classes only measure; this is where the app's content decisions are.
struct PerformanceProfile {
  // Redraws nobody asked for (the playhead, the meters, animation) come at most this many a second.
  // A frame the user's input caused is never held back by it.
  int autonomous_fps = 60;
  // Easing, smooth scrolling and pulses off.
  bool reduced_motion = false;
};

inline PerformanceProfile profileFor(cellui::LinkClass cls) {
  using cellui::LinkClass;
  switch (cls) {
    case LinkClass::FULL:
      return {60, false};
    case LinkClass::HZ_30:
      return {30, false};
    case LinkClass::HZ_15:
      return {15, true};
    case LinkClass::HZ_5:
      return {5, true};
    case LinkClass::HZ_1:
      return {1, true};
  }
  return {};
}

// Whether the frame loop may draw now. A frame something asked for (a key, a click, an edit) is due
// at once; one only the stream of audio blocks wants waits for the profile's gap, and when nothing
// at all wants one, for the idle gap (the widgets have nothing new to find).
inline bool frameDue(std::chrono::steady_clock::duration since_last, bool asked, bool wanted, const PerformanceProfile & profile,
                     std::chrono::steady_clock::duration idle_gap) {
  if (asked) return true;
  auto gap = std::chrono::steady_clock::duration(std::chrono::seconds(1)) / std::max(1, profile.autonomous_fps);
  if (!wanted) gap = std::max(gap, idle_gap);
  return since_last >= gap;
}

#endif
