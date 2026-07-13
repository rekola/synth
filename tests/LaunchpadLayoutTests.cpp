#include "TestFramework.h"

#include "../LaunchpadLayout.h"

#include <algorithm>
#include <set>

using namespace std;
using namespace LaunchpadLayout;

TEST(edo_steps_matches_each_pitched_tuning_and_is_zero_for_unpitched_ones) {
  CHECK(edoSteps(Tuning::TET12) == 12);
  CHECK(edoSteps(Tuning::TET19) == 19);
  CHECK(edoSteps(Tuning::TET31) == 31);
  CHECK(edoSteps(Tuning::TET53) == 53);
  CHECK(edoSteps(Tuning::PERCUSSION) == 0);
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

TEST(classify_pad_identifies_tonic_and_diatonic_pads_for_12edo) {
  auto basis = computeBasis(12);
  // pad (0,0) is the base note itself - always the tonic.
  CHECK(classifyPad(basis, 12, 0, 0, 60) == PadCategory::TONIC);
  // an octave away (0,12/S -> not a whole number of pad steps in general,
  // but x*T+y*S == 12 for some (x,y): e.g. x=6,y=0 -> 6*2=12).
  CHECK(classifyPad(basis, 12, 6, 0, 60) == PadCategory::TONIC);
  // pad (1,0): +2 semitones (whole tone) from tonic - D, in the major scale {0,2,4,5,7,9,11}.
  CHECK(classifyPad(basis, 12, 1, 0, 60) == PadCategory::DIATONIC);
  // F (pitch class 5) must be DIATONIC and F# (pitch class 6) must not be -
  // this is exactly the bug being regression-tested: stacking fifths
  // upward from the tonic used to get this backwards (F# in the scale, F
  // out of it - Lydian, not major).
  CHECK(classifyPad(basis, 12, 2, 1, 60) == PadCategory::DIATONIC); // x*2+y*1 = 5 -> F
  CHECK(classifyPad(basis, 12, 3, 0, 60) != PadCategory::DIATONIC); // x*2+y*1 = 6 -> F#
}

TEST(classify_pad_12edo_black_keys_are_accidentals_not_diesis) {
  auto basis = computeBasis(12); // T=2, S=1
  // Every 12edo black key (pitch classes 1,3,6,8,10) is exactly equidistant
  // (distance 1) from its two diatonic neighbors - a genuine tie, not a
  // diesis (which requires being *closer* to one side).
  CHECK(classifyPad(basis, 12, 0, 1, 60) == PadCategory::ACCIDENTAL); // pitch 1 (C#/Db)
  CHECK(classifyPad(basis, 12, 1, 1, 60) == PadCategory::ACCIDENTAL); // pitch 3 (D#/Eb)
  CHECK(classifyPad(basis, 12, 3, 0, 60) == PadCategory::ACCIDENTAL); // pitch 6 (F#/Gb)
  CHECK(classifyPad(basis, 12, 4, 0, 60) == PadCategory::ACCIDENTAL); // pitch 8 (G#/Ab)
  CHECK(classifyPad(basis, 12, 5, 0, 60) == PadCategory::ACCIDENTAL); // pitch 10 (A#/Bb)

  // Full-scale tally: 1 tonic + 6 diatonic + 5 accidental, no sharp/flat/diesis.
  // x=0, y=p sweeps pitch classes 0..11 via y*S = y*1 = p directly (a
  // bijection, unlike x=p,y=0 which would only reach every-other pitch
  // class through x*T = p*2).
  int tonic = 0, diatonic = 0, sharp = 0, flat = 0, diesis = 0, accidental = 0;
  for (int p = 0; p < 12; p++) {
    switch (classifyPad(basis, 12, 0, p, 60)) {
    case PadCategory::TONIC: tonic++; break;
    case PadCategory::DIATONIC: diatonic++; break;
    case PadCategory::SHARP: sharp++; break;
    case PadCategory::FLAT: flat++; break;
    case PadCategory::DIESIS: diesis++; break;
    case PadCategory::ACCIDENTAL: accidental++; break;
    }
  }
  CHECK(tonic == 1);
  CHECK(diatonic == 6);
  CHECK(sharp == 0);
  CHECK(flat == 0);
  CHECK(diesis == 0);
  CHECK(accidental == 5);
}

TEST(classify_pad_31edo_matches_fokker_organ_counts) {
  auto basis = computeBasis(31); // T=5, S=3
  // x=0, y=p sweeps pitch classes 0..30 via y*S = p*3 mod 31 - a bijection
  // since gcd(3,31)=1 (just visited in a shuffled order, which doesn't
  // matter for a tally).
  int tonic = 0, diatonic = 0, sharp = 0, flat = 0, diesis = 0, accidental = 0;
  for (int p = 0; p < 31; p++) {
    switch (classifyPad(basis, 31, 0, p, 60)) {
    case PadCategory::TONIC: tonic++; break;
    case PadCategory::DIATONIC: diatonic++; break;
    case PadCategory::SHARP: sharp++; break;
    case PadCategory::FLAT: flat++; break;
    case PadCategory::DIESIS: diesis++; break;
    case PadCategory::ACCIDENTAL: accidental++; break;
    }
  }
  CHECK(tonic == 1);
  CHECK(diatonic == 6);
  CHECK(sharp == 5);
  CHECK(flat == 5);
  CHECK(diesis == 14);
  CHECK(accidental == 0);
}

TEST(classify_pad_31edo_named_examples_match_historical_practice) {
  auto basis = computeBasis(31); // fifth=18, T=5, S=3
  // Tonic = C = pitch 0; degrees {0,5,10,13,18,23,28} = C,D,E,F,G,A,B.
  // x=1,y=-1 -> x*T+y*S = 5-3 = 2 (C#): closer to C (dist 2 below) than D (dist 3 above).
  CHECK(classifyPad(basis, 31, 1, -1, 60) == PadCategory::SHARP);
  // x=0,y=1 -> 0+3 = 3 (Db): closer to D (dist 2 above) than C (dist 3 below).
  CHECK(classifyPad(basis, 31, 0, 1, 60) == PadCategory::FLAT);
  // x=-1,y=2 -> -5+6 = 1 (C-half-sharp): closer to C (dist 1) than D (dist 4).
  CHECK(classifyPad(basis, 31, -1, 2, 60) == PadCategory::DIESIS);
  // x=3,y=-1 -> 15-3 = 12 (E#/Fb): E is at 10 (dist 2 below), F is at 13
  // (dist 1 above) - closer to F, so this is a diesis, not a sharp: the
  // E-F gap is only a semitone (3 steps), so "E#" is actually closer to F
  // than to E, unlike 12edo where E-F has no note in between at all.
  CHECK(classifyPad(basis, 31, 3, -1, 60) == PadCategory::DIESIS);
}

TEST(percussion_note_for_pad_is_a_perfect_bijection_onto_gm_values_27_to_82) {
  set<int> seen;
  int unused_count = 0;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      auto note = percussionNoteForPad(x, y);
      if (note < 0) {
	unused_count++;
	continue;
      }
      CHECK(note >= 27 && note <= 82);
      CHECK(seen.find(note) == seen.end()); // no duplicates
      seen.insert(note);
    }
  }
  CHECK(unused_count == 8);
  CHECK(seen.size() == 56);
  // every value 27..82 is covered
  for (int v = 27; v <= 82; v++) CHECK(seen.find(v) != seen.end());
}

