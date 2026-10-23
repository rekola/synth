#include <cellui/BlendMode.h>

#include <algorithm>
#include <array>
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
    default: break;
  }
  return s;
}

using Rgb = std::array<float, 3>;

float lum(const Rgb & c) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }

Rgb clipColor(Rgb c) {
  float l = lum(c), n = std::min({c[0], c[1], c[2]}), x = std::max({c[0], c[1], c[2]});
  if (n < 0.0f) {
    for (float & v : c) v = l + (v - l) * l / (l - n);
  }
  if (x > 1.0f) {
    for (float & v : c) v = l + (v - l) * (1.0f - l) / (x - l);
  }
  return c;
}

Rgb setLum(const Rgb & c, float l) {
  float d = l - lum(c);
  return clipColor({c[0] + d, c[1] + d, c[2] + d});
}

float sat(const Rgb & c) { return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}); }

Rgb setSat(Rgb c, float s) {
  // The smallest channel goes to 0, the largest to s, the middle in proportion.
  int lo = 0, hi = 0;
  for (int i = 1; i < 3; i++) {
    if (c[static_cast<size_t>(i)] < c[static_cast<size_t>(lo)]) lo = i;
    if (c[static_cast<size_t>(i)] > c[static_cast<size_t>(hi)]) hi = i;
  }
  if (lo == hi) return {0.0f, 0.0f, 0.0f};
  int mid = 3 - lo - hi;
  float mn = c[static_cast<size_t>(lo)], mx = c[static_cast<size_t>(hi)], md = c[static_cast<size_t>(mid)];
  Rgb out{};
  out[static_cast<size_t>(mid)] = (md - mn) * s / (mx - mn);
  out[static_cast<size_t>(hi)] = s;
  out[static_cast<size_t>(lo)] = 0.0f;
  return out;
}

bool nonSeparable(BlendMode mode) {
  return mode == BlendMode::HUE || mode == BlendMode::SATURATION || mode == BlendMode::COLOR || mode == BlendMode::LUMINOSITY;
}

Rgb blendNonSeparable(BlendMode mode, const Rgb & b, const Rgb & s) {
  switch (mode) {
    case BlendMode::HUE: return setLum(setSat(s, sat(b)), lum(b));
    case BlendMode::SATURATION: return setLum(setSat(b, sat(s)), lum(b));
    case BlendMode::COLOR: return setLum(s, lum(b));
    case BlendMode::LUMINOSITY: return setLum(b, lum(s));
    default: break;
  }
  return s;
}

}  // namespace

Color blendColors(BlendMode mode, const Color & below, const Color & top, float alpha) {
  auto b8 = below.toSrgb8();
  auto s8 = top.toSrgb8();
  Rgb b{}, s{}, blended{};
  for (size_t i = 0; i < 3; i++) {
    b[i] = static_cast<float>(b8[i]) / 255.0f;
    s[i] = static_cast<float>(s8[i]) / 255.0f;
  }
  if (nonSeparable(mode)) {
    blended = blendNonSeparable(mode, b, s);
  } else {
    for (size_t i = 0; i < 3; i++) blended[i] = blendChannel(mode, b[i], s[i]);
  }
  int out[3];
  for (size_t i = 0; i < 3; i++) {
    float result = b[i] + alpha * (blended[i] - b[i]);
    out[i] = static_cast<int>(std::lround(std::clamp(result, 0.0f, 1.0f) * 255.0f));
  }
  return Color(out[0], out[1], out[2]);
}

}  // namespace cellui
