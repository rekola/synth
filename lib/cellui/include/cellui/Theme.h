#pragma once

#include <cellui/Color.h>

#include <cmath>
#include <string>

namespace cellui {

// How a track's identity colour is made from its ordinal: the original steps
// the HSL hue by the golden angle at a fixed saturation and lightness; the
// modern one steps the OKLCH hue by the golden ratio over the arcs the clip
// state colours leave free, at one lightness and chroma, so a track never
// looks like a playing or a recording clip.
struct TrackPalette {
  enum class Model { GOLDEN_HSL, OKLCH_ARCS };
  Model model = Model::GOLDEN_HSL;
  float lightness = 0.42f;
  float saturation_or_chroma = 0.35f;

  static TrackPalette original() { return { Model::GOLDEN_HSL, 0.42f, 0.35f }; }
  static TrackPalette modern() { return { Model::OKLCH_ARCS, 0.585f, 0.12f }; }

  // The model's own hue, in degrees.
  float hue(int ordinal) const {
    if (model == Model::GOLDEN_HSL) return std::fmod(static_cast<float>(ordinal) * 137.50776f, 360.0f);
    constexpr float arcs[2][2] = { { 45.0f, 105.0f }, { 180.0f, 340.0f } };
    float t = std::fmod(0.5f + static_cast<float>(ordinal) * 0.6180339887f, 1.0f);
    float pos = t * ((arcs[0][1] - arcs[0][0]) + (arcs[1][1] - arcs[1][0]));
    for (auto & arc : arcs) {
      if (pos <= arc[1] - arc[0]) return arc[0] + pos;
      pos -= arc[1] - arc[0];
    }
    return arcs[1][1];
  }
  Color color(int ordinal) const {
    return model == Model::GOLDEN_HSL ? Color::fromHSL(hue(ordinal), saturation_or_chroma, lightness)
                                      : Color::fromOklch(lightness, saturation_or_chroma, hue(ordinal));
  }
  // The same colour's HSL hue, for a device that shades it its own way.
  float hslHue(int ordinal) const { return color(ordinal).hslHue(); }
};

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

  // State, each with its dim form (paused; armed or stopping).
  Color state_playing;
  Color state_playing_dim;
  Color state_recording;
  Color state_recording_dim;

  // Pattern editor rows: beat and bar accents, as tones.
  Color grid_beat_fg;
  Color grid_beat_bg;
  Color grid_bar_fg;
  Color grid_bar_bg;

  TrackPalette tracks = TrackPalette::original();

