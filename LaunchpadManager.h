#ifndef _LAUNCHPADMANAGER_H_
#define _LAUNCHPADMANAGER_H_

#include "Tuning.h"
#include "LaunchpadProtocol.h"

#include <array>
#include <map>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class LaunchpadIO;
class Song;
class PlaybackInfo;

// Owns everything about how connected Launchpads relate to the song being
// edited: per-device track assignment and octave, the isomorphic/
// percussion layout math needed to map a pad to a note and to an LED
// color (via LaunchpadLayout), and the active-note/redraw-row bookkeeping
// for chord-safe note entry. PatternEditor still performs the actual
// pattern edit (Song/Cursor/Pattern access stays its job) - this class
// owns Launchpad *layout*, keyed per physical device, not pattern
// editing. Musiceditor-only (depends on LaunchpadIO), like LaunchpadIO
// itself - the pure math it delegates to (LaunchpadLayout) stays in
// synth_engine and is unit-tested there.
class LaunchpadManager {
 public:
  void setLaunchpadIO(LaunchpadIO * io) { launchpad_io_ = io; }

  struct ActiveNote {
    int note_column;
    int row, track_id;
    int last_aftertouch_value = -1;
  };

  ActiveNote * findActiveNote(int device_id, int x, int y);
  void recordActiveNote(int device_id, int x, int y, ActiveNote note);
  void clearActiveNote(int device_id, int x, int y);
  bool hasAnyActiveNotes(int device_id) const;

  // Pattern row a Launchpad aftertouch/press write landed on when it isn't
  // the current playback row - see PatternEditor::render()'s incremental
  // redraw. A single shared value (not per-device): it names a row in the
  // shared pattern grid, not a per-device concept.
  int extraRedrawRow() const { return extra_redraw_row_; }
  void setExtraRedrawRow(int row) { extra_redraw_row_ = row; }
  void clearExtraRedrawRow() { extra_redraw_row_ = -1; }

  int octave(int device_id) const;
  void octaveUp(int device_id);
  void octaveDown(int device_id);

  // Returns the track *index* (into whatever track_ids vector the caller
  // is using, not a raw internal track id) this device should act on:
  // fallback_track_index if the device has never been explicitly
  // assigned one of its own.
  int assignedTrackIndex(int device_id, int fallback_track_index) const;

  // Advances this device's own assigned track index by delta (+1/-1),
  // seeding from fallback_track_index the first time (see
  // LaunchpadLayout::advanceTrackIndex) - the moment a device "detaches"
  // from following the shared fallback and starts tracking its own.
  void advanceTrack(int device_id, int delta, int fallback_track_index, int num_tracks);

  // Resolves which track_id this device's assigned track currently is
  // (falling back to fallback_track_index, same convention as
  // assignedTrackIndex/refresh) - -1 if track_ids is empty or the
  // resolved index still somehow ends up out of range. The one place
  // "device -> target track_id" is computed, so a caller (the Launchpad
  // command-dispatch path) doesn't need to duplicate assignedTrackIndex's
  // own bounds-clamping.
  int resolveTrackId(int device_id, const std::vector<int> & track_ids, int fallback_track_index) const;

  // Which of the 8x8 grid's meanings a device is currently showing - normal
  // note entry, a per-track SendA/SendB/SendMain fader, a per-track Pan
  // (azimuth) control, or DRAW (a plain coloring toy - see advanceDrawColor
  // - no Song/Track meaning at all): for SEND_A/SEND_B/SEND_MAIN the row
  // pressed within a column sets that (first-8-root-track) column's send
  // level (a bargraph, filled bottom-up); for PAN it sets that column's
  // azimuth to one of 8 compass points around the full circle (only the
  // one matching row lights up - a direction, not a magnitude, so a fill
  // doesn't make sense). Mutually exclusive - toggling one mode off
  // (pressing its own button again) or switching directly to another
  // always returns/moves to exactly one state.
  // Ordered to match the physical buttons' own row order (Volume/Pan/Send
  // A/Send B, CC 89/79/69/59 - see handleRawButton()'s own comment), not
  // declaration-arbitrary.
  enum class GridMode { NOTES, SEND_MAIN, PAN, SEND_A, SEND_B, DRAW };
  GridMode gridMode(int device_id) const;
  void toggleGridMode(int device_id, GridMode mode);

  // DRAW mode only: advances pad (x,y)'s own color to the next one in a
  // fixed palette (wrapping back to off), independent of every other pad -
  // a plain coloring toy, not tied to any Song/Track data, for whoever
  // just wants to press buttons and watch them light up. No-op for an
  // out-of-range (x,y) or a device not currently in DRAW mode.
  void advanceDrawColor(int device_id, int x, int y);

  // The Pan row<->azimuth mapping (8 compass points, 45 degrees apart,
  // around the full circle - row 4 is dead-center/0 degrees front) - a
  // static, stateless pair so both the LED bargraph (refreshLeds) and a
  // pad-press handler (PatternEditor::handleLaunchpadPadEvent) use the
  // exact same convention rather than two independently-declared copies.
  static int azimuthToRow(float azimuth);
  static float rowToAzimuth(int row);

