#include "TestFramework.h"

#include "../src/ui/KeyRepeatFilter.h"

using namespace std::chrono_literals;
using Verdict = KeyRepeatFilter::Verdict;

namespace {

KeyRepeatFilter::Clock::time_point at(std::chrono::milliseconds ms) {
  return KeyRepeatFilter::Clock::time_point{} + ms;
}

} // namespace

TEST(key_repeat_held_key_is_a_press_then_one_first_repeat_then_repeats) {
  KeyRepeatFilter filter;
  CHECK(filter.observe('a', at(0ms)) == Verdict::PRESS);
  CHECK(filter.observe('a', at(500ms)) == Verdict::PRESS); // the first repeat: not yet known
  CHECK(filter.observe('a', at(530ms)) == Verdict::FIRST_REPEAT);
  CHECK(filter.observe('a', at(560ms)) == Verdict::REPEAT);
  CHECK(filter.observe('a', at(590ms)) == Verdict::REPEAT);
}

TEST(key_repeat_separate_presses_are_all_presses) {
  KeyRepeatFilter filter;
  CHECK(filter.observe('a', at(0ms)) == Verdict::PRESS);
  CHECK(filter.observe('a', at(300ms)) == Verdict::PRESS);
  CHECK(filter.observe('a', at(600ms)) == Verdict::PRESS);
  CHECK(filter.observe('a', at(900ms)) == Verdict::PRESS);
}

TEST(key_repeat_two_quick_presses_are_not_a_repeat) {
  KeyRepeatFilter filter;
  CHECK(filter.observe('a', at(0ms)) == Verdict::PRESS);
  CHECK(filter.observe('a', at(60ms)) == Verdict::PRESS); // nothing before it to make it a repeat stream
}

TEST(key_repeat_another_key_ends_the_stream) {
  KeyRepeatFilter filter;
  filter.observe('a', at(0ms));
  filter.observe('a', at(500ms));
  filter.observe('a', at(530ms));
  CHECK(filter.observe('s', at(550ms)) == Verdict::PRESS);
  CHECK(filter.observe('a', at(570ms)) == Verdict::PRESS);
}

TEST(key_repeat_pressing_again_after_a_stream_is_a_press) {
  KeyRepeatFilter filter;
  filter.observe('a', at(0ms));
  filter.observe('a', at(500ms));
  filter.observe('a', at(530ms));
  filter.observe('a', at(560ms));
  CHECK(filter.observe('a', at(900ms)) == Verdict::PRESS);
  CHECK(filter.observe('a', at(930ms)) == Verdict::PRESS); // right after a fresh press, still no stream
}

TEST(key_repeat_start_delay_outside_the_range_is_not_a_stream) {
  KeyRepeatFilter filter;
  filter.observe('a', at(0ms));
  filter.observe('a', at(100ms)); // too soon after the press to be the start delay
  CHECK(filter.observe('a', at(130ms)) == Verdict::PRESS);
}
