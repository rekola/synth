#include "PatternEditor.h"

#include "InputEvent.h"
#include "SongState.h"
#include "Controller.h"
#include "StyleProvider.h"
#include "Tuner.h"
#include "Tuning.h"
#include "InstrumentTrack.h"
#include "SampleTrack.h"
#include "DrumMachineTrack.h"
#include "MidiEvent.h"
#include "PlaybackControlEvent.h"
#include "LogEvent.h"
#include "KeyChord.h"
#include "PatternScroll.h"

#include <string>
#include <algorithm>
#include <fmt/core.h>

#include <iostream>

using namespace std;
using namespace fmt;

// Track flattening moved to Song::getRootTrackIds() - shared with the
// Launchpad command-dispatch path (UI::handleLaunchpadButtonEvent), which
// needs the exact same addressable-track list/order to resolve a device's
// assigned track to a track_id, without PatternEditor being involved.

PatternEditor::PatternEditor(UIPlane & parent) : UIElement(parent) {
  // getPlane().setScrolling(true);

  // Emacs-style commands, dispatched centrally via UIElement::dispatchCommand
  // (see offerInput() below). Each lambda re-fetches song/info/track_ids
  // itself, exactly like the code that used to run inline here did on every
  // call - none of this is cached across calls, so there's no staleness risk
  // from moving it into a constructor-time closure.

  commands_.define("set-mark", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto track_ids = song.getRootTrackIds();
    selection_start_pattern_ = info.getPatternIndex();
    selection_start_row_ = info.getRowIndex();
    selection_start_track_ = current_cursor.track;
    // A fresh mark starts scoped to just the note column it's set on;
    // moving sideways afterward widens/narrows to the touched range (see
    // kill-region etc.) - to select the whole track, widen across all of
    // its note columns.
    auto track_info = getTrackInfoFor(song, track_ids[current_cursor.track]);
    selection_start_note_ = clamp(track_info.getNoteNumber(current_cursor.col), 0, max(track_info.num_subtracks_ - 1, 0));
    selection_active_ = true;
    getController().getUIEventQueue().push(make_unique<LogEvent>("Mark set"));
  });

  // Both commands below always have a region to act on, even with no mark
  // active: it degenerates to the single note the cursor is currently on
  // (see getEffectiveSelectionBounds) - there's no "No selection" case.
  commands_.define("kill-region", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto track_ids = song.getRootTrackIds();
    auto & pattern = song.getPattern(info.getPatternIndex());

    auto b = getEffectiveSelectionBounds(song, track_ids);
    clipboard_column_scoped_ = b.column_scoped;
    clipboard_includes_command_ = b.includes_command;
    if (b.column_scoped) {
      auto track_id = track_ids[b.track_lo];
      clipboard_ = copyPatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, b.includes_command);
      clearPatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, b.includes_command);
    } else {
      clipboard_ = copyPatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
      clearPatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
    }
    song.incVersion();
    selection_active_ = false;
    // move point to the start of the killed region, matching Emacs
    // kill-region, so an immediate yank restores it exactly in place
    getController().moveEditPosition(b.row_lo - info.getRowIndex());
    new_cursor.track = b.track_lo;
    if (!b.column_scoped) {
      // Only reset to the first column for a whole-track kill; a
      // column-scoped kill (e.g. just note column 2) should leave the
      // cursor on the column it was already on, not jump back to 0.
      new_cursor.col = new_cursor.subcol = 0;
    } else {
      // Killing the track's last remaining voice can shrink its note-column
      // count (num_subtracks_ is derived from the widest row left in the
      // pattern). A raw index-bounds check isn't enough here: the old
      // index can still be "in range" of the narrower layout while meaning
      // something entirely different now (getNoteNumber() mechanically
      // extrapolates past the end the same way it does for the effect
      // column, so a stale index can silently resolve to the effect
      // column instead of clamping) - check via note number instead, and
      // snap to the corresponding sub-column of the last remaining voice.
      auto new_track_info = getTrackInfoFor(song, track_ids[b.track_lo]);
      auto new_max_note = max(new_track_info.num_subtracks_ - 1, 0);
      if (new_track_info.getNoteNumber(new_cursor.col) > new_max_note) {
        auto n = (new_track_info.has_note_column_ ? 1 : 0) + new_track_info.num_velocity_columns_ +
          (new_track_info.has_delay_column_ ? 1 : 0);
        new_cursor.col = new_max_note * n;
        new_cursor.subcol = 0;
      }
    }
    getController().getUIEventQueue().push(make_unique<LogEvent>("Region killed"));
  });

  commands_.define("kill-ring-save", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto track_ids = song.getRootTrackIds();
    auto & pattern = song.getPattern(info.getPatternIndex());

    auto b = getEffectiveSelectionBounds(song, track_ids);
    clipboard_column_scoped_ = b.column_scoped;
    clipboard_includes_command_ = b.includes_command;
    if (b.column_scoped) {
      auto track_id = track_ids[b.track_lo];
      clipboard_ = copyPatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, b.includes_command);
    } else {
      clipboard_ = copyPatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
    }
    selection_active_ = false;
    getController().getUIEventQueue().push(make_unique<LogEvent>("Region copied"));
  });

  commands_.define("yank", [this]() {
    if (!clipboard_.empty()) {
      auto & song = getController().getSong();
      auto & info = getController().getPlaybackInfo();
      auto track_ids = song.getRootTrackIds();
      auto & pattern = song.getPattern(info.getPatternIndex());
      if (clipboard_column_scoped_) {
        auto track_id = track_ids[current_cursor.track];
        auto track_info = getTrackInfoFor(song, track_id);
        auto target_note = clamp(track_info.getNoteNumber(current_cursor.col), 0, max(track_info.num_subtracks_ - 1, 0));
        pastePatternBlockNotes(pattern, clipboard_, song.getPatternLength(), info.getRowIndex(), track_id, target_note, clipboard_includes_command_);
      } else {
        pastePatternBlock(pattern, clipboard_, song.getPatternLength(), info.getRowIndex(), track_ids, current_cursor.track);
      }
      song.incVersion();
      getController().getUIEventQueue().push(make_unique<LogEvent>("Yanked"));
    } else {
      getController().getUIEventQueue().push(make_unique<LogEvent>("Clipboard empty"));
    }
  });

  commands_.define("keyboard-quit", [this]() {
    if (selection_active_) {
      selection_active_ = false;
      getController().getUIEventQueue().push(make_unique<LogEvent>("Mark deactivated"));
    }
  });

  // Unlike kill-region/kill-ring-save, transpose isn't destructive, so it
  // never clears the mark - repeated presses keep transposing the same
  // region. With no mark active, the region degenerates to the single
  // note under the cursor (see getEffectiveSelectionBounds) - to transpose
  // the whole pattern, select all of it first.
  commands_.define("transpose-region-up", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto & pattern = song.getPattern(info.getPatternIndex());
    auto track_ids = song.getRootTrackIds();

    // A percussion track's Note::getValue() selects which drum sound
    // plays (a MIDI key), not a pitch - transposing it would silently
    // swap to a different, unrelated drum instead of "transposing"
    // anything, so it's excluded rather than shifted.
    auto is_percussion = [&song](int track_id) {
      auto * track = song.getTrackByInternalId(track_id);
      return track && track->getType() == TrackType::PERCUSSION_CONTROL;
    };

    auto b = getEffectiveSelectionBounds(song, track_ids);
    if (b.column_scoped) {
      auto track_id = track_ids[b.track_lo];
      transposePatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, true, is_percussion(track_id));
    } else {
      transposePatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi, true, is_percussion);
    }
    song.incVersion();
  });

  commands_.define("transpose-region-down", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto & pattern = song.getPattern(info.getPatternIndex());
    auto track_ids = song.getRootTrackIds();

    // See transpose-region-up's own comment.
    auto is_percussion = [&song](int track_id) {
      auto * track = song.getTrackByInternalId(track_id);
      return track && track->getType() == TrackType::PERCUSSION_CONTROL;
    };

    auto b = getEffectiveSelectionBounds(song, track_ids);
    if (b.column_scoped) {
      auto track_id = track_ids[b.track_lo];
      transposePatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, false, is_percussion(track_id));
    } else {
      transposePatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi, false, is_percussion);
    }
    song.incVersion();
  });

  // Row navigation while stopped (playback owns the row while playing -
  // see the isPlaying() guard) - same MOVE_POSITION event the mouse
  // scroll-wheel and Page Up/Down already push. Named as a command (not
  // left inline the way it used to be) so a Launchpad's up/down-arrow
  // buttons can trigger the exact same code, not a re-implementation of
  // it - see LaunchpadProtocol's CC91/92 mapping and LaunchpadManager::
  // handleCommand.
  commands_.define("move-row-up", [this]() {
    auto & info = getController().getPlaybackInfo();
    if (info.isPlaying()) return;
    getController().moveEditPosition(-1);
    new_cursor.subcol = 0;
  });

  commands_.define("move-row-down", [this]() {
    auto & info = getController().getPlaybackInfo();
    if (info.isPlaying()) return;
    getController().moveEditPosition(1);
    new_cursor.subcol = 0;
  });

  // Named once here (PatternEditor is where "the current track" - the
  // shared cursor - already lives), reached identically whether from the
  // keybinding below, an M-x invocation, or a Launchpad button press (via
  // UI::handleLaunchpadButtonEvent's generic executeCommand() fallback,
  // once LaunchpadManager::handleCommand has declined the name). The
  // actual mutation (+ keeping the running SongState in sync) lives in
  // Controller, shared by any caller - consumePendingCommandTrack reads
  // (and clears) the Emacs-prefix-argument-style transient a Launchpad
  // dispatch stashes ahead of time (see Controller.h), falling back to the
  // shared cursor's own track when nothing set it.
  commands_.define("toggle-mute", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.empty()) return;
    auto track_id = getController().consumePendingCommandTrack(track_ids[current_cursor.track]);
    getController().toggleTrackMuted(track_id);
  });

  commands_.define("toggle-solo", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.empty()) return;
    auto track_id = getController().consumePendingCommandTrack(track_ids[current_cursor.track]);
    getController().toggleTrackSolo(track_id);
  });

  // Renoise-style manual note-column add/remove (see Controller::
  // addNoteColumn/removeNoteColumn and InstrumentTrack::getMinNoteColumns) -
  // todo.txt's own long-standing "add shortcut for add note column" idea.
  commands_.define("add-note-column", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.empty()) return;
    auto track_id = getController().consumePendingCommandTrack(track_ids[current_cursor.track]);
    getController().addNoteColumn(track_id);
  });

  commands_.define("remove-note-column", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.empty()) return;
    auto track_id = getController().consumePendingCommandTrack(track_ids[current_cursor.track]);
    getController().removeNoteColumn(track_id);
  });

  // Create-fresh only (see plans/drum-machine.md) - no "convert an
  // existing track" path exists, since TrackType is fixed at construction
  // for every track and a drum machine's sequence lives outside Pattern
  // data anyway (existing notes would just be orphaned). seedDefaultKit()
  // is the single place the default rock kit's note list lives - shared
  // with Song.cpp's own loadDrumMachineData() for a hand-authored
  // <drumMachineTrack> with no <drumMachine> child at all.
  commands_.define("add-drum-machine-track", [this]() {
    auto & song = getController().getSong();
    auto & track = dynamic_cast<DrumMachineTrack &>(song.addTrack(make_unique<DrumMachineTrack>()));
    track.seedDefaultKit();
    song.incVersion();
  });

  // "send-a-mode"/"send-b-mode" are NOT defined here (or anywhere in
  // commands_) - they mutate nothing outside a single Launchpad device's
  // own transient UI state (which grid mode it's showing), never Song/
  // Track data, and have no keyboard/M-x equivalent that would make sense
  // ("open the Send A fader" - for which device?). LaunchpadManager::
  // handleCommand handles them directly, synchronously, with the
  // device_id it's already given - see UI::handleLaunchpadButtonEvent.

  keymap_.bind(KeyChord::pack(' ', true, false, false, false), "set-mark");  // Ctrl-Space
  keymap_.bind(KeyChord::pack('b', true, false, false, false), "set-mark");  // Ctrl-B (see todo.txt; works on any terminal)
  keymap_.bind(KeyChord::pack('w', true, false, false, false), "kill-region");
  keymap_.bind(KeyChord::pack('w', false, true, false, false), "kill-ring-save");  // Alt-W
  keymap_.bind(KeyChord::pack('y', true, false, false, false), "yank");
  keymap_.bind(KeyChord::pack('g', true, false, false, false), "keyboard-quit");
  keymap_.bind(KeyChord::pack(NCKEY_UP, true, false, true, false), "transpose-region-up");    // Ctrl+Shift+Up
  keymap_.bind(KeyChord::pack(NCKEY_DOWN, true, false, true, false), "transpose-region-down"); // Ctrl+Shift+Down
  keymap_.bind(KeyChord::pack('\\', true, false, false, false), "toggle-solo");  // Ctrl-\ (was Ctrl-only inline handling)
  keymap_.bind(KeyChord::pack('\\', false, false, false, false), "toggle-mute"); // backslash key
  keymap_.bind(KeyChord::pack(NCKEY_UP, false, false, false, false), "move-row-up");     // plain Up (was inline handling)
  keymap_.bind(KeyChord::pack(NCKEY_DOWN, false, false, false, false), "move-row-down"); // plain Down
  keymap_.bind(KeyChord::pack(NCKEY_RIGHT, true, false, true, false), "add-note-column");   // Ctrl+Shift+Right
  keymap_.bind(KeyChord::pack(NCKEY_LEFT, true, false, true, false), "remove-note-column"); // Ctrl+Shift+Left
  // Ctrl+Shift+D ("Drum") - otherwise only reachable via M-x, which meant
  // there was no way to discover this command exists at all. Plain Ctrl-D
  // is already the (stub, not-yet-implemented) "duplicate track" raw
  // handler below, and Ctrl+Shift+T is already the (also-stub) "delete
  // track" one, so this picks a still-free Ctrl+Shift combo rather than
  // colliding with either.
  keymap_.bind(KeyChord::pack('d', true, false, true, false), "add-drum-machine-track"); // Ctrl+Shift+D

  assertCommandBindingsValid();
}

