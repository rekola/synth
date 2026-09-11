#include "TestFramework.h"

#include "../src/dsp/ValueRamp.h"

using namespace dsp;

// A default-constructed ramp starts at 0, already at rest - nothing to
// advance until glideTo()/snapTo() is called.
TEST(value_ramp_starts_at_rest_on_zero) {
  ValueRamp ramp;
  CHECK(!ramp.isActive());
  CHECK(ramp.getCurrentValue() == 0.0f);
}

TEST(value_ramp_snap_to_is_instant_and_leaves_it_at_rest) {
  ValueRamp ramp;
  ramp.snapTo(0.5f);
  CHECK(!ramp.isActive());
  CHECK(ramp.getCurrentValue() == 0.5f);
}

// glideTo() with frames <= 0 has nothing to spread across - same as
// snapTo() outright, not a one-frame glide.
TEST(value_ramp_glide_to_with_zero_frames_snaps_instead) {
  ValueRamp ramp;
  ramp.snapTo(0.0f);
  ramp.glideTo(1.0f, 0);
  CHECK(!ramp.isActive());
  CHECK(ramp.getCurrentValue() == 1.0f);
}

// The actual glide: advancing by fewer frames than the ramp's own total
// length reaches a real intermediate value, not the target - the whole
// point of moving this server-side (see LeafTrackState.h's own comment).
TEST(value_ramp_advance_partway_reaches_an_intermediate_value_not_the_target) {
  ValueRamp ramp;
  ramp.snapTo(0.0f);
  ramp.glideTo(1.0f, 100);
  CHECK(ramp.isActive());

  float halfway = ramp.advance(50);
  CHECK_NEAR(halfway, 0.5f, 1e-5f);
  CHECK(ramp.isActive()); // still 50 frames left

  float done = ramp.advance(50);
  CHECK_NEAR(done, 1.0f, 1e-5f);
  CHECK(!ramp.isActive());
}

// advance() never overshoots its own remaining length, even when handed
// more frames than are left - a caller (LeafTrackState::advanceSendRamps())
// doesn't need to clamp the frame count itself.
TEST(value_ramp_advance_clamps_to_its_own_remaining_length) {
  ValueRamp ramp;
  ramp.snapTo(0.0f);
  ramp.glideTo(1.0f, 10);
  float result = ramp.advance(1000);
  CHECK_NEAR(result, 1.0f, 1e-6f);
  CHECK(!ramp.isActive());
}

// Repeated small advance() calls (the real usage shape - one per render
// chunk, not one per whole glide) sum to the exact same result as a
// single big one - proves a glide crossing several chunk boundaries still
// lands correctly.
TEST(value_ramp_many_small_advances_sum_to_the_same_result_as_one_big_one) {
  ValueRamp a, b;
  a.snapTo(0.0f);
  a.glideTo(10.0f, 100);
  for (int i = 0; i < 100; i++) a.advance(1);

  b.snapTo(0.0f);
  b.glideTo(10.0f, 100);
  b.advance(100);

  CHECK_NEAR(a.getCurrentValue(), b.getCurrentValue(), 1e-4f);
  CHECK_NEAR(a.getCurrentValue(), 10.0f, 1e-4f);
}

// An instant snapTo() mid-glide must win outright, not be raced by a
// later advance() still carrying the old target forward - what
// LeafTrackState::setSendMain()/etc. rely on for "an instant set beats
// any glide in flight".
TEST(value_ramp_snap_to_mid_glide_cancels_it) {
  ValueRamp ramp;
  ramp.snapTo(0.0f);
  ramp.glideTo(1.0f, 100);
  ramp.advance(50); // now at 0.5, still gliding

  ramp.snapTo(0.2f);
  CHECK(!ramp.isActive());
  CHECK(ramp.getCurrentValue() == 0.2f);
  // A further advance() call must not resume the cancelled glide.
  CHECK(ramp.advance(50) == 0.2f);
}

// A glideTo() issued while a previous glide is still in flight starts
// fresh from wherever the ramp actually is right now (not the old
// target) - the same "retarget immediately" behavior a real fader
// press expects on a second touch before the first has settled.
TEST(value_ramp_glide_to_mid_glide_restarts_from_the_current_value) {
  ValueRamp ramp;
  ramp.snapTo(0.0f);
  ramp.glideTo(1.0f, 100);
  ramp.advance(50); // now at 0.5

  ramp.glideTo(0.0f, 50);
  CHECK(ramp.isActive());
  float result = ramp.advance(50);
  CHECK_NEAR(result, 0.0f, 1e-5f);
  CHECK(!ramp.isActive());
}
