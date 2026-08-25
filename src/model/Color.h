#ifndef _COLOR_H_
#define _COLOR_H_

#include "../util/digit.h"

#include <cmath>
#include <string_view>

// Model-layer, not UI-layer - a color is a fundamental fact about a piece
// of song data (see VisibleTrackInfo::getColor()), independent of
// whatever renders it. No UI/notcurses dependency - just plain
// arithmetic on three bytes.
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

 private:
  // digit() returns -1 for a non-hex character rather than rejecting the
  // whole value; every hexDigit() call below coerces that to 0, same as
  // this function's own behavior always was.
  static inline int hexDigit(char c) {
    auto d = digit(c, 16);
    return d < 0 ? 0 : d;
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
      red = (r * 16 + r) / 255.0f;
      green = (g * 16 + g) / 255.0f;
      blue = (b * 16 + b) / 255.0f;
    } else {
      red = green = blue = 0;
    }
  }

  unsigned char red, green, blue;
};

#endif
