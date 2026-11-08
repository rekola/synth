#pragma once

#include "../model/Color.h"
#include <cellui/Theme.h>

#include <cmath>
#include <string>

// How a track's identity colour is made from its ordinal: the original steps
// the HSL hue by the golden angle at a fixed saturation and lightness. The
// modern one has one OKLCH lightness and, for each of the first eight tracks,
// a hue and a chroma picked to keep the closest two as different as can be on
// the screen and on the Launchpad's pads, with the hues clear of the clip
// state colours: one colour per family (orange, olive, cyan, blue, purple,
// pink) and two greys, without the gamut's pure blue corner;
// later tracks step the hue by the golden ratio over the arcs the states leave
// free, so a track never looks like a playing or a recording clip.
struct TrackPalette {
  enum class Model { GOLDEN_HSL,
                     OKLCH_TABLE };
  Model model = Model::GOLDEN_HSL;
  float lightness = 0.42f;
  float saturation_or_chroma = 0.35f;

  static TrackPalette original() { return {Model::GOLDEN_HSL, 0.42f, 0.35f}; }
  static TrackPalette modern() { return {Model::OKLCH_TABLE, 0.585f, 0.12f}; }

  // The model's own hue, in degrees.
  float hue(int ordinal) const {
    if (model == Model::GOLDEN_HSL) return std::fmod(static_cast<float>(ordinal) * 137.50776f, 360.0f);
    if (ordinal < kTableSize) return kTable[ordinal].hue;
    constexpr float arcs[2][2] = {{62.0f, 110.0f}, {180.0f, 352.0f}};
    float t = std::fmod(0.5f + static_cast<float>(ordinal - kTableSize) * 0.6180339887f, 1.0f);
    float pos = t * ((arcs[0][1] - arcs[0][0]) + (arcs[1][1] - arcs[1][0]));
    for (auto & arc : arcs) {
      if (pos <= arc[1] - arc[0]) return arc[0] + pos;
      pos -= arc[1] - arc[0];
    }
    return arcs[1][1];
  }
  float chroma(int ordinal) const {
    return model == Model::OKLCH_TABLE && ordinal < kTableSize && ordinal >= 0 ? kTable[ordinal].chroma : saturation_or_chroma;
  }
  Color color(int ordinal) const {
    return model == Model::GOLDEN_HSL ? Color::fromHSL(hue(ordinal), saturation_or_chroma, lightness)
                                      : Color::fromOklchClipped(lightness, chroma(ordinal), hue(ordinal));
  }
  // The same colour's HSL hue, and the saturation a pad shows it with: a pad
  // has only its primaries, so a muted colour is shown less saturated.
  float hslHue(int ordinal) const { return color(ordinal).hslHue(); }
  float padSaturation(int ordinal) const {
    if (model == Model::GOLDEN_HSL) return 0.8f;
    return std::fmin(1.0f, 0.35f + chroma(ordinal) / 0.17f * 0.55f);
  }

 private:
  struct Entry {
    float hue, chroma;
  };
  static constexpr int kTableSize = 8;
  static constexpr Entry kTable[kTableSize] = {
      {205, 0.14f},
      {344, 0.08f},
      {108, 0.19f},
      {263, 0.12f},
      {108, 0.04f},
      {308, 0.12f},
      {69, 0.19f},
      {230, 0.04f},
  };
};

// The colour roles that exist only because this is a tracker: clip and
// transport states, the pattern editor's rows and data columns, track flags,
// locators, note-name arrows. cellui's Theme has the generic roles.
struct SynthPalette {
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

  // Columns of a grid are told apart by a line between them; without lines
  // each column body is a panel surface with a gutter of the base surface
  // between. (The tracker view keeps its lines either way.)
  bool grid_lines = true;

  // A track header is the raised surface with a narrow slice of the track's
  // colour at its left, instead of a block of that colour.
  bool track_chips = false;