static void fill_track_info(const Track & track, std::unordered_map<int, VisibleTrackInfo> & track_info) {
  if (track.getType() == TrackType::INSTRUMENT_CONTROL ||
      track.getType() == TrackType::PERCUSSION_CONTROL) {
    auto & info = track_info[track.getInternalId()];
    auto & instrument_track = dynamic_cast<const InstrumentTrack&>(track);
    info.has_note_column_ = instrument_track.showNoteColumn();
    info.num_velocity_columns_ = instrument_track.showVelocityColumn() ? 1 : 0;
    info.has_delay_column_ = instrument_track.showDelayColumn();
    info.has_effect_column_ = instrument_track.showEffectsColumn();
    // Renoise-style manually-added note columns (see Controller::addNoteColumn) -
    // a floor updateNumSubtracks's usual max-of-actual-note-data below is
    // still taken against, so a column with real note data in it can never
    // be hidden by this, only extended past it.
    info.updateNumSubtracks(instrument_track.getMinNoteColumns());
  } else if (track.getType() == TrackType::DRUM_MACHINE || track.getType() == TrackType::SAMPLE) {
    // Single placeholder column (see renderRow's own SAMPLE/DRUM_MACHINE
    // branch), so an explicit, default-constructed 1-column VisibleTrackInfo
    // entry is keyed here rather than left absent - a track_id present in
    // track_ids (getRootTrackIds() includes both types) with no entry here
    // at all makes every downstream per-track-id width/bounds computation
    // (scroll included - see PatternScroll.cpp) silently treat it as zero
    // width instead of its real ~4-character placeholder width, which can
    // undercount how much screen space an earlier SAMPLE/DRUM_MACHINE track
    // actually consumes and let a later track's cursor column be computed
    // as fitting on screen when it doesn't, by however many characters got
    // undercounted.
    track_info[track.getInternalId()];
  } else {
    for (auto & child : track.getChildren()) {
      fill_track_info(*child, track_info);
    }
  }
}

