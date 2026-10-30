#include "TestFramework.h"

#include <cellui/WheelAcceleration.h>

using namespace cellui;
using std::chrono::milliseconds;

TEST(wheel_acceleration_grows_with_quick_notches_and_resets_after_a_pause) {
  WheelAcceleration wheel;
  auto t = WheelAcceleration::Clock::now();
  CHECK(wheel.step(3, t) == 3);
  CHECK(wheel.step(3, t += milliseconds(30)) == 3);
  CHECK(wheel.step(3, t += milliseconds(30)) == 6);
  for (int i = 0; i < 20; i++) wheel.step(3, t += milliseconds(30));
  CHECK(wheel.step(3, t += milliseconds(30)) == 12);  // capped
  CHECK(wheel.step(3, t += milliseconds(300)) == 3);  // a pause starts over
}
