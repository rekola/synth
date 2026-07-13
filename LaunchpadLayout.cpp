#include "LaunchpadLayout.h"

#include <cmath>

using namespace std;

namespace LaunchpadLayout {

int edoSteps(Tuning tuning) {
  switch (tuning) {
  case Tuning::TET12: return 12;
  case Tuning::TET19: return 19;
  case Tuning::TET31: return 31;
  case Tuning::TET53: return 53;
  default: return 0; // PERCUSSION, LIGHTING: no fixed pitch structure
  }
}

Basis
computeBasis(int edo_steps) {
  Basis basis;
  basis.fifth = static_cast<int>(lround(edo_steps * log2(3.0 / 2.0)));
  basis.whole_tone = 2 * basis.fifth - edo_steps;
  basis.semitone = 3 * edo_steps - 5 * basis.fifth;
  basis.degenerate = basis.whole_tone <= 0 || basis.semitone <= 0 || basis.whole_tone == basis.semitone;
  return basis;
}

int
noteForPad(const Basis & basis, int x, int y, int base_note) {
  if (basis.degenerate) return base_note + x + y * 8;
  return base_note + x * basis.whole_tone + y * basis.semitone;
}

vector<int>
diatonicScaleDegrees(const Basis & basis, int edo_steps) {
  if (basis.degenerate || edo_steps <= 0) return {};

  // The major/diatonic scale is the chain of fifths from one below the
  // tonic through five above it (F-C-G-D-A-E-B for 12edo, tonic=C) - NOT
  // six fifths stacked upward from the tonic, which gives Lydian instead
  // (F#-C-G-D-A-E-B has F# in place of F).
  vector<int> degrees;
  for (int k = -1; k <= 5; k++) {
    auto value = k * basis.fifth;
    degrees.push_back(((value % edo_steps) + edo_steps) % edo_steps);
  }

  return degrees;
}

PadCategory
classifyPad(const Basis & basis, int edo_steps, int x, int y, int base_note) {
  auto note = noteForPad(basis, x, y, base_note);
  auto pitch_class = ((note - base_note) % edo_steps + edo_steps) % edo_steps;
  if (pitch_class == 0) return PadCategory::TONIC;

  auto degrees = diatonicScaleDegrees(basis, edo_steps);
  for (auto degree : degrees) {
    if (degree == pitch_class) return PadCategory::IN_SCALE;
  }
  return PadCategory::CHROMATIC;
}

}
