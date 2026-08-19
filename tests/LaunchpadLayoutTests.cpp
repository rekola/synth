#include "TestFramework.h"

#include "../src/launchpad/LaunchpadLayout.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

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
  // Both level-2 landmarks use hues close to one shared center, but
  // (confirmed against real hardware) distinct rather than identical.
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
  // Confirmed against a worked example (in 31-EDO: major-family
  // {D#,E,A,A#}, minor-family {Ebb,Eb,Ab,Bbb}) - see the plan this was
  // implemented from. Position+depth is the
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
  // supported EDO. Only depths 3-4 get their own distinguishable hue
  // shade (LaunchpadManager's consonanceColor() shares one saturation
  // across depth 4+ rather than continuing to differentiate by shade -
  // every depth still keeps a real family hue, none are flattened to
  // gray), so this is mainly a termination/coverage regression guard.
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
  // a bug to work around. Both land at depth 3, deterministically. This is
  // a distinct phenomenon from the seconds-based ENHARMONIC overlay below
  // (neither 3 nor 9 is the seconds collision, pitch class 2) - included
  // here as a regression guard that the overlay pass's TONIC/FOURTH/FIFTH
  // exclusion doesn't leak into unrelated RECURSIVE pitch classes.
  auto basis = computeBasis(12);
  auto levels = computeConsonanceLevels(basis, 12);
  CHECK(levels[3].tier == PadTier::RECURSIVE);
  CHECK(levels[3].depth == 3);
  CHECK(levels[9].tier == PadTier::RECURSIVE);
  CHECK(levels[9].depth == 3);
}

TEST(compute_consonance_levels_grades_enharmonic_blend_via_mediant_recursion) {
  // Hand-verified (Python) against the mediant-splitting recursion
  // collectMediantCollisions() implements: walk the tree down from the
  // octave until each EDO's own resolution runs out (self-terminating -
  // see its own comment), mirror every low-half finding across the
  // octave, and accumulate 1/2^(depth-2) per hit at each pitch class
  // (a pitch class is often hit more than once, most of all near unison,
  // where every branch of the tree eventually converges), then normalize
  // against the EDO's own highest total. That highest-scoring pitch class
  // is always the near-unison one (pc 1, or its octave mirror) - many
  // independent, ever-finer colliding ratio pairs outweigh a shallower
  // pitch class hit only once or twice, which is deliberate: a note under
  // pressure from many directions reads as more contested than one with a
  // single, simple ambiguity. Every other pitch class must stay at 0 -
  // that's the "no pitch class quietly inherits a stray blend" half of
  // the regression guard.
  struct Case { int edo_steps; vector<pair<int, float>> expected_blend; };
  Case cases[] = {
    {12, {{1, 1.0f}, {2, 0.32f}, {10, 0.32f}, {11, 1.0f}}},
    {19, {{1, 1.0f}, {2, 0.4f}, {3, 0.16f}, {4, 0.32f}, {15, 0.32f}, {16, 0.16f}, {17, 0.4f}, {18, 1.0f}}},
    {31, {{1, 1.0f}, {2, 0.5f}, {3, 0.25f}, {4, 1.0f / 6.0f}, {5, 1.0f / 6.0f},
          {26, 1.0f / 6.0f}, {27, 1.0f / 6.0f}, {28, 0.25f}, {29, 0.5f}, {30, 1.0f}}},
    {53, {{1, 1.0f}, {2, 0.36f}, {3, 0.32f}, {4, 0.16f}, {5, 0.16f}, {6, 0.16f}, {7, 0.16f},
          {46, 0.16f}, {47, 0.16f}, {48, 0.16f}, {49, 0.16f}, {50, 0.32f}, {51, 0.36f}, {52, 1.0f}}},
  };
  for (auto & c : cases) {
    auto basis = computeBasis(c.edo_steps);
    auto levels = computeConsonanceLevels(basis, c.edo_steps);
    map<int, float> expected(c.expected_blend.begin(), c.expected_blend.end());
    for (int pc = 0; pc < c.edo_steps; pc++) {
      auto it = expected.find(pc);
      auto expected_blend = it == expected.end() ? 0.0f : it->second;
      CHECK_NEAR(levels[static_cast<size_t>(pc)].enharmonic_blend, expected_blend, 1e-3f);
    }
  }
}

