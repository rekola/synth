#ifndef _LAUNCHPADMANAGER_H_
#define _LAUNCHPADMANAGER_H_

#include "../instruments/Tuning.h"
#include "LaunchpadProtocol.h"
#include "LaunchpadTiming.h"

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
class DrumMachineTrack;

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

  // Resolved absolute octave: the global octave (Controller::
  // getGlobalOctave(), cached once per refresh() - see
  // cached_global_octave_) plus this device's own octave_offset, clamped
  // to [constants::MIN_OCTAVE, constants::MAX_OCTAVE].
  int octave(int device_id) const;
  // Adjust this device's own octave_offset by +/-1 - relative to the
  // global octave, not an absolute octave shift.
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

  // The Send A/B/Main fader rows<->dB mapping - row 0 is a hard "off"
  // (matching a real mixing console's fader bottoming out at true silence,
  // not just a very quiet dB value), rows 1-7 span a floor up to 0dB/unity
  // at row 7 in even steps. Same static, stateless pair convention as
  // azimuthToRow/rowToAzimuth above: the pad-press handler and refreshLeds'
  // own bargraph readback (which only ever sees the linear gain
  // InstrumentTrack::getSends() stores) both go through these, so a press
  // and its own LED redraw always agree.
  static float sendRowToDb(int row);
  static int sendLinearToRow(float linear);

  // The Send A/Pan/Send B/Volume/Custom/Record-Arm buttons (raw CC
  // 69/79/59/89/97/19 - 69/79/89 confirmed against a real Launchpad X, 59
  // inferred from Ableton's standard Launchpad "Track" control row order,
  // 97 inferred from the top row's own Up/Down/Left/Right/Session/Note/
  // Custom/Capture layout, 19 inferred by continuing that same right-
  // column row order one further (Volume/Pan/SendA/SendB/Stop
  // Clip/Mute/Solo/Record Arm - see LaunchpadProtocol::commandForButton's
  // own comment for Mute/Solo at 39/29) - 91/92/93/94 already confirmed as
  // Up/Down/Left/Right; Volume/CC89 is repurposed as the Send Main fader
  // mode, the same shape as Send A/Send B) are intercepted here, by raw CC
  // number, *before* any command-name resolution happens at all (see UI::
  // handleLaunchpadButtonEvent) - pressing one only ever flips this one
  // device's own transient grid-display state, never Song/Track data, so
  // it isn't a "command" (which implies "reachable identically from a
  // keybinding or M-x") at all, just a direct hardware-state toggle.
  // Returns false for any other CC, so the caller proceeds to the normal
  // command pipeline. CC98 ("Capture MIDI") no longer does anything here -
  // the record-arm toggle moved to CC19 ("Record Arm") - see
  // DeviceState::capture_enabled's own comment. CC97 ("Custom") is the
  // drum-picker latch (plans/drum-machine.md, Phase 6) - unconditional,
  // the same way Send/Pan/etc. above are: picking is only ever meaningful
  // once a DrumMachineTrack is actually assigned, but the toggle itself
  // is plain per-device UI state regardless of what's currently assigned,
  // matching every other raw-CC toggle here.
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
  // returns true (handled) for both press and release. No longer reached
  // directly from CC97 (see handleStopClipButton() - Custom now owns the
  // drum picker unconditionally, so DRAW moved to Stop Clip) but kept
  // under its original name since the toggle-vs-long-hold-clears logic
  // itself is unchanged.
  bool handleDrawToggleButton(int device_id, bool is_press);

  // CC49 ("Stop Clip" physical button)'s dispatcher (plans/drum-machine.md,
  // Phase 6/7): its meaning depends on what this device is currently
  // assigned to. When `assigned_drum_track` is non-null, it's the drum
  // machine's own Clear gesture, double-press to confirm: a first press
  // arms a short confirm window (kClearConfirmWindow - see refreshLeds()'s
  // own blinking-indicator comment), a second press within that window
  // clears every one of that track's lanes' step data back to all-rest
  // (the lane list itself is untouched - only the picker removes lanes),
  // and letting the window lapse (or an already-stale arm) just re-arms
  // rather than clearing - there's deliberately no separate "pressing any
  // other button cancels the arm" mechanism (that would mean threading a
  // cancel call through every other input path in this class for an edge
  // case the timeout already covers in practice: by the time a player
  // returns to Stop Clip after doing something else, the window has
  // essentially always lapsed). Clear writes unconditionally regardless
  // of Record Arm, matching the step grid/picker's own "arm gates
  // performance capture, not editing" rule. When `assigned_drum_track` is
  // null, this is DRAW mode's toggle instead (moved here from Custom,
  // which the drum picker now owns unconditionally - see
  // handleRawButton()'s own comment), forwarded to handleDrawToggleButton()
  // unchanged, long-hold-clears-canvas gesture included. Needs both press
  // and release for that same reason handleDrawToggleButton() does (the
  // Clear gesture itself only acts on press - release is a no-op there).
  // Always returns true (handled) for both press and release.
  bool handleStopClipButton(int device_id, bool is_press, DrumMachineTrack * assigned_drum_track, Controller & controller);

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
  // `controller` is only needed for the free-running drum-machine
  // audition clock below (to reach the playback event queue) - every
  // other per-device computation here still only touches `song`/
  // `playback_info` directly, unchanged from before that clock existed.
  void refresh(const Song & song, const std::vector<int> & track_ids, const PlaybackInfo & playback_info, int fallback_track_index, Controller & controller);

  // Called whenever the active buffer changes (Controller::
  // setBufferChangeListener()'s UI.cpp wiring): resets every connected
  // device back to "unassigned" (falls back to whatever fallback_track_index
  // refresh()/resolveTrackId() are passed that frame) rather than leaving
  // assigned_track_id pointing at an index carried over from the old song.
  // Left alone, that index would still be in-bounds whenever the new song
  // happens to have at least as many root tracks as the old one, silently
  // reassigning the device to a different, unrelated track instead of
  // erroring out or visibly resetting - worse than simply going out of
  // range, which resolveTrackId() already falls back on safely.
  void resetTrackAssignments();

 private:
  struct DeviceState {
    int assigned_track_id = -1; // index into track_ids, or -1 = unassigned
    // Relative to Controller::getGlobalOctave(), not an absolute octave -
    // 0 means "follow the global octave exactly". See
    // LaunchpadManager::octave()/cached_global_octave_.
    int octave_offset = 0;
    std::map<std::pair<int, int>, ActiveNote> active_notes;

    // Inputs refreshLeds() needs to compute this device's colors.
    bool connected = false;
    Tuning tuning = Tuning::TET12;
    int key = -1;
    bool muted = false, solo = false;
    // note_value -> loudness (0..1) for the assigned track's currently
    // sounding notes, used to brighten pads above LAUNCHPAD_IDLE_BRIGHTNESS.
    std::unordered_map<int, float> active_note_loudness;

    // Record-arm state, mirrored here from LaunchpadManager's single
    // song-wide capture_enabled_ (see that member's own comment) every
    // refresh() call - reading it per-device like this keeps every other
    // use site (handlePadEvent, anyCaptureArmedNoteHeld(), refreshLeds())
    // unchanged even though there's really only one flag for the whole
    // session, not one per Launchpad. Defaults off: a freshly connected
    // Launchpad is just an instrument until the player deliberately arms
    // recording, not a silent trap that overwrites pattern data the
    // moment you start experimenting. Gates pattern mutation only (Note
    // writes, song version bumps, the not-playing step-advance
    // MOVE_POSITION) - live PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE audition
    // events fire regardless, so every device is always audible whether
    // or not recording is armed. Originally CC98 ("Capture MIDI"), moved
    // to CC19 ("Record Arm") - plans/drum-machine.md's own rationale:
    // "armed" names the distinction exactly, and Record Arm sits away
    // from the top-row arrow cluster used for track selection, unlike
    // Capture MIDI - a mis-hit there used to silently arm writes with no
    // undo. Also gates whether "free playing" (ordinary note entry) is
    // captured, per the drum-machine step grid's own rule that the step
    // grid and drum picker write in *both* arm states - only free playing
    // is gated.
    bool capture_enabled = false;

    GridMode grid_mode = GridMode::NOTES;

    // Step-grid surface (plans/drum-machine.md, Phase 5): not a GridMode
    // value of its own - it displays automatically whenever this device's
    // assigned track is a DrumMachineTrack, the same way the percussion
    // layout below already displays automatically from track type rather
    // than a mode toggle, and (like percussion) only within grid_mode==
    // NOTES, so Send/Pan/Draw stay fully usable on a device currently
    // assigned to a drum machine. Recomputed fresh every refresh() call
    // (same cadence as tuning/muted/solo above), never read back from a
    // stale copy by handlePadEvent() - a press always re-resolves the
    // assigned DrumMachineTrack directly for up-to-the-moment lane/step
    // data, this cache exists purely for refreshLeds()'s drawing.
    bool assigned_track_is_drum_machine = false;
    std::vector<int> drum_lane_notes; // bottom-to-top, already DrumRankTable-ordered
    std::array<uint8_t, 8> drum_lane_steps {}; // parallel to drum_lane_notes
    int drum_loop_length = 8;
    // Pattern-relative row % loop length while playing, or the free-
    // running audition clock's own step % loop length while stopped (see
    // LaunchpadManager::audition_clock_step_) - or -1 when there's no
    // playhead to show at all (stopped, but the audition clock isn't
    // currently running because Record Arm is on - see refresh()'s own
    // gating check).
    int drum_playhead_step = -1;

    // Stop-Clip Clear double-press confirm state (plans/drum-machine.md,
    // Phase 7) - see handleStopClipButton()'s own comment for the full
    // arm/confirm/timeout rule; ConfirmTimer itself (LaunchpadTiming.h) is
    // the pure, unit-tested arm/confirm/timeout logic. Purely per-device
    // (unlike Record Arm): each Launchpad's own Stop Clip press arms only
    // that device's confirm window, since Clear is a deliberate,
    // immediate action taken on the spot, not a mode setting that needs
    // to stay consistent across every connected device the way
    // recording-armed does.
    ConfirmTimer clear_confirm;

    // Drum picker latch (plans/drum-machine.md, Phase 6) - CC97
    // ("Custom")'s own toggle, unconditional (see handleRawButton()'s own
    // comment). Only actually shown/acted on while
    // assigned_track_is_drum_machine is also true - left as whatever it
    // was if the device's assigned track later stops being a drum
    // machine, so switching back re-shows the picker rather than losing
    // the latch state.
    bool picker_active = false;

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

  // The step-grid's own pad-press handling (plans/drum-machine.md, Phase
  // 5) - a DrumMachineTrack's grid means something else entirely from
  // ordinary NOTES-mode chord entry, the same way Send/Pan mode already
  // does (see handlePadEvent()'s own dispatch): x = step, y = lane (row 0
  // = bottom = the lowest-ranked lane). A press toggles that lane/step's
  // bit - unconditionally, regardless of capture_enabled, since "the arm
  // flag gates performance capture, not editing" (the step grid writes in
  // both modes) - and always auditions via PLAY_NOTE/STOP_NOTE at a fixed
  // velocity (constants::DEFAULT_VELOCITY - pad pressure/aftertouch are
  // both ignored here, matching the brief's "no per-step velocity"
  // decision). The GM note number doubles as the PLAY_NOTE/STOP_NOTE
  // column, the same convention SongState::renderBlock()'s own step-driven
  // emission already uses (see DrumMachineTrack.h), so editing a step and
  // hearing it sequenced later choke/retrigger consistently.
  void handleStepGridPadEvent(LaunchpadPadEvent & ev, Controller & controller, DrumMachineTrack & track, int track_id);

  // The drum picker's own pad-press handling (plans/drum-machine.md,
  // Phase 6): reuses the free-drumming layout's own note lookup
  // (LaunchpadLayout::percussionNoteForPad) rather than a second copy.
  // PRESS on an already-assigned note removes that lane (silently
  // deleting its step data - no confirmation, no undo, per the brief's
  // own accepted risk); PRESS on an unassigned note adds a fresh
  // all-rest lane. addLane()/removeLane() apply the lane-list and
  // step-data mutation as a single call, so the two can never be
  // observed disagreeing. RELEASE/AFTERTOUCH are no-ops - picking is a
  // plain tap, not a held gesture.
  void handleDrumPickerPadEvent(LaunchpadPadEvent & ev, Controller & controller, DrumMachineTrack & track);

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
  // session (see Controller::ensureRowCleared, which owns the actual
  // clear-once-per-session logic - this is just the per-session
  // bookkeeping it's called with) - reset whenever a fresh session
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

  // Phase 7's free-running drum-machine audition clock (plans/drum-
  // machine.md): a second, independent clock from SongState's own
  // position - deliberately never touches sample_pos_/absolute_pos_ and
  // never pushes MOVE_POSITION/SET_POSITION, exactly the same "don't
  // unify the two clocks" invariant the plan calls out. Lives here (UI
  // thread, wall-clock-timed via refresh()'s own call cadence - Player.cpp
  // pushes a fresh playback-position UI event every single audio-callback
  // block regardless of play state, which is what wakes UI::renderComponents()
  // /refresh() up that often) rather than inside SongState::renderBlock() on the
  // audio thread, so it can reuse the exact same PLAY_NOTE-event-queue
  // audition path handleStepGridPadEvent/handleDrumPickerPadEvent already
  // use for their own one-shot presses, instead of a second, parallel
  // triggering mechanism. One shared step counter for the whole song (not
  // per-device, not per-track) - each DrumMachineTrack still wraps at its
  // own loop length via getHitNotesForRow()'s own modulo, so tracks with
  // different loop lengths phase-align at step 0 and diverge after,
  // exactly like two pattern-driven DrumMachineTracks already would while
  // playing normally.
  // StepClock itself (LaunchpadTiming.h) is the pure, unit-tested
  // step-advance logic; this is only the wall-clock timestamp of the
  // last refresh() call, needed to turn "now" into a dt to feed it.
  StepClock audition_clock_;
  std::chrono::steady_clock::time_point audition_clock_last_refresh_;

  // Fires every DrumMachineTrack's getHitNotesForRow(step) as a PLAY_NOTE
  // audition event - the free-running clock's own per-step action,
  // factored out of refresh() since it's called from two places there
  // (the moment the clock (re)starts, and once per row boundary crossed
  // while it's already running).
  void triggerAuditionStep(const Song & song, const std::vector<int> & track_ids, Controller & controller, int step);

  // Record Arm (CC19) is one shared, song-wide flag, not a per-device
  // setting (deliberate change from the original per-device design, made
  // while implementing Phase 7's free-running drum-machine audition loop:
  // arming should be a single global state, since "am I recording" isn't
  // a question that should have a different answer on two Launchpads
  // plugged into the same session). handleRawButton()'s CC19
  // branch flips this; refresh() copies it into every connected device's
  // own DeviceState::capture_enabled every frame (see that field's own
  // comment) so the entire rest of this file - handlePadEvent's
  // capture-gated writes, anyCaptureArmedNoteHeld(), refreshLeds()'s CC19
  // LED - keeps reading the per-device mirror unchanged, and every
  // connected Launchpad's Record Arm LED shows the same lit/unlit state.
  bool capture_enabled_ = false;

  // Controller::getGlobalOctave(), mirrored here once per refresh() call
  // (same pattern as capture_enabled_ above) rather than threading
  // Controller into octave()/resolveNote()/refreshLeds(), none of which
  // otherwise need it. See octave()'s own comment.
  int cached_global_octave_ = 4;
};

#endif
