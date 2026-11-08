#include "TestFramework.h"

#include <cellui/StyleProvider.h>

#include <array>
#include <cmath>
#include <cstdlib>

using namespace cellui;

namespace {
bool same(const Color & a, const Color & b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}
}  // namespace

TEST(applying_a_theme_changes_what_its_tokens_cover) {
  Theme t = Theme::original();
  t.surface_base = Color("#ffffff");
  StyleProvider s(t);
  CHECK(same(s.window_bg_color, "#ffffff"));
  CHECK(same(s.window_fg_color, "#9e9e9e"));
}

namespace {
bool near(const Color & a, const Color & b, int tol = 1) {
  return std::abs(a.getRed() - b.getRed()) <= tol && std::abs(a.getGreen() - b.getGreen()) <= tol &&
         std::abs(a.getBlue() - b.getBlue()) <= tol;
}

double luminance(const Color & c) {
  auto lin = [](int v) {
    double x = v / 255.0;
    return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin(c.getRed()) + 0.7152 * lin(c.getGreen()) + 0.0722 * lin(c.getBlue());
}

double contrast(const Color & a, const Color & b) {
  double la = luminance(a), lb = luminance(b);
  if (la < lb) std::swap(la, lb);
  return (la + 0.05) / (lb + 0.05);
}
}  // namespace

TEST(themes_are_found_by_name) {
  Theme t;
  CHECK(Theme::byName("original", t));
  CHECK(same(t.surface_base, "#111111"));
  CHECK(Theme::byName("dark", t));
  CHECK(same(t.surface_base, Theme::dark().surface_base));
  CHECK(Theme::byName("light", t));
  CHECK(same(t.surface_base, "#f2f3f7"));
  CHECK(!Theme::byName("neon", t));
}

TEST(a_theme_changes_the_ramps_too) {
  Theme t = Theme::original();
  t.level_low = Color("#010203");
  StyleProvider s(t);
  CHECK(same(s.levelColor(0.0f), "#010203"));
}

TEST(modern_themes_keep_text_readable) {
  for (auto t : {Theme::dark(), Theme::light()}) {
    CHECK(contrast(t.text_primary, t.surface_base) >= 4.5);
    CHECK(contrast(t.text_primary, t.surface_raised) >= 4.5);
    CHECK(contrast(t.text_secondary, t.surface_base) >= 4.5);
    CHECK(contrast(t.text_secondary, t.surface_panel) >= 4.5);
    CHECK(contrast(t.text_secondary, t.surface_raised) >= 4.5);
    CHECK(contrast(t.text_on_accent, t.accent) >= 4.5);
    CHECK(contrast(t.text_primary, t.accent_dim) >= 4.5);
    CHECK(contrast(t.text_disabled, t.surface_base) >= 3.0);
    CHECK(contrast(t.accent, t.surface_base) >= 3.0);
    CHECK(contrast(t.button_fg, t.button_bg) >= 4.5);
    CHECK(contrast(t.info_line_fg, t.info_line_bg) >= 4.5);
    CHECK(contrast(t.menu_fg, t.menu_bg) >= 4.5);
    CHECK(contrast(t.menu_section_fg, t.menu_section_bg) >= 4.5);
    CHECK(contrast(t.picker_fg, t.picker_bg) >= 4.5);
  }
}

TEST(the_light_theme_derives_its_menu_and_picker_from_its_own_surfaces) {
  auto l = Theme::light();
  CHECK(same(l.menu_bg, l.surface_panel));
  CHECK(same(l.menu_section_bg, l.surface_overlay));
  CHECK(same(l.picker_bg, l.surface_overlay));
  CHECK(same(l.picker_fg, l.text_primary));
  CHECK(same(l.plane_base_bg, l.surface_base));
}

TEST(a_modern_widget_plane_shows_the_base_surface_where_it_draws_nothing) {
  CHECK(same(StyleProvider(Theme::dark()).plane_base_bg_color, Theme::dark().surface_base));
  CHECK(same(StyleProvider(Theme::light()).plane_base_bg_color, "#f2f3f7"));
}

