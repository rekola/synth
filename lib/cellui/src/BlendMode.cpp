#include <cellui/BlendMode.h>

#include <algorithm>
#include <cmath>

namespace cellui {

namespace {

float blendChannel(BlendMode mode, float b, float s) {
  switch (mode) {
    case BlendMode::NORMAL: return s;
    case BlendMode::MULTIPLY: return b * s;
    case BlendMode::SCREEN: return b + s - b * s;
    case BlendMode::OVERLAY: return b <= 0.5f ? 2.0f * b * s : s + (2.0f * b - 1.0f) - s * (2.0f * b - 1.0f);
    case BlendMode::DARKEN: return std::min(b, s);
    case BlendMode::LIGHTEN: return std::max(b, s);
    case BlendMode::DIFFERENCE: return std::fabs(b - s);
    case BlendMode::EXCLUSION: return b + s - 2.0f * b * s;
  }
  return s;
}

}  // namespace

Color blendColors(BlendMode mode, const Color & below, const Color & top, float alpha) {
  auto b = below.toSrgb8();
  auto s = top.toSrgb8();
  int out[3];
  for (int i = 0; i < 3; i++) {
    float cb = static_cast<float>(b[static_cast<size_t>(i)]) / 255.0f;
    float cs = static_cast<float>(s[static_cast<size_t>(i)]) / 255.0f;
    float result = cb + alpha * (blendChannel(mode, cb, cs) - cb);
    out[i] = static_cast<int>(std::lround(std::clamp(result, 0.0f, 1.0f) * 255.0f));
  }
  return Color(out[0], out[1], out[2]);
}

}  // namespace cellui
