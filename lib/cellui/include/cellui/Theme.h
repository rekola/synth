#pragma once

#include <cellui/Backdrop.h>
#include <cellui/Color.h>

#include <cmath>
#include <string>

namespace cellui {

// The colour tokens of a theme, by role: widgets ask for what a colour
// means, a theme says what it is. Themes are plain data.
struct Theme {
  // Background tones, lowest to highest elevation.
  Color surface_base;
  Color surface_panel;
  Color surface_raised;
  Color surface_overlay;

  Color text_primary;
  Color text_secondary;
  Color text_disabled;
  Color text_on_accent;

  // The cursor and region of the focused widget (accent), the same
  // for a widget without focus (accent_dim), and the text on both.
  Color accent;
  Color accent_dim;
  Color focus;

  // Roles outside the surfaces: the cursor tints, the reader text, the info
  // bar, the level meter and line chart ramps, and buttons.
  Color cursor_tint;
  Color cursor_row_tint;
  Color reader_text;
  Color info_line_fg;
  Color info_line_bg;
  Color level_full;
  Color level_active;
  Color level_low;
  Color level_mid;
  Color level_lime;
  Color level_warn;
  Color level_hot;
  Color line_low;
  Color line_high;
  Color button_fg;
  Color button_bg;
  Color button_pressed_bg;
  Color row_pressed_bg;
  // The menu bar, and the sections that drop down from it.
  Color menu_fg;
  Color menu_bg;
  Color menu_section_fg;
  Color menu_section_bg;
  // The base of a widget's plane, which shows wherever the widget draws
  // nothing (a scope between its bars), and the list picker's background,
  // and text.
  Color plane_base_bg;
  Color picker_bg;
  Color picker_fg;
  // What a modal dialog does to everything behind it.
  Backdrop modal_backdrop;
  // How far above its parent a plane (or a lifted cell) stands, by role, in
  // cell widths: what is higher is drawn over what is lower, and a display
  // with depth can show the difference.
  struct Elevation {
    float panel = 0.25f;
    float raised = 0.5f;  // a track's header
    float card = 1;       // a clip
    float overlay = 2;    // menus, pickers, popups
    float modal = 4;      // a modal dialog and its scrim
  } elevation;
  // The corner radius in pixels at 96 dpi of a dialog (the display scales it
  // by its DPI), where it draws by pixels. 0 is square.
  double dialog_radius_px = 6.0;

  static Theme original() {
    Theme t;
    t.surface_base = Color("#111111");
    t.surface_panel = Color("#292929");
    t.surface_raised = Color("#3d3d3d");
    t.surface_overlay = Color("#323232");
    t.text_primary = Color("#ffffff");
    t.text_secondary = Color("#9e9e9e");
    t.text_disabled = Color("#323232");
    t.text_on_accent = Color("#000000");
    t.accent = Color("#bcd4e0");
    t.accent_dim = Color("#3a4a54");
    t.focus = Color("#bcd4e0");
    t.cursor_tint = Color("#d0ecff");
    t.cursor_row_tint = Color("#90b8cc");
    t.reader_text = Color("#c080c0");
    t.info_line_fg = Color("#1e1e1e");
    t.info_line_bg = Color("#787878");
    t.level_full = Color("#e01040");
    t.level_active = Color("#10e040");
    t.level_low = Color("#0b5f2a");
    t.level_mid = Color("#0f9a3a");
    t.level_lime = Color("#9ae02e");
    t.level_warn = Color("#f09020");
    t.level_hot = Color("#ff5a20");
    t.line_low = Color("#1e5fd0");
    t.line_high = Color("#7af0d8");
    t.button_fg = Color("#ffffff");
    t.button_bg = Color("#c04080");
    t.button_pressed_bg = Color("#7a2850");
    t.row_pressed_bg = Color("#5c7c8c");
    t.menu_fg = Color("#ffffff");
    t.menu_bg = Color("#7f347f");
    t.menu_section_fg = Color("#ffffff");
    t.menu_section_bg = Color("#000000");
    t.plane_base_bg = Color("#200020");
    t.picker_bg = Color("#202020");
    t.picker_fg = Color("#c0c0c0");
    return t;
  }

