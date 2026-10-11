#ifndef _SCALE_H_
#define _SCALE_H_

#include <string>
#include <string_view>
#include <vector>

// A song-wide scale, for the sequencer (Song::getScaleDegreesWindow()); the
// Launchpad's keyboard ignores it. NONE is the explicit "no scale chosen"
// state, not a fifth real scale - getScaleDegreesWindow() falls back to the
// plain chromatic scale for it.
enum class Scale {
  NONE = 0,
  MAJOR,
  MINOR,
  OTONAL,
  UTONAL,
};

static inline std::string to_string(Scale scale) {
  switch (scale) {
  case Scale::NONE: return "";
  case Scale::MAJOR: return "major";
  case Scale::MINOR: return "minor";
  case Scale::OTONAL: return "otonal";
  case Scale::UTONAL: return "utonal";
  }
  return "";
}

static inline Scale scaleFromString(const std::string & text) {
  if (text == "major") return Scale::MAJOR;
  if (text == "minor") return Scale::MINOR;
  if (text == "otonal") return Scale::OTONAL;
  if (text == "utonal") return Scale::UTONAL;
  return Scale::NONE;
}

// MAJOR/MINOR's degrees above C as note names in Note::stringToKey()'s
// syntax, resolved against the song's tuning (Song::getScaleDegreesWindow()),
// so the one list follows each tuning's own spelling: meantone major and
// minor in 12/19/31-EDO, just major and minor in 53-EDO. Empty for the scales
// given as ratios below and for Scale::NONE, which getScaleDegreesWindow()
// reads as the chromatic scale.
static inline std::vector<std::string_view> scaleDegreeNames(Scale scale) {
  switch (scale) {
  case Scale::MAJOR: return { "C", "D", "E", "F", "G", "A", "B" };
  case Scale::MINOR: return { "C", "D", "E♭", "F", "G", "A♭", "B♭" };
  case Scale::OTONAL:
  case Scale::UTONAL:
  case Scale::NONE:
    return {};
  }
  return {};
}

struct ScaleRatio {
  int num, den;
};

// OTONAL/UTONAL's degrees as frequency ratios above the tonic; a tuning plays
// the nearest step to each. They are not note names because not every
// septimal degree has one spelling that means it in every tuning (8:7 is E𝄫
// in 31-EDO but D𝄮 in 53-EDO). OTONAL is harmonics 12, 14, 15, 16, 18, 20
// and 21 of one fundamental, UTONAL its mirror image about the tonic. Partch
// coined the terms.
static inline std::vector<ScaleRatio> scaleDegreeRatios(Scale scale) {
  switch (scale) {
    case Scale::OTONAL:
      return {{1, 1}, {7, 6}, {5, 4}, {4, 3}, {3, 2}, {5, 3}, {7, 4}};
    case Scale::UTONAL:
      return {{1, 1}, {8, 7}, {6, 5}, {4, 3}, {3, 2}, {8, 5}, {12, 7}};
    case Scale::MAJOR:
    case Scale::MINOR:
    case Scale::NONE:
      return {};
  }
  return {};
}

#endif
