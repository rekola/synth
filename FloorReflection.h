#ifndef _FLOORREFLECTION_H_
#define _FLOORREFLECTION_H_

#include <algorithm>
#include <cmath>

// Speed of sound in air at ~20C - a fixed physical constant, not a song
// parameter (nothing in this engine's scope varies with temperature or
// altitude).
constexpr float kSpeedOfSoundMPerSec = 343.0f;

// The floor reflection's own resolved values for one voice, everything an
// InstrumentVoice needs to actually render the tap.
struct FloorReflectionGeometry {
  float delaySamples;      // relative to the direct path - never negative
  float gainRatio;         // multiply into the direct path's own gain (Send Main * 1/distance) - already includes floorReflectionStrength, never above it
  float elevationDegrees;  // reflected arrival elevation; azimuth is unchanged from the direct path
};

// Given the listener's ear height `earHeight`, a voice's own `distance`/
// `elevationDegrees`, the song's `reflectionStrength` (floorReflectionStrength),
// and the audio sample rate, computes the floor reflection's relative
// delay/gain/direction - pure geometry, no allocation, no class
// dependency, so it can be exercised directly against hand-computed
// values.
//
// Geometry: source height above the floor hs = earHeight +
// distance*sin(el); horizontal distance dh = distance*cos(el); the image
// source sits at -hs. Reflected path length p = sqrt(dh^2 + (hs' +
// earHeight)^2), using hs' = max(hs, 0) - clamped only in this reflection
// geometry, never in the direct path's own rendering. At hs == 0 exactly,
// p == distance and elevationDegrees == the direct elevation - a
// continuous coincidence, not a discontinuity, since the reflection
// becomes a coincident copy at that boundary.
//
// Below-floor correctness needs two clamps beyond the hs one above, not
// one: once hs is clamped to 0, a deeply below-floor source (large
// negative unclamped hs, small dh) can fold to an image *nearer* than the
// true (unclamped) direct distance, making the naive (p - distance)/c
// negative and d/p exceed 1 - implying a reflection arriving before the
// direct sound, and louder than it, both impossible for a passive
// reflection. Clamping delaySamples to >= 0 and the distance ratio to <=
// 1 keeps both invariants everywhere, and is a true no-op for every
// above-floor placement (p >= distance always holds there).
inline FloorReflectionGeometry computeFloorReflectionGeometry(float earHeight, float distance, float elevationDegrees, float reflectionStrength, float sampleRate) {
  constexpr float kDeg2Rad = static_cast<float>(M_PI) / 180.0f;
  constexpr float kRad2Deg = 180.0f / static_cast<float>(M_PI);

  float el = elevationDegrees * kDeg2Rad;
  float hs = earHeight + distance * sinf(el);
  float dh = distance * cosf(el);
  float hs_clamped = std::max(hs, 0.0f);
  float p = std::sqrt(dh * dh + (hs_clamped + earHeight) * (hs_clamped + earHeight));

  float delay_seconds = (p - distance) / kSpeedOfSoundMPerSec;
  if (delay_seconds < 0.0f) delay_seconds = 0.0f;

  float distance_ratio = distance / p;
  if (distance_ratio > 1.0f) distance_ratio = 1.0f;

  // atan2, not atan: dh == 0 (a source directly overhead or underfoot)
  // saturates to the correct +-90 degree limit instead of dividing by
  // zero.
  float elevation_refl = -std::atan2(hs_clamped + earHeight, dh) * kRad2Deg;

  return { delay_seconds * sampleRate, distance_ratio * reflectionStrength, elevation_refl };
}

// Max possible relative delay (samples) for any voice in a song with the
// given ear height - occurs as distance -> 0 and shrinks monotonically as
// distance grows, so every voice can safely share one buffer length sized
// from the song-wide ear height alone, regardless of its own placement.
// +4 samples of margin for the delay line's own interpolated read.
inline int floorReflectionMaxDelaySamples(float earHeight, float sampleRate) {
  return static_cast<int>(2.0f * earHeight / kSpeedOfSoundMPerSec * sampleRate) + 4;
}

#endif
