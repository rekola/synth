#ifndef _LAUNCHPADMANAGER_H_
#define _LAUNCHPADMANAGER_H_

#include "Tuning.h"
#include "LaunchpadProtocol.h"

#include <array>
#include <chrono>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class LaunchpadIO;
class Song;
class PlaybackInfo;
class Controller;
class LaunchpadPadEvent;
class LaunchpadChannelPressureEvent;

// Owns everything about how connected Launchpads relate to the song being
// edited: per-device track assignment and octave, the isomorphic/
// percussion layout math needed to map a pad to a note and to an LED
// color (via LaunchpadLayout), the active-note/redraw-row bookkeeping for
// chord-safe note entry, and (handlePadEvent()) the actual pattern edit
// itself. Deliberately self-sufficient - it only needs a Controller
// reference (never a PatternEditor/UI), so Launchpad note entry keeps
// working the same way whether or not any particular UI (or any UI at
// all - a headless setup) happens to exist; a caller with its own cursor/
// edit-step-size UI state (see UI::handleLaunchpadPadEvent) just passes
// those in as plain values. Musiceditor-only (depends on LaunchpadIO),
// like LaunchpadIO itself - the pure math it delegates to
// (LaunchpadLayout) stays in synth_engine and is unit-tested there.
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

  // DRAW mode only: registers a touch-down on pad (x,y) - starts (or, for a
  // hold-continuation resend, extends) its held-duration tracking and
  // rebases its brightness intensity to this press's own velocity. Does
  // NOT touch the pad's hue - that decision is deferred to releaseDrawPad(),
  // which needs to know how long the pad was held before it can decide
  // what the release should do. No-op for an out-of-range (x,y) or a
  // device not currently in DRAW mode.
  void pressDrawPad(int device_id, int x, int y, int velocity);

  // DRAW mode only: raises pad (x,y)'s brightness intensity to `velocity`
  // if that's higher than what's already stored, never lowers it - so
  // holding a pad and pressing harder mid-hold (aftertouch) brightens it
  // further, but easing off afterward doesn't dim it back down; the pad
  // keeps showing the loudest hit it ever received since it was last
  // pressed. No-op for an out-of-range (x,y) or a device not currently in
  // DRAW mode.
  void updateDrawIntensity(int device_id, int x, int y, int velocity);

  // DRAW mode only: the release half of pressDrawPad() - this is where the
  // hue decision actually happens, based on how long the pad was held and
  // whether it was already lit: pad was off -> lit with the default hue
  // (palette index 1) either way, short or long, since there's nothing yet
  // to just brighten; pad was already lit and released quickly -> cycles
  // to the next palette hue; pad was already lit and held past the
  // long-press threshold -> hue is left exactly as it was, since a long
  // hold means the user was only adjusting brightness (already live via
  // pressDrawPad/updateDrawIntensity), not choosing a new color. No-op for
  // an out-of-range (x,y) or a pad with no press currently in flight
  // (stray/duplicate release).
  void releaseDrawPad(int device_id, int x, int y);

  // The Pan row<->azimuth mapping (8 compass points, 45 degrees apart,
  // around the full circle - row 4 is dead-center/0 degrees front) - a
  // static, stateless pair so both the LED bargraph (refreshLeds) and the
  // pad-press handler (handlePadEvent) use the exact same convention
  // rather than two independently-declared copies.
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

  // CC97 (DRAW mode toggle) on its own, separate entry point: unlike every
  // button handleRawButton() covers, it needs both press and release to
  // tell a quick tap from a long hold. Released quickly, it toggles DRAW
  // mode on/off, same as before; held past a threshold and released while
  // DRAW mode is already active, it blanks the canvas instead (see
  // advanceDrawColor's own comment on the palette) - the button took over
  // this "clear canvas" gesture after CC99 (the grid position the
  // Programmer-mode protocol maps one past the top row) turned out not to
  // be an actual pressable button on real Launchpad X hardware, just a
  // CC-addressable LED kept for symmetry with the Launchpad Pro. Always
  // returns true (handled) for both press and release.
  bool handleDrawToggleButton(int device_id, bool is_press);

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

  // The actual pattern-editing entry point for Launchpad pad (note) input -
  // handles both grid-mode-as-fader (Send A/B/Main/Pan, mutating tracks via
  // Controller's existing setters) and normal NOTES-mode chord-safe note
  // entry (auto-creating tracks as needed, writing into the pattern,
  // pushing PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE/MOVE_POSITION playback
  // events). Only needs Controller (Song/PlaybackInfo/event queue) - no UI
  // of any kind - which is what lets Launchpad note entry work whether or
  // not a PatternEditor (or any UI at all) exists. fallback_track_index
  // and edit_step_size are supplied by the caller rather than read from
  // any UI cursor state (see UI::handleLaunchpadPadEvent, which passes
  // PatternEditor's own getCursorTrackIndex()/getEditStepSize() when a UI
  // happens to be present).
  void handlePadEvent(LaunchpadPadEvent & ev, Controller & controller, int fallback_track_index, int edit_step_size);

  // Device-wide aftertouch (the alternative to handlePadEvent's per-pad
  // AFTERTOUCH case - see LaunchpadChannelPressureEvent) - there's no
  // pad, so no single note_column/track to target the way per-pad
  // aftertouch has; instead this applies to every track this device
  // currently has a held note on (its active_notes bookkeeping), the same
  // "one shared pressure value, not per-note" semantics
  // InstrumentTrackState::broadcastChannelPressure() already gives poly
  // pressure once aggregated up to channel level.
  void handleChannelPressureEvent(LaunchpadChannelPressureEvent & ev, Controller & controller);

  // Called whenever the UI thread learns of a new playhead position (see
  // UI::handlePlaybackEvent, right after Controller::receivePlaybackSnapshot() -
  // that ordering matters, see this method's own definition) - while a
  // realtime auto-play-while-held recording session is active (see
  // auto_started_playback_), sweeps every row the playhead just passed
  // through and clears each currently-recorded track's notes there, so a
  // live take replaces whatever was previously on that stretch instead
  // of merging with it. A no-op outside such a session.
  void onRowAdvanced(Controller & controller);

  // Called once per render() frame: recomputes each ready device's LED
  // colors (base consonance-hierarchy/percussion palette plus a
  // brightness overlay for whatever notes are currently sounding on its
  // assigned track, from playback_info) and pushes an LED refresh only
  // when the resulting
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
    bool muted = false, solo = false;
    // note_value -> loudness (0..1) for the assigned track's currently
    // sounding notes, used to brighten pads above LAUNCHPAD_IDLE_BRIGHTNESS.
    std::unordered_map<int, float> active_note_loudness;

    // Record-arm toggle for this device (CC98, the physical "Capture
    // MIDI" button - see handleRawButton()) - defaults off: a freshly
    // connected Launchpad is just an instrument until the player
    // deliberately arms it, not a silent trap that overwrites pattern
    // data the moment you start experimenting. Gates pattern mutation
    // only (Note writes, song version bumps, the not-playing step-
    // advance MOVE_POSITION) - live PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE
    // audition events fire regardless, so this device is always audible
    // whether or not it's recording.
    bool capture_enabled = false;

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
    // (see releaseDrawPad/refreshLeds), independent of Song/Track state
    // entirely and of each other - x + y*8.
    std::array<int, 64> draw_color_index {};
    // DRAW mode: each pad's own current brightness intensity (0-127),
    // driven by the largest press velocity/aftertouch pressure seen since
    // that pad was last pressed - see pressDrawPad()/updateDrawIntensity()/
    // colorForDrawPad(). Rebased (not maxed) on a fresh press, since a
    // press starts a fresh intensity history, not a continuation of the
    // previous press's.
    std::array<int, 64> draw_intensity {};
    // DRAW mode: whether each pad currently has an unreleased press "in
    // flight" - see pressDrawPad()/releaseDrawPad(). Some Launchpad units
    // resend Note On instead of real Polyphonic Key Pressure while a pad is
    // held; without this, each resend would look like a brand new press and
    // restart the held-duration measurement releaseDrawPad() relies on to
    // tell a short click from a long press.
    std::array<bool, 64> draw_pad_held {};
    // DRAW mode: when each currently-held pad's press started (only
    // meaningful while draw_pad_held is true for that pad) - releaseDrawPad()
    // measures against this to decide short click (cycle the hue) vs. long
    // press (leave the hue alone, brightness-only).
    std::array<std::chrono::steady_clock::time_point, 64> draw_pad_press_time {};
    // CC97 (DRAW mode toggle) press/release tracking - see
    // handleDrawToggleButton() for why a tap and a long hold need to be
    // told apart.
    bool draw_toggle_pressed = false;
    std::chrono::steady_clock::time_point draw_toggle_press_time;

    // LED diff cache: refreshLeds() only calls sendLeds() when the newly
    // computed colors differ from what was last actually sent, so
    // brightness fades (which change every refresh() call while a note is
    // decaying) don't retrigger a send once the colors settle back to idle.
    std::vector<LaunchpadProtocol::PadColor> last_sent_colors;
  };

  DeviceState & deviceState(int device_id);
  const DeviceState * findDeviceState(int device_id) const;
  void refreshLeds(int device_id, DeviceState & state);

  // True iff some connected device both has capture_enabled and
  // currently has at least one held note - the realtime auto-play
  // trigger (see handlePadEvent()'s PRESS/RELEASE handling) recomputes
  // this fresh on every press/release rather than caching it per-note,
  // so toggling Capture mid-hold takes effect on the very next event
  // without needing to track "was this specific note captured."
  bool anyCaptureArmedNoteHeld() const;

  // True iff some currently-held note (on any device, not just the one
  // about to claim a column) already occupies (track_id, note_column) -
  // needed alongside the pattern's own defined-note check in
  // handlePadEvent()'s PRESS free-slot search: with Capture off, a held
  // note is never written to the pattern at all, so the pattern-only
  // check can't see it and would hand out the same "free" column to
  // every simultaneously-held note, each PLAY_NOTE then stealing the
  // previous one's voice via Player.cpp's stopVoices(column) - killing
  // polyphony entirely. Scans every device, not just the caller's own,
  // since two different Launchpads could be assigned to the same track.
  bool isColumnLiveHeld(int track_id, int note_column) const;

  // Clears (row, track_id)'s notes exactly once per recording session -
  // idempotent (checked against auto_record_cleared_rows_) so it's safe
  // to call defensively from every write site during an active session
  // (a fresh press, a release's explicit off-write, an aftertouch write,
  // and onRowAdvanced()'s own sweep) without worrying about which one
  // gets there first or double-clearing.
  void ensureRowCleared(Song & song, int pattern_idx, int row, int track_id);

  LaunchpadIO * launchpad_io_ = nullptr;
  std::map<int, DeviceState> devices_;

  // Whether this code (not the user manually pressing Space) was the one
  // that pushed PLAY for the realtime-advance-while-held feature - only
  // set when a capture-armed device's held-note count goes 0->1 while
  // the transport wasn't already running; only consulted (and cleared)
  // when it goes back to 0, so a manually-started session is never
  // stopped just because a Launchpad note happened to be released. One
  // instance-wide flag, not per-device - there is only one shared
  // playhead/transport (see the plan's "separate playheads" caveat).
  bool auto_started_playback_ = false;

  // Which (row, track_id) pairs have already been cleared this recording
  // session (see ensureRowCleared) - reset whenever a fresh session
  // starts (auto_started_playback_ false -> true). last_cleared_row_/
  // last_cleared_pattern_idx_ track how far onRowAdvanced()'s sweep has
  // already reached, so it only clears newly-passed rows, not the whole
  // pattern on every call - reset the same way, and also resynced
  // (rather than trying to backfill a range) if the pattern index itself
  // changes or the row goes backwards (a loop/pattern-sequence
  // wraparound), since a range spanning that boundary has no single
  // well-defined meaning here.
  std::set<std::pair<int, int>> auto_record_cleared_rows_;
  int last_cleared_row_ = -1;
  int last_cleared_pattern_idx_ = -1;
};

#endif
