#include "TestFramework.h"

#include "../src/ui/ChordGrouper.h"

using namespace std::chrono_literals;

namespace {

ChordGrouper::Clock::time_point at(std::chrono::milliseconds ms) {
  return ChordGrouper::Clock::time_point{} + ms;
}

} // namespace

TEST(chord_grouper_keys_within_window_share_the_first_keys_row) {
  ChordGrouper chord;
  chord.begin(at(100ms), 1, RowAddress{ 0, 8 }, 0);

  auto second = chord.join(at(112ms), 1);
  auto third = chord.join(at(130ms), 1);
  CHECK(second.has_value() && third.has_value());
  if (!second || !third) return;
  CHECK(second->row == (RowAddress{ 0, 8 }));
  CHECK(third->row == (RowAddress{ 0, 8 }));
  CHECK(second->column == 1);
  CHECK(third->column == 2);
}

TEST(chord_grouper_window_runs_from_the_first_key_not_the_last) {
  ChordGrouper chord;
  chord.begin(at(0ms), 1, RowAddress{}, 0);
  CHECK(chord.join(at(80ms), 1).has_value());
  CHECK(!chord.join(at(130ms), 1).has_value()); // 130ms after the first, though 50ms after the previous
}

TEST(chord_grouper_key_outside_window_starts_a_new_chord) {
  ChordGrouper chord;
  chord.begin(at(0ms), 1, RowAddress{ 0, 4 }, 0);
  CHECK(!chord.join(at(200ms), 1).has_value());
}

TEST(chord_grouper_other_track_does_not_join) {
  ChordGrouper chord;
  chord.begin(at(0ms), 1, RowAddress{}, 0);
  CHECK(!chord.join(at(5ms), 2).has_value());
}

TEST(chord_grouper_continues_from_the_first_keys_column) {
  ChordGrouper chord;
  chord.begin(at(0ms), 1, RowAddress{}, 2);
  auto slot = chord.join(at(5ms), 1);
  CHECK(slot.has_value());
  if (slot) CHECK(slot->column == 3);
}

TEST(chord_grouper_nothing_joins_before_a_chord_is_begun_or_after_reset) {
  ChordGrouper chord;
  CHECK(!chord.join(at(0ms), 1).has_value());
  chord.begin(at(0ms), 1, RowAddress{}, 0);
  chord.reset();
  CHECK(!chord.join(at(1ms), 1).has_value());
}
