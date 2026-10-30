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

TEST(dark_theme_keeps_the_values_the_widgets_always_used) {
  StyleProvider s(Theme::original());
  CHECK(same(s.highlight_fg_color, "#000000"));
  CHECK(same(s.highlight_bg_color, "#bcd4e0"));
  CHECK(same(s.highlight_unfocused_bg_color, "#3a4a54"));
  CHECK(same(s.window_border_color, "#323232"));
  CHECK(same(s.window_fg_color, "#9e9e9e"));
  CHECK(same(s.window_bg_color, "#111111"));
  CHECK(same(s.window_accent_fg_color, "#ffffff"));
  CHECK(same(s.window_accent_bg_color, "#292929"));
  CHECK(same(s.window_beat_accent_fg_color, "#c4c4c4"));
  CHECK(same(s.window_beat_accent_bg_color, "#232323"));
  CHECK(same(s.window_bar_accent_fg_color, "#ffffff"));
  CHECK(same(s.window_bar_accent_bg_color, "#323232"));
  CHECK(same(s.clip_playing_color, "#50e070"));
  CHECK(same(s.clip_paused_color, "#2e7a3e"));
  CHECK(same(s.clip_recording_color, "#ff3c3c"));
  CHECK(same(s.clip_armed_color, "#a04040"));
  CHECK(same(s.heading_bg_color, "#3d3d3d"));
}

TEST(dark_theme_keeps_the_other_colours_too) {
  StyleProvider s(Theme::original());
  CHECK(same(s.cursor_tint_color, "#d0ecff"));
  CHECK(same(s.cursor_row_tint_color, "#90b8cc"));
  CHECK(same(s.command_column_color, "#c67610"));
  CHECK(same(s.clip_text_color, "#ffffff"));
  CHECK(same(s.reader_text_color, "#c080c0"));
  CHECK(same(s.info_line_fg_color, "#1e1e1e"));
  CHECK(same(s.info_line_bg_color, "#787878"));
  CHECK(same(s.meter_clip_color, "#e01040"));
  CHECK(same(s.meter_active_color, "#10e040"));
  CHECK(same(s.meter_low_color, "#0b5f2a"));
  CHECK(same(s.meter_mid_color, "#0f9a3a"));
  CHECK(same(s.meter_lime_color, "#9ae02e"));
  CHECK(same(s.meter_warn_color, "#f09020"));
  CHECK(same(s.meter_hot_color, "#ff5a20"));
  CHECK(same(s.spectrum_low_color, "#1e5fd0"));
  CHECK(same(s.spectrum_high_color, "#7af0d8"));
  CHECK(same(s.mute_color, "#ff5a5a"));
  CHECK(same(s.solo_color, "#ffdc5a"));
  CHECK(same(s.monitor_color, "#5ad2ff"));
  CHECK(same(s.clip_override_color, "#ff9a3c"));
  CHECK(same(s.locator_color, "#e03030"));
  CHECK(same(s.locator_bg_color, "#702020"));
  CHECK(same(s.locator_mark_color, "#c8c8c8"));
  CHECK(same(s.button_fg_color, "#ffffff"));
  CHECK(same(s.button_bg_color, "#c04080"));
  CHECK(same(s.button_pressed_bg_color, "#7a2850"));
  CHECK(same(s.row_pressed_bg_color, "#5c7c8c"));
  CHECK(same(s.master_track_color, "#533918"));
  CHECK(same(s.menu_bg_color, "#7f347f"));
  CHECK(same(s.menu_section_bg_color, "#000000"));
  CHECK(same(s.plane_base_bg_color, "#200020"));
  CHECK(same(s.picker_bg_color, "#202020"));
  CHECK(same(s.picker_fg_color, "#c0c0c0"));
  CHECK(same(s.data_fx_color, "#d65cd6"));
  CHECK(same(s.data_velocity_color, "#bfa426"));
  CHECK(same(s.data_delay_color, "#42c1ea"));
}

TEST(applying_a_theme_changes_what_its_tokens_cover) {
  Theme t = Theme::original();
  t.surface_base = Color("#ffffff");
  StyleProvider s(t);
  CHECK(same(s.window_bg_color, "#ffffff"));
  CHECK(same(s.window_fg_color, "#9e9e9e"));
}