static void get_track_parents(Track & track, Track * parent, std::unordered_map<int, Track *> & parents) {
  parents[track.getInternalId()] = parent;
  for (auto & child : track.getChildren()) {
    get_track_parents(*child, &track, parents);
  }
}

std::unordered_map<int, VisibleTrackInfo>
PatternEditor::getTrackInformation(const Song & song, int scroll_row) const {
  auto [rows, cols] = getDim();
  auto heading_height = song.getTrackDepth() + 1;
  auto & info = getController().getPlaybackInfo();

  std::unordered_map<int, VisibleTrackInfo> track_info;
  for (auto row = 0; row < rows - heading_height; ) {
    auto [ pattern_idx, pattern_row ] = song.normalizePosition(info.getPatternIndex(), row + scroll_row);
    if (pattern_idx >= song.getPatterns().size()) break;

    auto & pattern = song.getPattern(pattern_idx);
    pattern.getTrackInformation(track_info);
    row += song.getPatternLength() - pattern_row;
  }
  for (auto & track : song.getTracks()) {
    fill_track_info(*track, track_info);
  }

  return track_info;
}

VisibleTrackInfo
PatternEditor::getTrackInfoFor(const Song & song, int track_id) const {
  auto all_track_info = getTrackInformation(song, current_scroll_.row);
  auto it = all_track_info.find(track_id);
  return it != all_track_info.end() ? it->second : VisibleTrackInfo();
}

PatternEditor::SelectionBounds
PatternEditor::getEffectiveSelectionBounds(const Song & song, const vector<int> & track_ids) const {
  auto & info = getController().getPlaybackInfo();
  bool has_mark = selection_active_ && selection_start_pattern_ == info.getPatternIndex();

  SelectionBounds b;
  auto start_row = has_mark ? selection_start_row_ : info.getRowIndex();
  auto start_track = has_mark ? selection_start_track_ : current_cursor.track;
  b.row_lo = min(start_row, info.getRowIndex());
  b.row_hi = max(start_row, info.getRowIndex());
  b.track_lo = min(start_track, current_cursor.track);
  b.track_hi = max(start_track, current_cursor.track);
  b.column_scoped = b.track_lo == b.track_hi;

  if (b.column_scoped) {
    auto track_info = getTrackInfoFor(song, track_ids[b.track_lo]);
    auto max_note = max(track_info.num_subtracks_ - 1, 0);
    if (track_info.isEffectColumn(current_cursor.col)) {
      // The effect command applies to every note column in the row, not
      // just one - there's no such thing as a partial effect-column
      // region, so widen to all of them regardless of any mark, and mark
      // the row's Command as part of the region too.
      b.note_lo = 0;
      b.note_hi = max_note;
      b.includes_command = true;
    } else {
      auto point_note = clamp(track_info.getNoteNumber(current_cursor.col), 0, max_note);
      auto start_note = has_mark ? clamp(selection_start_note_, 0, max_note) : point_note;
      b.note_lo = min(start_note, point_note);
      b.note_hi = max(start_note, point_note);
    }
  } else {
    b.note_lo = b.note_hi = 0;
  }

  return b;
}

bool
PatternEditor::render(const StyleProvider & styles, bool refresh) {
  bool render_all = refresh;
  auto & info = getController().getPlaybackInfo();
  auto score_pattern = info.getPatternIndex();
  auto score_playing_row = info.getRowIndex();
  auto & song = getController().getSong();

  // Playback's own playhead (unlike the stopped-transport edit cursor -
  // see Controller::moveEditPosition()'s own clampRowToCurrentPattern()
  // call) crosses pattern boundaries freely, and must keep doing so
  // regardless of any selection - ending the selection the instant
  // playback starts (rather than waiting for the boundary-cross check
  // below to eventually notice) keeps that unambiguous: an open selection
  // never has a chance to look like it's constraining where the playhead
  // goes.
  if (selection_active_ && info.isPlaying()) {
    selection_active_ = false;
    getController().getUIEventQueue().push(make_unique<LogEvent>("Selection cleared: playback started"));
  } else if (selection_active_ && selection_start_pattern_ != score_pattern) {
    selection_active_ = false;
    getController().getUIEventQueue().push(make_unique<LogEvent>("Selection cleared: crossed pattern boundary"));
  }

  auto [rows, cols] = getDim();
  auto heading_height = song.getTrackDepth() + 1;

  // Computed before getTrackInformation() below, not after (as this used
  // to be ordered) - a real, confirmed bug: getTrackInformation() only
  // scans whatever's within the *current* scroll window (deliberately -
  // see its own doc comment), so if this frame is the one where playback
  // crosses a pattern boundary and the scroll window needs to jump to
  // follow it, computing track_info against the stale pre-jump window
  // could miss a note just written into the row that's about to scroll
  // into view (e.g. a Launchpad chord landing exactly on that transition),
  // silently failing to grow that track's note-column width this frame.
  auto new_row = current_scroll_.row;
  if (score_playing_row < new_row) {
    new_row = score_playing_row;
  } else if (score_playing_row >= new_row + rows - heading_height) {
    new_row = score_playing_row - (rows - heading_height) + 1;
  }

  auto track_info = getTrackInformation(song, new_row);

  auto track_ids = song.getRootTrackIds();

  auto score_total_columns = 0;
  for (auto wd : track_info) score_total_columns += wd.second.getColumnCount();

  auto new_scroll = computeScrollPosition(current_scroll_, new_row, new_cursor.track, new_cursor.col, track_ids, track_info, cols);

  bool selection_changed = selection_active_ != current_selection_active_ ||
      (selection_active_ && (selection_start_pattern_ != current_selection_start_pattern_ ||
			     selection_start_row_ != current_selection_start_row_ ||
			     selection_start_track_ != current_selection_start_track_ ||
			     selection_start_note_ != current_selection_start_note_ ||
			     score_playing_row != current_score_playing_row ||
			     new_cursor.track != current_cursor.track ||
			     new_cursor.col != current_cursor.col));

  if (score_pattern != current_score_pattern ||
      song.getVersion() != current_song_version ||
      score_total_columns != current_score_total_columns ||
      new_scroll != current_scroll_ ||
      selection_changed
      ) {
    render_all = true;
  }
  
  bool cursor_changed = new_cursor.track != current_cursor.track || new_cursor.col != current_cursor.col || new_cursor.subcol != current_cursor.subcol;
  
  current_cursor.track = new_cursor.track;
  current_cursor.col = new_cursor.col;
  current_cursor.subcol = new_cursor.subcol;

  // Always something to highlight - degenerates to just the note under the
  // cursor when no mark is set (see getEffectiveSelectionBounds). Computed
  // after current_cursor is updated above, so it reflects where the cursor
  // just moved *to* this frame, not where it was before.
  auto sel_bounds = getEffectiveSelectionBounds(song, track_ids);

  bool need_redraw = false;
  if (render_all) {
    current_scroll_ = new_scroll;

    erase();
    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);
    fill();

    renderHeading(styles, track_ids, track_info);
    for (auto row = 0; row < rows - heading_height; row++) {
      renderRow(styles, heading_height, track_ids, track_info, row, (row + current_scroll_.row) == score_playing_row, sel_bounds);
    }
    need_redraw = true;
  } else if (current_score_playing_row != score_playing_row) {
    renderHeading(styles, track_ids, track_info);
    renderRow(styles, heading_height, track_ids, track_info, current_score_playing_row - current_scroll_.row, false, sel_bounds);
    renderRow(styles, heading_height, track_ids, track_info, score_playing_row - current_scroll_.row, true, sel_bounds);
    need_redraw = true;
  } else if (cursor_changed || row_edited) {
    renderRow(styles, heading_height, track_ids, track_info, score_playing_row - current_scroll_.row, true, sel_bounds);
    need_redraw = true;
  }

  int new_tempo = song.getTempo();
  
  if (render_all || edit_step_size != new_edit_step_size || new_tempo != current_tempo || cursor_changed) {    
    edit_step_size = new_edit_step_size;
    current_tempo = new_tempo;
    
    need_redraw = true;
  }
  
  current_score_pattern = score_pattern;
  current_score_playing_row = score_playing_row;
  current_score_total_columns = score_total_columns;
  current_song_version = song.getVersion();
  row_edited = false;

  current_selection_active_ = selection_active_;
  current_selection_start_pattern_ = selection_start_pattern_;
  current_selection_start_row_ = selection_start_row_;
  current_selection_start_track_ = selection_start_track_;
  current_selection_start_note_ = selection_start_note_;
  
  return need_redraw;
}

