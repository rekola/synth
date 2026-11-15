#ifndef _JUSTINTONATION_H_
#define _JUSTINTONATION_H_

#include "Scale.h"
#include "Tuning.h"

#include <array>
#include <cstdint>
#include <optional>

// Just-intonation corrections for the song's EDO notes. Every scale names a
// gamut (Gamut.h, scaleGamut()); a scale with none is tempered and gets no
// corrections. A correction is how many cents a gamut note's exact ratio sits
// from its step's equal-tempered pitch.
//
// The rule is relative to the key alone: each key (step above the tonic, whole
// octaves ignored, they are pure in every EDO) gets the gamut note whose
// nearest step in the current tuning is that key. Where several notes have the
// same key, the one in the song's scale has it, else the one nearer to the key
// in cents; where none does, the key stays tempered. There is no search for
// simple ratios and no chord context, so some triads are a comma off: in C
// major the D-A fifth is 40/27. A fixed note set cannot have every fifth and
// third pure.
namespace just_intonation {

// The correction of every gamut note, by its nearest step above the key: the
// exact ratio's cents minus the step's equal-tempered cents. Empty for a scale
// with no gamut. Fixed-size, so the audio thread can hold a copy.
struct ScaleCorrections {
  static constexpr int kMaxSteps = 53;
  int edo = 0; // 0: no gamut
  int key = 0; // the key's pitch class
  std::array<double, kMaxSteps> cents{};
  std::array<bool, kMaxSteps> present{};
  // The gamut note's ratio for each step, within the step's own octave above
  // the key (a ratio whose nearest step is a whole octave counts as below it).
  std::array<Ratio, kMaxSteps> ratios{};
  // A key can carry a second ratio, when the scale holds one of each side of the
  // gamut there (the extended diamond: 49/48 and 50/49 on key 2 of 53-EDO).
  // `present` and `cents` above are the first; these are the other.
  std::array<double, kMaxSteps> second_cents{};
  std::array<bool, kMaxSteps> second_present{};
  std::array<Ratio, kMaxSteps> second_ratios{};

  bool empty() const { return edo == 0; }
  // The exact correction (cents, not yet rounded to what a note's fx can hold,
  // see Note::tuningCorrectionFx()) for an absolute note value, if its key has
  // a gamut note. Where the key carries two ratios, the one `current` (the
  // note's own correction, 0 for none) is nearer to: with none, the one nearer
  // the key's own pitch.
  std::optional<double> forNote(int note_value, double current = 0.0) const;
  // A note's just ratio to the key in the octave at or below it, `ratio`
  // times 2^`octaves`, if its key has a gamut note (the nearer of two, as above).
  struct NoteRatio {
    Ratio ratio;
    int octaves = 0;
  };
  std::optional<NoteRatio> ratioForNote(int note_value, double current = 0.0) const;
  bool operator==(const ScaleCorrections & o) const {
    return edo == o.edo && key == o.key && cents == o.cents && present == o.present && ratios == o.ratios && second_cents == o.second_cents &&
           second_present == o.second_present && second_ratios == o.second_ratios;
  }

 private:
  // Which of a step's ratios `current` is nearer to: false the first, true the second.
  bool nearerSecond(size_t step, double current) const;
};

// `key` is a full note value of which only the pitch class counts (-1 reads as C).
ScaleCorrections scaleCorrectionsFor(Tuning tuning, Scale scale, int key);
// The same for any set of gamut notes, such as a scale built for a test.
ScaleCorrections scaleCorrectionsFor(Tuning tuning, const JustScale & just, int key);
}

#endif
