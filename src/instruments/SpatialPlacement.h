#ifndef _SPATIALPLACEMENT_H_
#define _SPATIALPLACEMENT_H_

#include "Tuning.h"
#include "../ambisonic/AmbisonicEncoding.h"
#include "../ambisonic/SpatialMode.h"
#include "../ambisonic/SphericalPosition.h"

#include <algorithm>
#include <cmath>

// Where a note sits around its track's position. Every offset is a (u, v)
// fraction of the position's extent, horizontal and vertical, turned into an
// azimuth/elevation delta by applyNormalizedOffset().
namespace spatial {

// The slot where the spiral reaches the rim of the extent; a by-ear value.
constexpr int kSpiralFull = 6;
constexpr float kGoldenAngleDegrees = 137.50776f;
// The fixed key span a generic arc runs over: A0 to C8.
constexpr int kArcLowKey = 21;
constexpr int kArcHighKey = 108;

// Converts a normalized (u, v) offset (fractions of the instrument's own
// extent, horizontal/vertical) into a real azimuth/elevation delta and
// adds it to `position`: x = u*extent, y = v*extent/kExtentShapeRatio,
// delta = atan2(x or y, distance). A zero-extent position (a point source)
// or no position set at all (distance <= 0) is left untouched. The mirror
// is computed from distance: <= 1 reads as "player" (u/v as given), > 1 as
// "audience" (mirrored).
inline SphericalPosition applyNormalizedOffset(const SphericalPosition & position, float u, float v) {
  if (position.extent <= 0.0f || position.distance <= 0.0f) return position;

  float mirror_sign = position.distance <= 1.0f ? 1.0f : -1.0f;
  float x = u * position.extent;
  float y = v * position.extent / kExtentShapeRatio;
  constexpr float kRad2Deg = 180.0f / static_cast<float>(M_PI);

  SphericalPosition result = position;
  result.azimuth += std::atan2(mirror_sign * x, position.distance) * kRad2Deg;
  result.elevation += std::atan2(y, position.distance) * kRad2Deg;
  return result;
}

struct Offset {
  float u = 0.0f, v = 0.0f;
};

// Slot k of the spiral. Slot 0 is the centre; slot k >= 1 sits at k golden
// angles round and at sqrt(k / kSpiralFull) of the extent (the rim from
// there on). It depends on k alone, so a slot never moves when others are
// added.
inline Offset spiralOffset(int slot) {
  if (slot <= 0) return {};
  float radius = std::min(1.0f, std::sqrt(static_cast<float>(slot) / static_cast<float>(kSpiralFull)));
  float angle = std::fmod(static_cast<float>(slot) * kGoldenAngleDegrees, 360.0f) * static_cast<float>(M_PI) / 180.0f;
  return {radius * std::cos(angle), radius * std::sin(angle)};
}

inline SphericalPosition placeOnSpiral(const SphericalPosition & position, int slot) {
  auto offset = spiralOffset(slot);
  return applyNormalizedOffset(position, offset.u, offset.v);
}

// `key` along [low_key, high_key] runs from -1 to +1 across the extent. An
// empty range leaves the position alone.
inline SphericalPosition placeOnArc(const SphericalPosition & position, float key, int low_key, int high_key) {
  if (low_key >= high_key) return position;
  float u = 2.0f * (key - static_cast<float>(low_key)) / static_cast<float>(high_key - low_key) - 1.0f;
  u = std::max(-1.0f, std::min(1.0f, u));
  return applyNormalizedOffset(position, u, 0.0f);
}

// A MIDI-style key number (fractional for a microtonal note) for any tuning;
// a percussion note value already is one.
inline float keyFor(Tuning tuning, int note_value) {
  if (tuning == Tuning::PERCUSSION) return static_cast<float>(note_value);
  return std::log2(getFrequencyFor(tuning, note_value) / 440.0f) * 12.0f + 69.0f;
}

// What an instrument with no placement of its own does for each mode.
inline SphericalPosition placeGeneric(const SphericalPosition & position, SpatialMode mode, int slot, Tuning tuning, int note_value) {
  if (mode == SpatialMode::ARC) return placeOnArc(position, keyFor(tuning, note_value), kArcLowKey, kArcHighKey);
  return placeOnSpiral(position, slot);
}

} // namespace spatial

#endif