void
PatternEditor::handleMidiEvent(MidiEvent & ev) {
  auto & event_queue = getController().getPlaybackEventQueue();

  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();

  bool was_playing = !active_midi_notes.empty();

  auto track_ids = song.getRootTrackIds();

  auto & pattern = song.getPattern(info.getPatternIndex());
  int track_id = track_ids[new_cursor.track];

  // Channel-wide, not tied to any specific note - unlike every other case
  // below, ev.getNote() is unused (always 0, see AlsaAudio.cpp), so this
  // must be handled before the active_midi_notes lookup that follows, or
  // it would be misread as "note 0" and corrupt that per-note bookkeeping.
  // No pattern write either: there's no single note whose velocity this
  // could sensibly become.
  if (ev.getType() == MidiEvent::CHANNEL_PRESSURE) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CHANNEL_PRESSURE, track_id, ev.getVelocity()));
    return;
  }

  bool is_off = ev.getType() == MidiEvent::NOTE_OFF || (ev.getType() == MidiEvent::NOTE_ON && ev.getVelocity() == 0);

  int note_value = 0;
  if (song.getTuning() == Tuning::TET12) note_value = ev.getNote();
  else {
    float best_diff = 1000000.0f, f = Tuner::getFrequency(Tuning::TET12, ev.getNote());
    for (int i = 0; i < 255; i++) {
      float diff = fabsf(f - Tuner::getFrequency(song.getTuning(), i));
      if (diff < best_diff) {
	note_value = i;
	best_diff = diff;
      }
    }
  }

  auto current_delay = info.getCurrentDelay();
  
  int note_column;
  auto it = active_midi_notes.find(ev.getNote());
  if (it != active_midi_notes.end()) {
    note_column = it->second;
  } else {
    active_midi_notes[ev.getNote()] = note_column = active_midi_notes.size();
    cerr << "new note: " << note_column << endl;
  }
  
  if (is_off) {
    active_midi_notes.erase(ev.getNote());
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, track_id, note_column));

    pattern.setNote(info.getRowIndex(), track_id, note_column, Note(0, 0, current_delay));
  } else if (ev.getType() == MidiEvent::NOTE_ON) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note_column, note_value, ev.getVelocity()));

    Note note(note_value, ev.getVelocity(), current_delay);
    pattern.setNote(info.getRowIndex(), track_id, note_column, note);
    row_edited = true;
  } else if (ev.getType() == MidiEvent::NOTE_PRESSURE) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, track_id, note_column, note_value, ev.getVelocity()));    

    auto note = pattern.getNote(info.getRowIndex(), track_id, note_column);
    if (!note.isDefined()) note.setDelay(current_delay);
    note.setVelocity(ev.getVelocity());    
    pattern.setNote(info.getRowIndex(), track_id, note_column, note);
    row_edited = true;
  }

#if 0
  if ((!was_playing && !active_midi_notes.empty() && !info.is_playing) ||
      (was_playing && active_midi_notes.empty() && info.is_playing)) {
    bool playing = getController().togglePlaying();
    // setStatus(playing ? "Playing" : "Stopped");
  }
#endif
}

void
PatternEditor::ensureRowCleared(Song & song, int pattern_idx, int row, int track_id) {
  if (!auto_record_cleared_rows_.insert({row, track_id}).second) return; // already cleared this session
  auto & pattern = song.getPattern(pattern_idx);
  pattern.setNotes(row, track_id, {});
  song.incVersion();
}

void
PatternEditor::onRowAdvanced(Controller & controller) {
  if (!auto_started_playback_) return;

  auto & info = controller.getPlaybackInfo();
  auto new_row = info.getRowIndex();
  auto pattern_idx = info.getPatternIndex();

  if (pattern_idx != last_cleared_pattern_idx_ || new_row < last_cleared_row_) {
    // Pattern changed, or the row went backwards (a loop/pattern-sequence
    // wraparound) - resync to just this row rather than trying to
    // backfill a range spanning the boundary, which has no single
    // well-defined meaning here.
    last_cleared_row_ = new_row - 1;
    last_cleared_pattern_idx_ = pattern_idx;
  }
  if (new_row <= last_cleared_row_) return; // nothing new to sweep

  // Every track currently receiving live input - almost always just one
  // (the cursor's own track at press time), but each active note stores
  // its own track_id, the same union-of-tracks approach
  // LaunchpadManager::onRowAdvanced() uses, so this stays correct even
  // in the edge case of the cursor moving to a different track mid-hold.
  vector<int> track_ids;
  for (auto & [ id, note ] : active_keyboard_notes_) {
    if (find(track_ids.begin(), track_ids.end(), note.track_id) == track_ids.end()) {
      track_ids.push_back(note.track_id);
    }
  }

  auto & song = controller.getSong();
  for (int row = last_cleared_row_ + 1; row <= new_row; row++) {
    for (auto track_id : track_ids) {
      ensureRowCleared(song, pattern_idx, row, track_id);
    }
  }
  last_cleared_row_ = new_row;
}

