#ifndef _TUNING_H_
#define _TUNING_H_

#include <cmath>
#include <string>

enum class Tuning {
  PERCUSSION = 0,
  EDO12,
  EDO19,
  EDO31,
  EDO53,
  EDO24
};

static inline std::string to_string(Tuning tuning) {
  switch (tuning) {
  case Tuning::PERCUSSION: return "percussion";
  case Tuning::EDO12: return "12edo";
  case Tuning::EDO19: return "19edo";
  case Tuning::EDO31: return "31edo";
  case Tuning::EDO53: return "53edo";
  case Tuning::EDO24:
    return "24edo";
  default: return "";
  }
}

// Steps per octave - 0 for PERCUSSION, which has no interval structure at
// all (a percussion "note" is a fixed GM identity, never a scale degree).
// The single shared definition - LaunchpadLayout::edoSteps() (the pitched
// isomorphic grid's own layout math) and Song::getScaleDegreesWindow() (the step
// sequencer's own scale-to-pitch-class resolution) both need this and
// must never drift apart.
static inline int edoStepsFor(Tuning tuning) {
  switch (tuning) {
  case Tuning::EDO12: return 12;
  case Tuning::EDO19: return 19;
  case Tuning::EDO24:
    return 24;
  case Tuning::EDO31: return 31;
  case Tuning::EDO53: return 53;
  case Tuning::PERCUSSION: return 0;
  }
  return 0;
}

// PERCUSSION reuses the EDO12 formula (steps=12, center=69) rather than
// returning something derived from edoStepsFor(PERCUSSION) == 0 - a
// percussion note's own frequency (when one is even needed, e.g. a
// non-SoundFont percussion voice) is meaningless as a scale degree, but
// still has to resolve to *some* pitch, and 12-EDO/A440 is as good a
// convention as any single fixed one.
inline float getFrequencyFor(Tuning tuning, int note_value) {
  const int steps = edoStepsFor(tuning == Tuning::PERCUSSION ? Tuning::EDO12 : tuning);
  const int a4 = 5 * steps + (9 * steps + 6) / 12; // 69, 109, 138, 178, 304
  return 440.0f * powf(2.0f, float(note_value - a4) / steps);
}

// The const Note & overload lives in Note.h, not here - Note.h already
// includes this header, so declaring it here too would make the two
// headers include each other.

#endif
