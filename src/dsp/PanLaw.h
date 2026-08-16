#ifndef _PANLAW_H_
#define _PANLAW_H_

#include <cmath>

struct StereoGains {
  float left, right;
};

// Maps an azimuth in degrees to an equal-power stereo pan position in
// [-0.5, 0.5], where -0.5 is hard left and 0.5 is hard right. Not clamped:
// combine with additional offsets (e.g. a SoundFont region's own pan) before
// clamping via panToStereoGains().
static inline float azimuthToPan(float azimuthDegrees) {
  return sinf(azimuthDegrees / 180.0f * static_cast<float>(M_PI)) / 2.0f;
}

// Converts a pan position to equal-power stereo gains (left^2 + right^2 is
// constant across the pan range), clamping it to [-0.5, 0.5] first.
static inline StereoGains panToStereoGains(float pan) {
  if (pan < -0.5f) pan = -0.5f;
  else if (pan > 0.5f) pan = 0.5f;
  return { sqrtf(0.5f - pan), sqrtf(0.5f + pan) };
}

#endif
