#include "TestFramework.h"

#include "../src/ui/StyleProvider.h"

#include <array>
#include <set>
#include <cmath>

namespace {
bool same(const Color & a, const Color & b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

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
} // namespace

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
  CHECK(same(s.level_full_color, "#e01040"));
  CHECK(same(s.level_active_color, "#10e040"));
  CHECK(same(s.level_low_color, "#0b5f2a"));
  CHECK(same(s.level_mid_color, "#0f9a3a"));
  CHECK(same(s.level_lime_color, "#9ae02e"));
  CHECK(same(s.level_warn_color, "#f09020"));
  CHECK(same(s.level_hot_color, "#ff5a20"));
  CHECK(same(s.line_low_color, "#1e5fd0"));
  CHECK(same(s.line_high_color, "#7af0d8"));
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
  CHECK(same(s.comma_up_color, "#ff8a3d"));
  CHECK(same(s.comma_down_color, "#5ea0ff"));
}

TEST(a_theme_changes_the_ramps_and_flags_too) {
  Theme t = Theme::original();
  t.base.level_low = Color("#010203");
  t.palette.mute = Color("#040506");
  StyleProvider s(t);
  CHECK(same(s.levelColor(0.0f), "#010203"));
  CHECK(same(s.mute_color, "#040506"));
}

TEST(the_original_track_colours_are_the_golden_angle_hsl_ones) {
  auto p = Theme::original().palette.tracks;
  for (int i = 0; i < 8; i++) CHECK(same(p.color(i), Color::fromHSL(std::fmod(i * 137.50776f, 360.0f), 0.35f, 0.42f)));
}

