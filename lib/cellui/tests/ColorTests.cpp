#include "TestFramework.h"

#include <cellui/Color.h>

using namespace cellui;

TEST(color_parses_long_and_short_hex) {
  Color a("#c67610");
  CHECK(a.getRed() == 0xc6 && a.getGreen() == 0x76 && a.getBlue() == 0x10);
  Color b("fff");
  CHECK(b.getRed() == 255 && b.getGreen() == 255 && b.getBlue() == 255);
  Color c("#0af");
  CHECK(c.getRed() == 0x00 && c.getGreen() == 0xaa && c.getBlue() == 0xff);
}

TEST(color_treats_bad_hex_digits_as_zero_and_short_text_as_black) {
  Color a("#zz00ff");
  CHECK(a.getRed() == 0 && a.getGreen() == 0 && a.getBlue() == 255);
  Color b("#12");
  CHECK(b.getRed() == 0 && b.getGreen() == 0 && b.getBlue() == 0);
}

TEST(color_blend_and_hsl) {
  Color black(0, 0, 0), white(255, 255, 255);
  Color mid = black.blend(0.5f, white);
  CHECK(mid.getRed() == 127 && mid.getGreen() == 127 && mid.getBlue() == 127);
  Color red = Color::fromHSL(0.0f, 1.0f, 0.5f);
  CHECK(red.getRed() == 255 && red.getGreen() == 0 && red.getBlue() == 0);
  Color wrapped = Color::fromHSL(360.0f, 1.0f, 0.5f);
  CHECK(wrapped.getRed() == 255 && wrapped.getGreen() == 0);
}
