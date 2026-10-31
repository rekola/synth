#include "TestFramework.h"

#include <cellui/Color.h>

#include <clocale>
#include <set>
#include <string>

using namespace cellui;

namespace {
bool srgb8(const Color & c, int r, int g, int b) {
  auto v = c.toSrgb8();
  return v[0] == r && v[1] == g && v[2] == b;
}
}  // namespace

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

TEST(color_srgb8_round_trips_through_the_float_for_every_value) {
  for (int r = 0; r < 256; r++)
    for (int g = 0; g < 256; g += 5)
      for (int b = 0; b < 256; b += 7) {
        Color c(r, g, b);
        auto v = c.toSrgb8();
        CHECK(v[0] == r && v[1] == g && v[2] == b);
      }
  // Every level of every channel, alone and as a grey.
  for (int i = 0; i < 256; i++) {
    CHECK(srgb8(Color(i, i, i), i, i, i));
    CHECK(srgb8(Color(i, 0, 0), i, 0, 0));
    CHECK(srgb8(Color(0, i, 0), 0, i, 0));
    CHECK(srgb8(Color(0, 0, i), 0, 0, i));
  }
}

TEST(color_white_is_one_in_every_channel_and_green_is_inside_rec2020) {
  Color white(255, 255, 255);
  CHECK_NEAR(white.r(), 1.0f, 1e-5f);
  CHECK_NEAR(white.g(), 1.0f, 1e-5f);
  CHECK_NEAR(white.b(), 1.0f, 1e-5f);
  CHECK_NEAR(white.luminance(), 1.0f, 1e-5f);
  // sRGB green in Rec.2020 light.
  Color green(0, 255, 0);
  CHECK_NEAR(green.r(), 0.3293f, 2e-4f);
  CHECK_NEAR(green.g(), 0.9195f, 2e-4f);
  CHECK_NEAR(green.b(), 0.0880f, 2e-4f);
}

TEST(color_has_no_negative_component_whatever_it_is_built_from) {
  const Color samples[] = {Color::linear(-0.5f, 1.0f, -0.2f), Color::fromOklch(0.8f, 0.5f, 140.0f), Color::fromOklch(0.5f, 0.9f, 30.0f),
                           Color::displayP3(0.0f, 1.0f, 0.0f), Color::rec2020(0.0f, 1.0f, 0.0f), Color::srgb(-0.2f, 1.3f, 0.5f),
                           Color("color(srgb-linear 2 -0.5 0.1)"), Color(255, 0, 0).mix(Color(0, 0, 255), 0.5f, MixSpace::OKLAB)};
  for (const Color & c : samples) CHECK(c.r() >= 0.0f && c.g() >= 0.0f && c.b() >= 0.0f);
  for (int i = 0; i < 360; i += 7)
    for (float chroma = 0.0f; chroma < 1.2f; chroma += 0.1f) {
      Color c = Color::fromOklch(0.7f, chroma, static_cast<float>(i));
      CHECK(c.r() >= 0.0f && c.g() >= 0.0f && c.b() >= 0.0f);
    }
}

TEST(color_rec2020_green_primary_is_unit_green_and_clips_to_srgb_green) {
  Color g = Color::rec2020(0.0f, 1.0f, 0.0f);
  CHECK_NEAR(g.r(), 0.0f, 1e-5f);
  CHECK_NEAR(g.g(), 1.0f, 1e-5f);
  CHECK_NEAR(g.b(), 0.0f, 1e-5f);
  // The sink keeps the hue: a green, with no red.
  auto v = g.toSrgb8();
  CHECK(v[1] > 200 && v[0] == 0 && v[2] < v[1]);
  Color g2("color(rec2020 0 1 0)");
  CHECK(g2 == g);
}

TEST(color_matrices_put_sRGB_and_p3_inside_rec2020) {
  // P3 red is more saturated than sRGB red, and both are inside Rec.2020.
  Color p3 = Color::displayP3(1.0f, 0.0f, 0.0f);
  Color s = Color::srgb(1.0f, 0.0f, 0.0f);
  CHECK(p3.r() > s.r());
  CHECK(p3.g() >= 0.0f && p3.b() >= 0.0f);
  Color a(40, 120, 200);
  Color b = Color::srgb(40 / 255.0f, 120 / 255.0f, 200 / 255.0f);
  CHECK_NEAR(a.r(), b.r(), 1e-5f);
  CHECK_NEAR(a.g(), b.g(), 1e-5f);
  CHECK_NEAR(a.b(), b.b(), 1e-5f);
}

