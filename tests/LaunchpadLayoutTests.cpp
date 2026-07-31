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

TEST(basis_major_and_minor_third_match_expected_step_counts) {
  // Confirmed for every EDO this engine supports: major_third+minor_third
  // == fifth exactly - the identity computeConsonanceLevels() relies on to
  // reduce its rescaled split back to the exact thirds for any fifth-sized
  // span (see its own doc comment).
  auto basis12 = computeBasis(12);
  CHECK(basis12.major_third == 4);
  CHECK(basis12.minor_third == 3);
  CHECK(basis12.major_third + basis12.minor_third == basis12.fifth);

  auto basis31 = computeBasis(31);
  CHECK(basis31.major_third == 10);
  CHECK(basis31.minor_third == 8);
  CHECK(basis31.major_third + basis31.minor_third == basis31.fifth);

  auto basis19 = computeBasis(19);
  CHECK(basis19.major_third + basis19.minor_third == basis19.fifth);

  auto basis53 = computeBasis(53);
  CHECK(basis53.major_third + basis53.minor_third == basis53.fifth);
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

TEST(compute_consonance_levels_tonic_fourth_fifth_for_12edo) {
  auto basis = computeBasis(12);
  auto levels = computeConsonanceLevels(basis, 12);
  CHECK(levels.size() == 12);
  CHECK(levels[0].tier == PadTier::TONIC);
  CHECK(levels[0].depth == 1);
  CHECK(levels[static_cast<size_t>(basis.fifth)].tier == PadTier::FIFTH); // pitch 7
  CHECK(levels[static_cast<size_t>(12 - basis.fifth)].tier == PadTier::FOURTH); // pitch 5
  CHECK(levels[static_cast<size_t>(basis.fifth)].depth == 2);
  CHECK(levels[static_cast<size_t>(12 - basis.fifth)].depth == 2);
}

TEST(compute_consonance_levels_fourth_and_fifth_hues_are_close_but_distinct) {
  // "They haven't diverged all that much" - both level-2 landmarks use
  // hues close to one shared center, but (per later hardware feedback)
  // distinct rather than identical.
  for (int edo_steps : {12, 19, 31, 53}) {
    auto basis = computeBasis(edo_steps);
    auto levels = computeConsonanceLevels(basis, edo_steps);
    auto fourth_hue = levels[static_cast<size_t>(edo_steps - basis.fifth)].hue;
    auto fifth_hue = levels[static_cast<size_t>(basis.fifth)].hue;
    CHECK(fourth_hue != fifth_hue);
    CHECK(fabs(fourth_hue - fifth_hue) < 20.0f);
  }
}

TEST(compute_consonance_levels_31edo_depth3_landmarks_match_worked_example) {
  // Confirmed interactively against the user's own worked example (in
  // 31-EDO: major-family {D#,E,A,A#}, minor-family {Ebb,Eb,Ab,Bbb}) - see
  // the plan this was implemented from. Position+depth is the
  // load-bearing, exactly-reproducible invariant; exact hue is a tunable
  // display constant, checked separately (not here) as a property instead.
  auto basis = computeBasis(31);
  auto levels = computeConsonanceLevels(basis, 31);
  for (int pitch : {7, 10, 23, 25, 6, 8, 21, 24}) {
    CHECK(levels[static_cast<size_t>(pitch)].tier == PadTier::RECURSIVE);
    CHECK(levels[static_cast<size_t>(pitch)].depth == 3);
  }
}

TEST(compute_consonance_levels_reaches_full_coverage_for_every_supported_edo) {
  // The key new invariant this scheme provides over the old distance-based
  // one: no catch-all/dissonant bucket is needed - every pitch class ends
  // up genuinely classified. depth==0 is otherwise impossible (TONIC is
  // depth 1, everything else deeper), so it doubles as "never assigned".
  for (int edo_steps : {12, 19, 31, 53}) {
    auto basis = computeBasis(edo_steps);
    auto levels = computeConsonanceLevels(basis, edo_steps);
    for (auto & classification : levels) {
      CHECK(classification.depth >= 1);
    }
  }
}

TEST(compute_consonance_levels_max_depth_matches_worked_table) {
  // Confirmed numerically - depth stays bounded (4-7) across every
  // supported EDO. Only depths 3-4 are ever shown as distinct hues
  // (LaunchpadManager's consonanceColor() flattens 5+ to one flat gray),
  // so this is mainly a termination/coverage regression guard.
  struct { int edo_steps, expected_max_depth; } cases[] = {
    {12, 4}, {19, 5}, {31, 6}, {53, 7},
  };
  for (auto & c : cases) {
    auto basis = computeBasis(c.edo_steps);
    auto levels = computeConsonanceLevels(basis, c.edo_steps);
    int max_depth = 0;
    for (auto & classification : levels) max_depth = max(max_depth, classification.depth);
    CHECK(max_depth == c.expected_max_depth);
  }
}

TEST(compute_consonance_levels_12edo_ties_resolve_via_fifth_priority) {
  // 12-EDO is coarse enough that pitch classes 3 and 9 are reachable via
  // both a fifth-descended and a fourth-descended span at the same depth -
  // resolved by processing order (fifth-descended spans split first), not
  // a bug to work around. Both land at depth 3, deterministically.
  auto basis = computeBasis(12);
  auto levels = computeConsonanceLevels(basis, 12);
  CHECK(levels[3].tier == PadTier::RECURSIVE);
  CHECK(levels[3].depth == 3);
  CHECK(levels[9].tier == PadTier::RECURSIVE);
  CHECK(levels[9].depth == 3);
}

TEST(classify_pad_looks_up_the_precomputed_table_by_pitch_class) {
  auto basis = computeBasis(12);
  auto levels = computeConsonanceLevels(basis, 12);
  // pad (0,0) is the base note itself - always the tonic.
  CHECK(classifyPad(levels, basis, 12, 0, 0, 60).tier == PadTier::TONIC);
  // an octave away (x*T+y*S == 12 for some (x,y): e.g. x=6,y=0 -> 6*2=12) -
  // pitch class wraps back to the tonic.
  CHECK(classifyPad(levels, basis, 12, 6, 0, 60).tier == PadTier::TONIC);
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

TEST(clamp_octave_shifts_within_bounds_and_clamps_at_the_edges) {
  CHECK(clampOctave(4, 1) == 5);
  CHECK(clampOctave(4, -1) == 3);
  CHECK(clampOctave(0, -1) == 0);  // already at the floor
  CHECK(clampOctave(9, 1) == 9);   // already at the ceiling
  CHECK(clampOctave(9, -1) == 8);
}

TEST(advance_track_index_steps_within_bounds_and_clamps_at_the_edges) {
  CHECK(advanceTrackIndex(2, 1, 5) == 3);
  CHECK(advanceTrackIndex(2, -1, 5) == 1);
  CHECK(advanceTrackIndex(4, 1, 5) == 4);  // already at the last track (index 4 of 5)
  CHECK(advanceTrackIndex(0, -1, 5) == 0); // already at the first track
  CHECK(advanceTrackIndex(0, 0, 0) == 0);  // defensive: no tracks at all
}

TEST(advance_track_index_seeds_from_the_fallback_when_unassigned) {
  // -1 means "not yet assigned" - the caller folds the fallback track in
  // before calling, so this is really just "seed from N, then step".
  CHECK(advanceTrackIndex(/*fallback=*/2, 1, 5) == 3);
  CHECK(advanceTrackIndex(/*fallback=*/2, -1, 5) == 1);
}
