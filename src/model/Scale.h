#ifndef _SCALE_H_
#define _SCALE_H_

#include <string>
#include <string_view>
#include <vector>

// A song-wide scale, driving the Launchpad's in-key keyboard
// (Song::getScaleDegreesWindow()). NONE is the explicit "no scale chosen"
// state, not a fifth real scale - getScaleDegreesWindow() falls back to the
// plain chromatic scale for it (or major, for the keyboard).
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

// Each scale's own degrees, relative to C, in Note::stringToKey()'s own
// note-name syntax - resolved against the song's actual tuning/key there
// (Song::getScaleDegreesWindow()), not decoded into semitone numbers here, so
// the exact same degree list is correct under every supported tuning
// (12/19/31/53-EDO) rather than needing one hardcoded interval set per
// tuning. Empty for Scale::NONE - Song::getScaleDegreesWindow() reads that as
// "use the plain chromatic scale instead" rather than calling this at
// all. OTONAL/UTONAL are two fixed scales named after the otonal
// (overtone) and utonal (undertone) series of just intonation - UTONAL's
// own E𝄫/A♭ spelling only actually diverges from an enharmonic respelling once the
// tuning has finer-than-semitone resolution (31/53-EDO); in 12-EDO it
// still resolves correctly (Note::stringToKey()'s own double-flat
// support), just degenerately (E𝄫 there sounds identical to D).
static inline std::vector<std::string_view> scaleDegreeNames(Scale scale) {
  switch (scale) {
  case Scale::MAJOR: return { "C", "D", "E", "F", "G", "A", "B" };
  case Scale::MINOR: return { "C", "D", "E♭", "F", "G", "A♭", "B♭" };
  case Scale::OTONAL: return { "C", "D♯", "E", "F", "G", "A", "A♯" };
  case Scale::UTONAL: return { "C", "E𝄫", "E♭", "F", "G", "A♭", "B𝄫" };
  case Scale::NONE: return {};
  }
  return {};
}

#endif
