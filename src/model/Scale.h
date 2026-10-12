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

// The name shown in menus and dialogs.
static inline std::string scaleLabel(Scale scale) {
  switch (scale) {
    case Scale::NONE:
      return "None (chromatic)";
    case Scale::MAJOR:
      return "Major";
    case Scale::MINOR:
      return "Minor";
    case Scale::OTONAL:
      return "Otonal";
    case Scale::UTONAL:
      return "Utonal";
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
// all. OTONAL is 1/1 7/6 5/4 4/3 3/2 5/3 7/4, harmonics 12, 14, 15, 16, 18,
// 20 and 21 of the note a fifth below the tonic; UTONAL, 1/1 8/7 6/5 4/3 3/2
// 8/5 12/7, is its mirror image (Partch's terms; his otonalities are chords,
// not these scales). The septimal degrees are spelled to mean
// those ratios in both 31 and 53-EDO; 7:4 is A𝄰 because 53-EDO's A♯ is 26:15
// and the other tunings ignore the arrow. In 12 and 19-EDO the septimal
// degrees are approximations (in 12-EDO E𝄫 sounds identical to D).
static inline std::vector<std::string_view> scaleDegreeNames(Scale scale) {
  switch (scale) {
  case Scale::MAJOR: return { "C", "D", "E", "F", "G", "A", "B" };
  case Scale::MINOR: return { "C", "D", "E♭", "F", "G", "A♭", "B♭" };
  case Scale::OTONAL:
    return {"C", "D♯", "E", "F", "G", "A", "A𝄰"};
  case Scale::UTONAL: return { "C", "E𝄫", "E♭", "F", "G", "A♭", "B𝄫" };
  case Scale::NONE: return {};
  }
  return {};
}

#endif
