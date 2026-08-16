#ifndef _UICOLOR_H_
#define _UICOLOR_H_

#include "../util/digit.h"

#include <string_view>

class UIColor {
 public:
  explicit UIColor() : red(0), green(0), blue(0) { }
  explicit UIColor(int _red, int _green, int _blue) : red(_red), green(_green), blue(_blue) { }
  UIColor(std::string_view s) {
    setValue(std::move(s));
  }
  UIColor(const char * s) {
    setValue(s);
  }
  UIColor & operator=(std::string_view s) {
    setValue(std::move(s));
    return *this;
  }
  int getRed() const { return red; }
  int getGreen() const { return green; }
  int getBlue() const { return blue; }

  UIColor blend(float f, const UIColor & other) const {
    float inv_f = 1-f;
    return UIColor(int(red * inv_f + other.red * f),
		   int(green * inv_f + other.green * f),
		   int(blue * inv_f + other.blue * f)
		   );
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
