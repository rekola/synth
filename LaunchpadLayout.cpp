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
  case Tuning::PERCUSSION: return 0; // no fixed pitch structure
  }
  return 0;
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

  // Nearest diatonic degree in each direction: dist_from_below is how far
  // pitch_class sits above its closest lower neighbor (a "raised" note),
  // dist_from_above is how far it sits below its closest upper neighbor
  // (a "lowered" note).
  int dist_from_below = edo_steps;
  int dist_from_above = edo_steps;
  for (auto degree : degrees) {
    auto up = ((pitch_class - degree) % edo_steps + edo_steps) % edo_steps;
    if (up == 0) return PadCategory::DIATONIC; // pitch_class IS a (non-tonic) degree
    if (up < dist_from_below) dist_from_below = up;

    auto down = ((degree - pitch_class) % edo_steps + edo_steps) % edo_steps;
    if (down < dist_from_above) dist_from_above = down;
  }

  if (dist_from_below < dist_from_above) {
    return dist_from_below == 2 ? PadCategory::SHARP : PadCategory::DIESIS;
  }
  if (dist_from_above < dist_from_below) {
    return dist_from_above == 2 ? PadCategory::FLAT : PadCategory::DIESIS;
  }
  return PadCategory::ACCIDENTAL; // equidistant tie, e.g. a 12edo black key
}

// Row 0 (bottom) = core kit, ascending through toms/cymbals/hand-perc/
// latin/electronic-FX; row 7 (top) is unused. A perfect bijection onto
// Note.h's percussion_names[] range (GM values 27-82, 56 sounds).
static const int PERCUSSION_TABLE[8][8] = {
  { 35, 36, 37, 38, 39, 40, 42, 44 },
  { 46, 41, 43, 45, 47, 48, 50, 49 },
  { 51, 52, 53, 55, 57, 59, 54, 56 },
  { 58, 69, 70, 80, 81, 82, 60, 61 },
  { 62, 63, 64, 65, 66, 67, 68, 73 },
  { 74, 75, 76, 77, 78, 79, 71, 72 },
  { 27, 28, 29, 30, 31, 32, 33, 34 },
  { -1, -1, -1, -1, -1, -1, -1, -1 },
};

int
percussionNoteForPad(int x, int y) {
  if (x < 0 || x > 7 || y < 0 || y > 7) return -1;
  return PERCUSSION_TABLE[y][x];
}

PercussionFamily
percussionFamilyForPad(int x, int y) {
  auto note = percussionNoteForPad(x, y);
  if (note < 0) return PercussionFamily::UNUSED;

  switch (note) {
  case 35: case 36: case 37: case 38: case 39: case 40:
    return PercussionFamily::CORE;
  case 42: case 44: case 46:
    return PercussionFamily::HI_HAT;
  case 41: case 43: case 45: case 47: case 48: case 50:
    return PercussionFamily::TOMS;
  case 49: case 51: case 52: case 53: case 55: case 57: case 59:
    return PercussionFamily::CYMBALS;
  case 54: case 56: case 58: case 69: case 70: case 80: case 81: case 82:
    return PercussionFamily::HAND_PERC;
  case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 67: case 68:
  case 73: case 74: case 75: case 76: case 77: case 78: case 79:
    return PercussionFamily::LATIN;
  case 71: case 72:
    return PercussionFamily::WHISTLE;
  case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34:
    return PercussionFamily::ELECTRONIC;
  default:
    return PercussionFamily::UNUSED;
  }
}

}