  // Roles outside the surfaces: the cursor tints, text on a track colour,
  // the command and reader text, the info bar, the meter and spectrum
  // ramps, track flags, locators, buttons and the master track's bar.
  Color cursor_tint;
  Color cursor_row_tint;
  Color command_column;
  Color text_on_track;
  Color reader_text;
  Color info_line_fg;
  Color info_line_bg;
  Color meter_clip;
  Color meter_active;
  Color meter_low;
  Color meter_mid;
  Color meter_lime;
  Color meter_warn;
  Color meter_hot;
  Color spectrum_low;
  Color spectrum_high;
  Color mute;
  Color solo;
  Color monitor;
  Color clip_override;
  Color locator;
  Color locator_bg;
  Color locator_mark;
  Color button_fg;
  Color button_bg;
  Color button_pressed_bg;
  Color row_pressed_bg;
  Color master_track;

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
    t.state_playing = Color("#50e070");
    t.state_playing_dim = Color("#2e7a3e");
    t.state_recording = Color("#ff3c3c");
    t.state_recording_dim = Color("#a04040");
    t.grid_beat_fg = Color("#c4c4c4");
    t.grid_beat_bg = Color("#232323");
    t.grid_bar_fg = Color("#ffffff");
    t.grid_bar_bg = Color("#323232");
    t.cursor_tint = Color("#d0ecff");
    t.cursor_row_tint = Color("#90b8cc");
    t.command_column = Color("#c67610");
    t.text_on_track = Color("#ffffff");
    t.reader_text = Color("#c080c0");
    t.info_line_fg = Color("#1e1e1e");
    t.info_line_bg = Color("#787878");
    t.meter_clip = Color("#e01040");
    t.meter_active = Color("#10e040");
    t.meter_low = Color("#0b5f2a");
    t.meter_mid = Color("#0f9a3a");
    t.meter_lime = Color("#9ae02e");
    t.meter_warn = Color("#f09020");
    t.meter_hot = Color("#ff5a20");
    t.spectrum_low = Color("#1e5fd0");
    t.spectrum_high = Color("#7af0d8");
    t.mute = Color("#ff5a5a");
    t.solo = Color("#ffdc5a");
    t.monitor = Color("#5ad2ff");
    t.clip_override = Color("#ff9a3c");
    t.locator = Color("#e03030");
    t.locator_bg = Color("#702020");
    t.locator_mark = Color("#c8c8c8");
    t.button_fg = Color("#ffffff");
    t.button_bg = Color("#c04080");
    t.button_pressed_bg = Color("#7a2850");
    t.row_pressed_bg = Color("#5c7c8c");
    t.master_track = Color("#533918");
    return t;
  }

  // The modern look (colour values and their contrast checks: the modern
  // look token tables).
  static Theme dark() {
    Theme t = original();
    t.tracks = TrackPalette::modern();
    t.surface_base = Color("#0d1115");
    t.surface_panel = Color("#161a1e");
    t.surface_raised = Color("#21262b");
    t.surface_overlay = Color("#2d3238");
    t.text_primary = Color("#e7ecf0");
    t.text_secondary = Color("#acb2b7");
    t.text_disabled = Color("#70757a");
    t.text_on_accent = Color("#071417");
    t.accent = Color("#7acce0");
    t.accent_dim = Color("#234951");
    t.focus = t.accent;
    shareStates(t);
    t.grid_beat_fg = t.text_secondary;
    t.grid_beat_bg = t.surface_panel;
    t.grid_bar_fg = t.text_primary;
    t.grid_bar_bg = t.surface_raised;
    t.cursor_tint = Color("#bce5ef");
    t.cursor_row_tint = t.accent;
    t.command_column = Color("#d4963c");
    t.reader_text = t.accent;
    t.info_line_fg = t.text_secondary;
    t.info_line_bg = t.surface_raised;
    t.spectrum_low = Color("#2b5fa8");
    t.spectrum_high = t.accent;
    t.mute = Color("#e8746a");
    t.solo = Color("#e6ad00");
    t.monitor = t.accent;
    t.clip_override = Color("#e09036");
    t.locator = Color("#e4312f");
    t.locator_bg = Color("#4a2321");
    t.locator_mark = t.text_secondary;
    t.button_fg = t.text_on_accent;
    t.button_bg = t.accent;
    t.button_pressed_bg = Color("#4f9fb4");
    t.row_pressed_bg = Color("#2d5f6a");
    t.master_track = t.surface_raised;
    return t;
  }

  static Theme light() {
    Theme t = dark();
    t.surface_base = Color("#f0f4f7");
    t.surface_panel = Color("#e2e7eb");
    t.surface_raised = Color("#cfd5db");
    t.surface_overlay = Color("#fcfeff");
    t.text_primary = Color("#11171d");
    t.text_secondary = Color("#3a4148");
    t.text_disabled = Color("#5e646a");
    t.text_on_accent = Color("#f7fbfd");
    t.accent = Color("#006185");
    t.accent_dim = Color("#a2ccdd");
    t.focus = t.accent;
    t.grid_beat_fg = t.text_secondary;
    t.grid_beat_bg = t.surface_panel;
    t.grid_bar_fg = t.text_primary;
    t.grid_bar_bg = t.surface_raised;
    t.cursor_tint = t.accent;
    t.cursor_row_tint = t.accent;
    t.command_column = Color("#8a5200");
    t.reader_text = t.accent;
    t.info_line_fg = t.text_secondary;
    t.info_line_bg = t.surface_raised;
    t.spectrum_low = Color("#2b5fa8");
    t.spectrum_high = Color("#00849e");
    t.mute = Color("#b3261e");
    t.solo = Color("#8a6400");
    t.monitor = t.accent;
    t.clip_override = Color("#a35a00");
    t.locator = Color("#b3261e");
    t.locator_bg = Color("#f0c4c0");
    t.locator_mark = t.text_secondary;
    t.button_fg = t.text_on_accent;
    t.button_bg = t.accent;
    t.button_pressed_bg = Color("#004a66");
    t.row_pressed_bg = Color("#8ab8cc");
    t.master_track = t.surface_raised;
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
  // The colours that are the same in every modern theme: the Launchpad's
  // clip states, the text on a track colour and the meter ramp.
  static void shareStates(Theme & t) {
    t.state_playing = Color("#009722");
    t.state_playing_dim = Color("#668666");
    t.state_recording = Color("#e23532");
    t.state_recording_dim = Color("#a46a64");
    t.text_on_track = Color("#010204");
    t.meter_low = Color("#2d8949");
    t.meter_mid = Color("#429c44");
    t.meter_active = Color("#56b93e");
    t.meter_lime = Color("#e6ad00");
    t.meter_warn = Color("#f08a00");
    t.meter_hot = Color("#f76400");
    t.meter_clip = Color("#e4312f");
  }
};

}  // namespace cellui
