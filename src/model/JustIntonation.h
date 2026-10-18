#ifndef _JUSTINTONATION_H_
#define _JUSTINTONATION_H_

#include "Scale.h"
#include "../instruments/Tuning.h"

#include <array>
#include <cstdint>
#include <optional>

// Just-intonation corrections for the song's EDO notes. A note's interval
// from the tonic (whole octaves ignored, they are pure in every EDO) is
// matched to a small-integer ratio; the correction is how many cents that
// ratio sits from the note's equal-tempered pitch.
//
// A ratio is a candidate for a step when that step is its nearest, and it is
// within min(half a step, kMaxErrorCents) of it, so a note never turns into
// another one. The simplest candidate wins (smallest numerator times
// denominator, the Tenney height). No prime limit is chosen: what is
// reachable follows from the EDO and the error cap. Only a fixed ceiling keeps
// out ratios that mean nothing harmonically (31/30 for 31-EDO's first step).
namespace just_intonation {

// The widest a candidate may be from its step; a by-ear constant.
constexpr double kMaxErrorCents = 20.0;
// The largest prime a candidate ratio may contain; also a by-ear constant.
constexpr int kMaxPrime = 13;

struct Interval {
  int num = 1, den = 1;
  // false when no ratio lies within the error cap: the note keeps its pitch.
  bool found = true;
};

// The ratio chosen for `steps` EDO steps above the tonic (taken mod `edo`).
Interval intervalFor(int edo, int steps);

// Whole cents to add to that step's equal-tempered pitch; 0 when no ratio
// was found.
int correctionCentsFor(int edo, int steps);

// The correction for a note that sounds with a chord whose lowest note is
// `bass` (absolute note values; `value` is at or above it). The bass is
// tuned from the key as above; every other note is tuned as its interval
// above the bass, the bass's ratio times the simplest ratio for that
// interval, so a chord is pure against its own bass while the bass keeps
// the key from drifting. A note that is the bass's own pitch class gets the
// bass's correction.
int correctionCentsInChord(int edo, int value, int bass, int key);

// The correction of every note of a just scale (scaleDegreeRatios()) and of
// the scale paired with it, by its step above the key: the exact ratio's
// cents minus the step's equal-tempered cents. Which step a degree sits on
// comes from its note name, as everywhere else the scale is used. Where the
// two scales name the same step (12 and 19-EDO), the song's own scale has it.
// Empty for a scale without ratios. Fixed-size, so the audio thread can hold
// a copy.
struct ScaleCorrections {
  static constexpr int kMaxSteps = 53;
  int edo = 0; // 0: no scale ratios
  int key = 0; // the key's pitch class
  std::array<int16_t, kMaxSteps> cents{};
  std::array<bool, kMaxSteps> present{};

  bool empty() const { return edo == 0; }
  // The correction for an absolute note value, if it is a note of the scales.
  std::optional<int> forNote(int note_value) const;
  bool operator==(const ScaleCorrections & o) const {
    return edo == o.edo && key == o.key && cents == o.cents && present == o.present;
  }
};

// `key` is a full note value of which only the pitch class counts (-1 reads as C).
ScaleCorrections scaleCorrectionsFor(Tuning tuning, Scale scale, int key);

// The correction for a note sounding with a chord whose lowest note is
// `bass`, in a song with these scale corrections: a scale note gets its
// scale's correction whatever else sounds; any other note is tuned above the
// bass as correctionCentsInChord() does, against the bass's exact scale
// ratio when the bass is a scale note.
int correctionCentsInChord(int edo, int value, int bass, int key, const ScaleCorrections & scale);

// The correction for a pitched note. `key` is the song key, a full note
// value of which only the pitch class counts. 0 for a tuning with no
// intervals (percussion).
int correctionCentsForNote(Tuning tuning, int note_value, int key);

}

#endif
