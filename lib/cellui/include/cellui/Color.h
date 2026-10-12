#pragma once

#include <cmath>
#include <string_view>
#include <utility>

namespace cellui {

// An 8-bit-per-channel RGB color: plain arithmetic on three bytes, no
// rendering dependency. Built from "#rrggbb" or "#rgb" text, or from HSL.
class Color {
 public:
  Color() : red(0), green(0), blue(0) { }
  explicit Color(int _red, int _green, int _blue) : red(_red), green(_green), blue(_blue) { }
  Color(std::string_view s) {
    setValue(std::move(s));
  }
  Color(const char * s) {
    setValue(s);
  }
  Color & operator=(std::string_view s) {
    setValue(std::move(s));
    return *this;
  }
  int getRed() const { return red; }
  int getGreen() const { return green; }
  int getBlue() const { return blue; }

  Color blend(float f, const Color & other) const {
    float inv_f = 1-f;
    return Color(int(red * inv_f + other.red * f),
		 int(green * inv_f + other.green * f),
		 int(blue * inv_f + other.blue * f)
		 );
  }

  // Standard HSL->RGB conversion - h in degrees (any range, wrapped via
  // fmod), s/l in [0,1]. Lets a caller (see VisibleTrackInfo::getColor())
  // pick colors by generating a hue rather than hand-picking RGB triples,
  // while keeping saturation/lightness fixed.
  static Color fromHSL(float h, float s, float l) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
    float hp = h / 60.0f;
    float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r1 = 0, g1 = 0, b1 = 0;
    if (hp < 1.0f) { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2.0f) { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3.0f) { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4.0f) { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5.0f) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }
    float m = l - c / 2.0f;
    return Color(static_cast<int>((r1 + m) * 255.0f + 0.5f),
		 static_cast<int>((g1 + m) * 255.0f + 0.5f),
		 static_cast<int>((b1 + m) * 255.0f + 0.5f));
  }

  // OKLCH (lightness 0..1, chroma, hue in degrees) to sRGB, each channel
  // clamped into gamut. Lets a palette hold lightness and chroma fixed and
  // step only the hue, with the same perceived brightness at every hue.
  static Color fromOklch(float lightness, float chroma, float hue) {
    float a = chroma * std::cos(hue * 3.14159265f / 180.0f);
    float b = chroma * std::sin(hue * 3.14159265f / 180.0f);
    float l_ = lightness + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = lightness - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = lightness - 0.0894841775f * a - 1.2914855480f * b;
    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    float rgb[3] = {
      4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
      -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
      -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s,
    };
    int out[3];
    for (int i = 0; i < 3; i++) {
      float x = std::fmin(1.0f, std::fmax(0.0f, rgb[i]));
      float g = x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
      out[i] = static_cast<int>(g * 255.0f + 0.5f);
    }
    return Color(out[0], out[1], out[2]);
  }

  // The HSL hue in degrees, 0 for a grey.
  float hslHue() const {
    float r = red / 255.0f, g = green / 255.0f, b = blue / 255.0f;
    float mx = std::fmax(r, std::fmax(g, b)), mn = std::fmin(r, std::fmin(g, b));
    float d = mx - mn;
    if (d <= 0.0f) return 0.0f;
    float h = mx == r ? std::fmod((g - b) / d, 6.0f) : mx == g ? (b - r) / d + 2.0f : (r - g) / d + 4.0f;
    h *= 60.0f;
    return h < 0.0f ? h + 360.0f : h;
  }

 private:
  // A non-hex character counts as 0 rather than rejecting the whole value.
  static inline int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  }

  void setValue(std::string_view s) {
    if (!s.empty() && s[0] == '#') s.remove_prefix(1);
    if (s.size() >= 6) {
      red = hexDigit(s[0]) * 16 + hexDigit(s[1]);
      green = hexDigit(s[2]) * 16 + hexDigit(s[3]);
      blue = hexDigit(s[4]) * 16 + hexDigit(s[5]);
    } else if (s.size() >= 3) {
      auto r = hexDigit(s[0]);
      auto g = hexDigit(s[1]);
      auto b = hexDigit(s[2]);
      red = static_cast<unsigned char>(r * 17);   // #rgb is #rrggbb with each digit doubled
      green = static_cast<unsigned char>(g * 17);
      blue = static_cast<unsigned char>(b * 17);
    } else {
      red = green = blue = 0;
    }
  }

  unsigned char red, green, blue;
};

}  // namespace cellui
