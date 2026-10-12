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

TEST(applying_a_theme_changes_what_its_tokens_cover) {
  Theme t = Theme::dark();
  t.surface_base = Color("#ffffff");
  StyleProvider s(t);
  CHECK(same(s.window_bg_color, "#ffffff"));
  CHECK(same(s.window_fg_color, "#9e9e9e"));
}
