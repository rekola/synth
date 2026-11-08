#pragma once

#include "Theme.h"
#include <cellui/Plane.h>
#include <cellui/StyleProvider.h>

#include <algorithm>
#include <cmath>

// cellui's generic colours plus the tracker's own (SynthPalette). Every plane
// of the app is made with one of these, so a widget that needs a tracker role
// takes it from the plane with UIElement::synthStyles().
class StyleProvider : public cellui::StyleProvider {
 public:
  StyleProvider() { applyTheme(Theme::dark()); }
  explicit StyleProvider(const Theme & initial) { applyTheme(initial); }

  // Sets every colour from the theme's tokens.
  void applyTheme(const Theme & t) {
    cellui::StyleProvider::applyTheme(t.base);
    const SynthPalette & p = t.palette;
    tracks = p.tracks;
    grid_lines = p.grid_lines;
    track_chips = p.track_chips;
    grid_bg_color = p.grid_lines ? t.base.surface_base : t.base.surface_panel;
    window_beat_accent_fg_color = p.grid_beat_fg;
    window_beat_accent_bg_color = p.grid_beat_bg;
    window_bar_accent_fg_color = p.grid_bar_fg;
    window_bar_accent_bg_color = p.grid_bar_bg;
    clip_playing_color = p.state_playing;
    clip_paused_color = p.state_playing_dim;
    clip_recording_color = p.state_recording;
    clip_armed_color = p.state_recording_dim;
    command_column_color = p.command_column;
    clip_text_color = p.text_on_track;
    mute_color = p.mute;
    solo_color = p.solo;
    monitor_color = p.monitor;
    clip_override_color = p.clip_override;
    locator_color = p.locator;
    locator_bg_color = p.locator_bg;
    locator_mark_color = p.locator_mark;
    master_track_color = p.master_track;
    data_fx_color = p.data_fx;
    data_velocity_color = p.data_velocity;
    data_delay_color = p.data_delay;
    comma_up_color = p.comma_up;
    comma_down_color = p.comma_down;
  }

  // Whether grid columns have a line between them, and the colour of a
  // column's body: the window's, or a panel surface over the gutters.
  bool grid_lines = true;
  // Track headers carry the track's colour as a slice, not a block.
  bool track_chips = false;
  Color grid_bg_color;

  // A track's identity colour, from the theme's palette.
  TrackPalette tracks;
  Color trackColor(int ordinal) const { return tracks.color(ordinal); }
  // The slice of it on a track header: a shade toward the text colour, so it
  // stays apart from the track's colour on a clip slot below.
  Color trackChipColor(int ordinal) const { return tracks.color(ordinal).blend(0.3f, window_accent_fg_color); }

  // A note that has just started is lit past white (linear light, 1 = SDR white);
  // each display decides what that looks like, and the light falls back to
  // normal over kNoteFlashSeconds.
  static constexpr float kNoteFlashSeconds = 0.7f;
  // 1 at the moment of the attack, falling to 0 after kNoteFlashSeconds; a reduced-motion
  // display gets the first half at full strength and then nothing.
  static float noteFlash(double age_seconds, bool reduced_motion) {
    if (age_seconds < 0.0 || age_seconds >= kNoteFlashSeconds) return 0.0f;
    float left = 1.0f - static_cast<float>(age_seconds / kNoteFlashSeconds);
    return reduced_motion ? (left > 0.5f ? 1.0f : 0.0f) : left;
  }
  // How much of a full flash a note of this velocity (0 to 127) gets: three quarters, up to all.
  // Narrow, so a quiet note is still lit as far as a louder one for long enough to be seen.
  static float velocityFlash(int velocity) { return 0.75f + 0.25f * static_cast<float>(std::clamp(velocity, 0, 127)) / 127.0f; }
  // How lit a colour looks, as one number that rises evenly: its brightest channel up to white,
  // and past white, where a display can only show it paler, 1 plus how pale (1 - 1/channel). It
  // is smooth through white, and a colour's visible brightness and paleness both follow it.
  static float intensity(float peak) { return peak <= 1.0f ? peak : 2.0f - 1.0f / peak; }
  static float peakFor(float intensity) { return intensity <= 1.0f ? intensity : 1.0f / (2.0f - std::min(intensity, 1.99f)); }
  // The intensity a note is lit to at its full flash (a peak channel of 5, a colour 80% white).
  static constexpr float kNoteFlashIntensity = 1.8f;
  // `c` lit by `flash` (0 to 1, velocity included): its intensity moves in a straight line from its
  // own to kNoteFlashIntensity, so the brightness and the paleness a display shows fall evenly as
  // the flash does, with no stretch where nothing seems to change.
  static Color lit(const Color & c, float flash) {
    float peak = std::max({c.r(), c.g(), c.b()});
    if (!(flash > 0.0f) || !(peak > 0.0f)) return c;
    float own = intensity(peak);
    float lit_to = own + (std::max(own, kNoteFlashIntensity) - own) * std::clamp(flash, 0.0f, 1.0f);
    return c * (peakFor(lit_to) / peak);
  }

  // The clip grid header's track flags when on: Mute, Solo, and a Monitor
  // set to In (Auto shows in window_fg_color, Off in window_border_color).
  Color mute_color;
  Color solo_color;
  Color monitor_color;
  // The mark on a track Live View has taken over from the arrangement.
  Color clip_override_color;

  // A clip slot's state glyph in the clip grid (ClipHighlight):
  // playing or queued; launched while the transport is paused; recording
  // or queued to record; an armed track's empty slot, or a take queued to
  // stop.
  Color clip_playing_color;
  Color clip_paused_color;
  Color clip_recording_color;
  Color clip_armed_color;

  // The pattern editor's beat rows, and its bar rows (Song::
  // getRowsPerBar()) a step above them. Both stay well below the cursor
  // row's tint, so the cursor still stands out on an accented row.
  Color window_beat_accent_fg_color;
  Color window_beat_accent_bg_color;
  Color window_bar_accent_fg_color;
  Color window_bar_accent_bg_color;

  Color command_column_color;

  // Text drawn on a clip's own track color (a clip's name, a placed
  // instance's digit).
  Color clip_text_color;

  // Pattern editor locators.
  Color locator_color;
  Color locator_bg_color;
  // The arrangement grid's mark for a bar with a locator.
  Color locator_mark_color;

  // The pattern editor's master-track ancestor row (a whole title bar, not
  // text on a dark background).
  Color master_track_color;

  // The pattern editor's data columns: local fx, velocity, delay.
  Color data_fx_color;
  Color data_velocity_color;
  Color data_delay_color;
  // The comma arrows of a note name.
  Color comma_up_color;
  Color comma_down_color;
};

// The app's styles behind a plane: every plane of the app is made with one.
inline const StyleProvider & synthStyles(const cellui::Plane & plane) { return static_cast<const StyleProvider &>(plane.getStyles()); }