  // The Send A/Pan/Send B/Volume/Custom buttons (raw CC 69/79/59/89/97 -
  // 69/79/89 confirmed against a real Launchpad X, 59 inferred from
  // Ableton's standard Launchpad "Track" control row order, 97 inferred
  // from the top row's own Up/Down/Left/Right/Session/Note/Custom/Capture
  // layout - 91/92/93/94 already confirmed as Up/Down/Left/Right; Volume/
  // CC89 is repurposed as the Send Main fader mode, the same shape as
  // Send A/Send B) are intercepted here, by raw CC number, *before* any
  // command-name resolution happens at all (see UI::
  // handleLaunchpadButtonEvent) - pressing one only ever flips this one
  // device's own transient grid-display state, never Song/Track data, so
  // it isn't a "command" (which implies "reachable identically from a
  // keybinding or M-x") at all, just a direct hardware-state toggle.
  // Returns false for any other CC, so the caller proceeds to the normal
  // command pipeline.
  bool handleRawButton(int cc_number, int device_id);

  // Handles this device's own pure per-device commands - octave and
  // track-follow navigation - entirely from LaunchpadManager's own state,
  // no Song/Track access needed. Returns false for any other command
  // name: Song/Track-mutating commands like "toggle-mute" are defined
  // centrally instead (see PatternEditor's commands_) and reached via
  // UI::handleLaunchpadButtonEvent's generic executeCommand() fallback
  // once this declines the name - the same CommandRegistry lookup a
  // keybinding or M-x invocation goes through.
  bool handleCommand(std::string_view name, int device_id, int fallback_track_index, int num_tracks);

  // Resolves the note this pad currently maps to for this device (using
  // its own octave and the given track's tuning/the song's key). Returns
  // -1 for an inactive percussion pad (row 7) or a degenerate/unpitched
  // tuning - callers should treat that as "ignore this press".
  int resolveNote(const Song & song, int device_id, int track_id, int x, int y) const;

  // Called once per render() frame: recomputes each ready device's LED
  // colors (base Fokker/percussion palette plus a brightness overlay for
  // whatever notes are currently sounding on its assigned track, from
  // playback_info) and pushes an LED refresh only when the resulting
  // colors actually changed - the multi-device generalization of the old
  // single-device diff block. track_ids is whatever root-track-id list
  // the caller already computed (avoids this class needing to know how
  // to walk the track tree itself); fallback_track_index is used for any
  // device that hasn't been explicitly assigned a track of its own.
  void refresh(const Song & song, const std::vector<int> & track_ids, const PlaybackInfo & playback_info, int fallback_track_index);

 private:
  struct DeviceState {
    int assigned_track_id = -1; // index into track_ids, or -1 = unassigned
    int octave = 4;
    std::map<std::pair<int, int>, ActiveNote> active_notes;

    // Inputs refreshLeds() needs to compute this device's colors.
    bool connected = false;
    Tuning tuning = Tuning::TET12;
    int key = -1;
    bool playing = false, muted = false, solo = false;
    // note_value -> loudness (0..1) for the assigned track's currently
    // sounding notes, used to brighten pads above LAUNCHPAD_IDLE_BRIGHTNESS.
    std::unordered_map<int, float> active_note_loudness;

    GridMode grid_mode = GridMode::NOTES;
    // First 8 root tracks' current SendMain/SendA/SendB/azimuth - refreshed
    // every frame (refresh()), same as muted/solo above, so the fader/pan
    // display always reflects the live value even before any pad press
    // (e.g. right after opening a mode). Only meaningful/painted when
    // grid_mode selects the matching one.
    std::array<float, 8> track_send_main {}, track_send_a {}, track_send_b {}, track_azimuth {};
    // How many of the 8 columns actually have a track behind them (0-8) -
    // a column past this has no real value to show (its array slot is
    // just a stale/default 0.0f, not "this track's level is 0"), so
    // refreshLeds must go fully dark there rather than painting whatever a
    // default value happens to map to (row 0 for Send A/B, dead-center for
    // Pan - both misleadingly "lit").
    int grid_track_count = 0;

    // DRAW mode: each of the 64 pads' own index into the color palette
    // (see advanceDrawColor/refreshLeds), independent of Song/Track state
    // entirely and of each other - x + y*8.
    std::array<int, 64> draw_color_index {};

    // LED diff cache: refreshLeds() only calls sendLeds() when the newly
    // computed colors differ from what was last actually sent, so
    // brightness fades (which change every refresh() call while a note is
    // decaying) don't retrigger a send once the colors settle back to idle.
    std::vector<LaunchpadProtocol::PadColor> last_sent_colors;
  };

  DeviceState & deviceState(int device_id);
  const DeviceState * findDeviceState(int device_id) const;
  void refreshLeds(int device_id, DeviceState & state);

  LaunchpadIO * launchpad_io_ = nullptr;
  std::map<int, DeviceState> devices_;
  int extra_redraw_row_ = -1;
};

#endif
