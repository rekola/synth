#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace cellui {

// The space a mix interpolates in. LINEAR is light itself (what compositing
// uses); SRGB is the gamma-encoded space a theme's tweens were tuned in;
// OKLAB is perceptual (gradients, hue ramps).
enum class MixSpace { LINEAR, SRGB, OKLAB };

// A colour that can hold any colour a display can show: linear light, Rec.2020
// primaries, 1.0 = SDR white, never negative, and above 1 for HDR. Every sRGB
// and Display-P3 colour is inside Rec.2020, so the lighting model can add,
// scale and multiply colours as light. A sink that can only show sRGB takes
// toSrgb8(), which maps the gamut and range in one place.
//
// Built from "#rrggbb" or "#rgb" text, the CSS forms oklch(L C H) and
// color(srgb | srgb-linear | display-p3 | rec2020 r g b), 8-bit sRGB, HSL,
// OKLCH or linear light.
class Color {
 public:
  Color() = default;
  // 8-bit sRGB (clamped to 0..255).
  explicit Color(int red, int green, int blue);
  Color(std::string_view s) { setValue(s); }
  Color(const char * s) { setValue(s); }
  Color & operator=(std::string_view s) {
    setValue(s);
    return *this;
  }

  // Linear Rec.2020 itself; above 1 is HDR. A component below 0 is outside the
  // gamut and is mapped in (constant OKLCH hue and lightness).
  static Color linear(float r, float g, float b);
  // Linear Rec.709 light (what linearSrgb() gives).
  static Color fromLinearSrgb(float r, float g, float b);
  // Gamma-encoded components as written in CSS, 0..1 (more or less is kept).
  static Color srgb(float r, float g, float b);
  static Color displayP3(float r, float g, float b);
  static Color rec2020(float r, float g, float b);

  // Standard HSL->RGB conversion - h in degrees (any range, wrapped via
  // fmod), s/l in [0,1]. Lets a caller (see VisibleTrackInfo::getColor())
  // pick colors by generating a hue rather than hand-picking RGB triples,
  // while keeping saturation/lightness fixed. The result is a plain 8-bit
  // sRGB colour.
  static Color fromHSL(float h, float s, float l);

  // OKLCH (lightness 0..1, chroma, hue in degrees). Lets a palette hold
  // lightness and chroma fixed and step only the hue, with the same perceived
  // brightness at every hue. A colour outside Rec.2020 is mapped in; one
  // outside sRGB stays as it is until a sink shows it.
  static Color fromOklch(float lightness, float chroma, float hue);
  // The same with each channel clipped into sRGB and encoded to 8 bits, the
  // way the theme's colours were tuned.
  static Color fromOklchClipped(float lightness, float chroma, float hue);

  float r() const { return r_; }
  float g() const { return g_; }
  float b() const { return b_; }

  // Relative to SDR white (Rec.2020 weights).
  float luminance() const { return 0.2627f * r_ + 0.6780f * g_ + 0.0593f * b_; }

  // The light multiplied: `c * 4` is four times as bright.
  Color operator*(float gain) const;

  // this * (1 - t) + other * t in `space`.
  Color mix(const Color & other, float t, MixSpace space = MixSpace::LINEAR) const;
  // A widget's tween of two theme colours, in the gamma-encoded space those
  // were tuned in: this * (1 - f) + other * f.
  Color blend(float f, const Color & other) const { return mix(other, f, MixSpace::SRGB); }

  // What a display that can only show sRGB shows: the gamut mapped in
  // (chroma reduced at constant OKLCH hue and lightness), the range brought to
  // 0..1 (a colour above 1 is scaled down, keeping its hue), and encoded with
  // the sRGB transfer. A colour inside sRGB with every component at most 1
  // comes back exactly.
  std::array<uint8_t, 3> toSrgb8() const;
  // The same, one channel at a time. Prefer toSrgb8() when you need more than
  // one.
  int getRed() const { return toSrgb8()[0]; }
  int getGreen() const { return toSrgb8()[1]; }
  int getBlue() const { return toSrgb8()[2]; }

  // The colour as linear Rec.709 light (not mapped: a wide colour has a
  // component below 0 or above 1). With the two functions below, a pixel
  // loop can work in linear light on 8-bit sRGB data without building a Color
  // per pixel.
  std::array<float, 3> linearSrgb() const;
  // Linear light of an 8-bit sRGB level (0..255).
  static float srgb8ToLinear(int level);
  // The 8-bit sRGB level of linear light (clamped to 0..1).
  static uint8_t linearToSrgb8(float linear);

  // The HSL hue in degrees of the sRGB colour, 0 for a grey.
  float hslHue() const;

  bool operator==(const Color & o) const { return r_ == o.r_ && g_ == o.g_ && b_ == o.b_; }
  bool operator!=(const Color & o) const { return !(*this == o); }
  size_t hash() const;

 private:
  Color(float r, float g, float b, int) : r_(r), g_(g), b_(b) { }
  void setValue(std::string_view s);

  float r_ = 0.0f, g_ = 0.0f, b_ = 0.0f;
};

}  // namespace cellui
