#include "TestFramework.h"

#include <cellui/Fade.h>

#include <cstdlib>

using namespace cellui;
using Clock = Fades::Clock;
using std::chrono::milliseconds;

namespace {

// Within one step: the ease runs in floats.
bool is(const Color & c, int r, int g, int b) {
  return std::abs(c.getRed() - r) <= 1 && std::abs(c.getGreen() - g) <= 1 && std::abs(c.getBlue() - b) <= 1;
}

}  // namespace

TEST(fades_a_thing_seen_first_shows_its_target_and_does_not_fade) {
  Fades fades;
  auto t0 = Clock::now();
  CHECK(is(fades.color(1, Color(200, 0, 0), true, t0), 200, 0, 0));
  CHECK(!fades.active(t0));
}

TEST(fades_a_changed_target_eases_in_over_the_duration) {
  Fades fades;
  auto t0 = Clock::now();
  fades.color(1, Color(0, 0, 0), true, t0);
  // The target changes: the first frame still shows the old colour.
  CHECK(is(fades.color(1, Color(200, 100, 0), true, t0), 0, 0, 0));
  CHECK(fades.active(t0));
  // Halfway, the ease is at one half.
  auto half = fades.color(1, Color(200, 100, 0), true, t0 + Fades::kDuration / 2);
  CHECK(is(half, 100, 50, 0));
  // Past the end it is the target, and the fade is over.
  auto end = t0 + Fades::kDuration + milliseconds(1);
  CHECK(is(fades.color(1, Color(200, 100, 0), true, end), 200, 100, 0));
  CHECK(!fades.active(end));
}

TEST(fades_a_target_changed_midway_starts_from_what_is_shown) {
  Fades fades;
  auto t0 = Clock::now();
  fades.color(1, Color(0, 0, 0), true, t0);
  fades.color(1, Color(200, 0, 0), true, t0);
  auto mid = t0 + Fades::kDuration / 2;
  CHECK(is(fades.color(1, Color(200, 0, 0), true, mid), 100, 0, 0));
  // Back to black from there: no jump.
  CHECK(is(fades.color(1, Color(0, 0, 0), true, mid), 100, 0, 0));
  CHECK(is(fades.color(1, Color(0, 0, 0), true, mid + Fades::kDuration / 2), 50, 0, 0));
}

TEST(fades_reduced_motion_shows_the_target_at_once) {
  Fades fades;
  auto t0 = Clock::now();
  fades.color(1, Color(0, 0, 0), false, t0);
  CHECK(is(fades.color(1, Color(200, 0, 0), false, t0), 200, 0, 0));
  CHECK(!fades.active(t0));
}

TEST(fades_things_not_asked_for_are_forgotten_by_the_sweep) {
  Fades fades;
  auto t0 = Clock::now();
  fades.color(1, Color(0, 0, 0), true, t0);
  fades.color(2, Color(0, 0, 0), true, t0);
  fades.sweep();
  fades.color(1, Color(0, 0, 0), true, t0);  // 2 is not asked for
  fades.sweep();
  fades.sweep();  // 1 is not either
  // Seen again, each is new and takes its target at once.
  CHECK(is(fades.color(1, Color(50, 50, 50), true, t0), 50, 50, 50));
  CHECK(is(fades.color(2, Color(50, 50, 50), true, t0), 50, 50, 50));
}

TEST(fades_ease_is_flat_at_the_ends_and_half_in_the_middle) {
  CHECK_NEAR(Fades::ease(0.0f), 0.0f, 1e-6);
  CHECK_NEAR(Fades::ease(1.0f), 1.0f, 1e-6);
  CHECK_NEAR(Fades::ease(0.5f), 0.5f, 1e-6);
  CHECK(Fades::ease(0.1f) < 0.1f);
  CHECK(Fades::ease(-1.0f) == 0.0f && Fades::ease(2.0f) == 1.0f);
}

TEST(fades_a_number_eases_like_a_colour_and_a_first_sight_is_at_once) {
  Fades fades;
  auto t0 = Clock::now();
  CHECK_NEAR(fades.value(1, 0.0f, true, t0), 0.0f, 1e-6);
  CHECK(!fades.active(t0));
  CHECK_NEAR(fades.value(1, 1.0f, true, t0), 0.0f, 1e-6);
  CHECK(fades.active(t0));
  CHECK_NEAR(fades.value(1, 1.0f, true, t0 + Fades::kDuration / 2), 0.5f, 0.01);
  // Turned back midway: no jump.
  CHECK_NEAR(fades.value(1, 0.0f, true, t0 + Fades::kDuration / 2), 0.5f, 0.01);
  auto end = t0 + Fades::kDuration * 3;
  CHECK_NEAR(fades.value(1, 0.0f, true, end), 0.0f, 1e-6);
  CHECK(!fades.active(end));
  // Reduced motion: at once.
  CHECK_NEAR(fades.value(2, 0.0f, false, t0), 0.0f, 1e-6);
  CHECK_NEAR(fades.value(2, 1.0f, false, t0), 1.0f, 1e-6);
}
