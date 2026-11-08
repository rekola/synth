#pragma once

#include <algorithm>

namespace cellui {

// Brings linear light that is brighter than a display can show into what it can,
// the way Khronos PBR Neutral does: a colour whose brightest channel is at or under
// `start` is left as it is; above it the brightest channel is compressed toward 1
// on a smooth curve, and the colour drifts toward white in proportion to how much
// light the compression took away, so a very bright colour has a pale core instead
// of a flat plateau of its own hue. Neutral's dark offset is left out: dark colours
// stay as they are.
//
// Neutral itself is `start` 0.76 and `desaturation` 0.15, with scene light below the
// exposure that puts white at 1: scene 1 comes out at 0.88. Light here is already
// exposed - 1 is the display's white, which must stay white - so the defaults start
// the compression at 1 (which clips the brightest channel to 1) and make the
// colour drift faster.
inline void neutralToneMap(float rgb[3], float start = 1.0f, float desaturation = 1.0f) {
  float peak = std::max({ rgb[0], rgb[1], rgb[2] });
  if (!(peak > start)) return;
  float d = 1.0f - start;
  float new_peak = 1.0f - d * d / (peak + d - start);
  float scale = new_peak / peak;
  float drift = 1.0f - 1.0f / (desaturation * (peak - new_peak) + 1.0f);
  for (int i = 0; i < 3; i++) {
    float c = rgb[i] * scale;
    rgb[i] = c + (new_peak - c) * drift;
  }
}

}  // namespace cellui