  Color command_column;
  Color text_on_track;
  // Track flags, the mark on a track Live View has taken over, locators and
  // the master track's bar.
  Color mute;
  Color solo;
  Color monitor;
  Color clip_override;
  Color locator;
  Color locator_bg;
  Color locator_mark;
  Color master_track;
  // The pattern editor's data columns: local fx, velocity, delay.
  Color data_fx;
  Color data_velocity;
  Color data_delay;
  // The comma arrows of a note name (↑ ↓ and the sharps and flats that carry one).
  Color comma_up;
  Color comma_down;

  static SynthPalette original() {
    SynthPalette p;
    p.state_playing = Color("#50e070");
    p.state_playing_dim = Color("#2e7a3e");
    p.state_recording = Color("#ff3c3c");
    p.state_recording_dim = Color("#a04040");
    p.grid_beat_fg = Color("#c4c4c4");
    p.grid_beat_bg = Color("#232323");
    p.grid_bar_fg = Color("#ffffff");
    p.grid_bar_bg = Color("#323232");
    p.command_column = Color("#c67610");
    p.text_on_track = Color("#ffffff");
    p.mute = Color("#ff5a5a");
    p.solo = Color("#ffdc5a");
    p.monitor = Color("#5ad2ff");
    p.clip_override = Color("#ff9a3c");
    p.locator = Color("#e03030");
    p.locator_bg = Color("#702020");
    p.locator_mark = Color("#c8c8c8");
    p.master_track = Color("#533918");
    p.data_fx = Color("#d65cd6");
    p.data_velocity = Color("#bfa426");
    p.data_delay = Color("#42c1ea");
    p.comma_up = Color("#ff8a3d");
    p.comma_down = Color("#5ea0ff");
    return p;
  }

  // The modern look, given the generic theme it goes with (some roles are
  // that theme's own tones).
  static SynthPalette dark(const cellui::Theme & t) {
    SynthPalette p = original();
    p.tracks = TrackPalette::modern();
    p.grid_lines = false;
    p.track_chips = true;
    p.state_playing = Color("#009722");
    p.state_playing_dim = Color("#668666");
    p.state_recording = Color("#e23532");
    p.state_recording_dim = Color("#a46a64");
    p.text_on_track = Color("#010204");
    p.grid_beat_fg = t.text_secondary;
    p.grid_beat_bg = t.surface_panel;
    p.grid_bar_fg = t.text_primary;
    p.grid_bar_bg = t.surface_raised;
    p.command_column = Color("#d4963c");
    p.mute = Color("#e8746a");
    p.solo = Color("#e6ad00");
    p.monitor = t.accent;
    p.clip_override = Color("#e09036");
    p.locator = t.text_primary;
    p.locator_bg = t.surface_raised;
    p.locator_mark = t.text_secondary;
    p.master_track = t.surface_overlay;
    p.data_fx = Color("#c58be0");
    p.data_velocity = Color("#d4b03c");
    p.data_delay = Color("#6cc4e0");
    p.comma_up = Color("#ff9a52");
    p.comma_down = Color("#6aa8ff");
    return p;
  }

  static SynthPalette light(const cellui::Theme & t) {
    SynthPalette p = dark(t);
    p.grid_bar_bg = Color("#dde0e7");
    p.command_column = Color("#c76a00");
    p.mute = Color("#ff3b30");
    p.solo = Color("#ff9500");
    p.clip_override = Color("#d97706");
    p.data_fx = Color("#9b3fc7");
    p.data_velocity = Color("#8a5a00");
    p.data_delay = Color("#0070a0");
    p.comma_up = Color("#b84a00");
    p.comma_down = Color("#1f55c4");
    return p;
  }
};

// A generic theme with the tracker's palette that goes with it.
struct Theme {
  cellui::Theme base;
  SynthPalette palette;

  static Theme original() { return {cellui::Theme::original(), SynthPalette::original()}; }
  static Theme dark() {
    auto t = cellui::Theme::dark();
    return {t, SynthPalette::dark(t)};
  }
  static Theme light() {
    auto t = cellui::Theme::light();
    return {t, SynthPalette::light(t)};
  }

  // The names a setting or command line can give.
  static bool byName(const std::string & name, Theme & out) {
    if (name == "original")
      out = original();
    else if (name == "dark")
      out = dark();
    else if (name == "light")
      out = light();
    else
      return false;
    return true;
  }
};
