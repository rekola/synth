#include "TestFramework.h"

#include <cellui/StyleProvider.h>

using namespace cellui;

namespace {
bool same(const Color & a, const Color & b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}
}  // namespace

TEST(dark_theme_keeps_the_values_the_widgets_always_used) {
  StyleProvider s;
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
  StyleProvider s;
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
}

TEST(applying_a_theme_changes_what_its_tokens_cover) {
  Theme t = Theme::dark();
  t.surface_base = Color("#ffffff");
  StyleProvider s(t);
  CHECK(same(s.window_bg_color, "#ffffff"));
  CHECK(same(s.window_fg_color, "#9e9e9e"));
}

TEST(a_theme_changes_the_ramps_and_flags_too) {
  Theme t = Theme::dark();
  t.meter_low = Color("#010203");
  t.mute = Color("#040506");
  StyleProvider s(t);
  CHECK(same(s.meterColor(0.0f), "#010203"));
  CHECK(same(s.mute_color, "#040506"));
}
