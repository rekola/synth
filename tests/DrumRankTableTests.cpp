#include "TestFramework.h"

#include "../src/instruments/DrumRankTable.h"

#include <set>

using namespace std;
using namespace DrumRankTable;

TEST(order_lanes_reproduces_the_appendix_b_worked_example) {
  // Bass drum, snare, three toms, both hats, a crash - given in scrambled
  // input order, since orderLanes() must not depend on input order.
  auto lanes = orderLanes({ 49, 42, 46, 50, 47, 45, 38, 36 });
  vector<int> expected = { 36, 38, 45, 47, 50, 42, 46, 49 };
  CHECK(lanes == expected);
}

TEST(order_lanes_ignores_input_order_entirely) {
  auto forward = orderLanes({ 36, 38, 45, 47, 50, 42, 46, 49 });
  auto reverse = orderLanes({ 49, 46, 42, 50, 47, 45, 38, 36 });
  CHECK(forward == reverse);
}

TEST(rank_for_note_gets_the_gm_note_number_exceptions_right) {
  // The brief calls these out explicitly as cases where plain GM note-
  // number order would be wrong.
  CHECK(rankForNote(61) < rankForNote(60)); // Low Bongo before Hi Bongo
  CHECK(rankForNote(66) < rankForNote(65)); // Low Timbale before Hi Timbale
  CHECK(rankForNote(68) < rankForNote(67)); // Low Agogo before High Agogo
}

TEST(rank_for_note_covers_all_47_standard_gm_percussion_notes) {
  for (int note = 35; note <= 81; note++) CHECK(rankForNote(note) != -1);
}

TEST(rank_for_note_also_covers_the_9_extra_sounds_the_pad_layout_supports) {
  // The drum picker reuses the full 56-sound free-drumming layout
  // (LaunchpadLayout.cpp's PERCUSSION_TABLE), not just the standard
  // 47-note range - every pickable note needs a rank or the picker could
  // add a lane with no defined lane order.
  for (int note : { 27, 28, 29, 30, 31, 32, 33, 34, 82 }) CHECK(rankForNote(note) != -1);
}

TEST(rank_for_note_is_unranked_outside_the_supported_percussion_range) {
  CHECK(rankForNote(-1) == -1);
  CHECK(rankForNote(0) == -1);
  CHECK(rankForNote(26) == -1);
  CHECK(rankForNote(83) == -1);
  CHECK(rankForNote(127) == -1);
}

TEST(rank_for_note_is_a_bijection_onto_0_through_55) {
  set<int> ranks;
  for (int note = 27; note <= 82; note++) {
    auto rank = rankForNote(note);
    CHECK(rank >= 0 && rank < 56);
    CHECK(ranks.find(rank) == ranks.end()); // no two notes share a rank
    ranks.insert(rank);
  }
  CHECK(ranks.size() == 56);
}
