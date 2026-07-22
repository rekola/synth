#ifndef _DELAYPATTERN_H_
#define _DELAYPATTERN_H_

#include <string>

// High-level direction-pattern mode for MultiTapDelay's feedback tap (see
// MultiTapDelay.h) - a Song-level parameter (Song::getDelayPattern()), so
// this tiny standalone header (no dependency on Song.h or MultiTapDelay.h)
// can be included by both without a layering cycle.
enum class DelayPattern { Static = 0, PingPong, Orbit, Recede };

static inline const std::string to_string(DelayPattern pattern) {
  switch (pattern) {
  case DelayPattern::PingPong: return "pingpong";
  case DelayPattern::Orbit: return "orbit";
  case DelayPattern::Recede: return "recede";
  default: return "static";
  }
}

static inline DelayPattern parseDelayPattern(const std::string & text) {
  if (text == "pingpong") return DelayPattern::PingPong;
  if (text == "orbit") return DelayPattern::Orbit;
  if (text == "recede") return DelayPattern::Recede;
  return DelayPattern::Static;
}

#endif