  // The modern look (colour values and their contrast checks: the modern
  // look token tables).
  static Theme dark() {
    Theme t = original();
    t.surface_base = Color("#11161a");
    t.surface_panel = Color("#1c2227");
    t.surface_raised = Color("#2c3338");
    t.surface_overlay = Color("#3e464c");
    t.text_primary = Color("#f0f4f7");
    t.text_secondary = Color("#bfc5ca");
    t.text_disabled = Color("#82878c");
    t.text_on_accent = Color("#040f13");
    t.accent = Color("#75d4eb");
    t.accent_dim = Color("#245560");
    t.focus = t.accent;
    shareStates(t);
    t.cursor_tint = Color("#bae9f5");
    t.cursor_row_tint = t.accent;
    t.reader_text = t.accent;
    t.info_line_fg = t.text_secondary;
    t.info_line_bg = t.surface_raised;
    t.line_low = Color("#2b5fa8");
    t.line_high = t.accent;
    t.button_fg = t.text_on_accent;
    t.button_bg = t.accent;
    t.button_pressed_bg = Color("#4f9fb4");
    t.row_pressed_bg = Color("#2f6b78");
    t.menu_fg = t.text_primary;
    t.menu_bg = t.surface_panel;
    t.menu_section_fg = t.text_primary;
    t.menu_section_bg = t.surface_overlay;
    t.plane_base_bg = t.surface_base;
    t.picker_bg = t.surface_overlay;
    t.picker_fg = t.text_primary;
    t.modal_backdrop = Backdrop{ 2, 0.35f, t.surface_base };
    return t;
  }

  static Theme light() {
    Theme t = dark();
    // Elevation lightens here too: the window is a soft grey and what stands
    // above it (headers, cards, menus) is white.
    t.surface_base = Color("#f2f3f7");
    t.surface_panel = Color("#e9ebf0");
    t.surface_raised = Color("#ffffff");
    t.surface_overlay = Color("#ffffff");
    t.text_primary = Color("#1c1c1e");
    t.text_secondary = Color("#55575d");
    t.text_disabled = Color("#85858b");
    t.text_on_accent = Color("#ffffff");
    t.accent = Color("#0071e3");
    t.accent_dim = Color("#c7e0ff");
    t.focus = t.accent;
    t.cursor_tint = t.accent;
    t.cursor_row_tint = t.accent;
    t.reader_text = t.accent;
    t.info_line_fg = t.text_secondary;
    t.info_line_bg = t.surface_raised;
    t.line_low = Color("#4f4dd0");
    t.line_high = Color("#0071e3");
    t.button_fg = t.text_on_accent;
    t.button_bg = t.accent;
    t.button_pressed_bg = Color("#0058b8");
    t.row_pressed_bg = Color("#b9d8ff");
    t.menu_fg = t.text_primary;
    t.menu_bg = t.surface_panel;
    t.menu_section_fg = t.text_primary;
    t.menu_section_bg = t.surface_overlay;
    t.plane_base_bg = t.surface_base;
    t.picker_bg = t.surface_overlay;
    t.picker_fg = t.text_primary;
    t.modal_backdrop = Backdrop{ 2, 0.3f, t.surface_base };
    return t;
  }

  // The names a setting or command line can give.
  static bool byName(const std::string & name, Theme & out) {
    if (name == "original") out = original();
    else if (name == "dark") out = dark();
    else if (name == "light") out = light();
    else return false;
    return true;
  }

 private:
  // The colours that are the same in every modern theme: the level meter ramp.
  static void shareStates(Theme & t) {
    t.level_low = Color("#2d8949");
    t.level_mid = Color("#429c44");
    t.level_active = Color("#56b93e");
    t.level_lime = Color("#e6ad00");
    t.level_warn = Color("#f08a00");
    t.level_hot = Color("#f76400");
    t.level_full = Color("#e4312f");
  }
};

}  // namespace cellui