TEST(percussion_note_for_pad_row_7_is_entirely_unused) {
  for (int x = 0; x < 8; x++) CHECK(percussionNoteForPad(x, 7) == -1);
}

TEST(percussion_note_for_pad_rejects_out_of_range_coordinates) {
  CHECK(percussionNoteForPad(-1, 0) == -1);
  CHECK(percussionNoteForPad(8, 0) == -1);
  CHECK(percussionNoteForPad(0, -1) == -1);
  CHECK(percussionNoteForPad(0, 8) == -1);
}

TEST(percussion_family_for_pad_matches_the_documented_grouping) {
  // Core kit: row 0, x=0..5 (35,36,37,38,39,40)
  for (int x = 0; x <= 5; x++) CHECK(percussionFamilyForPad(x, 0) == PercussionFamily::CORE);
  // Hi-hats span two different rows (42,44 on row 0; 46 on row 1) but must
  // share the same family regardless of placement.
  CHECK(percussionFamilyForPad(6, 0) == PercussionFamily::HI_HAT); // 42 Closed Hi-hat
  CHECK(percussionFamilyForPad(7, 0) == PercussionFamily::HI_HAT); // 44 Pedal Hi-hat
  CHECK(percussionFamilyForPad(0, 1) == PercussionFamily::HI_HAT); // 46 Open Hi-hat
  // Toms: row 1, x=1..6 (41,43,45,47,48,50)
  for (int x = 1; x <= 6; x++) CHECK(percussionFamilyForPad(x, 1) == PercussionFamily::TOMS);
  // Cymbals: row 1 x=7 (49 Crash 1) and row 2 x=0..5
  CHECK(percussionFamilyForPad(7, 1) == PercussionFamily::CYMBALS);
  for (int x = 0; x <= 5; x++) CHECK(percussionFamilyForPad(x, 2) == PercussionFamily::CYMBALS);
  // Hand percussion: row 2 x=6,7 (tambourine, cowbell) + row 3 x=0..5
  CHECK(percussionFamilyForPad(6, 2) == PercussionFamily::HAND_PERC);
  CHECK(percussionFamilyForPad(7, 2) == PercussionFamily::HAND_PERC);
  for (int x = 0; x <= 5; x++) CHECK(percussionFamilyForPad(x, 3) == PercussionFamily::HAND_PERC);
  // Latin: row 3 x=6,7 (bongos) + row 4 (all) + row 5 x=0..5
  CHECK(percussionFamilyForPad(6, 3) == PercussionFamily::LATIN);
  CHECK(percussionFamilyForPad(7, 3) == PercussionFamily::LATIN);
  for (int x = 0; x < 8; x++) CHECK(percussionFamilyForPad(x, 4) == PercussionFamily::LATIN);
  for (int x = 0; x <= 5; x++) CHECK(percussionFamilyForPad(x, 5) == PercussionFamily::LATIN);
  // Whistles: row 5 x=6,7
  CHECK(percussionFamilyForPad(6, 5) == PercussionFamily::WHISTLE);
  CHECK(percussionFamilyForPad(7, 5) == PercussionFamily::WHISTLE);
  // Electronic/FX: row 6 (all)
  for (int x = 0; x < 8; x++) CHECK(percussionFamilyForPad(x, 6) == PercussionFamily::ELECTRONIC);
  // Unused: row 7 (all)
  for (int x = 0; x < 8; x++) CHECK(percussionFamilyForPad(x, 7) == PercussionFamily::UNUSED);
}