bool
PatternEditor::offerInput(const InputEvent & input) {
  if (dispatchCommand(input)) return true;

  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();
  auto & event_queue = getController().getPlaybackEventQueue();

  // Kitty-protocol RELEASE events now reach offerInput() (previously
  // dropped in TerminalUI::readInput() - see InputEvent::Kind's own doc
  // comment). Only the raw computer-keyboard note-entry code below cares
  // about a key actually going up (to stop a held note, mirroring real
  // MIDI/Launchpad note-off) - it's fully self-contained, since
  // active_keyboard_notes_ is keyed by the physical key alone, with no
  // dependency on modifier state or which on-screen column/track the
  // cursor happens to be over right now. Every *other* manual key handler
  // below (cursor movement, Ctrl+Left/Right instrument change, hex-digit
  // entry for effect/velocity/delay columns, ...) assumes a single-fire
  // press and has no Kind-awareness of its own - without this guard, a
  // held note key's RELEASE naturally falls into the same branch its
  // PRESS did if the release happened during a different one (e.g. a
  // held note key was actually just an arrow key, whose PRESS-branch has
  // no note to release), causing it to fire the same action a second
  // time. Handling every RELEASE right here, before any of that code
  // even runs, means a release either matches a held note (fully handled)
  // or is inert - it never reaches the rest of this function either way.
  if (input.getKind() == InputEvent::Kind::RELEASE) {
    auto it = active_keyboard_notes_.find(input.getId());
    if (it == active_keyboard_notes_.end()) return false;
    auto held = it->second;
    active_keyboard_notes_.erase(it);

    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, held.track_id, held.note_column));

    // Mirrors LaunchpadManager's own RELEASE handling: while playing,
    // write an explicit off at the row the transport has since reached
    // - unless that's still the note's own row, which would erase the
    // note it belongs to instead of ending it.
    if (info.isPlaying()) {
      auto & pattern = song.getPattern(info.getPatternIndex());
      auto release_row = info.getRowIndex();
      if (release_row != held.row) {
	if (auto_started_playback_) ensureRowCleared(song, info.getPatternIndex(), release_row, held.track_id);
	pattern.setNote(release_row, held.track_id, held.note_column, Note(0, 0, info.getCurrentDelay()));
	song.incVersion();
      }
    }

    // Realtime auto-play-while-held (mirrors LaunchpadManager's own -
    // see its RELEASE branch for the identical reasoning): stop exactly
    // when the last held note key releases, but only if this code
    // started the transport itself, and only if it's still genuinely
    // playing - otherwise the user must have manually stopped it in the
    // meantime, and toggling again here would incorrectly restart it.
    if (auto_started_playback_ && active_keyboard_notes_.empty()) {
      if (info.isPlaying()) {
	getController().togglePlaying();
	// Land past the just-written final OFF, not directly on it - see
	// LaunchpadManager's own identical fix for the full reasoning. Only
	// when *we* actually stopped it here, same care as above. Absolute
	// SET_POSITION, not relative MOVE_POSITION(1) - see SongState::
	// setPosition()'s own comment for why a relative move occasionally
	// overshot by an extra row.
	getController().setEditPosition(info.getAbsolutePosition() + 1);
      }
      // Unconditional, regardless of the isPlaying() check above - see
      // LaunchpadManager's identical fix for why (a manual mid-hold stop
      // must not leave recording_muted_ stuck true).
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_RECORDING_MUTE, 0));
      auto_started_playback_ = false;
      auto_record_cleared_rows_.clear(); // not required for correctness (the next session's own start resets this too) - just don't hold onto a finished session's bookkeeping longer than needed
    }
    return true;
  }

  auto all_track_info = getTrackInformation(song, current_scroll_.row);

  auto track_ids = song.getRootTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());

  auto current_track = song.getTrackByInternalId(track_ids[current_cursor.track]);

  auto input_hex_value = digit(input.getId(), 16);

  VisibleTrackInfo track_info;
  if (current_track) {
    auto it0 = all_track_info.find(current_track->getInternalId());
    if (it0 != all_track_info.end()) track_info = it0->second;
  }

  if (input.getId() == NCKEY_BUTTON1) {
    
  } else if (input.hasCtrl() && input.hasShift() && !input.hasMeta()) {
    if (input.getId() == 't') {
      // delete track
      return true;
    }
  } else if (input.hasCtrl() && !input.hasMeta()) {
    if (input.getId() == 'r') {
      int track_id;
      auto sample = getController().startRecording();
      if (current_track && current_track->getType() == TrackType::SAMPLE) {
	auto & sample_track = dynamic_cast<SampleTrack&>(*current_track);
	sample_track.setSample(sample);
	track_id = sample_track.getInternalId();
      } else {
	new_cursor.track = track_ids.size();
	auto & track = song.addTrack(make_unique<SampleTrack>(sample));
	track_id = track.getInternalId();
      }
      getController().setRecordingTrackId(track_id);
      song.incVersion();
      return true;
    } else if (input.getId() == 'a') {
      new_cursor.track = new_cursor.col = new_cursor.subcol = 0;
      return true;
    } else if (input.getId() == 'e') {
      new_cursor.track = num_tracks > 1 ? num_tracks - 1 : 0;
      new_cursor.subcol = 0;

      auto it = all_track_info.find(track_ids[new_cursor.track]);
      new_cursor.col = it != all_track_info.end() ? it->second.getColumnCount() - 1: 0;
      return true;
    } else if (input.getId() == 't') {
      song.addTrack(make_unique<InstrumentTrack>(0));
      return true;
    } else if (input.getId() == 'd') {
      // duplicate track
      return true;
    } else if (input.getId() == '+') {
      edit_step_size++;
      return true;
    } else if (input.getId() == '-') {
      if (edit_step_size > 0) edit_step_size--;
      return true;
    } else if (input.getId() == NCKEY_KP_DIVIDE || input.getId() == NCKEY_KP_MULTIPLY) {
      // Instrument selection - moved here from Ctrl+Left/Right (now "move
      // cursor to the neighboring track", see below) to free that chord
      // up. Numpad Divide/Multiply rather than a modifier combo on
      // ordinary keys since every other candidate (Ctrl+Up/Down,
      // Alt+PageUp/PageDown, ...) either collided with something else or
      // risked terminal/WM interception - see TerminalUI::readInput()'s
      // own comment for why these two specifically need their own
      // escape-sequence recognizer to even arrive as a single key event.
      auto track = song.getTrackByInternalId(track_ids[current_cursor.track]);
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL || track->getType() == TrackType::DRUM_MACHINE)) {
	auto & instrument_track = dynamic_cast<InstrumentTrack&>(*track);
	bool changed = false;
	if (input.getId() == NCKEY_KP_DIVIDE && instrument_track.getInstrumentId() > 0) {
	  instrument_track.setInstrumentId(instrument_track.getInstrumentId() - 1);
	  changed = true;
	} else {
	  auto & instruments = song.getInstruments();
	  if (input.getId() == NCKEY_KP_MULTIPLY && instrument_track.getInstrumentId() + 1 < instruments.size()) {
	    instrument_track.setInstrumentId(instrument_track.getInstrumentId() + 1);
	    changed = true;
	  }
	}
	if (changed) {
	  song.incVersion();
	  event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CLEAR_VOICES, instrument_track.getInternalId()));
	}
      }
      return true;
    } else if (input.getId() == NCKEY_LEFT || input.getId() == NCKEY_RIGHT) {
      // Emacs-style word motion, one level up from plain Left/Right's
      // note-column navigation (see the !input.hasMeta() branch below):
      // jumps straight to the neighboring track rather than stepping
      // through its columns, landing on its first column the same way
      // crossing a track boundary during plain-Right navigation already
      // does.
      if (input.getId() == NCKEY_LEFT && new_cursor.track > 0) {
	new_cursor.track--;
	new_cursor.col = 0;
	new_cursor.subcol = 0;
      } else if (input.getId() == NCKEY_RIGHT && new_cursor.track + 1 < num_tracks) {
	new_cursor.track++;
	new_cursor.col = 0;
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == 'i') {
      // create new instrument
    } else {
      return false;
    }
  } else if (input.hasAlt()) {
    if (input.getId() == NCKEY_LEFT) {
      // move selected track to left
      return true;
    } else if (input.getId() == NCKEY_RIGHT) {
      // move selected track to right
      return true;
    }
  } else if (!input.hasMeta()) {
    if (input.getId() == '[') {
      if (current_keyboard_octave > 0) current_keyboard_octave--;
      return true;
    } else if (input.getId() == ']') {
      if (current_keyboard_octave < 9) current_keyboard_octave++;
      return true;
    } else if (input.getId() == NCKEY_LEFT) {
      if (new_cursor.col > 0) {
	new_cursor.col--;
	new_cursor.subcol = 0;
      } else if (new_cursor.track > 0) {
	new_cursor.track--;
	new_cursor.subcol = 0;

	auto it = all_track_info.find(track_ids[new_cursor.track]);
	new_cursor.col = it != all_track_info.end() ? it->second.getColumnCount() - 1 : 0;
      }
      return true;
    } else if (input.getId() == NCKEY_RIGHT) {
      if (new_cursor.col + 1 < track_info.getColumnCount()) {
	new_cursor.col++;
	new_cursor.subcol = 0;
      } else if (new_cursor.track + 1 < num_tracks) {
	new_cursor.track++;
	new_cursor.col = 0;
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == NCKEY_BUTTON4) { // scroll wheel up - plain Up is now "move-row-up" (see the keymap)
      if (!info.isPlaying()) {
	getController().moveEditPosition(-1);
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == NCKEY_BUTTON5) { // scroll wheel down - plain Down is now "move-row-down"
      if (!info.isPlaying()) {
	getController().moveEditPosition(1);
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == NCKEY_PGUP) {
      if (!info.isPlaying()) {
	getController().moveEditPosition(-16);
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == NCKEY_PGDOWN) { // scrollwheel down
      if (!info.isPlaying()) {
	getController().moveEditPosition(16);
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == '\t') {
      if (track_info.isEffectColumn(new_cursor.col)) { // effect
	new_cursor.subcol = (new_cursor.subcol + 1) % 4;
      } else if (!track_info.isNoteColumn(new_cursor.col)) {
	new_cursor.subcol = (new_cursor.subcol + 1) % 2;
      }
      return true;
    } else if (input.getId() == NCKEY_INS) {
      auto & pattern = song.getPattern(info.getPatternIndex());
      int track_id = track_ids[new_cursor.track];
      pattern.insertRow(info.getRowIndex(), track_id, song.getPatternLength());
      song.incVersion();
      return true;
    } else {
      auto & pattern = song.getPattern(info.getPatternIndex());
      int track_id = track_ids[new_cursor.track];
      auto column_type = track_info.getColumnType(new_cursor.col);
    
      if (column_type == ColumnType::EFFECT) {
	// Delete/Backspace clear the whole 4-character command, regardless
	// of which of its subcol characters the cursor happens to be on -
	// Command has no meaningful "delete just this one character" (a
	// mnemonic's two letters and its argument are only ever valid
	// together, see Command.h) - same reasoning kill-region's own
	// include_command path already uses. Without this explicit check,
	// these two keys used to fall into the permissive "first two
	// characters can be anything" branch below whenever the cursor was
	// on subcol 0/1, silently writing NCKEY_DEL/NCKEY_BACKSPACE's own
	// (non-ASCII, notcurses-internal) key code into the command as if
	// it were a typed character, instead of being ignored (subcol 2/3)
	// or actually deleting.
	if (input.getId() == NCKEY_DEL || input.getId() == NCKEY_BACKSPACE) {
	  pattern.setCommand(info.getRowIndex(), track_id, Command());
	  row_edited = true;
	  // Same row-level Backspace-steps-back/Delete-stays-put distinction
	  // the note column's own is_delete handling makes below.
	  if (!info.isPlaying() && input.getId() == NCKEY_BACKSPACE) {
	    getController().moveEditPosition(-edit_step_size);
	  }
	  return true;
	}

	// In effect command, the first two characters can be any letter,
	// digit, or '/' (Command::isMnemonicChar() - see its own comment);
	// the rest is a dash or hex value. Without the isMnemonicChar()
	// guard (a plain `true` here, as it effectively was before), every
	// unbound non-printable key (arrows/F-keys/Insert/PageUp/... not
	// already intercepted by an earlier else-if branch above, or
	// Ctrl/Alt chords with no keymap entry) is a notcurses key code far
	// outside any printable range, and would otherwise get silently
	// written into the command as if it were a typed character.
	if (input_hex_value != -1 || input.getId() == '-' || (new_cursor.subcol < 2 && Command::isMnemonicChar(input.getId()))) {
	  auto command = pattern.getCommand(info.getRowIndex(), track_id);
	  command.updateData(new_cursor.subcol, toupper(input.getId()));
	  pattern.setCommand(info.getRowIndex(), track_id, command);
	  row_edited = true;

	  if (new_cursor.subcol + 1 < 4) {
	    new_cursor.subcol++;
	  } else if (new_cursor.track + 1 < num_tracks) {
	    new_cursor.track++;
	    new_cursor.col = 0;
	    new_cursor.subcol = 0;
	  }
	  return true;
	}
      } else if (column_type == ColumnType::VELOCITY || column_type == ColumnType::DELAY) {
	if (input_hex_value != -1) {
	  auto & notes = pattern.getNotes(info.getRowIndex(), track_id);
	  auto note_column = track_info.getNoteNumber(new_cursor.col);
	  Note note;
	  if (note_column < notes.size()) note = notes[note_column];
	  int current_value = column_type == ColumnType::VELOCITY ? note.getVelocity() : note.getDelay();
	  if (new_cursor.subcol == 0) current_value = (input_hex_value << 4) | (current_value & 0x0f);
	  else current_value = (current_value & 0xf0) | input_hex_value;
	  if (column_type == ColumnType::VELOCITY) note.setVelocity(current_value);
	  else note.setDelay(current_value);
	  pattern.setNote(info.getRowIndex(), track_id, note_column, note);
	  row_edited = true;
	  if (new_cursor.subcol == 0) {
	    new_cursor.subcol++;
	  } else if (new_cursor.col + 1 < track_info.getColumnCount()) {
	    new_cursor.col++;
	    new_cursor.subcol = 0;
	  } else if (new_cursor.track + 1 < num_tracks) {
	    new_cursor.track++;
	    new_cursor.col = 0;
	    new_cursor.subcol = 0;
	  }
	  return true;
	}
      } else {
	// SAMPLE/DRUM_MACHINE tracks render this column as a placeholder
	// block (see renderRow's own branch), never real note data - without
	// this exclusion, typing here would still silently write into
	// Pattern via pattern.setNote()/pushNote() below, just with nothing
	// on screen to show it happened. A DrumMachineTrack's sequence lives
	// on the track itself (DrumMachineTrack.h), never in Pattern rows,
	// so this guard is required for correctness there, not just tidiness.
	auto entry_track = song.getTrackByInternalId(track_id);
	if (entry_track && (entry_track->getType() == TrackType::SAMPLE || entry_track->getType() == TrackType::DRUM_MACHINE)) {
	  return true;
	}

	bool is_off = input.getId() == 'a';
	bool is_delete = input.getId() == NCKEY_DEL || input.getId() == NCKEY_BACKSPACE;
	auto note_column = track_info.getNoteNumber(new_cursor.col);
	auto current_delay = info.getCurrentDelay();

	// A held note key's terminal-generated auto-repeat must not retrigger
	// a fresh note-on (holding a key should sustain one note, not restart
	// its envelope over and over) - RELEASE itself is handled once, up
	// front in this function (see offerInput()'s own top-of-function
	// comment), so only REPEAT needs handling here.
	bool is_repeat = input.getKind() == InputEvent::Kind::REPEAT;

	int midi_note = -1;
	if (!is_off) {
	  auto track = song.getTrackByInternalId(track_id);
	  auto tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
	  midi_note = input.toMidiNote(current_keyboard_octave, tuning);
	}

	if (is_repeat && midi_note >= 0) return true; // already sounding - nothing to redo

	if (is_delete || midi_note >= 0 || is_off) {
	  if (is_delete) {
	    pattern.deleteNote(info.getRowIndex(), track_id, note_column);
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, track_id, note_column));
	  } else if (is_off) {
	    pattern.setNote(info.getRowIndex(), track_id, note_column, Note(0, 0, current_delay));
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, track_id, note_column));
	  } else {
	    Note note(midi_note, 0x28, current_delay);

	    // A terminal that never negotiated the Kitty keyboard protocol
	    // reports every keystroke as InputEvent::Kind::UNKNOWN (see its
	    // own doc comment) - there is no way to ever learn such a key was
	    // released, so none of the hold-tracking machinery below
	    // (auto-play-while-held, whole-row replace, the live-column
	    // collision check, active_keyboard_notes_ itself) can safely run:
	    // an entry added for an UNKNOWN key would never be removed,
	    // permanently occupying "a note is held" state and (among other
	    // things) engaging auto-play exactly once, on the very first
	    // note, then never letting go - which is the bug this guard
	    // fixes. Falls back to the simple, immediate, one-shot-per-
	    // keystroke behavior this codebase always had before Kitty
	    // support existed.
	    bool has_hold_info = input.getKind() != InputEvent::Kind::UNKNOWN;

	    // Realtime auto-play-while-held (mirrors LaunchpadManager's own -
	    // see its PRESS branch for the identical reasoning): the first
	    // held note key, while stopped, engages real transport playback
	    // for the duration of the hold, so rows advance at the song's
	    // actual tempo instead of everything landing on one static row.
	    // Engaged *before* this key's own write below, so - when this is
	    // the session-starting key - the very first row gets cleared
	    // ahead of this note landing on it, not after.
	    bool was_first_held_note = has_hold_info && active_keyboard_notes_.empty();
	    if (was_first_held_note && !info.isPlaying()) {
	      getController().togglePlaying();
	      // Mutes only the song's own pattern-driven scheduling
	      // (SongState::render()'s own comment has the full reasoning) -
	      // never the live PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE path this
	      // key's own sound comes through.
	      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_RECORDING_MUTE, 1));
	      auto_started_playback_ = true;
	      auto_record_cleared_rows_.clear();
	      last_cleared_row_ = -1;
	      last_cleared_pattern_idx_ = -1;
	    }

	    if (input.hasShift()) {
	      if (auto_started_playback_) ensureRowCleared(song, info.getPatternIndex(), info.getRowIndex(), track_id);
	      note_column = pattern.pushNote(info.getRowIndex(), track_id, note);
	    } else {
	      // A lone key still lands exactly on the cursor's own column,
	      // unchanged - only steps off it when another currently-held key
	      // already claims that column (a genuine chord), so it sounds
	      // alongside the others instead of stealing the voice already
	      // there. See isKeyColumnLiveHeld()'s own comment. Meaningless
	      // (and always false, since active_keyboard_notes_ never gets an
	      // entry) without hold info anyway - simultaneous polyphony
	      // isn't distinguishable from quick sequential taps on a
	      // terminal with no hold tracking at all.
	      while (isKeyColumnLiveHeld(track_id, note_column)) note_column++;
	      // Whole-row replace semantics for a live take: idempotent (see
	      // ensureRowCleared's own comment), safe to call defensively -
	      // only actually does anything the first time (row, track_id) is
	      // touched this session.
	      if (auto_started_playback_) ensureRowCleared(song, info.getPatternIndex(), info.getRowIndex(), track_id);
	      pattern.setNote(info.getRowIndex(), track_id, note_column, note);
	    }

	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note_column, note.getValue(), note.getVelocity()));
	    if (has_hold_info) active_keyboard_notes_[input.getId()] = { note_column, info.getRowIndex(), track_id };
	  }

	  row_edited = true;

	  if (!info.isPlaying()) {
	    int n = 0;
	    // Backspace mirrors a text editor's own backspace: delete (already
	    // done above) and step backward, undoing the forward step a note
	    // entry would have made - unlike Delete, which deletes in place and
	    // leaves the cursor where it was (Renoise draws the same
	    // distinction between the two keys).
	    if (input.getId() == NCKEY_BACKSPACE) n = -1;
	    else if (input.getId() != NCKEY_DEL) n = 1;
	    if (n) {
	      getController().moveEditPosition(n * edit_step_size);
	    }
	  }

	  return true;
	}
      }
    }
  }
  
  return false;
}