TEST(color_notation) {
  CHECK(Color("#ff8040") == Color(255, 128, 64));
  CHECK(Color("color(srgb 1 0.5 0.25)") == Color::srgb(1.0f, 0.5f, 0.25f));
  CHECK(Color("COLOR(display-p3 1 0 0)") == Color::displayP3(1.0f, 0.0f, 0.0f));
  CHECK(Color("color(rec2020 0.25 0.5 0.75)") == Color::rec2020(0.25f, 0.5f, 0.75f));
  CHECK(Color("oklch(0.75 0.15 60)") == Color::fromOklch(0.75f, 0.15f, 60.0f));
  CHECK(Color("oklch(75% 0.15 60deg)") == Color::fromOklch(0.75f, 0.15f, 60.0f));
  CHECK(Color("oklch(0.75 0.15 60 / 0.5)") == Color::fromOklch(0.75f, 0.15f, 60.0f));
  // srgb-linear is linear Rec.709 light, and may be above 1.
  Color hdr("color(srgb-linear 2 1 0.5)");
  CHECK(hdr.luminance() > 1.0f);
  Color one("color(srgb-linear 1 1 1)");
  CHECK_NEAR(one.r(), 1.0f, 1e-5f);
  CHECK_NEAR(one.b(), 1.0f, 1e-5f);
  // Nothing else is accepted: nits and unknown spaces read as black.
  CHECK(Color("color(rec2100-pq 0.5 0.5 0.5)") == Color(0, 0, 0));
  CHECK(Color("oklch(0.5)") == Color(0, 0, 0));
}

TEST(color_gain_lights_and_the_sink_keeps_the_hue) {
  Color red(200, 30, 30);
  Color lit = red * 6.0f;
  CHECK_NEAR(lit.luminance(), red.luminance() * 6.0f, 1e-4f);
  auto v = lit.toSrgb8();
  CHECK(v[0] == 255);
  CHECK(v[1] < 100 && v[2] < 100);
  CHECK(srgb8(red * 1.0f, 200, 30, 30));
  CHECK(srgb8(red * 0.0f, 0, 0, 0));
  CHECK(srgb8(red * -2.0f, 0, 0, 0));
}

TEST(color_out_of_srgb_gamut_maps_in_with_the_hue_kept) {
  // Display-P3 green is outside sRGB: the sink shows a saturated green, not a
  // channel-clipped one.
  auto v = Color::displayP3(0.0f, 1.0f, 0.0f).toSrgb8();
  CHECK(v[1] > 200);
  CHECK(v[0] < 60 && v[2] < 120);
  // Black stays black; an in-gamut grey stays grey.
  CHECK(srgb8(Color(), 0, 0, 0));
  CHECK(srgb8(Color(128, 128, 128), 128, 128, 128));
}

TEST(color_mix_in_linear_light_and_in_encoded_srgb) {
  Color black(0, 0, 0), white(255, 255, 255);
  // Half the light is far above the encoded middle.
  auto lin = black.mix(white, 0.5f).toSrgb8();
  CHECK(lin[0] == 188 && lin[1] == 188 && lin[2] == 188);
  // A widget's tween halves the encoded value.
  auto mid = black.blend(0.5f, white).toSrgb8();
  CHECK(mid[0] >= 127 && mid[0] <= 128 && mid[0] == mid[1] && mid[1] == mid[2]);
  CHECK(black.mix(white, 0.0f) == black);
  CHECK(black.mix(white, 1.0f) == white);
  CHECK(black.mix(white, 2.0f) == white);
  // OKLab keeps the lightness of the middle perceptually even.
  auto ok = black.mix(white, 0.5f, MixSpace::OKLAB).toSrgb8();
  CHECK(ok[0] > 90 && ok[0] < 130 && ok[0] == ok[1] && ok[1] == ok[2]);
}

TEST(color_hsl_and_hue) {
  Color red = Color::fromHSL(0.0f, 1.0f, 0.5f);
  CHECK(red.getRed() == 255 && red.getGreen() == 0 && red.getBlue() == 0);
  Color wrapped = Color::fromHSL(360.0f, 1.0f, 0.5f);
  CHECK(wrapped.getRed() == 255 && wrapped.getGreen() == 0);
  CHECK_NEAR(Color(0, 255, 0).hslHue(), 120.0f, 0.5f);
  CHECK_NEAR(Color(128, 128, 128).hslHue(), 0.0f, 1e-6f);
}

TEST(color_equality_and_hash) {
  CHECK(Color(10, 20, 30) == Color("#0a141e"));
  CHECK(Color(10, 20, 30) != Color(10, 20, 31));
  CHECK(Color(10, 20, 30).hash() == Color("#0a141e").hash());
  std::set<size_t> hashes;
  for (int i = 0; i < 100; i++) hashes.insert(Color(i, 2 * i, 255 - i).hash());
  CHECK(hashes.size() == 100);
}

// A window toolkit sets the locale; with a decimal comma "0.7" must still parse.
TEST(color_parsing_ignores_the_locale_decimal_separator) {
  std::string previous = std::setlocale(LC_NUMERIC, nullptr);
  bool comma = false;
  for (const char * name : {"fi_FI.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "sv_SE.UTF-8"}) {
    if (std::setlocale(LC_NUMERIC, name) && std::localeconv()->decimal_point[0] == ',') {
      comma = true;
      break;
    }
  }
  Color parsed("oklch(0.75 0.15 60)");
  Color percent("oklch(75% 0.15 60deg)");
  std::setlocale(LC_NUMERIC, previous.c_str());
  if (!comma) return;  // no such locale installed here
  CHECK(parsed == Color::fromOklch(0.75f, 0.15f, 60.0f));
  CHECK(percent == Color::fromOklch(0.75f, 0.15f, 60.0f));
}
