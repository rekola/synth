#include "TestFramework.h"

#include "../src/launchpad/LaunchpadTiming.h"

#include <chrono>
#include <vector>

using namespace std;

// --- StepClock (plans/drum-machine.md, Phase 7's audition clock) ---

TEST(step_clock_is_not_running_before_start_and_advance_is_a_no_op) {
  StepClock clock;
  CHECK(!clock.isRunning());
  CHECK(clock.currentStep() == 0);
  CHECK(clock.advance(10.0f, 0.25f).empty()); // never started
}

TEST(step_clock_start_resets_to_step_zero_but_does_not_fire_it) {
  StepClock clock;
  clock.start();
  CHECK(clock.isRunning());
  CHECK(clock.currentStep() == 0);
}

TEST(step_clock_advance_fires_exactly_one_step_per_row_duration_elapsed) {
  StepClock clock;
  clock.start();

  CHECK(clock.advance(0.1f, 0.25f).empty()); // less than one row's worth
  CHECK(clock.currentStep() == 0);

  auto fired = clock.advance(0.15f, 0.25f); // 0.1 + 0.15 == 0.25, exactly one row
  vector<int> expected = { 1 };
  CHECK(fired == expected);
  CHECK(clock.currentStep() == 1);
}

TEST(step_clock_advance_fires_every_step_a_slow_tick_skipped_over) {
  // The whole reason advance() loops internally instead of a single if -
  // a caller (LaunchpadManager::refresh()) that goes a while between
  // calls must still get every intervening step, not just the latest one,
  // since each one needs its own PLAY_NOTE audition event.
  StepClock clock;
  clock.start();

  auto fired = clock.advance(1.0f, 0.25f); // 4 rows' worth in one call
  vector<int> expected = { 1, 2, 3, 4 };
  CHECK(fired == expected);
  CHECK(clock.currentStep() == 4);
}

TEST(step_clock_advance_carries_leftover_phase_across_calls) {
  StepClock clock;
  clock.start();

  clock.advance(0.2f, 0.25f); // 0.2 accumulated, no step yet
  auto fired = clock.advance(0.1f, 0.25f); // 0.2 + 0.1 == 0.3 -> one step, 0.05 leftover
  vector<int> expected = { 1 };
  CHECK(fired == expected);

  auto fired2 = clock.advance(0.19f, 0.25f); // 0.05 + 0.19 == 0.24, still short
  CHECK(fired2.empty());
  auto fired3 = clock.advance(0.01f, 0.25f); // now exactly at 0.25
  vector<int> expected3 = { 2 };
  CHECK(fired3 == expected3);
}

TEST(step_clock_stop_then_start_restarts_from_step_zero_not_wherever_it_left_off) {
  StepClock clock;
  clock.start();
  clock.advance(1.0f, 0.25f);
  CHECK(clock.currentStep() == 4);

  clock.stop();
  CHECK(!clock.isRunning());
  clock.start();
  CHECK(clock.currentStep() == 0); // fresh start, not resumed at 4
}

TEST(step_clock_advance_is_a_no_op_when_row_duration_is_not_positive) {
  // Mirrors DrumMachineTrack::getHitNotesForRow()'s own "non-positive
  // loop length/tempo is degenerate, not a crash" convention - a
  // momentarily-zero tempo shouldn't advance the clock at all.
  StepClock clock;
  clock.start();
  CHECK(clock.advance(10.0f, 0.0f).empty());
  CHECK(clock.advance(10.0f, -1.0f).empty());
  CHECK(clock.currentStep() == 0);
}

// --- ConfirmTimer (Stop-Clip Clear double-press confirm) ---

namespace {
  using Clock = chrono::steady_clock;
  Clock::time_point t(int ms) { return Clock::time_point() + chrono::milliseconds(ms); }
  constexpr auto kWindow = chrono::milliseconds(1500);
}

TEST(confirm_timer_first_press_arms_but_does_not_confirm) {
  ConfirmTimer timer;
  CHECK(!timer.isArmed());
  bool confirmed = timer.press(t(0), kWindow);
  CHECK(!confirmed);
  CHECK(timer.isArmed());
}

TEST(confirm_timer_second_press_within_window_confirms_and_disarms) {
  ConfirmTimer timer;
  timer.press(t(0), kWindow);
  bool confirmed = timer.press(t(500), kWindow); // well within the 1500ms window
  CHECK(confirmed);
  CHECK(!timer.isArmed()); // confirming disarms - a third press starts fresh
}

TEST(confirm_timer_second_press_past_the_window_does_not_confirm_but_rearms) {
  ConfirmTimer timer;
  timer.press(t(0), kWindow);
  bool confirmed = timer.press(t(2000), kWindow); // 2000ms > 1500ms window - stale
  CHECK(!confirmed);
  CHECK(timer.isArmed()); // the stale press itself became a fresh arm

  // A third press, soon after this "fresh" arm, does confirm.
  bool confirmed2 = timer.press(t(2200), kWindow);
  CHECK(confirmed2);
}

TEST(confirm_timer_press_exactly_at_the_window_boundary_still_confirms) {
  ConfirmTimer timer;
  timer.press(t(0), kWindow);
  CHECK(timer.press(t(1500), kWindow)); // <= window, inclusive
}

TEST(confirm_timer_expire_if_stale_clears_an_old_arm_without_a_press) {
  ConfirmTimer timer;
  timer.press(t(0), kWindow);
  CHECK(timer.isArmed());

  timer.expireIfStale(t(1000), kWindow); // still within the window
  CHECK(timer.isArmed());

  timer.expireIfStale(t(2000), kWindow); // past the window now
  CHECK(!timer.isArmed());
}

TEST(confirm_timer_expire_if_stale_is_a_no_op_when_never_armed) {
  ConfirmTimer timer;
  timer.expireIfStale(t(999999), kWindow);
  CHECK(!timer.isArmed());
}
