#include "TestFramework.h"

#include "../dsp/HashField.h"

#include <algorithm>

TEST(hashfield_unit_is_deterministic_per_coord_param_salt) {
  HashField a(12345), b(12345);
  for (int64_t coord = 0; coord < 100; coord++) {
    CHECK(a.unit(coord, 7) == b.unit(coord, 7));
  }
}

TEST(hashfield_unit_stays_half_open_zero_to_one) {
  HashField field(999);
  for (int64_t coord = -5000; coord < 5000; coord++) {
    float v = field.unit(coord, 1);
    CHECK(v >= 0.0f);
    CHECK(v < 1.0f);
  }
}

TEST(hashfield_different_coords_decorrelate) {
  HashField field(1);
  bool any_different = false;
  float first = field.unit(0, 1);
  for (int64_t coord = 1; coord < 100; coord++) {
    if (field.unit(coord, 1) != first) { any_different = true; break; }
  }
  CHECK(any_different);
}

TEST(hashfield_different_params_decorrelate_same_coord) {
  HashField field(1);
  bool any_different = false;
  float first = field.unit(42, 0);
  for (uint32_t param = 1; param < 100; param++) {
    if (field.unit(42, param) != first) { any_different = true; break; }
  }
  CHECK(any_different);
}

TEST(hashfield_different_salts_decorrelate) {
  HashField a(1), b(2);
  bool any_different = false;
  for (int64_t coord = 0; coord < 100; coord++) {
    if (a.unit(coord, 3) != b.unit(coord, 3)) { any_different = true; break; }
  }
  CHECK(any_different);
}

TEST(hashfield_range_stays_within_bounds_and_hits_both_ends_approximately) {
  HashField field(55);
  float lo = -3.0f, hi = 8.0f;
  float min_seen = hi, max_seen = lo;
  for (int64_t coord = 0; coord < 2000; coord++) {
    float v = field.range(coord, 4, lo, hi);
    CHECK(v >= lo);
    CHECK(v < hi);
    min_seen = std::min(min_seen, v);
    max_seen = std::max(max_seen, v);
  }
  // Over 2000 draws, a uniform-ish distribution should get reasonably
  // close to both ends - not a strict statistical test, just a sanity
  // check that range() isn't accidentally collapsed onto a narrow band.
  CHECK(min_seen < lo + (hi - lo) * 0.05f);
  CHECK(max_seen > hi - (hi - lo) * 0.05f);
}

TEST(hashfield_bipolar_stays_within_spread_and_can_be_negative) {
  HashField field(77);
  float spread = 2.5f;
  bool saw_negative = false, saw_positive = false;
  for (int64_t coord = 0; coord < 200; coord++) {
    float v = field.bipolar(coord, 9, spread);
    CHECK(v >= -spread);
    CHECK(v < spread);
    if (v < 0.0f) saw_negative = true;
    if (v > 0.0f) saw_positive = true;
  }
  CHECK(saw_negative);
  CHECK(saw_positive);
}

// Sub-voice/step decorrelation (NoteMultiplier/ArpeggiatorState's own
// usage shape) is handled by NoteCoordinate::withInstance(), not by any
// HashField method - HashField's own two-argument (coord, param) shape
// already covers "vary the param axis per discriminator" (e.g.
// paramId("voice") ^ voice_id) without needing a dedicated
// coordinate-derivation primitive of its own.

TEST(paramid_is_stable_and_distinguishes_names) {
  static_assert(paramId("note_phase") == paramId("note_phase"), "paramId must be stable for the same literal");
  CHECK(paramId("note_phase") == paramId("note_phase"));
  CHECK(paramId("note_phase") != paramId("note_detune"));
  CHECK(paramId("a") != paramId("b"));
}

TEST(hashfield_hash64_is_constexpr) {
  // Compiles only if hash64 is usable in a constant-expression context -
  // the point of the test is the static_assert, not the runtime CHECK.
  constexpr HashField field(1);
  constexpr uint64_t h = field.hash64(10, 20);
  static_assert(h == field.hash64(10, 20), "hash64 must be constexpr-evaluable");
  CHECK(h == field.hash64(10, 20));
}