TEST(compute_consonance_levels_enharmonic_depth_marks_the_shallowest_colliding_ratio_pair) {
  // Hand-verified (Python) alongside the enharmonic_blend table above -
  // enharmonic_depth is the same per-pitch-class minimum this function
  // internally tracks before normalizing it into enharmonic_blend, so
  // every pitch class with a nonzero blend must have a nonzero depth
  // here too, and vice versa. D (12/19-EDO's pitch class 2, 31-EDO's
  // pitch class 5) always lands at depth 3 - a third's own immediate
  // split, the shallowest depth that actually occurs for every EDO this
  // engine supports except 19-EDO, where splitting the fourth itself
  // (8/7 vs 7/6) reaches depth 2, one level shallower still.
  struct Case { int edo_steps; vector<pair<int, int>> expected_depth; };
  Case cases[] = {
    {12, {{1, 3}, {2, 3}, {10, 3}, {11, 3}}},
    {19, {{1, 4}, {2, 3}, {3, 3}, {4, 2}, {15, 2}, {16, 3}, {17, 3}, {18, 4}}},
    {31, {{1, 4}, {2, 4}, {3, 3}, {4, 3}, {5, 3}, {26, 3}, {27, 3}, {28, 3}, {29, 4}, {30, 4}}},
    {53, {{1, 5}, {2, 5}, {3, 4}, {4, 4}, {5, 3}, {6, 3}, {7, 3},
          {46, 3}, {47, 3}, {48, 3}, {49, 4}, {50, 4}, {51, 5}, {52, 5}}},
  };
  for (auto & c : cases) {
    auto basis = computeBasis(c.edo_steps);
    auto levels = computeConsonanceLevels(basis, c.edo_steps);
    map<int, int> expected(c.expected_depth.begin(), c.expected_depth.end());
    for (int pc = 0; pc < c.edo_steps; pc++) {
      auto it = expected.find(pc);
      auto expected_depth = it == expected.end() ? 0 : it->second;
      CHECK(levels[static_cast<size_t>(pc)].enharmonic_depth == expected_depth);
      CHECK((levels[static_cast<size_t>(pc)].enharmonic_blend > 0.0f) == (expected_depth != 0));
    }
  }
}

TEST(compute_consonance_levels_31edo_c_double_sharp_is_enharmonic_but_less_than_the_near_unison_worst_case) {
  // The specific worked example that prompted collectMediantCollisions()'s
  // mediant recursion to go one level deeper than the whole-tone case:
  // splitting the minor third (6/5) into 12/11 * 11/10 - both land on
  // pitch class 4, the same pitch class the codebase's own depth-4 hue
  // recursion comment already calls "C-double-sharp". It's hit only once,
  // by a single shallow (depth-3) collision, so it reads as clearly
  // enharmonic but well below the near-unison pitch class (1, and its
  // mirror 30) that many independent, deeper collisions all also land on
  // - see the mediant_recursion test above for the exact expected values.
  auto basis = computeBasis(31);
  auto levels = computeConsonanceLevels(basis, 31);
  CHECK_NEAR(levels[4].enharmonic_blend, 1.0f / 6.0f, 1e-4f);
  CHECK(levels[4].enharmonic_blend < levels[1].enharmonic_blend);
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

TEST(percussion_note_for_pad_unused_pads_are_scattered_gaps_not_one_row) {
  // The 8 unused pads sit as small gaps between families (a spacer
  // column between the kit and hand-percussion halves), not confined to
  // a single row the way the earlier row-linear table's row 7 was.
  static const int expected_unused[][2] = {
    {2, 1}, {3, 1}, {7, 1}, {3, 2}, {7, 2}, {3, 4}, {7, 4}, {3, 6},
  };
  set<pair<int,int>> unused;
  for (auto & p : expected_unused) unused.insert({p[0], p[1]});
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      bool should_be_unused = unused.count({x, y}) > 0;
      CHECK((percussionNoteForPad(x, y) == -1) == should_be_unused);
    }
  }
}

TEST(percussion_note_for_pad_rejects_out_of_range_coordinates) {
  CHECK(percussionNoteForPad(-1, 0) == -1);
  CHECK(percussionNoteForPad(8, 0) == -1);
  CHECK(percussionNoteForPad(0, -1) == -1);
  CHECK(percussionNoteForPad(0, 8) == -1);
}

