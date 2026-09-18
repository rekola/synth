#ifndef _TUNING_H_
#define _TUNING_H_

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

#endif