void
PatternEditor::renderHeading(const StyleProvider & styles, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & all_track_info) {
  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();
  auto & pattern = song.getPattern(info.getPatternIndex());

  auto [rows, cols] = getDim();

  unordered_map<int, Track *> track_parents;
  for (auto & track : song.getTracks()) {
    get_track_parents(*track, nullptr, track_parents);
  }

  auto heading_height = song.getTrackDepth() + 1;

  string padding(cols, ' ');
  
  setBgColor(styles.window_bg_color);
  for (auto i = 0; i < heading_height; i++) {
    putstr(i, 0, padding);
  }
  
  auto & instruments = song.getInstruments();
  
  for (auto level = 0; level < heading_height - 1; level++) {
    vector<Track *> tracks;
    vector<int> track_widths;

    for (auto i = 0; i < static_cast<int>(track_ids.size()); i++) {
      int track_id = track_ids[i];
      auto track = song.getTrackByInternalId(track_id);
      for (auto k = 0; k < level && track; k++) {
	auto it = track_parents.find(track->getInternalId());
	if (it != track_parents.end()) track = it->second;
	else track = nullptr;	
      }
      auto it = all_track_info.find(track_id);
      auto w = it != all_track_info.end() ? it->second.getTrackWidth() : 0;
      // current_scroll_.col skips this many of the leftmost visible
      // track's own leading columns (see render()'s own comment) - shrink
      // its contribution here so the heading stays aligned with what
      // renderRow() actually draws for it.
      if (i == current_scroll_.track && current_scroll_.col > 0 && it != all_track_info.end()) {
	for (auto k = 0; k < current_scroll_.col; k++) w -= it->second.getColumnWidth(k);
      }

      if (!tracks.empty() && tracks.back() == track) {
	track_widths.back() += w;
      } else {
	tracks.push_back(track);
	track_widths.push_back(w);
      }
    }
    
    auto current_pos = 5;
    for (auto i = 0; i < static_cast<int>(tracks.size()); i++) {
      if (i < current_scroll_.track) continue;
      if (current_pos >= cols) break;
      
      auto track = tracks[i];
      auto actual_width = track_widths[i];

      if (track) {
	if (level == 0) {
	  setFgColor(0x00, 0x00, 0x00);
	  setBgColor(0xf0, 0x80, 0x10);

	  auto text_width = actual_width - 3;

	  bool is_solo = false, is_muted = false;
	  string instrument_name;
	  if (track->getType() == TrackType::SAMPLE) {
	    instrument_name = "Sample";
	  } else if (track->getType() == TrackType::DRUM_MACHINE) {
	    instrument_name = "Drum Machine";
	  } else if (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL) {
	    auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
	    if (instrument_track.getInstrumentId() >= 0 && instrument_track.getInstrumentId() < instruments.size()) {
	      instrument_name = instruments[instrument_track.getInstrumentId()]->getName();
	      is_solo = instrument_track.isSolo();
	      is_muted = instrument_track.isMuted();
	    }
	  }
	  auto name = !track->getName().empty() ? track->getName() : (!track->getId().empty() ? "Trk " + track->getId() : format("Trk {:02d}", track->getInternalId()));
	  if (name.size() > text_width) name.erase(text_width);
	  else {
	    while (name.size() < text_width) name += ' ';
	  }
	  putstr(heading_height - 2 - level, current_pos, name);
	  putstr(heading_height - 2 - level, current_pos + text_width + 2, "│");
	  if (is_muted) setFgColor(0x00, 0x00, 0x00);
	  else setFgColor(0xe0, 0x70, 0x08);
	  putstr(heading_height - 2 - level, current_pos + text_width, "M");
	  if (is_solo) setFgColor(0x00, 0x00, 0x00);
	  else setFgColor(0xe0, 0x70, 0x08);
	  putstr(heading_height - 2 - level, current_pos + text_width + 1, "S");
	  
	  setFgColor(0xf0, 0xf0, 0xf0);
	  setBgColor(styles.window_bg_color);
	  	  
	  if (instrument_name.size() > actual_width - 1) instrument_name.erase(actual_width - 1);
	  putstr(heading_height - 2 - level + 1, current_pos, instrument_name);
	} else {	  
	  std::string name = track->getElementName();
	  auto & track_info = info.getTrackInfo(track->getInternalId());
	  
	  if (name.size() > actual_width - 4) name.erase(actual_width - 4);
	  else {
	    while (name.size() < actual_width - 4) name += ' ';
	  }
	  name += "│";

	  setBgColor(0x50, 0x50, 0x60);

	  if (track_info.isClipping()) {
	    setFgColor(0xe0, 0x10, 0x40);
	  } else {
	    setFgColor(0x10, 0xe0, 0x40);
	  }
	  
	  putstr(heading_height - 2 - level, current_pos, track_info.isActive() ? " • " : "   ");
	  
	  setFgColor(0x00, 0x00, 0x00);
	  putstr(heading_height - 2 - level, current_pos + 3, name);
	}
      }
      
      current_pos += actual_width;
    }

    // Extend the leaf-track heading row's orange background the rest of
    // the way to the right edge, past the last visible track - otherwise
    // it stops wherever the tracks happen to end, leaving the remainder
    // of the row at the plain window_bg_color fill above instead of
    // reading as one continuous divider bar under the scopes.
    if (level == 0 && current_pos < cols) {
      setFgColor(0x00, 0x00, 0x00);
      setBgColor(0xf0, 0x80, 0x10);
      putstr(heading_height - 2 - level, current_pos, string(static_cast<size_t>(cols - current_pos), ' '));
    }
  }
}

