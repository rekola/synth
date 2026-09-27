#ifndef _TUNING_H_
#define _TUNING_H_

#include <cmath>
#include <string>

enum class Tuning {
  PERCUSSION = 0,
  TET12,
  TET19,
  TET31,
  TET53
};

static inline std::string to_string(Tuning tuning) {
  switch (tuning) {
  case Tuning::PERCUSSION: return "percussion";
  case Tuning::TET12: return "12edo";
  case Tuning::TET19: return "19edo";
  case Tuning::TET31: return "31edo";
  case Tuning::TET53: return "53edo";
  default: return "";
  }
}

// Steps per octave - 0 for PERCUSSION, which has no interval structure at
// all (a percussion "note" is a fixed GM identity, never a scale degree).
// The single shared definition - LaunchpadLayout::edoSteps() (the pitched
// isomorphic grid's own layout math) and Song::getScaleDegrees() (the step
// sequencer's own scale-to-pitch-class resolution) both need this and
// must never drift apart.
static inline int edoStepsFor(Tuning tuning) {
  switch (tuning) {
  case Tuning::TET12: return 12;
  case Tuning::TET19: return 19;
  case Tuning::TET31: return 31;
  case Tuning::TET53: return 53;
  case Tuning::PERCUSSION: return 0;
  }
  return 0;
}

// PERCUSSION reuses the TET12 formula (steps=12, center=69) rather than
// returning something derived from edoStepsFor(PERCUSSION) == 0 - a
// percussion note's own frequency (when one is even needed, e.g. a
// non-SoundFont percussion voice) is meaningless as a scale degree, but
// still has to resolve to *some* pitch, and 12-TET/A440 is as good a
// convention as any single fixed one.
inline float getFrequencyFor(Tuning tuning, int note_value) {
  switch (tuning) {
  case Tuning::TET12:
  case Tuning::PERCUSSION: return 440.0f * powf(2.0f, (note_value - 69) / 12.0f);
  case Tuning::TET19: return 440.0f * powf(2.0f, (note_value - 109) / 19.0f);
  case Tuning::TET31: return 440.0f * powf(2.0f, (note_value - 178) / 31.0f);
  case Tuning::TET53: return 440.0f * powf(2.0f, (note_value - 304) / 53.0f);
  }
  return 0.0f;
}

// The const Note & overload lives in Note.h, not here - Note.h already
// includes this header, so declaring it here too would make the two
// headers include each other.

#endif
