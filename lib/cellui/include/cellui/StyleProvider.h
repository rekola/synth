#pragma once

#include <cellui/Color.h>
#include <cellui/Theme.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace cellui {

class StyleProvider {
 public:
  // Motion off: states change at once instead of easing, and what pulses or
  // flashes holds a steady shade. Not part of a theme; set by the app.
  bool reduced_motion = false;

  StyleProvider() { applyTheme(Theme::dark()); }
  explicit StyleProvider(const Theme & initial) { applyTheme(initial); }

  // Sets every colour from the theme's tokens.
  void applyTheme(const Theme & t) {
    theme = t;
    highlight_fg_color = t.text_on_accent;
    highlight_bg_color = t.accent;
    highlight_unfocused_bg_color = t.accent_dim;
    window_border_color = t.surface_overlay;
    window_fg_color = t.text_secondary;
    window_bg_color = t.surface_base;
    window_accent_fg_color = t.text_primary;
    window_accent_bg_color = t.surface_panel;
    heading_bg_color = t.surface_raised;
    cursor_tint_color = t.cursor_tint;
    cursor_row_tint_color = t.cursor_row_tint;
    reader_text_color = t.reader_text;
    info_line_fg_color = t.info_line_fg;
    info_line_bg_color = t.info_line_bg;
    level_full_color = t.level_full;
    level_active_color = t.level_active;
    level_low_color = t.level_low;
    level_mid_color = t.level_mid;
    level_lime_color = t.level_lime;
    level_warn_color = t.level_warn;
    level_hot_color = t.level_hot;
    line_low_color = t.line_low;
    line_high_color = t.line_high;
    button_fg_color = t.button_fg;
    button_bg_color = t.button_bg;
    button_pressed_bg_color = t.button_pressed_bg;
    row_pressed_bg_color = t.row_pressed_bg;
    menu_fg_color = t.menu_fg;
    menu_bg_color = t.menu_bg;
    menu_section_fg_color = t.menu_section_fg;
    menu_section_bg_color = t.menu_section_bg;
    plane_base_bg_color = t.plane_base_bg;
    picker_bg_color = t.picker_bg;
    picker_fg_color = t.picker_fg;
  }

  Theme theme;

  // Cursor and playhead colors lean cyan, so they never read as the
  // neutral grey bar/beat highlighting.
  // The cursor/region of the focused widget.
  Color highlight_fg_color;
  Color highlight_bg_color;
  // The cursor of an unfocused widget - where its edits would land - on
  // plain window_fg_color text.
  Color highlight_unfocused_bg_color;
  // What a colored cell (a clip, an arrangement instance) brightens toward
  // under a cursor or playhead, instead of taking either background above.
  Color cursor_tint_color;
  // The translucent tint over a marked row: the pattern editor's cursor
  // row and playheads (in Arrangement view the transport's row, in
  // Live View each track's own), the clip grid's scene row and the
  // arrangement grid's playing row.
  // Whether the window has the focus; cursors are hidden while it has not.
  bool window_focused = true;
  Color cursor_row_tint_color;
  static constexpr float kRowTintAlpha = 0.35f;
  Color cursorRowTint(Color base) const { return base.blend(kRowTintAlpha, cursor_row_tint_color); }
  // The rows of a cursor below its first: its colour, far fainter than a selection.
  static constexpr float kCursorBodyAlpha = 0.16f;
  Color cursorBodyTint(Color base) const { return base.blend(kCursorBodyAlpha, highlight_bg_color); }

  Color window_border_color;
  Color window_fg_color;
  Color window_bg_color;
  Color window_accent_fg_color;
  Color window_accent_bg_color;

  // Typed text in a reader (the status line's name prompts, inline name editors).
  Color reader_text_color;

  // The info bar at the bottom, and the controls sitting inline in it.
  Color info_line_fg_color;
  Color info_line_bg_color;

  // A level indicator's colours: full scale, and sounding.
  Color level_full_color;
  Color level_active_color;
  // The level meters' bars shade by height, quiet to loud: greens, then
  // lime, orange and hot orange, ending in level_full_color at full scale.
  Color level_low_color;
  Color level_mid_color;
  Color level_lime_color;
  Color level_warn_color;
  Color level_hot_color;
  // The spectrum's bars shade from low to high.
  Color line_low_color;
  Color line_high_color;

  // The color of a level meter at `fraction` (0..1, level_meter::fraction())
  // of its height.
  Color levelColor(float fraction) const {
    const std::pair<float, const Color *> stops[] = {
      { 0.0f, &level_low_color }, { 0.3f, &level_mid_color }, { 0.55f, &level_active_color }, { 0.78f, &level_lime_color },
      { 0.86f, &level_warn_color }, { 0.94f, &level_hot_color }, { 1.0f, &level_full_color },
    };
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    for (size_t i = 1; i < std::size(stops); i++) {
      if (fraction <= stops[i].first) {
        float t = (fraction - stops[i - 1].first) / (stops[i].first - stops[i - 1].first);
        return stops[i - 1].second->blend(t, *stops[i].second);
      }
    }
    return level_full_color;
  }
  Color lineColor(float fraction) const { return line_low_color.blend(std::clamp(fraction, 0.0f, 1.0f), line_high_color); }

  // Panel headers (a list's heading, a grid's header row, and
  // the divider between them) - brighter than
  // window_accent_bg_color, so they read as a stronger accent than the
  // content beneath them.
  Color heading_bg_color;

  // Action buttons in a details panel.
  Color button_fg_color;
  Color button_bg_color;
  // While the mouse is held down on a button, all of it.
  Color button_pressed_bg_color;
  // While the mouse is held down on a list row.
  Color row_pressed_bg_color;

  // The menu bar, and the sections that drop down from it.
  Color menu_fg_color;
  Color menu_bg_color;
  Color menu_section_fg_color;
  Color menu_section_bg_color;

  // A widget plane's base, shown where the widget draws nothing, and the
  // list picker's background and text.
  Color plane_base_bg_color;
  Color picker_bg_color;
  Color picker_fg_color;
};

}  // namespace cellui
