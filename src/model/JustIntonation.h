#ifndef _JUSTINTONATION_H_
#define _JUSTINTONATION_H_

#include "../instruments/Tuning.h"

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

// The correction for a pitched note. `key` is the song key, a full note
// value of which only the pitch class counts. 0 for a tuning with no
// intervals (percussion).
int correctionCentsForNote(Tuning tuning, int note_value, int key);

}

#endif