TEST(the_modern_track_colours_are_the_token_table) {
  const char * expected[] = {"#0092a3", "#9e6887", "#898100", "#557ac3", "#7e7e63", "#9067b2", "#c05e00", "#64818f"};
  auto p = Theme::dark().palette.tracks;
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

TEST(the_clip_states_are_the_same_in_every_modern_theme) {
  auto d = Theme::dark(), l = Theme::light();
  CHECK(same(d.palette.state_playing, l.palette.state_playing));
  CHECK(same(d.palette.state_recording, l.palette.state_recording));
  CHECK(same(d.palette.text_on_track, l.palette.text_on_track));
  CHECK(same(d.base.level_low, l.base.level_low));
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
    return std::array<double, 3>{0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                                 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                                 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
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

TEST(only_the_original_theme_draws_lines_between_grid_columns) {
  StyleProvider o(Theme::original()), d(Theme::dark()), l(Theme::light());
  CHECK(o.grid_lines);
  CHECK(same(o.grid_bg_color, o.window_bg_color));
  CHECK(!d.grid_lines);
  CHECK(same(d.grid_bg_color, d.theme.surface_panel));
  CHECK(!l.grid_lines);
  CHECK(same(l.grid_bg_color, l.theme.surface_panel));
}

TEST(themes_are_found_by_name_with_their_palettes) {
  Theme t;
  CHECK(Theme::byName("original", t));
  CHECK(t.palette.grid_lines);
  CHECK(Theme::byName("dark", t));
  CHECK(!t.palette.grid_lines);
  CHECK(Theme::byName("light", t));
  CHECK(same(t.base.surface_base, "#f2f3f7"));
  CHECK(!Theme::byName("neon", t));
}

TEST(the_data_columns_and_comma_arrows_stay_readable) {
  for (auto t : {Theme::dark(), Theme::light()}) {
    const auto & bg = t.base.surface_base;
    CHECK(contrast(t.palette.data_fx, bg) >= 4.5);
    CHECK(contrast(t.palette.data_velocity, bg) >= 4.5);
    CHECK(contrast(t.palette.data_delay, bg) >= 4.5);
    CHECK(contrast(t.palette.comma_up, bg) >= 4.5);
    CHECK(contrast(t.palette.comma_down, bg) >= 4.5);
    for (int i = 0; i < 8; i++) {
      CHECK(contrast(t.palette.tracks.color(i), bg) >= 3.0);
      CHECK(contrast(t.palette.text_on_track, t.palette.tracks.color(i)) >= 4.5);
    }
  }
}

TEST(note_flash_is_full_at_the_attack_and_gone_after_its_time) {
  CHECK_NEAR(StyleProvider::noteFlash(0.0, false), 1.0f, 1e-6f);
  CHECK(StyleProvider::noteFlash(0.05, false) > StyleProvider::noteFlash(0.15, false));
  CHECK(StyleProvider::noteFlash(-0.01, false) == 0.0f); // not started yet
  CHECK(StyleProvider::noteFlash(StyleProvider::kNoteFlashSeconds, false) == 0.0f);
}

TEST(note_flash_without_motion_is_a_step) {
  CHECK(StyleProvider::noteFlash(0.01, true) == 1.0f);
  CHECK(StyleProvider::noteFlash(StyleProvider::kNoteFlashSeconds * 0.75, true) == 0.0f);
}

TEST(note_flash_lights_a_dim_colour_as_far_as_a_bright_one) {
  Color dim = Color::linear(0.1f, 0.3f, 0.1f), bright = Color::linear(0.9f, 0.9f, 0.9f);
  auto peak = [](const Color & c) { return std::max({c.r(), c.g(), c.b()}); };
  CHECK(peak(StyleProvider::lit(dim, 1.0f)) >= StyleProvider::kNoteFlashPeak - 1e-4f);
  CHECK(peak(StyleProvider::lit(bright, 1.0f)) >= StyleProvider::kNoteFlashGain * peak(bright) - 1e-4f);
  CHECK(StyleProvider::lit(dim, 0.0f) == dim);
}

TEST(note_flash_is_stronger_for_a_louder_note) {
  CHECK_NEAR(StyleProvider::velocityFlash(127), 1.0f, 1e-6f);
  CHECK_NEAR(StyleProvider::velocityFlash(0), 0.5f, 1e-6f);
  CHECK(StyleProvider::velocityFlash(0x28) < StyleProvider::velocityFlash(0x60));
  CHECK_NEAR(StyleProvider::velocityFlash(500), 1.0f, 1e-6f);
}

TEST(note_flash_zoom_follows_the_flash_in_the_face) {
  CHECK(StyleProvider::zoomFace(0.0f).zoom == 0);
  CHECK(StyleProvider::zoomFace(1.0f).zoom == 255);
  CHECK(StyleProvider::zoomFace(2.0f).zoom == 255);
  CHECK(StyleProvider::zoomFace(-1.0f).zoom == 0);
  uint8_t last = 0;
  for (int i = 0; i <= 100; i++) {
    uint8_t z = StyleProvider::zoomFace(static_cast<float>(i) / 100.0f).zoom;
    CHECK(z >= last);
    last = z;
  }
  // Only the zoom: a face for a flash is otherwise the plain one.
  CHECK(StyleProvider::zoomFace(0.5f).weight == cellui::kWeightNormal);
}

// --- The terminal's own 16 (and 8) colours ---

#include <cellui/term/FrameEncoder.h>

namespace {

// What a colour is as the terminal is sent it: the palette index, or a marker for the terminal's own text or
// background colour (which is sent as the default and so is its own "colour").
int sentIndex(const Color & c, bool eight, const std::optional<Color> & fg, const std::optional<Color> & bg, bool background) {
  const auto & own = background ? bg : fg;
  if (own && c.toSrgb8() == own->toSrgb8()) return background ? 100 : 101;
  return eight ? cellui::term::nearest8(c) : cellui::term::nearest16(c);
}

struct TerminalColours {
  const char * name;
  std::optional<Color> fg, bg;
};

} // namespace

TEST(the_ansi_themes_use_the_terminals_own_text_and_background) {
  Theme t;
  Theme::Hints hints{Color("#d0d0d0"), Color("#101010")};
  CHECK(Theme::byName("ansi16", t, hints));
  CHECK(same(t.base.text_primary, "#d0d0d0"));
  CHECK(same(t.base.surface_base, "#101010"));
  CHECK(Theme::byName("ansi8", t, hints));
  CHECK(same(t.base.text_primary, "#d0d0d0"));
  // Without hints, a dark terminal's usual white on black.
  CHECK(Theme::byName("ansi16", t));
  CHECK(same(t.base.surface_base, cellui::ansi16Color(0)));
}

// Every pair of roles that must tell apart does, as the terminal receives them: by palette index. Both
// depths, a dark and a light terminal and one whose colours are not known.
TEST(the_ansi_themes_keep_the_pairs_that_must_differ_apart) {
  const TerminalColours terminals[] = {
      {"dark", Color("#d0d0d0"), Color("#101010")},
      {"light", Color("#202020"), Color("#f8f8f8")},
      {"unknown", std::nullopt, std::nullopt},
  };
  for (const auto & terminal : terminals) {
    for (bool eight : {false, true}) {
      Theme t = Theme::ansi16(terminal.fg, terminal.bg, eight);
      const auto & b = t.base;
      const auto & p = t.palette;
      auto bg = [&](const Color & c) { return sentIndex(c, eight, terminal.fg, terminal.bg, true); };
      auto fg = [&](const Color & c) { return sentIndex(c, eight, terminal.fg, terminal.bg, false); };
      auto differ = [&](int x, int y, const char * what) {
        if (x == y) std::fprintf(stderr, "    %s, %s colours: %s are the same\n", terminal.name, eight ? "8" : "16", what);
        return x != y;
      };
      // The cursor, focused and not, against the surface it is on and against each other.
      CHECK(differ(bg(b.accent), bg(b.surface_base), "cursor and surface"));
      CHECK(differ(bg(b.accent_dim), bg(b.surface_base), "unfocused cursor and surface"));
      CHECK(differ(bg(b.accent), bg(b.accent_dim), "cursor and unfocused cursor"));
      CHECK(differ(fg(b.text_on_accent), bg(b.accent), "cursor text and cursor"));
      CHECK(differ(fg(b.text_primary), bg(b.surface_base), "text and surface"));
      CHECK(differ(fg(b.focus), bg(b.surface_base), "focus and surface"));
      // Bar and beat rows, the grid's accents.
      CHECK(differ(bg(p.grid_bar_bg), bg(p.grid_beat_bg), "bar and beat rows"));
      CHECK(differ(fg(p.grid_bar_fg), bg(p.grid_bar_bg), "bar text and bar"));
      // The playhead and clip states.
      CHECK(differ(fg(p.state_playing), fg(p.state_playing_dim), "playing and paused"));
      CHECK(differ(fg(p.state_recording), fg(p.state_recording_dim), "recording and stopping"));
      CHECK(differ(fg(p.state_playing), fg(p.state_recording), "playing and recording"));
      // Buttons, the menu, the level meter's two ends.
      CHECK(differ(bg(b.button_bg), bg(b.button_pressed_bg), "button and pressed button"));
      CHECK(differ(bg(b.menu_bg), bg(b.surface_base), "menu bar and surface"));
      CHECK(differ(fg(b.level_low), fg(b.level_full), "meter low and clip"));
      CHECK(differ(fg(b.level_warn), fg(b.level_full), "meter warning and clip"));
      CHECK(differ(fg(p.mute), fg(p.solo), "mute and solo"));
      // Tracks: neighbours always differ, and the first N are all different for every N the palette allows.
      int distinct = eight ? 6 : 12;
      std::set<int> seen;
      for (int i = 0; i < distinct; i++) seen.insert(fg(p.tracks.color(i)));
      CHECK(static_cast<int>(seen.size()) == distinct);
      for (int i = 0; i < 40; i++) CHECK(differ(fg(p.tracks.color(i)), fg(p.tracks.color(i + 1)), "neighbouring tracks"));
    }
  }
}