void
PatternEditor::renderRow(const StyleProvider & styles, int heading_height, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & all_track_info, int display_row, bool highlight, const SelectionBounds & sel_bounds) {
  auto [rows, cols] = getDim();

  if (display_row >= rows - heading_height) {
    return;
  }
    
  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();
  auto [ pattern_idx, pattern_row ] = song.normalizePosition(info.getPatternIndex(), display_row + current_scroll_.row);
  bool is_neighboring_pattern = info.getPatternIndex() != pattern_idx;
  auto & pattern = song.getPattern(pattern_idx);

  display_row += heading_height;
          
  string padding(cols, ' ');
    
  setBgColor(styles.window_bg_color);
  putstr(display_row, 0, padding);
    
  auto current_pos = 0;
  for (int i = -1; i < static_cast<int>(track_ids.size()); i++) {
    if (i >= 0 && i < current_scroll_.track) continue;
    if (current_pos >= cols) break;
    
    UIColor fg, bg, cell_fg, cell_bg;
      
    if (highlight) {
      fg = UIColor("#80c080");
      bg = UIColor("#80a080");
    } else if (pattern_row % 4 == 0) {
      fg = styles.window_accent_fg_color;
      bg = styles.window_accent_bg_color;
    } else {
      fg = styles.window_fg_color;
      bg = styles.window_bg_color;
    }

    if (is_neighboring_pattern) {
      UIColor black;
      bg = bg.blend(0.75f, black);
      fg = fg.blend(0.75f, black);
    }

    // There's always an effective region to highlight, even with no mark
    // set - it degenerates to just the note under the cursor (see
    // getEffectiveSelectionBounds, computed once per render() call).
    bool row_track_in_selection = pattern_idx == info.getPatternIndex() &&
      i >= sel_bounds.track_lo && i <= sel_bounds.track_hi &&
      pattern_row >= sel_bounds.row_lo && pattern_row <= sel_bounds.row_hi;
    // A single-track region is scoped to specific note columns (see
    // set-mark/kill-region) - highlight per-column inside the loop below
    // instead of the whole track uniformly.
    bool column_scoped_selection = row_track_in_selection && sel_bounds.column_scoped;
    bool in_selection = row_track_in_selection && !column_scoped_selection;
    if (in_selection) {
      fg = styles.highlight_fg_color;
      bg = styles.highlight_bg_color;
    }

    if (i == -1) {
      setFgColor(fg);
      setBgColor(bg);
	
      putstr(display_row, current_pos, format(" {:02x} ", pattern_row));
	
      setFgColor(styles.window_border_color);
      setBgColor(styles.window_bg_color);
	
      putstr(display_row, current_pos + 4, "│");
	
      current_pos += 5;
    } else {
      auto track_id = track_ids[i];
      auto & notes = pattern.getNotes(pattern_row, track_id);
      auto & command = pattern.getCommand(pattern_row, track_id);
      VisibleTrackInfo track_info;
      auto it = all_track_info.find(track_id);
      if (it != all_track_info.end()) track_info = it->second;
      auto track = song.getTrackByInternalId(track_id);

      // current_scroll_.col skips this many of this track's own leading
      // columns - only meaningful for the leftmost visible track (see
      // render()'s own comment); every other track always starts at its
      // own column 0.
      auto first_col = i == current_scroll_.track ? current_scroll_.col : 0;
      for (auto k = first_col; k < track_info.getColumnCount(); k++) {
	if (k != first_col) {
	  putstr(display_row, current_pos++, " ");
	}
	// Only drives the active-character underline for numeric columns
	// further down now (which numbers columns are highlighted right at
	// the cursor's own row, unlike column_selected below) - a single-row,
	// exact-cursor-position flag, not a region membership one.
	bool column_highlighted = highlight && current_cursor.isHighlighted(i, k);
	// Per-column override of the track-level fg/bg for a single-track
	// (column-scoped) region. The effect column isn't part of the
	// note-range check (it's not a note column at all), so it's covered
	// by sel_bounds.includes_command instead - set across the *whole*
	// selected row range whenever the cursor is on the effect column
	// (see getEffectiveSelectionBounds()), not just column_highlighted's
	// single row: using column_highlighted here left every row but the
	// cursor's own unhighlighted after widening a multi-row note-column
	// selection to include the effect column.
	bool column_selected = column_scoped_selection &&
	  ((!track_info.isEffectColumn(k) &&
	    track_info.getNoteNumber(k) >= sel_bounds.note_lo && track_info.getNoteNumber(k) <= sel_bounds.note_hi) ||
	   (track_info.isEffectColumn(k) && sel_bounds.includes_command));
	UIColor cur_fg = column_selected ? styles.highlight_fg_color : fg;
	UIColor cur_bg = column_selected ? styles.highlight_bg_color : bg;

	setFgColor(styles.window_border_color);
	setBgColor(cur_bg);
	auto column_type = track_info.getColumnType(k);
	if (track && (track->getType() == TrackType::SAMPLE || track->getType() == TrackType::DRUM_MACHINE)) {
	  cell_fg = cur_fg;
	  cell_bg = cur_bg;
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);

	  // Placeholder width matches track_info.getTrackWidth() exactly,
	  // not a hardcoded literal - renderHeading() already laid this
	  // column out assuming that same width, so a mismatched literal
	  // here left the heading, this content, and the region-selection
	  // highlight (which colors exactly however many characters this
	  // putstr writes) all disagreeing about how wide the column
	  // actually is. One character short of the full width, same as
	  // every other column type below (NOTE/VELOCITY/DELAY/EFFECT) -
	  // the last column of a track always leaves its own final
	  // character for the shared trailing "│" this k-loop draws once
	  // it's done (further down), rather than drawing it itself.
	  auto width = std::max(track_info.getTrackWidth() - 1, 1);
	  bool defined = k < notes.size() && notes[k].isDefined();
	  putstr(display_row, current_pos, std::string(static_cast<size_t>(width), defined ? 'x' : ' '));
	  current_pos += width;
	} else if (column_type == ColumnType::EFFECT) {
	  cell_fg = command.isDefined() ? styles.command_column_color : cur_fg;
	  cell_bg = cur_bg;
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);
	  auto s = to_string(command);
	  putstr(display_row, current_pos, s);
	  if (column_highlighted) {
	    setUnderline(true);
	    putstr(display_row, current_pos + new_cursor.subcol, s[new_cursor.subcol]);
	    setUnderline(false);
	  }
	  current_pos += 4;
	} else if (column_type == ColumnType::NOTE) {
	  auto l = track_info.getNoteNumber(k);
	  auto note = l < notes.size() ? notes[l] : Note();

	  cell_fg = cur_fg;
	  cell_bg = cur_bg;
	  if (!note.isDefined()) cell_fg = cell_fg.blend(0.5f, cell_bg);
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);
	  auto tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
	  putstr(display_row, current_pos, note.toString(tuning));
	  current_pos += 3;
	} else if (column_type == ColumnType::VELOCITY || column_type == ColumnType::DELAY) {
	  auto l = track_info.getNoteNumber(k);
	  auto note = l < notes.size() ? notes[l] : Note();
	  string s;
	  if (note.isDefined()) {
	    if (column_type == ColumnType::VELOCITY && note.isOff()) {
	      s = "  ";
	    } else {
	      s = format("{:02x}", column_type == ColumnType::VELOCITY ? note.getVelocity() : note.getDelay());
	    }
	  } else {
	    s = "--";
	  }

	  // The bright velocity/delay colors are tuned for contrast against the
	  // normal dark row background; inside the (bright) effective-region
	  // highlight they'd be nearly unreadable, so use the region's own
	  // (dark) foreground there instead - same idea as the note column.
	  cell_fg = column_selected ? cur_fg : (column_type == ColumnType::VELOCITY ? UIColor("#bfa426") : UIColor("#42c1ea"));
	  cell_bg = cur_bg;
	  if (!note.isDefined()) cell_fg = cell_fg.blend(0.5f, cell_bg);
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);

	  putstr(display_row, current_pos, s);
	  if (column_highlighted) {
	    setUnderline(true);
	    putstr(display_row, current_pos + new_cursor.subcol, s[new_cursor.subcol]);
	    setUnderline(false);
	  }
	  current_pos += 2;
	}
      }

      setFgColor(styles.window_border_color);
      setBgColor(bg);
      putstr(display_row, current_pos, "│");
      current_pos++;
    }
  }

  if (current_pos + 2 < cols) {
    auto & annotation = pattern.getAnnotation(pattern_row);
    if (!annotation.empty()) {
      setFgColor("#e03030");
      setBgColor("#702020");
      putstr(display_row, current_pos + 2, annotation);
    }
  }
}
