#ifndef _SCALE_H_
#define _SCALE_H_

#include <string>
#include <string_view>
#include <vector>

#include "Ratio.h"

// A song-wide scale, for the sequencer (Song::getScaleDegreesWindow()) and,
// for a just scale, the ratio each note is tuned to; the Launchpad's keyboard
// ignores it. NONE is the explicit "no scale chosen"
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
// note-name syntax, resolved against the song's actual tuning/key there
// (Song::getScaleDegreesWindow()) rather than decoded into semitone numbers
// here, so the same degree list is correct under every supported tuning
// (12/19/31/53-EDO) instead of needing one hardcoded interval set per
// tuning. Empty for Scale::NONE: Song::getScaleDegreesWindow() reads that as
// "use the plain chromatic scale" and does not call this at all.
//
// OTONAL is 1/1 9/8 7/6 5/4 4/3 3/2 5/3 7/4 9/5 (C D D♯ E F G A A𝄰 B♭):
// harmonics 12, 14, 15, 16, 18, 20, 21 and 27 of the note a fifth below the
// tonic, plus 9/5, the harmonic mean of G and D'. UTONAL is its mirror
// image, 1/1 10/9 8/7 6/5 4/3 3/2 8/5 12/7 16/9 (C D↓ E𝄫 E♭ F G A♭ B𝄫 B𝄭).
// The terms are Partch's, though his otonalities are chords, not these
// scales. The seven-note scales are these without D and B♭.
//
// The septimal degrees are spelled to mean those ratios in both 31 and
// 53-EDO: 7:4 is A𝄰 because 53-EDO's A♯ is 26:15, and the other tunings
// ignore the arrow. The 9-note degrees carry a syntonic comma the same way:
// otonal D is 9/8 (plain D) and B♭ is 9/5 (plain B♭); utonal D is 10/9
// (D↓, natural down) and B♭ is 16/9 (B𝄭, the double-step flat).
// 53-EDO has separate steps for the two (8 and 9, 44 and 45); 31-EDO has one
// step for both (5 and 26) and ignores the mark, since each scale contains only one of each pair.
// ↓ (and ↑) on a natural mean one 53-EDO step, as the arrowed sharps and
// flats do. In 12 and 19-EDO the septimal degrees are approximations (in
// 12-EDO E𝄫 sounds identical to D, so utonal D↓ and E𝄫 fall on the same
// key).
static inline std::vector<std::string_view> scaleDegreeNames(Scale scale) {
  switch (scale) {
  case Scale::MAJOR: return { "C", "D", "E", "F", "G", "A", "B" };
  case Scale::MINOR: return { "C", "D", "E♭", "F", "G", "A♭", "B♭" };
  case Scale::OTONAL:
    return {"C", "D♭", "D", "D♯", "E", "F", "G", "A", "A𝄰", "C𝄫'", "B"};
  case Scale::UTONAL:
    return {"C", "D𝄭", "C𝄪", "E𝄫", "E♭", "F", "G", "A♭", "B𝄫", "B𝄭", "B↓"};
  case Scale::NONE: return {};
  }
  return {};
}

// Returns the exact ratio above the tonic of each of the degrees, for
// a scale that is a just scale: otonal's are in the proportion
// 12:14:15:16:18:20:21, utonal's its mirror. Empty for the others, whose notes
// have no one ratio that suits every chord.
static inline std::vector<Ratio> scaleDegreeRatios(Scale scale) {
  using namespace ratio_literals;

  switch (scale) {
    case Scale::OTONAL:
      return {1 / 1_r, 13 / 12_r, 9 / 8_r, 7 / 6_r, 5 / 4_r, 4 / 3_r, 3 / 2_r, 5 / 3_r, 7 / 4_r, 11 / 6_r, 15 / 8_r};
    case Scale::UTONAL:
      return {1 / 1_r, 16 / 15_r, 12 / 11_r, 8 / 7_r, 6 / 5_r, 4 / 3_r, 3 / 2_r, 8 / 5_r, 12 / 7_r, 16 / 9_r, 24 / 13_r};
    case Scale::NONE:
    case Scale::MAJOR:
    case Scale::MINOR:
      return {};
  }
  return {};
}

// The scale played together with this one: otonal and utonal are mirror
// images built from the same numbers, so a song in either has both.
static inline Scale pairedScale(Scale scale) {
  switch (scale) {
    case Scale::OTONAL:
      return Scale::UTONAL;
    case Scale::UTONAL:
      return Scale::OTONAL;
    case Scale::NONE:
    case Scale::MAJOR:
    case Scale::MINOR:
      return Scale::NONE;
  }
  return Scale::NONE;
}

#endif