TEST(a_theme_changes_the_ramps_and_flags_too) {
  Theme t = Theme::original();
  t.meter_low = Color("#010203");
  t.mute = Color("#040506");
  StyleProvider s(t);
  CHECK(same(s.meterColor(0.0f), "#010203"));
  CHECK(same(s.mute_color, "#040506"));
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

TEST(the_original_track_colours_are_the_golden_angle_hsl_ones) {
  auto p = Theme::original().tracks;
  for (int i = 0; i < 8; i++) CHECK(same(p.color(i), Color::fromHSL(std::fmod(i * 137.50776f, 360.0f), 0.35f, 0.42f)));
}

TEST(the_modern_track_colours_are_the_token_table) {
  const char * expected[] = {"#0092a3", "#9e6887", "#898100", "#557ac3", "#7e7e63", "#9067b2", "#c05e00", "#64818f"};
  auto p = Theme::dark().tracks;
  for (int i = 0; i < 8; i++) CHECK(near(p.color(i), expected[i]));
}

TEST(modern_track_hues_stay_clear_of_the_clip_state_hues) {
  auto p = TrackPalette::modern();
  for (int i = 0; i < 64; i++) {
    float h = p.hue(i);
    for (float state : {145.0f, 27.0f}) {
      float d = std::fabs(std::fmod(h - state + 540.0f, 360.0f) - 180.0f);
      CHECK(d >= 34.5f);
    }
  }
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
    CHECK(contrast(t.data_fx, t.surface_base) >= 4.5);
    CHECK(contrast(t.data_velocity, t.surface_base) >= 4.5);
    CHECK(contrast(t.data_delay, t.surface_base) >= 4.5);
    CHECK(contrast(t.info_line_fg, t.info_line_bg) >= 4.5);
    CHECK(contrast(t.menu_fg, t.menu_bg) >= 4.5);
    CHECK(contrast(t.menu_section_fg, t.menu_section_bg) >= 4.5);
    CHECK(contrast(t.picker_fg, t.picker_bg) >= 4.5);
    for (int i = 0; i < 8; i++) {
      CHECK(contrast(t.tracks.color(i), t.surface_base) >= 3.0);
      CHECK(contrast(t.text_on_track, t.tracks.color(i)) >= 4.5);
    }
  }
}

TEST(the_clip_states_are_the_same_in_every_modern_theme) {
  auto d = Theme::dark(), l = Theme::light();
  CHECK(same(d.state_playing, l.state_playing));
  CHECK(same(d.state_recording, l.state_recording));
  CHECK(same(d.text_on_track, l.text_on_track));
  CHECK(same(d.meter_low, l.meter_low));
}

TEST(the_first_modern_tracks_differ_from_each_other_and_on_the_pads) {
  auto p = TrackPalette::modern();
  auto lab = [](const Color & c) {
    auto lin = [](int v) {
      double x = v / 255.0;
      return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    double r = lin(c.getRed()), g = lin(c.getGreen()), b = lin(c.getBlue());
    double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return std::array<double, 3>{ 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                                  1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                                  0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
  };
  auto dist = [&](const Color & a, const Color & b) {
    auto x = lab(a), y = lab(b);
    return 100.0 * std::sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2]));
  };
  auto pad = [&](int i) { return Color::fromHSL(p.hslHue(i), p.padSaturation(i), 0.3f); };
  for (int i = 0; i < 8; i++) {
    for (int j = i + 1; j < 8; j++) {
      CHECK(dist(p.color(i), p.color(j)) >= 6.5);
      CHECK(dist(pad(i), pad(j)) >= 10.0);
    }
  }
}

TEST(the_light_theme_derives_its_menu_and_picker_from_its_own_surfaces) {
  auto l = Theme::light();
  CHECK(same(l.menu_bg, l.surface_raised));
  CHECK(same(l.menu_section_bg, l.surface_overlay));
  CHECK(same(l.picker_bg, l.surface_overlay));
  CHECK(same(l.picker_fg, l.text_primary));
  CHECK(same(l.plane_base_bg, l.surface_base));
}

TEST(only_the_modern_themes_show_a_track_as_a_slice_on_its_header) {
  CHECK(!StyleProvider(Theme::original()).track_chips);
  CHECK(StyleProvider(Theme::dark()).track_chips);
  CHECK(StyleProvider(Theme::light()).track_chips);
}

TEST(the_header_slice_is_a_shade_of_the_track_colour_toward_the_text) {
  StyleProvider d(Theme::dark()), l(Theme::light());
  for (int i = 0; i < 8; i++) {
    CHECK(!same(d.trackChipColor(i), d.trackColor(i)));
    CHECK(d.trackChipColor(i).getRed() + d.trackChipColor(i).getGreen() + d.trackChipColor(i).getBlue() >
          d.trackColor(i).getRed() + d.trackColor(i).getGreen() + d.trackColor(i).getBlue());
    CHECK(l.trackChipColor(i).getRed() + l.trackChipColor(i).getGreen() + l.trackChipColor(i).getBlue() <
          l.trackColor(i).getRed() + l.trackColor(i).getGreen() + l.trackColor(i).getBlue());
  }
}

TEST(a_modern_widget_plane_shows_the_base_surface_where_it_draws_nothing) {
  CHECK(same(StyleProvider(Theme::dark()).plane_base_bg_color, Theme::dark().surface_base));
  CHECK(same(StyleProvider(Theme::light()).plane_base_bg_color, "#f2f3f7"));
}

TEST(only_the_original_theme_draws_lines_between_grid_columns) {
  StyleProvider o(Theme::original()), d(Theme::dark()), l(Theme::light());
  CHECK(o.grid_lines);
  CHECK(same(o.grid_bg_color, o.window_bg_color));
  CHECK(!d.grid_lines);
  CHECK(same(d.grid_bg_color, d.theme.surface_panel));
  CHECK(!l.grid_lines);
  CHECK(same(l.grid_bg_color, l.theme.surface_panel));
}