TEST(percussion_family_for_pad_matches_the_documented_grouping) {
  // Bass & snare (core kit): row 0 x=0..3, row 1 x=0..1 (35,36,37,38,40,39)
  for (int x = 0; x <= 3; x++) CHECK(percussionFamilyForPad(x, 0) == PercussionFamily::CORE);
  for (int x = 0; x <= 1; x++) CHECK(percussionFamilyForPad(x, 1) == PercussionFamily::CORE);
  // Hi-hats: row 2 x=0..2 (42,44,46)
  for (int x = 0; x <= 2; x++) CHECK(percussionFamilyForPad(x, 2) == PercussionFamily::HI_HAT);
  // Toms: rows 3-4, x=0..2 (41,43,45,47,48,50)
  for (int y = 3; y <= 4; y++)
    for (int x = 0; x <= 2; x++) CHECK(percussionFamilyForPad(x, y) == PercussionFamily::TOMS);
  // Cymbals: row 5 x=0..3, row 6 x=0..2 (49,57,55,52,51,59,53)
  for (int x = 0; x <= 3; x++) CHECK(percussionFamilyForPad(x, 5) == PercussionFamily::CYMBALS);
  for (int x = 0; x <= 2; x++) CHECK(percussionFamilyForPad(x, 6) == PercussionFamily::CYMBALS);
  // Kit accessories: row 7 x=0..1 (54 Tambourine, 58 Vibraslap)
  CHECK(percussionFamilyForPad(0, 7) == PercussionFamily::KIT_ACCESSORIES);
  CHECK(percussionFamilyForPad(1, 7) == PercussionFamily::KIT_ACCESSORIES);
  // Latin hand drums: row 0 x=4..7, row 1 x=4..6 (61,60,66,65,64,62,63)
  for (int x = 4; x <= 7; x++) CHECK(percussionFamilyForPad(x, 0) == PercussionFamily::LATIN_DRUMS);
  for (int x = 4; x <= 6; x++) CHECK(percussionFamilyForPad(x, 1) == PercussionFamily::LATIN_DRUMS);
  // Latin metals: row 2 x=4..6 (68,67,56 - cowbell sits with the agogos)
  for (int x = 4; x <= 6; x++) CHECK(percussionFamilyForPad(x, 2) == PercussionFamily::LATIN_METAL);
  // Shakers & scrapers: row 3 x=3..7 (82 Shaker, 69,70,73,74)
  for (int x = 3; x <= 7; x++) CHECK(percussionFamilyForPad(x, 3) == PercussionFamily::SHAKERS);
  // Woods: row 4 x=4..6 (75,76,77)
  for (int x = 4; x <= 6; x++) CHECK(percussionFamilyForPad(x, 4) == PercussionFamily::WOODS);
  // Cuica, whistles, triangle: row 5 x=4..7, row 6 x=4..5 (78,79,71,72,80,81)
  for (int x = 4; x <= 7; x++) CHECK(percussionFamilyForPad(x, 5) == PercussionFamily::CUICA_WHISTLE);
  for (int x = 4; x <= 5; x++) CHECK(percussionFamilyForPad(x, 6) == PercussionFamily::CUICA_WHISTLE);
  // Electronic kit hits + metronome: row 6 x=6..7, row 7 x=2..7 (33,34,27-32)
  CHECK(percussionFamilyForPad(6, 6) == PercussionFamily::ELECTRONIC);
  CHECK(percussionFamilyForPad(7, 6) == PercussionFamily::ELECTRONIC);
  for (int x = 2; x <= 7; x++) CHECK(percussionFamilyForPad(x, 7) == PercussionFamily::ELECTRONIC);
  // Unused gaps
  for (auto [x, y] : {pair{2,1}, pair{3,1}, pair{7,1}, pair{3,2}, pair{7,2}, pair{3,4}, pair{7,4}, pair{3,6}})
    CHECK(percussionFamilyForPad(x, y) == PercussionFamily::UNUSED);
}

TEST(percussion_family_for_pad_every_family_is_a_single_connected_region) {
  // The defect being fixed is a family split across a row boundary (not
  // reachable as one block) - confirm every family (other than UNUSED,
  // which is deliberately scattered) forms exactly one 4-connected
  // region rather than two or more disconnected islands.
  for (auto family : { PercussionFamily::CORE, PercussionFamily::HI_HAT, PercussionFamily::TOMS,
                        PercussionFamily::CYMBALS, PercussionFamily::KIT_ACCESSORIES,
                        PercussionFamily::LATIN_DRUMS, PercussionFamily::LATIN_METAL,
                        PercussionFamily::SHAKERS, PercussionFamily::WOODS,
                        PercussionFamily::CUICA_WHISTLE, PercussionFamily::ELECTRONIC }) {
    vector<pair<int,int>> cells;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
        if (percussionFamilyForPad(x, y) == family) cells.push_back({x, y});
    CHECK(!cells.empty());

    set<pair<int,int>> remaining(cells.begin(), cells.end());
    vector<pair<int,int>> stack = { cells.front() };
    remaining.erase(cells.front());
    while (!stack.empty()) {
      auto [x, y] = stack.back();
      stack.pop_back();
      for (auto [dx, dy] : {pair{1,0}, pair{-1,0}, pair{0,1}, pair{0,-1}}) {
        auto neighbor = pair{x + dx, y + dy};
        if (remaining.erase(neighbor) > 0) stack.push_back(neighbor);
      }
    }
    CHECK(remaining.empty()); // every cell was reached from the first one
  }
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
