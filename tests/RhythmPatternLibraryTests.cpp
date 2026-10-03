#include "TestFramework.h"

#include "../src/model/RhythmPatternLibrary.h"

#include <set>

using namespace std;

// Guards the invariants OutlineView.cpp/Player.cpp both rely on without
// re-checking themselves: every hit actually lands inside its own
// pattern's length (a stale row past the end would silently never play,
// or - worse, for a Pattern built by row - assert/crash once turned into
// real Note data), every name is unique (findRhythmPattern() is a
// first-match linear scan), every entry carries the user-facing
// description OutlineView.cpp displays, and the library actually contains
// more than one row-length shape (the whole reason
// RhythmPatternTemplate::length is its own per-entry field rather than a
// fixed constant - see RhythmPatternLibrary.h's own doc comment).

TEST(every_rhythm_pattern_name_is_unique) {
  set<string> names;
  for (auto & pattern : getRhythmPatternLibrary()) {
    CHECK(names.insert(pattern.name).second);
  }
}

TEST(every_rhythm_pattern_has_a_description) {
  for (auto & pattern : getRhythmPatternLibrary()) {
    CHECK(!pattern.description.empty());
  }
}

TEST(every_rhythm_pattern_hit_lands_within_its_own_length) {
  for (auto & pattern : getRhythmPatternLibrary()) {
    CHECK(pattern.length > 0);
    CHECK(!pattern.hits.empty());
    for (auto & hit : pattern.hits) {
      CHECK(hit.row >= 0 && hit.row < pattern.length);
      CHECK(hit.velocity > 0 && hit.velocity <= 127);
      // General MIDI percussion key range (GM level 1's own defined
      // 27-87 span) - a note outside it isn't a real GM drum sound.
      CHECK(hit.note >= 27 && hit.note <= 87);
    }
  }
}

TEST(the_library_covers_more_than_one_bar_length_and_time_feel) {
  // Not every entry is a plain 16-row (4/4) bar - 3/4 (Waltz, 12 rows),
  // 5/4 (Five-Four, 20 rows), 7/8 (Seven-Eight, 14 rows), and 12/8 (Slow
  // Rock, 24 rows) all appear, plus real 2-bar (32-row) phrases (8 Beat,
  // Rumba) - exactly the variable-length case RhythmPatternTemplate::
  // length exists for.
  set<int> lengths;
  for (auto & pattern : getRhythmPatternLibrary()) lengths.insert(pattern.length);
  CHECK(lengths.count(12) == 1); // Waltz/Jazz Waltz/Six-Eight
  CHECK(lengths.count(14) == 1); // Seven-Eight
  CHECK(lengths.count(20) == 1); // Five-Four
  CHECK(lengths.count(24) == 1); // Slow Rock/Shuffle Blues
  CHECK(lengths.count(32) == 1); // 8 Beat/Rumba's own real 2-bar phrases
  CHECK(lengths.size() > 3);
}

TEST(find_rhythm_pattern_resolves_a_known_name_and_misses_an_unknown_one) {
  auto * waltz = findRhythmPattern("Waltz");
  CHECK(waltz != nullptr);
  if (waltz) CHECK(waltz->length == 12);

  CHECK(findRhythmPattern("nothing registered under this name") == nullptr);
}

TEST(rhythm_pattern_library_size_is_within_the_requested_range) {
  auto count = getRhythmPatternLibrary().size();
  CHECK(count >= 10 && count <= 50);
}

// Swing, Boogie and Jazz Waltz are the swung entries; every other rhythm is
// straight (the 12/8 ones already use real triplet rows). A swung entry
// is written on the straight grid, so it must be a whole number of pairs.
TEST(only_the_swung_rhythms_carry_swing_and_they_fit_whole_pairs) {
  set<string> swung;
  for (auto & pattern : getRhythmPatternLibrary()) {
    CHECK(pattern.swing >= swing::kStraight && pattern.swing <= swing::kMax);
    if (pattern.swing > swing::kStraight) {
      swung.insert(pattern.name);
      CHECK(pattern.length % swing::kPairRows == 0);
    }
  }
  CHECK((swung == set<string>{ "Swing", "Boogie", "Jazz Waltz" }));
}

TEST(the_swing_rhythms_ride_sits_on_the_straight_eighth_grid) {
  auto * pattern = findRhythmPattern("Swing");
  CHECK(pattern != nullptr);
  set<int> ride_rows;
  for (auto & hit : pattern->hits) if (hit.note == 51) ride_rows.insert(hit.row);
  CHECK((ride_rows == set<int>{ 0, 4, 6, 8, 12, 14 })); // beats 1-4 plus the "and" of 2 and 4
}
