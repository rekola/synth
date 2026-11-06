#ifndef _JUSTINTONATION_H_
#define _JUSTINTONATION_H_

#include "Scale.h"
#include "../instruments/Tuning.h"

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
// third pure; adaptive tuning, which shifts the whole scale by a comma on a
// switch, is a different thing and is not done here.
namespace just_intonation {

// The correction of every gamut note, by its nearest step above the key: the
// exact ratio's cents minus the step's equal-tempered cents. Empty for a scale
// with no gamut. Fixed-size, so the audio thread can hold a copy.
struct ScaleCorrections {
  static constexpr int kMaxSteps = 53;
  int edo = 0; // 0: no gamut
  int key = 0; // the key's pitch class
  std::array<int16_t, kMaxSteps> cents{};
  std::array<bool, kMaxSteps> present{};

  bool empty() const { return edo == 0; }
  // The correction for an absolute note value, if its key has a gamut note.
  std::optional<int> forNote(int note_value) const;
  bool operator==(const ScaleCorrections & o) const {
    return edo == o.edo && key == o.key && cents == o.cents && present == o.present;
  }
};

// `key` is a full note value of which only the pitch class counts (-1 reads as C).
ScaleCorrections scaleCorrectionsFor(Tuning tuning, Scale scale, int key);
}

#endif
