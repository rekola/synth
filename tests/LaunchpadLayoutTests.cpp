#include "TestFramework.h"

#include "../LaunchpadLayout.h"

#include <algorithm>

using namespace std;
using namespace LaunchpadLayout;

TEST(edo_steps_matches_each_pitched_tuning_and_is_zero_for_unpitched_ones) {
  CHECK(edoSteps(Tuning::TET12) == 12);
  CHECK(edoSteps(Tuning::TET19) == 19);
  CHECK(edoSteps(Tuning::TET31) == 31);
  CHECK(edoSteps(Tuning::TET53) == 53);
  CHECK(edoSteps(Tuning::PERCUSSION) == 0);
  CHECK(edoSteps(Tuning::LIGHTING) == 0);
}

TEST(basis_for_12edo_matches_wicki_hayden) {
  auto basis = computeBasis(12);
  CHECK(basis.fifth == 7);
  CHECK(basis.whole_tone == 2);
  CHECK(basis.semitone == 1);
  CHECK(!basis.degenerate);
}

TEST(basis_for_31edo_matches_expected_generalization) {
  auto basis = computeBasis(31);
  CHECK(basis.fifth == 18);
  CHECK(basis.whole_tone == 5);
  CHECK(basis.semitone == 3);
  CHECK(!basis.degenerate);
}

TEST(basis_for_19edo_and_53edo_are_well_formed) {
  auto basis19 = computeBasis(19);
  CHECK(basis19.fifth == 11);
  CHECK(basis19.whole_tone == 3);
  CHECK(basis19.semitone == 2);
  CHECK(!basis19.degenerate);

  auto basis53 = computeBasis(53);
  CHECK(basis53.fifth == 31);
  CHECK(basis53.whole_tone == 9);
  CHECK(basis53.semitone == 4);
  CHECK(!basis53.degenerate);
}

TEST(tiny_edo_values_trigger_the_degenerate_fallback) {
  // 5edo: T=1, S=0 - a whole step in y would never change pitch.
  CHECK(computeBasis(5).degenerate);
  // 2edo: T=0 - a whole step in x would never change pitch.
  CHECK(computeBasis(2).degenerate);
  // 1edo: S<0.
  CHECK(computeBasis(1).degenerate);
}

TEST(note_for_pad_applies_the_whole_tone_and_semitone_basis) {
  auto basis = computeBasis(12);
  CHECK(noteForPad(basis, 0, 0, 60) == 60);
  CHECK(noteForPad(basis, 1, 0, 60) == 62); // +T (whole tone)
  CHECK(noteForPad(basis, 0, 1, 60) == 61); // +S (semitone)
  CHECK(noteForPad(basis, 3, 2, 60) == 60 + 3 * 2 + 2 * 1);
}

TEST(note_for_pad_falls_back_to_chromatic_run_when_degenerate) {
  auto basis = computeBasis(5); // degenerate, per above
  CHECK(noteForPad(basis, 0, 0, 60) == 60);
  CHECK(noteForPad(basis, 1, 0, 60) == 61);
  CHECK(noteForPad(basis, 0, 1, 60) == 68); // + 8 (one grid row)
  CHECK(noteForPad(basis, 3, 1, 60) == 60 + 3 + 8);
}

TEST(diatonic_scale_degrees_for_12edo_are_the_major_scale_pitch_classes) {
  auto basis = computeBasis(12);
  auto degrees = diatonicScaleDegrees(basis, 12);
  CHECK(degrees.size() == 7);
  sort(degrees.begin(), degrees.end());
  // C major: C D E F G A B - the chain of fifths from one below the tonic
  // (F) through five above it (B), NOT six fifths stacked upward from the
  // tonic (which would give Lydian, F# instead of F).
  vector<int> expected = {0, 2, 4, 5, 7, 9, 11};
  CHECK(degrees == expected);
}

TEST(diatonic_scale_degrees_is_empty_when_degenerate) {
  auto basis = computeBasis(5);
  CHECK(diatonicScaleDegrees(basis, 5).empty());
}

TEST(classify_pad_identifies_tonic_scale_and_chromatic_pads_for_12edo) {
  auto basis = computeBasis(12);
  // pad (0,0) is the base note itself - always the tonic.
  CHECK(classifyPad(basis, 12, 0, 0, 60) == PadCategory::TONIC);
  // an octave away (0,12/S -> not a whole number of pad steps in general,
  // but x*T+y*S == 12 for some (x,y): e.g. x=6,y=0 -> 6*2=12).
  CHECK(classifyPad(basis, 12, 6, 0, 60) == PadCategory::TONIC);
  // pad (1,0): +2 semitones (whole tone) from tonic - D, in the major scale {0,2,4,5,7,9,11}.
  CHECK(classifyPad(basis, 12, 1, 0, 60) == PadCategory::IN_SCALE);
  // pad (0,1): +1 semitone from tonic - C#/Db, not in {0,2,4,5,7,9,11}.
  CHECK(classifyPad(basis, 12, 0, 1, 60) == PadCategory::CHROMATIC);
  // F (pitch class 5) must be IN_SCALE and F# (pitch class 6) must be
  // CHROMATIC - this is exactly the bug being regression-tested: stacking
  // fifths upward from the tonic used to get this backwards (F# in the
  // scale, F out of it - Lydian, not major).
  CHECK(classifyPad(basis, 12, 2, 1, 60) == PadCategory::IN_SCALE);  // x*2+y*1 = 5 -> F
  CHECK(classifyPad(basis, 12, 3, 0, 60) == PadCategory::CHROMATIC); // x*2+y*1 = 6 -> F#
}
