#include "PatternEditor.h"

#include "../playback/InputEvent.h"
#include "../state/SongState.h"
#include "../Controller.h"
#include "StyleProvider.h"
#include "../instruments/Tuner.h"
#include "../instruments/Tuning.h"
#include "../model/LeafTrack.h"
#include "../model/InstrumentTrack.h"
#include "../model/SampleTrack.h"
#include "../model/DrumMachineTrack.h"
#include "../playback/MidiEvent.h"
#include "../playback/PlaybackControlEvent.h"
#include "../playback/LogEvent.h"
#include "KeyChord.h"
#include "PatternScroll.h"
#include "../util/Utf8.h"

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
  // Whichever buffer is already active by construction time (main.cpp
  // always opens/creates one before the UI itself exists) - without this,
  // handleBufferChanged()'s first real call would have nothing to compare
  // against and would skip saving this buffer's own accumulated state
  // (see that method's own comment) the first time the user ever switches
  // away from it.
  last_active_song_ = &getController().getSong();
  last_active_buffer_name_ = getController().getActiveBufferName();

  // getPlane().setScrolling(true);

  // Emacs-style commands, dispatched centrally via UIElement::dispatchCommand
  // (see offerInput() below). Each lambda re-fetches song/info/track_ids
  // itself, exactly like the code that used to run inline here did on every
  // call - none of this is cached across calls, so there's no staleness risk
  // from moving it into a constructor-time closure.

  commands_.define("set-mark", [this]() {
    auto & info = getController().getPlaybackInfo();
    selection_start_pattern_ = info.getPatternIndex();
    selection_start_row_ = info.getRowIndex();
    selection_start_track_ = current_cursor.track;
    // A fresh mark starts scoped to just the column it's set on; moving
    // sideways afterward widens/narrows the touched range (see
    // getEffectiveSelectionBounds) - to select the whole track, widen past
    // every note column (and the effect column, if reached too), and past
    // the annotation to select the whole row.
    selection_start_col_ = current_cursor.col;
    selection_start_scope_ = current_cursor.scope;
    setSelectionActive(true);
    getController().getUIEventQueue().push(make_unique<LogEvent>("Mark set"));
  });

  // Emacs's own C-x C-x: swaps mark and point, keeping the region's two
  // endpoints exactly the same (just relabeled) and the mark active, so a
  // second press swaps right back. Requires a real mark, unlike kill-region/
  // transpose-region-*'s degenerate no-mark fallback (getEffectiveSelectionBounds) -
  // there's no point exchanging point with itself. selection_start_pattern_
  // is left untouched: render() already clears the mark the instant it and
  // the cursor land in different patterns (see its own pattern-boundary
  // check), so a live mark and point are always in the same pattern already.
  // Bound from UI, not here (see UI.cpp's own "exchange-point-and-mark"
  // definition) - the C-x prefix itself is recognized at that level.
  commands_.define("exchange-point-and-mark", [this]() {
    if (!selection_active_) {
      getController().getUIEventQueue().push(make_unique<LogEvent>("No mark set"));
      return;
    }
    auto & info = getController().getPlaybackInfo();
    auto old_point_row = info.getRowIndex();
    auto old_point_track = current_cursor.track;
    auto old_point_col = current_cursor.col;
    auto old_point_scope = current_cursor.scope;

    getController().moveEditPosition(selection_start_row_ - old_point_row);
    new_cursor.track = selection_start_track_;
    new_cursor.col = selection_start_col_;
    new_cursor.subcol = 0;
    new_cursor.scope = selection_start_scope_;

    selection_start_row_ = old_point_row;
    selection_start_track_ = old_point_track;
    selection_start_col_ = old_point_col;
    selection_start_scope_ = old_point_scope;

    getController().getUIEventQueue().push(make_unique<LogEvent>("Mark and point exchanged"));
  });

  // Both commands below always have a region to act on, even with no mark
  // active: it degenerates to the single note the cursor is currently on
  // (see getEffectiveSelectionBounds) - there's no "No selection" case.
  commands_.define("kill-region", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto track_ids = song.getRootTrackIds();
    auto & scene = song.getScene(info.getPatternIndex());

    auto b = getEffectiveSelectionBounds(song, track_ids);
    clipboard_.scope = b.scope;
    if (b.scope == SelectionScope::TRACK) {
      clipboard_.cells = copyPatternBlock(scene, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
      clipboard_.commands.clear();
      clipboard_.annotations.clear();
      clearPatternBlock(scene, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
    } else if (b.scope == SelectionScope::NOTE_COLUMN) {
      auto track_id = track_ids[static_cast<size_t>(b.track_lo)];
      clipboard_.cells = copyPatternBlockNotes(scene, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi);
      clipboard_.commands.clear();
      clipboard_.annotations.clear();
      clearPatternBlockNotes(scene, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi);
    } else if (b.scope == SelectionScope::COMMAND) {
      auto track_id = track_ids[static_cast<size_t>(b.track_lo)];
      clipboard_.commands = copyPatternBlockCommand(scene, b.row_lo, b.row_hi, track_id);
      clipboard_.cells.clear();
      clipboard_.annotations.clear();
      clearPatternBlockCommand(scene, b.row_lo, b.row_hi, track_id);
    } else if (b.scope == SelectionScope::ANNOTATION) {
      clipboard_.annotations = copyPatternBlockAnnotations(scene, b.row_lo, b.row_hi);
      clipboard_.cells.clear();
      clipboard_.commands.clear();
      clearPatternBlockAnnotations(scene, b.row_lo, b.row_hi);
    } else { // EVERYTHING - every track (TRACK's own PatternBlock capture)
      // plus the annotation (ANNOTATION's own capture), both at once - see
      // ClipboardEntry.h's own comment.
      clipboard_.cells = copyPatternBlock(scene, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
      clipboard_.commands.clear();
      clipboard_.annotations = copyPatternBlockAnnotations(scene, b.row_lo, b.row_hi);
      clearPatternBlock(scene, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
      clearPatternBlockAnnotations(scene, b.row_lo, b.row_hi);
    }
    song.incVersion();
    setSelectionActive(false);
    // move point to the start of the killed region, matching Emacs
    // kill-region, so an immediate yank restores it exactly in place
    getController().moveEditPosition(b.row_lo - info.getRowIndex());
    new_cursor.track = b.track_lo;
    if (b.scope == SelectionScope::TRACK || b.scope == SelectionScope::EVERYTHING) {
      // Only reset to the first column for a whole-track (or whole-row)
      // kill; a narrower kill (one or more note columns, or just the
      // command) should leave the cursor on the column it was already on,
      // not jump back to 0. EVERYTHING also needs its scope reset off
      // ANNOTATION explicitly - point may have been sitting on the
      // annotation slot (track/col pinned to the last track's last column
      // regardless - see GridPosition::scope's own comment), and the kill
      // just cleared it - landing back at (track 0, col 0) on the grid,
      // like TRACK, reads as the more useful "start of the killed region"
      // than staying parked on the now-empty annotation.
      new_cursor.col = new_cursor.subcol = 0;
      new_cursor.scope = SelectionScope::NOTE_COLUMN;
    } else if (b.scope == SelectionScope::NOTE_COLUMN) {
      // Killing the track's last remaining voice can shrink its note-column
      // count (num_subtracks_ is derived from the widest row left in the
      // pattern). A raw index-bounds check isn't enough here: the old
      // index can still be "in range" of the narrower layout while meaning
      // something entirely different now (getNoteNumber() mechanically
      // extrapolates past the end the same way it does for the effect
      // column, so a stale index can silently resolve to the effect
      // column instead of clamping) - check via note number instead, and
      // snap to the corresponding sub-column of the last remaining voice.
      auto new_track_info = getTrackInfoFor(song, track_ids[static_cast<size_t>(b.track_lo)]);
      auto new_max_note = max(new_track_info.num_subtracks_ - 1, 0);
      if (new_track_info.getNoteNumber(new_cursor.col) > new_max_note) {
        auto n = (new_track_info.has_note_column_ ? 1 : 0) + new_track_info.num_velocity_columns_ +
          (new_track_info.has_delay_column_ ? 1 : 0);
        new_cursor.col = new_max_note * n;
        new_cursor.subcol = 0;
      }
    }
    // SelectionScope::COMMAND: clearing a Command never changes note-column
    // layout, so the cursor (already on the effect column) needs no snap.
    // ANNOTATION: same reasoning - clearing annotation text never touches
    // track/note-column layout at all, and the cursor is already parked on
    // the annotation slot (track/col pinned to the last track's last
    // column - see GridPosition::scope's own comment).
    getController().getUIEventQueue().push(make_unique<LogEvent>("Region killed"));
  });

  commands_.define("kill-ring-save", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto track_ids = song.getRootTrackIds();
    auto & scene = song.getScene(info.getPatternIndex());

    auto b = getEffectiveSelectionBounds(song, track_ids);
    clipboard_.scope = b.scope;
    if (b.scope == SelectionScope::TRACK) {
      clipboard_.cells = copyPatternBlock(scene, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
      clipboard_.commands.clear();
      clipboard_.annotations.clear();
    } else if (b.scope == SelectionScope::NOTE_COLUMN) {
      clipboard_.cells = copyPatternBlockNotes(scene, b.row_lo, b.row_hi, track_ids[static_cast<size_t>(b.track_lo)], b.note_lo, b.note_hi);
      clipboard_.commands.clear();
      clipboard_.annotations.clear();
    } else if (b.scope == SelectionScope::COMMAND) {
      clipboard_.commands = copyPatternBlockCommand(scene, b.row_lo, b.row_hi, track_ids[static_cast<size_t>(b.track_lo)]);
      clipboard_.cells.clear();
      clipboard_.annotations.clear();
    } else if (b.scope == SelectionScope::ANNOTATION) {
      clipboard_.annotations = copyPatternBlockAnnotations(scene, b.row_lo, b.row_hi);
      clipboard_.cells.clear();
      clipboard_.commands.clear();
    } else { // EVERYTHING - see kill-region's own comment.
      clipboard_.cells = copyPatternBlock(scene, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
      clipboard_.commands.clear();
      clipboard_.annotations = copyPatternBlockAnnotations(scene, b.row_lo, b.row_hi);
    }
    setSelectionActive(false);
    getController().getUIEventQueue().push(make_unique<LogEvent>("Region copied"));
  });

  commands_.define("yank", [this]() {
    bool clipboard_empty = clipboard_.scope == SelectionScope::COMMAND ? clipboard_.commands.empty() :
      clipboard_.scope == SelectionScope::ANNOTATION ? clipboard_.annotations.empty() :
      clipboard_.cells.empty();
    if (!clipboard_empty) {
      auto & song = getController().getSong();
      auto & info = getController().getPlaybackInfo();
      auto track_ids = song.getRootTrackIds();
      auto & scene = song.getScene(info.getPatternIndex());
      if (clipboard_.scope == SelectionScope::TRACK) {
        pastePatternBlock(scene, clipboard_.cells, song.getPatternLength(), info.getRowIndex(), track_ids, current_cursor.track);
      } else if (clipboard_.scope == SelectionScope::NOTE_COLUMN) {
        auto track_id = track_ids[static_cast<size_t>(current_cursor.track)];
        auto track_info = getTrackInfoFor(song, track_id);
        auto target_note = clamp(track_info.getNoteNumber(current_cursor.col), 0, max(track_info.num_subtracks_ - 1, 0));
        pastePatternBlockNotes(scene, clipboard_.cells, song.getPatternLength(), info.getRowIndex(), track_id, target_note);
      } else if (clipboard_.scope == SelectionScope::COMMAND) {
        auto track_id = track_ids[static_cast<size_t>(current_cursor.track)];
        pastePatternBlockCommand(scene, clipboard_.commands, song.getPatternLength(), info.getRowIndex(), track_id);
      } else if (clipboard_.scope == SelectionScope::ANNOTATION) {
        // Row-keyed only, no track involved at all.
        pastePatternBlockAnnotations(scene, clipboard_.annotations, song.getPatternLength(), info.getRowIndex());
      } else { // EVERYTHING - cells always cover every track (that's what
        // "every track, and the annotation" means - see
        // getEffectiveSelectionBounds()), so unlike TRACK's own paste this
        // always targets track 0 rather than current_cursor.track: there's
        // no sense in which a whole-row block gets "shifted" to start at a
        // different track, only the row can move.
        pastePatternBlock(scene, clipboard_.cells, song.getPatternLength(), info.getRowIndex(), track_ids, 0);
        pastePatternBlockAnnotations(scene, clipboard_.annotations, song.getPatternLength(), info.getRowIndex());
      }
      song.incVersion();
      getController().getUIEventQueue().push(make_unique<LogEvent>("Yanked"));
    } else {
      getController().getUIEventQueue().push(make_unique<LogEvent>("Clipboard empty"));
    }
  });

  // Whole-row, every track at once (notes and effect command alike,
  // Scene::insertRow() shifts both together) plus the row annotation - the
  // same "kill-line acts on the whole line, mark or no mark" scope Emacs's
  // own C-k already implies (see kill-row below), not a single-track
  // operation. Promoted from the raw Ins-key handler, now reachable by
  // name (M-x, a menu item, Launchpad) rather than only a keystroke
  // notcurses happens to decode correctly on a given terminal.
  commands_.define("insert-row", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto & scene = song.getScene(info.getPatternIndex());
    scene.insertRow(info.getRowIndex(), song.getPatternLength());
    song.incVersion();
  });

  // Emacs's own C-k ("kill-line"): the inverse of insert-row's shift, same
  // whole-row scope (every track's notes and command, plus the row
  // annotation - kill-region/kill-ring-save's own EVERYTHING capture
  // shape, for a single row spanning every track). Unlike insert-row's own
  // destructive shift, this is a real "kill" (cut, not just delete): the
  // row's content is stashed in the clipboard first, so an immediate yank
  // restores it at the cursor. Not "just clear the row" - that's already
  // kill-region/Ctrl-W's job, region-scoped rather than whole-row-and-shift.
  commands_.define("kill-row", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.empty()) return;
    auto & scene = song.getScene(info.getPatternIndex());
    int row = info.getRowIndex();
    int track_hi = static_cast<int>(track_ids.size()) - 1;

    clipboard_.scope = SelectionScope::EVERYTHING;
    clipboard_.cells = copyPatternBlock(scene, row, row, track_ids, 0, track_hi);
    clipboard_.commands.clear();
    clipboard_.annotations = copyPatternBlockAnnotations(scene, row, row);

    scene.deleteRow(row, song.getPatternLength());
    song.incVersion();
    getController().getUIEventQueue().push(make_unique<LogEvent>("Row killed"));
  });

  commands_.define("keyboard-quit", [this]() {
    if (selection_active_) {
      setSelectionActive(false);
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
    auto & scene = song.getScene(info.getPatternIndex());
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
    if (b.scope == SelectionScope::TRACK) {
      transposePatternBlock(scene, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi, true, is_percussion);
    } else if (b.scope == SelectionScope::NOTE_COLUMN) {
      auto track_id = track_ids[static_cast<size_t>(b.track_lo)];
      transposePatternBlockNotes(scene, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, true, is_percussion(track_id));
    }
    // SelectionScope::COMMAND/ANNOTATION: nothing to transpose - Command.h
    // and Scene's annotation text both have no numeric/transposable
    // semantics. EVERYTHING: deliberately left alone too, even though its
    // PatternBlock half does have transposable notes - see
    // SelectionScope.h's own comment on why.
    song.incVersion();
  });

  commands_.define("transpose-region-down", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto & scene = song.getScene(info.getPatternIndex());
    auto track_ids = song.getRootTrackIds();

    // See transpose-region-up's own comment.
    auto is_percussion = [&song](int track_id) {
      auto * track = song.getTrackByInternalId(track_id);
      return track && track->getType() == TrackType::PERCUSSION_CONTROL;
    };

    auto b = getEffectiveSelectionBounds(song, track_ids);
    if (b.scope == SelectionScope::TRACK) {
      transposePatternBlock(scene, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi, false, is_percussion);
    } else if (b.scope == SelectionScope::NOTE_COLUMN) {
      auto track_id = track_ids[static_cast<size_t>(b.track_lo)];
      transposePatternBlockNotes(scene, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, false, is_percussion(track_id));
    }
    // SelectionScope::COMMAND: nothing to transpose - Command.h has no
    // numeric/transposable semantics. ANNOTATION/EVERYTHING: same - no
    // transposable content once the annotation is involved at all.
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
    auto track_id = getController().consumePendingCommandTrack(track_ids[static_cast<size_t>(current_cursor.track)]);
    getController().toggleTrackMuted(track_id);
  });

  commands_.define("toggle-solo", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.empty()) return;
    auto track_id = getController().consumePendingCommandTrack(track_ids[static_cast<size_t>(current_cursor.track)]);
    getController().toggleTrackSolo(track_id);
  });

  // Unlike toggle-mute/toggle-solo above, applies to whatever track the
  // cursor's column actually belongs to - track_ids here is
  // getRootTrackIds()'s full SongStructure-ordered list, so this already
  // reaches a nested effect track exactly as well as a top-level one, no
  // extra resolution needed (see Controller::toggleTrackCollapsed()'s own
  // comment on why it's generic over TrackType). Also reachable by pressing
  // Enter while the cursor sits on a collapsed track - see offerInput().
  commands_.define("toggle-track-collapse", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.empty()) return;
    auto track_id = getController().consumePendingCommandTrack(track_ids[static_cast<size_t>(current_cursor.track)]);
    getController().toggleTrackCollapsed(track_id);
  });

  // F2 (Renoise's own rename-track convention) opens the in-place editor
  // for whatever track the cursor's column belongs to - see
  // startTrackNameEdit()'s own comment for why it's a no-op on a track
  // with no name field at all.
  commands_.define("rename-track", [this]() { startTrackNameEdit(); });

  // Refuses to remove the last remaining root track: render() and several
  // sibling call sites index track_ids[cursor.track] with no bounds check
  // at all, on the assumption that at least one root track always exists
  // (see docs/known_bugs.md's zero-root-tracks entry) - this command is
  // the first thing that could actually reach that state, so it stays
  // above the floor rather than being the one to finally trigger it.
  // new_cursor is reset the same way the raw "jump to first track" (Ctrl-A)
  // handler already does below - the old column/subcol no longer means
  // anything once the track composition under it has changed.
  commands_.define("delete-track", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.size() <= 1) return;
    auto track_id = getController().consumePendingCommandTrack(track_ids[static_cast<size_t>(current_cursor.track)]);
    if (!song.removeTrack(track_id)) return;
    if (getController().getRecordingTrackId() == track_id) getController().setRecordingTrackId(0);
    auto remaining = static_cast<int>(song.getRootTrackIds().size());
    new_cursor.track = std::min(new_cursor.track, remaining - 1);
    new_cursor.col = new_cursor.subcol = 0;
  });

  // Renoise-style manual note-column add/remove (see Controller::
  // addNoteColumn/removeNoteColumn and LeafTrack::getMinNoteColumns) -
  // todo.txt's own long-standing "add shortcut for add note column" idea.
  commands_.define("add-note-column", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.empty()) return;
    auto track_id = getController().consumePendingCommandTrack(track_ids[static_cast<size_t>(current_cursor.track)]);
    getController().addNoteColumn(track_id);
  });

  commands_.define("remove-note-column", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (track_ids.empty()) return;
    auto track_id = getController().consumePendingCommandTrack(track_ids[static_cast<size_t>(current_cursor.track)]);
    getController().removeNoteColumn(track_id);
  });

  // addTrack() itself already bumps the version - no separate incVersion()
  // needed here, unlike add-drum-machine-track below (kept exactly as the
  // raw Ctrl-T handler this was promoted from, verbatim, always did).
  commands_.define("add-instrument-track", [this]() {
    auto & song = getController().getSong();
    song.addTrack(make_unique<InstrumentTrack>(0));
  });

  // Promoted from the raw Ctrl-R handler verbatim - "start recording"
  // really, not purely "add a track": reuses the current track if it's
  // already a SampleTrack, otherwise creates a fresh one and moves the
  // cursor onto it. song.incVersion() stays explicit (unlike
  // add-instrument-track just above) since the reuse branch never calls
  // addTrack() at all, so nothing else would bump the version for it.
  commands_.define("add-sample-track", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    auto current_track = current_cursor.track < static_cast<int>(track_ids.size()) ?
      song.getTrackByInternalId(track_ids[static_cast<size_t>(current_cursor.track)]) : nullptr;

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
  keymap_.bind(KeyChord::pack('k', true, false, false, false), "kill-row"); // Ctrl-K
  keymap_.bind(KeyChord::pack(NCKEY_INS, false, false, false, false), "insert-row"); // plain Insert (was inline handling)
  keymap_.bind(KeyChord::pack('g', true, false, false, false), "keyboard-quit");
  keymap_.bind(KeyChord::pack(NCKEY_UP, true, false, true, false), "transpose-region-up");    // Ctrl+Shift+Up
  keymap_.bind(KeyChord::pack(NCKEY_DOWN, true, false, true, false), "transpose-region-down"); // Ctrl+Shift+Down
  keymap_.bind(KeyChord::pack('\\', true, false, false, false), "toggle-solo");  // Ctrl-\ (was Ctrl-only inline handling)
  keymap_.bind(KeyChord::pack('\\', false, false, false, false), "toggle-mute"); // backslash key
  keymap_.bind(KeyChord::pack('c', true, false, true, false), "toggle-track-collapse"); // Ctrl+Shift+C ("Collapse")
  keymap_.bind(KeyChord::pack(NCKEY_F02, false, false, false, false), "rename-track"); // F2
  keymap_.bind(KeyChord::pack(NCKEY_UP, false, false, false, false), "move-row-up");     // plain Up (was inline handling)
  keymap_.bind(KeyChord::pack(NCKEY_DOWN, false, false, false, false), "move-row-down"); // plain Down
  keymap_.bind(KeyChord::pack(NCKEY_RIGHT, true, false, true, false), "add-note-column");   // Ctrl+Shift+Right
  keymap_.bind(KeyChord::pack(NCKEY_LEFT, true, false, true, false), "remove-note-column"); // Ctrl+Shift+Left
  keymap_.bind(KeyChord::pack('t', true, false, false, false), "add-instrument-track"); // Ctrl-T (was inline handling)
  keymap_.bind(KeyChord::pack('r', true, false, false, false), "add-sample-track");     // Ctrl-R (was inline handling)
  // Ctrl+Shift+D ("Drum") - otherwise only reachable via M-x, which meant
  // there was no way to discover this command exists at all. Plain Ctrl-D
  // is already the (stub, not-yet-implemented) "duplicate track" raw
  // handler below, so this picks a still-free Ctrl+Shift combo rather than
  // colliding with it.
  keymap_.bind(KeyChord::pack('d', true, false, true, false), "add-drum-machine-track"); // Ctrl+Shift+D
  keymap_.bind(KeyChord::pack('t', true, false, true, false), "delete-track"); // Ctrl+Shift+T (was a stub raw handler)

  assertCommandBindingsValid();
}

// Applies the SongStructure's precomputed baseline column shape
// (TrackType/own-settings-derived - note/velocity/delay/effect column
// presence) onto whatever scene.getTrackInformation()'s dynamic pass
// already put in `track_info` - field-by-field, not a whole-struct
// assignment, so a track's already-widened num_subtracks_ (real note data
// in the currently visible rows) is only ever widened further
// (updateNumSubtracks(), never shrunk) rather than clobbered. Every
// qualifying track_id (structure.getOrderedTrackIds() - the same list
// Song::getRootTrackIds() returns) gets an entry here even if it had no
// pattern data at all in the visible range, so every downstream per-track
// width/bounds computation (scroll included) has a real one to read
// instead of silently treating an absent id as zero width.
static void apply_baseline_track_info(const SongStructure & structure, std::unordered_map<int, VisibleTrackInfo> & track_info) {
  for (auto id : structure.getOrderedTrackIds()) {
    auto & info = track_info[id];
    auto & baseline = structure.getBaselineInfo(id);
    info.has_note_column_ = baseline.has_note_column_;
    info.num_velocity_columns_ = baseline.num_velocity_columns_;
    info.has_delay_column_ = baseline.has_delay_column_;
    info.has_effect_column_ = baseline.has_effect_column_;
    info.collapsed_ = baseline.collapsed_;
    info.collapsed_content_width_ = baseline.collapsed_content_width_;
    info.color_ordinal_ = baseline.color_ordinal_;
    info.updateNumSubtracks(baseline.num_subtracks_);
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
    if (pattern_idx >= static_cast<int>(song.getScenes().size())) break;

    auto & scene = song.getScene(pattern_idx);
    scene.getTrackInformation(track_info);
    row += song.getPatternLength() - pattern_row;
  }
  apply_baseline_track_info(SongStructure(song), track_info);

  return track_info;
}

VisibleTrackInfo
PatternEditor::getTrackInfoFor(const Song & song, int track_id) const {
  auto all_track_info = getTrackInformation(song, current_scroll_.row);
  auto it = all_track_info.find(track_id);
  return it != all_track_info.end() ? it->second : VisibleTrackInfo();
}

void
PatternEditor::setSelectionActive(bool active) {
  selection_active_ = active;
  getController().setPatternSelectionActive(active);
}

void
PatternEditor::startAnnotationEdit() {
  if (getPlane().readerActive()) return;

  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();
  auto & scene = song.getScene(info.getPatternIndex());

  // Already true in practice (the only caller is offerInput()'s Enter
  // check, gated on new_cursor.isOnAnnotation() already) - set directly
  // anyway so this stays correct regardless of what calls it, matching
  // GridPosition::scope's own "single source of truth" point.
  new_cursor.scope = current_cursor.scope = SelectionScope::ANNOTATION;

  annotation_edit_pattern_ = info.getPatternIndex();
  annotation_edit_row_ = info.getRowIndex();

  auto cols = getDim().second;
  // Fall back to the top-left corner if this is somehow reached before
  // renderRow() has ever cached a real position (there's always at least
  // one render before input can reach here in practice) - a wrong
  // position is a cosmetic nuisance, not a correctness problem.
  auto row = annotation_screen_row_ >= 0 ? annotation_screen_row_ : 0;
  auto col = annotation_screen_col_ >= 0 ? annotation_screen_col_ : 0;
  auto width = max(cols - col, 1);

  // Blank the target region on this plane first - the reader plane's own
  // base cell (showReader()'s ncplane_set_base(..., "", ...)) doesn't
  // paint over cells nothing ever explicitly writes to, so without this,
  // stale content already sitting there (renderRow()'s own "(annotation)"
  // placeholder, or a longer previous annotation than whatever gets typed
  // this time) can keep peeking out past the reader's own text.
  setFgColor(0, 0, 0);
  setBgColor(0, 0, 0);
  putstr(row, col, string(static_cast<size_t>(width), ' '));

  getPlane().showReader("", row, col, 1, width, scene.getAnnotation(annotation_edit_row_));
}

void
PatternEditor::startTrackNameEdit() {
  if (getPlane().readerActive()) return;

  auto & song = getController().getSong();
  auto track_ids = song.getRootTrackIds();
  if (current_cursor.track >= static_cast<int>(track_ids.size())) return;

  // Prefer showing the *whole* track (heading included) for the reader,
  // even if the cursor previously scrolled rightward into some of this
  // same track's own note columns and never moved back since -
  // computeScrollPosition() is a pure function of the cursor's *current*
  // position (see PatternScroll.h), so asking it again with this track's
  // own column reset to 0 as the trial starting point yields the
  // objectively best-fitting column for right now, independent of
  // whatever a wider past cursor position on this same track required.
  // Only reached when the cursor's own track is already the scroll
  // anchor (current_scroll_.track == current_cursor.track) - if some
  // earlier track is still the anchor instead, the cursor's own track
  // always renders from its own column 0 regardless (every track but
  // the anchor does), so there's nothing to correct.
  bool scroll_corrected = false;
  if (current_scroll_.track == current_cursor.track && current_scroll_.col > 0) {
    auto probe = current_scroll_;
    probe.col = 0;
    auto scroll_track_info = getTrackInformation(song, current_scroll_.row);
    auto corrected = computeScrollPosition(probe, current_scroll_.row, current_cursor.track, current_cursor.col,
					    track_ids, scroll_track_info, getDim().second);
    if (corrected != current_scroll_) {
      current_scroll_ = corrected;
      scroll_corrected = true;
    }
  }

  auto track_id = getController().consumePendingCommandTrack(track_ids[static_cast<size_t>(current_cursor.track)]);
  auto track = song.getTrackByInternalId(track_id);
  if (!track) return;

  // The scroll correction above only takes effect on screen once
  // renderHeading() actually runs again under it - re-run it right here
  // rather than waiting for the next regular render() pass, so
  // track_name_screen_col_/track_name_screen_width_ (read just below)
  // reflect *this* position instead of whatever the last regular render()
  // pass cached under the old, pre-correction scroll.
  if (scroll_corrected && last_styles_) {
    auto all_track_info = getTrackInformation(song, current_scroll_.row);
    renderHeading(*last_styles_, track_ids, all_track_info);
  }

  // No name field to edit at all under the cursor's current column (an
  // Effect's own title bar, or no render has happened yet) - see
  // renderHeading()'s own caching comment.
  if (track_name_screen_col_ < 0 || track_name_screen_width_ <= 0) return;

  track_name_edit_track_id_ = track_id;

  // A color-eligible track's own title bar is always level 0 (its box
  // never merges into a taller ancestor row - see renderHeading()'s own
  // per-level loop), so the row is deterministic from the tree's depth
  // alone, unlike the column/width above which depend on scroll position
  // and neighboring columns too.
  auto row = song.getTrackDepth() - 1;
  auto col = track_name_screen_col_;
  auto width = track_name_screen_width_;

  // The leading "T<N> " is renderHeading()'s own structural label (N =
  // color_ordinal_), not part of the track's own editable name - left
  // for renderHeading() itself to draw below, in its own normal color,
  // while only the name past it opens for editing.
  auto track_info = getTrackInfoFor(song, track_id);
  auto prefix_width = std::min(static_cast<int>(("T" + std::to_string(track_info.color_ordinal_) + " ").size()), width);
  auto edit_col = col + prefix_width;
  auto edit_width = std::max(width - prefix_width, 1);

  // White, not the reader's own default pink - pink reads poorly against
  // this field's own darkened-track-color backdrop (painted below),
  // unlike every other reader in this app (annotation editing, M-x),
  // which all sit over the plain window background pink was chosen to
  // read well against in the first place.
  getPlane().showReader("", row, edit_col, 1, edit_width, track->getName(), 0xff, 0xff, 0xff);

  // TerminalUI::showReader()'s own ncplane_erase_region() call erases
  // from edit_col all the way to the *plane's* right edge, not just this
  // field's own edit_width - fine for startAnnotationEdit() (nothing
  // else sits past the annotation slot at the far right), but this field
  // sits in the middle of the heading row: Mute/Solo glyphs, dividers,
  // and every later track's own heading still follow it, and all of that
  // just got wiped. A real renderHeading() pass repaints all of it (the
  // "T<N> " prefix included, in its normal, un-darkened color) - safe to
  // call directly here since the reader's own child plane, stacked on
  // top of just its own [edit_col, edit_col + edit_width) footprint, is
  // unaffected by whatever the parent plane underneath it gets redrawn
  // to. last_styles_ is always set by now in practice (render() always
  // runs at least once before any input reaches here) - skipped rather
  // than crashing on the off chance it isn't; the row would just stay
  // blank until the next real redraw, a cosmetic nuisance and not a
  // correctness problem, the same tradeoff startAnnotationEdit() already
  // makes for its own row/col fallback above.
  if (last_styles_) {
    auto all_track_info = getTrackInformation(song, current_scroll_.row);
    renderHeading(*last_styles_, track_ids, all_track_info);
  }

  // The reader's own cells (TerminalUI::showReader()'s ncreader_options::
  // tchannels/ncplane_set_base()) are background-alpha-transparent even
  // where a glyph's actually been typed, so whatever color the parent
  // plane shows underneath - painted here, after renderHeading() above
  // so it isn't immediately overwritten by that call's own normal-
  // colored redraw - shows straight through as the field's own backdrop.
  // This track's own heading color (renderHeading()'s segment_color(),
  // un-brightened base - see track_base_color() there), only barely
  // darkened (unlike toggle_color()'s own much heavier 0.4 darkening for
  // a heading control - a field the artist is actively looking at and
  // typing into shouldn't be dimmed anywhere near as far as an idle
  // control) so the field reads as "this track, now being edited"
  // rather than the reader's usual plain pink-on-black - restricted to
  // just the editable span, unlike the "T<N> " prefix right before it,
  // which stays whatever color renderHeading() just drew it in.
  auto bg = track_info.getColor().blend(0.05f, Color(0, 0, 0));
  setFgColor(0xff, 0xff, 0xff);
  setBgColor(bg);
  putstr(row, edit_col, string(static_cast<size_t>(edit_width), ' '));
}

SelectionBounds
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

  bool point_on_annotation = current_cursor.isOnAnnotation();
  bool mark_on_annotation = has_mark && selection_start_scope_ == SelectionScope::ANNOTATION;

  if (has_mark && mark_on_annotation != point_on_annotation) {
    // One end is on the row's annotation, the other on a real track -
    // there's no such thing as selecting "some tracks plus the
    // annotation," so this covers the whole row instead: every track,
    // and the annotation too (see renderRow()'s own EVERYTHING handling).
    b.track_lo = 0;
    b.track_hi = max(static_cast<int>(track_ids.size()) - 1, 0);
    b.scope = SelectionScope::EVERYTHING;
    return b;
  }

  if (point_on_annotation) {
    // Both ends (or the only end, with no mark) are on the annotation -
    // nothing on the grid is selected, just a row range of annotation
    // text (see kill-region/kill-ring-save/yank's own ANNOTATION handling).
    b.scope = SelectionScope::ANNOTATION;
    return b;
  }

  if (b.track_lo != b.track_hi) {
    // Crossing tracks is always whole-cell, the same way it always has been.
    b.scope = SelectionScope::TRACK;
    return b;
  }

  auto track_info = getTrackInfoFor(song, track_ids[static_cast<size_t>(b.track_lo)]);
  auto column_count = track_info.getColumnCount();
  // selection_start_col_ is the raw column the mark was set on - clamped
  // here the same way note_lo/note_hi used to be, since the track's own
  // column count can shrink out from under an active mark (e.g. removing a
  // note column). current_cursor.col is always kept valid by cursor
  // movement itself, so it isn't reclamped.
  auto start_col = has_mark ? clamp(selection_start_col_, 0, max(column_count - 1, 0)) : current_cursor.col;
  auto k_lo = min(start_col, current_cursor.col);
  auto k_hi = max(start_col, current_cursor.col);

  auto effect_k = column_count - 1; // only meaningful when has_effect_column_
  bool touches_command = track_info.has_effect_column_ && k_hi == effect_k;
  // At least one non-effect column falls within [k_lo, k_hi].
  bool touches_notes = k_lo < (track_info.has_effect_column_ ? effect_k : column_count);

  if (touches_command && touches_notes) {
    // Mixing a note column with the effect column - the command applies to
    // the whole row, so this escalates to a whole-track operation rather
    // than staying note-scoped.
    b.scope = SelectionScope::TRACK;
  } else if (touches_command) {
    b.scope = SelectionScope::COMMAND;
  } else {
    b.scope = SelectionScope::NOTE_COLUMN;
    auto max_note = max(track_info.num_subtracks_ - 1, 0);
    b.note_lo = clamp(track_info.getNoteNumber(k_lo), 0, max_note);
    b.note_hi = clamp(track_info.getNoteNumber(k_hi), 0, max_note);
  }

  return b;
}

bool
PatternEditor::render(const StyleProvider & styles, bool refresh) {
  last_styles_ = &styles; // see its own comment - startTrackNameEdit()'s only source of one
  bool render_all = refresh;
  auto & info = getController().getPlaybackInfo();
  auto score_pattern = info.getPatternIndex();
  auto score_playing_row = info.getRowIndex();
  auto & song = getController().getSong();

  // Playback's own playhead crosses pattern boundaries freely regardless
  // of any selection (real playback never goes through moveEditPosition()/
  // setEditPosition() at all - see Controller::moveEditPosition()'s own
  // comment on when those two clamp to the current pattern) - ending the
  // selection the instant playback starts (rather than waiting for the
  // boundary-cross check below to eventually notice) keeps that
  // unambiguous: an open selection never has a chance to look like it's
  // constraining where the playhead goes.
  if (selection_active_ && info.isPlaying()) {
    setSelectionActive(false);
    getController().getUIEventQueue().push(make_unique<LogEvent>("Selection cleared: playback started"));
  } else if (selection_active_ && selection_start_pattern_ != score_pattern) {
    setSelectionActive(false);
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

  // computeScrollPosition() has no notion of the annotation slot itself -
  // track_ids.size(), one past every real track, is the target it treats
  // as "reveal the last track in full" (see its own comment), which is
  // what actually needs to happen for the annotation area right after it
  // to become visible too. new_cursor.track/col stay exactly where they
  // already are either way (see GridPosition::scope's own comment).
  auto scroll_target_track = new_cursor.isOnAnnotation() ? static_cast<int>(track_ids.size()) : new_cursor.track;
  auto scroll_target_col = new_cursor.isOnAnnotation() ? 0 : new_cursor.col;
  auto new_scroll = computeScrollPosition(current_scroll_, new_row, scroll_target_track, scroll_target_col, track_ids, track_info, cols);

  // GridPosition::operator!= already covers track/col/subcol/scope (.row
  // is never set on a cursor, only current_scroll_) - one comparison
  // instead of a field-by-field list that has to be remembered and kept
  // in sync by hand whenever GridPosition itself gains a new field.
  bool cursor_changed = new_cursor != current_cursor;

  current_cursor = new_cursor;

  // Always something to highlight - degenerates to just the note under the
  // cursor when no mark is set (see getEffectiveSelectionBounds). Computed
  // after current_cursor is updated above, so it reflects where the cursor
  // just moved *to* this frame, not where it was before.
  auto sel_bounds = getEffectiveSelectionBounds(song, track_ids);

  // sel_bounds already reflects every piece of state that can change the
  // effective selection (mark set/cleared/moved, point moved, playhead
  // row moved, scope flipped, ...) via getEffectiveSelectionBounds()'s own
  // inputs, so a single comparison against last frame's bounds stands in
  // for what used to be a hand-rolled diff of each of those pieces
  // separately (see SelectionBounds::operator==).
  if (score_pattern != current_score_pattern ||
      song.getMajorVersion() != current_song_version ||
      score_total_columns != current_score_total_columns ||
      new_scroll != current_scroll_ ||
      sel_bounds != current_sel_bounds_ ||
      force_redraw_
      ) {
    render_all = true;
    force_redraw_ = false;
  }

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
  current_song_version = song.getMajorVersion();
  row_edited = false;

  current_sel_bounds_ = sel_bounds;
  
  return need_redraw;
}

void
PatternEditor::handleMidiEvent(MidiEvent & ev) {
  auto & event_queue = getController().getPlaybackEventQueue();

  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();

  auto track_ids = song.getRootTrackIds();

  auto & scene = song.getScene(info.getPatternIndex());
  int track_id = track_ids[static_cast<size_t>(new_cursor.track)];

  // Channel-wide, not tied to any specific note - unlike every other case
  // below, ev.getNote() is unused (always 0, see AlsaAudio.cpp), so this
  // must be handled before the active_midi_notes lookup that follows, or
  // it would be misread as "note 0" and corrupt that per-note bookkeeping.
  // No pattern write either: there's no single note whose velocity this
  // could sensibly become.
  if (ev.getType() == MidiEvent::CHANNEL_PRESSURE) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CHANNEL_PRESSURE, getController().getActiveBufferName(), track_id, ev.getVelocity()));
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
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, getController().getActiveBufferName(), track_id, note_column));

    scene.setNote(info.getRowIndex(), track_id, note_column, Note(0, 0, current_delay));
  } else if (ev.getType() == MidiEvent::NOTE_ON) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, getController().getActiveBufferName(), track_id, note_column, note_value, ev.getVelocity()));

    Note note(note_value, ev.getVelocity(), current_delay);
    scene.setNote(info.getRowIndex(), track_id, note_column, note);
    row_edited = true;
    song.incMinorVersion();
  } else if (ev.getType() == MidiEvent::NOTE_PRESSURE) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, getController().getActiveBufferName(), track_id, note_column, note_value, ev.getVelocity()));

    getController().applyNotePressure(info.getPatternIndex(), info.getRowIndex(), track_id, note_column, ev.getVelocity(), current_delay);
    row_edited = true;
    song.incMinorVersion();
  }
}

void
PatternEditor::onRowAdvanced(Controller & controller) {
  if (!auto_started_playback_) return;

  auto & info = controller.getPlaybackInfo();

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

  controller.sweepAutoRecordRows(auto_record_cleared_rows_, last_cleared_row_, last_cleared_pattern_idx_, info.getPatternIndex(), info.getRowIndex(), track_ids);
}

void
PatternEditor::saveEditingState(const string & name) {
  if (name.empty()) return; // startup - nothing has been active under this name yet
  auto & state = buffer_states_[name];
  state.current_cursor = current_cursor;
  state.new_cursor = new_cursor;
  state.current_scroll = current_scroll_;
  state.edit_step_size = edit_step_size;
  state.new_edit_step_size = new_edit_step_size;
  state.current_song_version = current_song_version;
  state.active_midi_notes = active_midi_notes;
  state.active_keyboard_notes = active_keyboard_notes_;
  state.auto_started_playback = auto_started_playback_;
  state.auto_record_cleared_rows = auto_record_cleared_rows_;
  state.last_cleared_row = last_cleared_row_;
  state.last_cleared_pattern_idx = last_cleared_pattern_idx_;
  state.selection_active = selection_active_;
  state.selection_start_pattern = selection_start_pattern_;
  state.selection_start_row = selection_start_row_;
  state.selection_start_track = selection_start_track_;
  state.selection_start_col = selection_start_col_;
  state.selection_start_scope = selection_start_scope_;
  state.current_sel_bounds = current_sel_bounds_;
  state.annotation_screen_row = annotation_screen_row_;
  state.annotation_screen_col = annotation_screen_col_;
  state.annotation_edit_row = annotation_edit_row_;
  state.annotation_edit_pattern = annotation_edit_pattern_;
  state.track_name_screen_col = track_name_screen_col_;
  state.track_name_screen_width = track_name_screen_width_;
  state.track_name_edit_track_id = track_name_edit_track_id_;
}

void
PatternEditor::loadEditingState(const string & name) {
  auto & state = buffer_states_[name]; // default-constructs a fresh slot for a never-before-visited buffer
  current_cursor = state.current_cursor;
  new_cursor = state.new_cursor;
  current_scroll_ = state.current_scroll;
  edit_step_size = state.edit_step_size;
  new_edit_step_size = state.new_edit_step_size;
  current_song_version = state.current_song_version;
  active_midi_notes = state.active_midi_notes;
  active_keyboard_notes_ = state.active_keyboard_notes;
  auto_started_playback_ = state.auto_started_playback;
  auto_record_cleared_rows_ = state.auto_record_cleared_rows;
  last_cleared_row_ = state.last_cleared_row;
  last_cleared_pattern_idx_ = state.last_cleared_pattern_idx;
  selection_start_pattern_ = state.selection_start_pattern;
  selection_start_row_ = state.selection_start_row;
  selection_start_track_ = state.selection_start_track;
  selection_start_col_ = state.selection_start_col;
  selection_start_scope_ = state.selection_start_scope;
  current_sel_bounds_ = state.current_sel_bounds;
  annotation_screen_row_ = state.annotation_screen_row;
  annotation_screen_col_ = state.annotation_screen_col;
  annotation_edit_row_ = state.annotation_edit_row;
  annotation_edit_pattern_ = state.annotation_edit_pattern;
  track_name_screen_col_ = state.track_name_screen_col;
  track_name_screen_width_ = state.track_name_screen_width;
  track_name_edit_track_id_ = state.track_name_edit_track_id;
  setSelectionActive(state.selection_active); // also mirrors into Controller::pattern_selection_active_
}

void
PatternEditor::handleBufferChanged() {
  // Prune any buffer_states_ entry for a name that isn't actually open any
  // more - reached whenever a buffer gets killed, or renamed (its old key
  // stops being open the moment Controller::renameActiveBuffer() renames
  // songs_'s own entry, which happens before this listener ever fires).
  // This listener isn't told *why* it fired, only that the active buffer
  // changed, so there's no single name to drop the way Controller's own
  // dropBufferState() calls each know exactly which one to target - a full
  // sweep instead, cheap since buffer counts stay small in practice (the
  // same reasoning the per-buffer editing-state plan gives elsewhere).
  auto live_names = getController().getBufferNames();
  std::set<std::string> live(live_names.begin(), live_names.end());
  for (auto it = buffer_states_.begin(); it != buffer_states_.end(); ) {
    if (live.count(it->first) == 0) it = buffer_states_.erase(it);
    else ++it;
  }

  auto & song = getController().getSong();
  auto & new_name = getController().getActiveBufferName();
  if (&song == last_active_song_) {
    // Same Song object as before - a rename, not a real switch (see this
    // method's own header comment) - nothing to save/restore.
    last_active_buffer_name_ = new_name;
    return;
  }
  saveEditingState(last_active_buffer_name_);
  loadEditingState(new_name);
  last_active_song_ = &song;
  last_active_buffer_name_ = new_name;
}

bool
PatternEditor::offerInput(const InputEvent & input) {
  // Mirrors StatusLine::offerInput()'s own reader-active handling exactly
  // (see its comment) - while the annotation editor (startAnnotationEdit())
  // or the track-name editor (startTrackNameEdit()) is open, Enter commits
  // and Ctrl-g cancels; everything else (including arrow keys, which would
  // otherwise move the pattern cursor) goes to the reader instead of any
  // of this class's own keybinding dispatch/manual handling below.
  if (getPlane().readerActive()) {
    if (input.getId() == NCKEY_ENTER) {
      auto text = getPlane().closeReader();
      if (annotation_edit_pattern_ >= 0) {
	auto & song = getController().getSong();
	auto & scene = song.getScene(annotation_edit_pattern_);
	scene.setAnnotation(annotation_edit_row_, std::move(text));
	song.incVersion();
      } else if (track_name_edit_track_id_ >= 0) {
	auto & song = getController().getSong();
	auto track = song.getTrackByInternalId(track_name_edit_track_id_);
	if (track) {
	  track->setName(std::move(text));
	  song.incVersion();
	}
      }
      annotation_edit_pattern_ = annotation_edit_row_ = -1;
      track_name_edit_track_id_ = -1;
      // Both branches above blanked their own target cells directly
      // before the reader ever opened (see startAnnotationEdit()'s/
      // startTrackNameEdit()'s own comments) - incVersion() already
      // covers the successful-commit case by itself (it changes
      // song.getMajorVersion(), one of render()'s own render_all
      // triggers), but a resolved-to-nothing track (deleted mid-edit)
      // skips that, so force it here too rather than leaving the blanked
      // cell on screen until some unrelated redraw happens to fire.
      force_redraw_ = true;
      return true;
    } else if (input.hasCtrl() && input.getId() == 'g') {
      getPlane().closeReader();
      annotation_edit_pattern_ = annotation_edit_row_ = -1;
      track_name_edit_track_id_ = -1;
      // Canceling never touches the model at all, so nothing else would
      // ever tell render() to repaint the cell this blanked - see
      // force_redraw_'s own comment.
      force_redraw_ = true;
      return true;
    } else {
      return getPlane().offerInput(input);
    }
  }

  // Cursor parked on the annotation slot (Right arrow past the last
  // track's last column - see GridPosition::scope's own comment) but not
  // editing it yet: Enter is the explicit "start editing" trigger -
  // reaching the slot on its own must never start editing by itself.
  if (new_cursor.isOnAnnotation() && input.getId() == NCKEY_ENTER) {
    startAnnotationEdit();
    return true;
  }

  // Enter also expands whatever track the cursor's column currently
  // belongs to, if it's collapsed - the same "drill into it" affordance
  // as the annotation slot's own Enter-to-edit just above, just for
  // VisibleTrackInfo::collapsed_ instead. One-directional deliberately:
  // toggle-track-collapse (Ctrl+Shift+C) is still the only way to
  // collapse a track, so Enter never surprises the user by hiding
  // something they just landed on.
  if (!new_cursor.isOnAnnotation() && input.getId() == NCKEY_ENTER) {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    if (current_cursor.track < static_cast<int>(track_ids.size())) {
      auto track_id = track_ids[static_cast<size_t>(current_cursor.track)];
      auto track = song.getTrackByInternalId(track_id);
      if (track && track->isCollapsed()) {
        getController().toggleTrackCollapsed(track_id);
        return true;
      }
    }
  }

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

    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, getController().getActiveBufferName(), held.track_id, held.note_column));

    // Mirrors LaunchpadManager's own RELEASE handling: while playing,
    // write an explicit off at the row the transport has since reached
    // - unless that's still the note's own row, which would erase the
    // note it belongs to instead of ending it.
    if (info.isPlaying()) {
      auto release_row = info.getRowIndex();
      if (release_row != held.row) {
	getController().writeReleaseOff(auto_record_cleared_rows_, auto_started_playback_, info.getPatternIndex(), release_row, held.track_id, held.note_column, info.getCurrentDelay());
      }
    }

    // Realtime auto-play-while-held (mirrors LaunchpadManager's own -
    // see its RELEASE branch for the identical reasoning): stop exactly
    // when the last held note key releases, but only if this code
    // started the transport itself - stopAutoRecordSession() itself
    // handles only actually stopping if it's still genuinely playing
    // (the user may have manually stopped it in the meantime).
    if (auto_started_playback_ && active_keyboard_notes_.empty()) {
      getController().stopAutoRecordSession(auto_started_playback_, auto_record_cleared_rows_, info);
    }
    return true;
  }

  auto all_track_info = getTrackInformation(song, current_scroll_.row);

  auto track_ids = song.getRootTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());

  auto current_track = song.getTrackByInternalId(track_ids[static_cast<size_t>(current_cursor.track)]);

  auto input_hex_value = digit(input.getId(), 16);

  VisibleTrackInfo track_info;
  if (current_track) {
    auto it0 = all_track_info.find(current_track->getInternalId());
    if (it0 != all_track_info.end()) track_info = it0->second;
  }

  if (input.getId() == NCKEY_BUTTON1) {

  } else if (input.hasCtrl() && !input.hasMeta()) {
    if (input.getId() == 'a') {
      new_cursor.track = new_cursor.col = new_cursor.subcol = 0;
      return true;
    } else if (input.getId() == 'e') {
      new_cursor.track = num_tracks > 1 ? num_tracks - 1 : 0;
      new_cursor.subcol = 0;

      auto it = all_track_info.find(track_ids[static_cast<size_t>(new_cursor.track)]);
      new_cursor.col = it != all_track_info.end() ? it->second.getColumnCount() - 1: 0;
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
      // INSTRUMENT_CONTROL only - PercussionTrack/DrumMachineTrack have no
      // instrument_id_ of their own to cycle any more (they play through
      // the song's one pool-wide drum kit instead - see
      // InstrumentPool::getDefaultKitInstrument()).
      auto track = song.getTrackByInternalId(track_ids[static_cast<size_t>(current_cursor.track)]);
      if (track && track->getType() == TrackType::INSTRUMENT_CONTROL) {
	auto & instrument_track = dynamic_cast<InstrumentTrack&>(*track);
	bool changed = false;
	if (input.getId() == NCKEY_KP_DIVIDE && instrument_track.getInstrumentId() > 0) {
	  instrument_track.setInstrumentId(instrument_track.getInstrumentId() - 1);
	  changed = true;
	} else {
	  auto & instruments = song.getInstruments();
	  if (input.getId() == NCKEY_KP_MULTIPLY && instrument_track.getInstrumentId() + 1 < static_cast<int>(instruments.size())) {
	    instrument_track.setInstrumentId(instrument_track.getInstrumentId() + 1);
	    changed = true;
	  }
	}
	if (changed) {
	  song.incVersion();
	  event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CLEAR_VOICES, getController().getActiveBufferName(), instrument_track.getInternalId()));
	}
      }
      return true;
    } else if (input.getId() == NCKEY_LEFT || input.getId() == NCKEY_RIGHT) {
      // Emacs-style word motion, one level up from plain Left/Right's
      // note-column navigation (see the !input.hasMeta() branch below):
      // jumps straight to the neighboring track rather than stepping
      // through its columns, landing on its first column the same way
      // crossing a track boundary during plain-Right navigation already
      // does. Mirrors plain Left/Right's own annotation-slot handling at
      // the two ends too (see that branch's own comments): Ctrl+Right
      // past the last track parks on the annotation slot instead of
      // doing nothing, and Ctrl+Left backs out of it the same way plain
      // Left does, rather than also stepping a track left in the same
      // keypress.
      if (input.getId() == NCKEY_LEFT) {
	if (new_cursor.isOnAnnotation()) {
	  new_cursor.scope = SelectionScope::NOTE_COLUMN;
	} else if (new_cursor.track > 0) {
	  new_cursor.track--;
	  new_cursor.col = 0;
	  new_cursor.subcol = 0;
	}
      } else if (new_cursor.track + 1 < num_tracks) {
	new_cursor.track++;
	new_cursor.col = 0;
	new_cursor.subcol = 0;
      } else if (!new_cursor.isOnAnnotation()) {
	new_cursor.scope = SelectionScope::ANNOTATION;
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
    if (input.getId() == NCKEY_LEFT) {
      if (new_cursor.isOnAnnotation()) {
	// Back out of the annotation slot without touching track/col -
	// they're already sitting on the last track's last column, exactly
	// where Left should land.
	new_cursor.scope = SelectionScope::NOTE_COLUMN;
      } else if (new_cursor.col > 0) {
	new_cursor.col--;
	new_cursor.subcol = 0;
      } else if (new_cursor.track > 0) {
	new_cursor.track--;
	new_cursor.subcol = 0;

	auto it = all_track_info.find(track_ids[static_cast<size_t>(new_cursor.track)]);
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
      } else {
	// Right at the last column of the last track used to do nothing -
	// now it parks the cursor on the row's annotation slot, the same
	// "one more column" mental model as everything else this key does -
	// but doesn't start editing it (see GridPosition::scope's own
	// comment): Enter is the explicit trigger for that. track/col are
	// left untouched, so they're still exactly the last track's last
	// column underneath.
	new_cursor.scope = SelectionScope::ANNOTATION;
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
    } else {
      auto & scene = song.getScene(info.getPatternIndex());
      int track_id = track_ids[static_cast<size_t>(new_cursor.track)];
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
	  scene.setCommand(info.getRowIndex(), track_id, Command());
	  row_edited = true;
	  song.incMinorVersion();
	  // Same row-level Backspace-steps-back/Delete-stays-put distinction
	  // the note column's own is_delete handling makes below.
	  if (!info.isPlaying() && input.getId() == NCKEY_BACKSPACE) {
	    getController().moveEditPosition(-edit_step_size);
	  }
	  return true;
	}

	// In effect command, column 0/1 (mnemonic) accepts [A-Za-z0-9-] and
	// column 2/3 (hex argument) accepts [A-Fa-f0-9-] - Command::
	// updateData() (see its own comment) validates and reports
	// success/failure itself, so this call site doesn't need to
	// pre-classify input.getId() at all before attempting it - without
	// updateData()'s own validation, every unbound non-printable key
	// (arrows/F-keys/Insert/PageUp/... not already intercepted by an
	// earlier else-if branch above, or Ctrl/Alt chords with no keymap
	// entry) is a notcurses key code far outside any printable range,
	// and would otherwise get silently written into the command as if
	// it were a typed character.
	auto command = scene.getCommand(info.getRowIndex(), track_id);
	if (command.updateData(new_cursor.subcol, input.getId())) {
	  scene.setCommand(info.getRowIndex(), track_id, command);
	  row_edited = true;
	  song.incMinorVersion();

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
	  auto & notes = scene.getNotes(info.getRowIndex(), track_id);
	  auto note_column = track_info.getNoteNumber(new_cursor.col);
	  Note note;
	  if (note_column < static_cast<int>(notes.size())) note = notes[static_cast<size_t>(note_column)];
	  int current_value = column_type == ColumnType::VELOCITY ? note.getVelocity() : note.getDelay();
	  if (new_cursor.subcol == 0) current_value = (input_hex_value << 4) | (current_value & 0x0f);
	  else current_value = (current_value & 0xf0) | input_hex_value;
	  if (column_type == ColumnType::VELOCITY) note.setVelocity(current_value);
	  else note.setDelay(current_value);
	  scene.setNote(info.getRowIndex(), track_id, note_column, note);
	  row_edited = true;
	  song.incMinorVersion();
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
	// Pattern via scene.setNote()/pushNote() below, just with nothing
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
	  midi_note = input.toMidiNote(getController().getGlobalOctave(), tuning);
	}

	if (is_repeat && midi_note >= 0) return true; // already sounding - nothing to redo

	if (is_delete || midi_note >= 0 || is_off) {
	  if (is_delete) {
	    scene.deleteNote(info.getRowIndex(), track_id, note_column);
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, getController().getActiveBufferName(), track_id, note_column));
	  } else if (is_off) {
	    scene.setNote(info.getRowIndex(), track_id, note_column, Note(0, 0, current_delay));
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, getController().getActiveBufferName(), track_id, note_column));
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
	      getController().startAutoRecordSession(auto_started_playback_, auto_record_cleared_rows_, last_cleared_row_, last_cleared_pattern_idx_);
	    }

	    if (input.hasShift()) {
	      if (auto_started_playback_) getController().ensureRowCleared(auto_record_cleared_rows_, info.getPatternIndex(), info.getRowIndex(), track_id);
	      note_column = scene.pushNote(info.getRowIndex(), track_id, note);
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
	      if (auto_started_playback_) getController().ensureRowCleared(auto_record_cleared_rows_, info.getPatternIndex(), info.getRowIndex(), track_id);
	      scene.setNote(info.getRowIndex(), track_id, note_column, note);
	    }

	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, getController().getActiveBufferName(), track_id, note_column, note.getValue(), note.getVelocity()));
	    if (has_hold_info) active_keyboard_notes_[input.getId()] = { note_column, info.getRowIndex(), track_id };
	  }

	  row_edited = true;
	  song.incMinorVersion();

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

  auto [rows, cols] = getDim();

  unordered_map<int, Track *> track_parents;
  for (auto & track : song.getTracks()) {
    get_track_parents(*track, nullptr, track_parents);
  }

  auto heading_height = song.getTrackDepth() + 1;

  string padding(static_cast<size_t>(cols), ' ');
  
  setBgColor(styles.window_bg_color);
  for (auto i = 0; i < heading_height; i++) {
    putstr(i, 0, padding);
  }
  
  auto & instruments = song.getInstruments();

  // Reset every render pass rather than only ever being set - a track
  // with no name field at all (or the cursor moving off a color-eligible
  // track entirely) must not leave startTrackNameEdit() reading a stale
  // position left over from whichever track was last selected while one
  // was visible.
  track_name_screen_col_ = track_name_screen_width_ = -1;

  // The cursor's own current track id (see the Selection highlight
  // comment on segment_color below) - purely structural, computed once
  // here rather than inside the per-level loop.
  auto selected_id = current_cursor.track < static_cast<int>(track_ids.size()) ?
    track_ids[static_cast<size_t>(current_cursor.track)] : -1;
  // Each track's own color (VisibleTrackInfo::getColor(), driven by
  // color_ordinal_) comes from SongStructure via all_track_info - not
  // recomputed here - so this heading and any future consumer of the
  // same SongStructure (e.g. a Pattern Matrix) necessarily agree on
  // every track's color without sharing anything beyond that.
  auto get_track_info = [&](Track * t) -> const VisibleTrackInfo * {
    if (!t) return nullptr;
    auto it = all_track_info.find(t->getInternalId());
    return it != all_track_info.end() ? &it->second : nullptr;
  };

  for (auto level = 0; level < heading_height - 1; level++) {
    vector<Track *> tracks;
    vector<int> track_widths;

    for (auto i = 0; i < static_cast<int>(track_ids.size()); i++) {
      // Tracks scrolled fully off the left edge don't exist here at
      // all, not even merged into some group that then gets skipped by
      // index below - the render loop's own tracks.size() only ever
      // sees what's actually visible, so its own index space stays a
      // straightforward 0-based count of *visible* segments regardless
      // of how many original tracks got merged into each one (merging
      // can make tracks[] considerably shorter than track_ids itself,
      // so comparing an index into one against a count meant for the
      // other - what used to happen in the render loop further down -
      // skipped the wrong entries entirely once anything was merged).
      if (i < current_scroll_.track) continue;
      int track_id = track_ids[static_cast<size_t>(i)];
      auto track = song.getTrackByInternalId(track_id);
      // A column's own leaf can itself own a subtree (an effect track
      // wrapping others still gets its own trailing column alongside
      // them - SongStructure::visit()'s EFFECT branch) - getDepth() - 1
      // hops shallower than a plain leaf at the same level, since its
      // subtree's own leaves already spend that many hops just reaching
      // it, while this column already starts there. Walking that many
      // fewer hops here makes it land in the very same ancestor bucket
      // as the rest of its subtree at every level from where that
      // subtree's deepest leaf first reaches it, so the merge below
      // extends that row's span to cover it too instead of leaving it a
      // separate, prematurely-promoted segment (plain leaves, whose
      // depth is always 1, are unaffected - 0 hops shallower).
      // Computed once, from the column's own starting leaf, rather than
      // inline in the loop condition below - track gets reassigned to
      // each successive parent as the loop climbs, and re-evaluating
      // getDepth() against that already-advanced track on every
      // iteration would silently shrink the hop budget mid-climb.
      auto hops = level - (track->getDepth() - 1);
      for (auto k = 0; k < hops && track; k++) {
	auto it = track_parents.find(track->getInternalId());
	auto parent = it != track_parents.end() ? it->second : nullptr;
	if (!parent) {
	  // Climbed to this column's own root. Its whole subtree has fully
	  // merged into one span by the level exactly matching its own
	  // depth (getDepth() - 1 - see the comment above) - any row past
	  // that point has nothing left to add (this root has no real
	  // parent to advance to), so it goes blank instead of repeating
	  // the exact same already-complete span again. At or before that
	  // level, keep reporting the root itself, so the merge below can
	  // still pull a shallower sibling branch together with a deeper
	  // one once its own climb reaches the root too (a bare leaf root,
	  // whose depth is 1, has no level at or before that point at all -
	  // it's always past it, so it always goes blank, same as before).
	  if (level > track->getDepth() - 1) track = nullptr;
	  break;
	}
	track = parent;
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

    // `t`'s own heading segment background, *not* accounting for
    // selection (see segment_color() below, which wraps this) - `t`'s
    // own color (VisibleTrackInfo::getColor(), which SongStructure has
    // already keyed to a stable ordinal) for a color-eligible leaf-track's
    // title bar (level 0 only), a grey shaded by nesting depth for
    // anything else that owns an ancestor row (an effect's own level-0
    // column, a non-color-eligible leaf, or wherever it's the resolved
    // ancestor for a level>0 row). Also what Mute/Solo's own OFF color
    // (below) darkens - the track's plain base color, never the
    // brightened-when-selected one.
    auto track_base_color = [&](Track * t) -> Color {
      auto vis_info = get_track_info(t);
      if (level == 0 && vis_info && vis_info->color_ordinal_ >= 0) return vis_info->getColor();
      // Each extra nesting level lightens the grey by one step, so an
      // outer wrapping effect's box reads visually "further out" than
      // what's nested inside it - clamped so a very deep chain doesn't
      // run off into full white.
      auto step = std::min(t->getDepth(), 6);
      auto v = 0x30 + step * 0x10;
      return Color(v, v, std::min(v + 0x10, 255));
    };
    // The background color renderHeading() actually draws for `t`'s own
    // heading segment - track_base_color() above, brightened toward
    // white when `t` is the cursor's own current track (selected_id
    // above), or the plain window background when there's no track at
    // all (a blank, "nothing resolved here" segment). Brightening here
    // rather than in track_base_color() itself means the whole box a
    // track owns (every row of a multi-level effect ancestor box
    // included, not just the row its name happens to draw on) reads as
    // selected consistently, while Mute/Solo's OFF color can still reach
    // the plain, non-brightened base. Shared by a segment's own fill and
    // the divider drawn at its trailing edge (draw_divider below), so
    // the two always agree exactly.
    auto segment_color = [&](Track * t) -> Color {
      if (!t) return styles.window_bg_color;
      auto base = track_base_color(t);
      return t->getInternalId() == selected_id ? base.blend(0.35f, Color(255, 255, 255)) : base;
    };
    // `t`'s own base color, darkened - the collapse toggle's look. A
    // heading control, not a pure status indicator like the ones
    // faint_color() below covers, so it keeps its own, more prominent
    // darkening, and (unlike those) stays the plain, non-brightened base
    // (track_base_color(), not segment_color()) even when `t` is
    // selected - the toggle reads the same regardless of selection.
    auto toggle_color = [&](Track * t) { return track_base_color(t).blend(0.4f, Color(0, 0, 0)); };
    // A faint "transparent black" tint over whatever background is
    // actually drawn (segment_color() - selection brighten included, not
    // the flat track_base_color()) - every idle-status indicator's own
    // look: Mute/Solo's OFF glyphs and the effect ancestor row's own idle
    // activity dot. All of them read lighter automatically once the
    // track is selected, rather than staying a fixed color indifferent
    // to that.
    auto faint_color = [&](Track * t) { return segment_color(t).blend(0.275f, Color(0, 0, 0)); };
    // The color a divider's right half should show: whatever heading
    // segment starts right after index `idx` in `tracks`, or - past the
    // last one actually drawn, where the annotation column's own
    // heading starts - the plain window background already pre-filled
    // at the top of this function.
    auto next_segment_color = [&](int idx) -> Color {
      if (idx + 1 < static_cast<int>(tracks.size())) return segment_color(tracks[static_cast<size_t>(idx + 1)]);
      return styles.window_bg_color;
    };
    // A "│" divider can only carry one color, but the cell it sits in
    // belongs half to the segment ending there and half to the one
    // starting right after it - "▌" (U+258C, left half block) lets the
    // foreground color paint the left half and the background color
    // paint the right half, so the divider shows both heading colors
    // exactly instead of whichever one happened to draw last. Except
    // when the two halves are the *same* color (e.g. two same-depth
    // effect boxes sitting side by side) - then "▌" would render as one
    // seamless block with no visible seam at all, so a plain black "│"
    // takes its place instead, so there's still a border to see.
    auto draw_edge = [&](int row, int col, const Color & left_color, const Color & right_color) {
      bool same = left_color.getRed() == right_color.getRed() && left_color.getGreen() == right_color.getGreen() &&
	left_color.getBlue() == right_color.getBlue();
      if (same) {
	setFgColor(0x00, 0x00, 0x00);
	setBgColor(right_color);
	putstr(row, col, "│");
      } else {
	setFgColor(left_color);
	setBgColor(right_color);
	putstr(row, col, "▌");
      }
    };
    auto draw_divider = [&](int row, int col, Track * left, int idx) {
      draw_edge(row, col, segment_color(left), next_segment_color(idx));
    };

    // Whether the left-edge marker (below) has been drawn yet for this
    // row - drawn once, for tracks[0] (the first actually-visible
    // segment - see the loop building tracks[] above), not once per
    // iteration.
    bool drew_left_edge = false;

    auto current_pos = 5;
    for (auto i = 0; i < static_cast<int>(tracks.size()); i++) {
      if (current_pos >= cols) break;

      auto track = tracks[static_cast<size_t>(i)];
      auto actual_width = track_widths[static_cast<size_t>(i)];

      if (!drew_left_edge) {
	// The row-number gutter (renderRow()'s own "│" at column 4) has no
	// heading-row counterpart - columns 0-4 up here are still just the
	// plain window background. Mark that same boundary here too, same
	// "▌" two-color trick as every other divider in this function:
	// window_bg_color on the left (the gutter itself has no heading
	// color of its own - plain black would read darker than the
	// window background used everywhere else for "nothing here") and
	// this row's first visible heading color on the right. Drawn over
	// column 4, not column 5, so the heading content actually drawn
	// below still starts exactly where it always has.
	draw_edge(heading_height - 2 - level, 4, styles.window_bg_color, segment_color(track));
	drew_left_edge = true;
      }

      if (track) {
	if (level == 0) {
	  // Whether `track` gets a color at all - a LeafTrack
	  // (VisibleTrackInfo::color_ordinal_ >= 0, assigned by SongStructure),
	  // which today means every leaf-track type except Effect. The single
	  // rule this whole branch uses to decide both coloring and whether to
	  // render Mute/Solo - Effect is the only track type with no
	  // LeafTrack in its ancestry, so the two questions ("does it
	  // get a color" and "does it have mute/solo") have always had the
	  // same answer, and sharing one boolean keeps that from silently
	  // drifting apart.
	  auto vis_info = get_track_info(track);
	  bool is_color_eligible = vis_info && vis_info->color_ordinal_ >= 0;
	  bool is_collapsed = track->isCollapsed();

	  setFgColor(0xff, 0xff, 0xff);
	  setBgColor(segment_color(track));

	  // A track with no color (Effect) has no mute/solo either - skip
	  // the "MS" glyphs entirely for it. A collapsed track hides them
	  // too - the title bar shrinks to a single placeholder column while
	  // collapsed (see VisibleTrackInfo::getColumnCount()), with no room
	  // for glyphs that would never fit either way.
	  bool has_mute_solo = is_color_eligible && !is_collapsed;
	  // Likewise, an Effect's own level-0 column carries no collapse
	  // toggle of its own - its blank name (below) already defers its
	  // whole identity, toggle included, to its ancestor-row box (see
	  // the level>0 branch further down).
	  bool has_collapse_toggle = is_color_eligible;

	  if (has_collapse_toggle && actual_width <= 1) {
	    // No room for the toggle glyph itself - a collapsed track's own
	    // title bar is 2 characters wide (see VisibleTrackInfo::
	    // getColumnWidth()), so this is only reached via horizontal
	    // scroll truncating an otherwise-wider column down to a sliver
	    // at the screen edge. Nothing to draw but its own trailing
	    // divider, same "no room" fallback as the ancestor-row branch
	    // further down.
	    draw_divider(heading_height - 2 - level, current_pos, track, i);
	  } else {
	    // std::max(0, ...): a narrow enough column (actual_width < 3) would
	    // otherwise make text_width negative, and it's used below both as a
	    // display-width budget and as a putstr() column offset.
	    auto text_width = std::max(0, actual_width - (has_mute_solo ? 3 : 1) - (has_collapse_toggle ? 1 : 0));
	    auto name_pos = current_pos + (has_collapse_toggle ? 1 : 0);

	    // The display label is still type-specific (a raw sample/step
	    // sequencer has nothing resembling an "instrument" to resolve),
	    // but solo/mute themselves are read uniformly from whatever
	    // LeafTrack this is - real state on every color-eligible
	    // track now, not just InstrumentControl/PercussionControl.
	    string instrument_name;
	    if (track->getType() == TrackType::SAMPLE) {
	      instrument_name = "Sample";
	    } else if (track->getType() == TrackType::DRUM_MACHINE || track->getType() == TrackType::PERCUSSION_CONTROL) {
	      // Same fixed, type-level label for both - neither has a
	      // per-track instrument to name (they play through the pool's
	      // one default kit - see InstrumentPool.h), and PercussionTrack's
	      // own individual keys are meant to be overridable independently
	      // of that kit later (not built yet), so even naming the kit here
	      // would misleadingly imply a fixed 1:1 relationship. Dimmed
	      // (below) for the same reason - a visual cue that, unlike every
	      // other instrument_name here, this isn't something the track
	      // has its own setting for.
	      instrument_name = "(percussion)";
	    } else if (track->getType() == TrackType::INSTRUMENT_CONTROL) {
	      auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
	      if (instrument_track.getInstrumentId() >= 0 && instrument_track.getInstrumentId() < static_cast<int>(instruments.size())) {
		instrument_name = instruments[static_cast<size_t>(instrument_track.getInstrumentId())]->getDisplayName();
	      }
	    }
	    bool is_solo = false, is_muted = false;
	    if (auto leaf_track = dynamic_cast<const LeafTrack *>(track)) {
	      is_solo = leaf_track->isSolo();
	      is_muted = leaf_track->isMuted();
	    }
	    // An Effect's own column carries no label of its own - the
	    // ancestor row above it already names it (spanning this column
	    // too, see the merge logic further up) - so it's left blank.
	    // Every color-eligible track instead always starts with its own
	    // "T<N>" (N = color_ordinal_, the same 0-based count that picks
	    // its heading color - stable across horizontal scroll and
	    // unaffected by any Effect/Group tracks interleaved between
	    // leaves, unlike getInternalId()'s raw process-wide counter),
	    // followed by the artist's own name if one is set. Deliberately
	    // not the track's own textual id as a further fallback - that's
	    // almost always an auto-generated "track3"-style cross-reference
	    // id (generateUniqueTrackId(), Song.h), not a real name, and
	    // showing it next to an unrelated T<N> number reads as confusing
	    // noise more often than it reads as useful; a hand-authored,
	    // genuinely meaningful id is the rare exception, not the case to
	    // design this fallback around.
	    auto name = !is_color_eligible ? string() :
	      "T" + std::to_string(vis_info->color_ordinal_) + (track->getName().empty() ? string() : " " + track->getName());
	    name = Utf8::truncateToWidth(name, text_width);
	    name = Utf8::padToWidth(name, text_width);
	    if (has_collapse_toggle) {
	      // toggle_color() - the track's own darkened base color (see its
	      // own comment) - same coloring method as Mute/Solo's OFF glyphs
	      // just below, but its own lighter darkening: a heading control,
	      // not a pure status indicator like those (or the ancestor row's
	      // own activity dot), so it stays a touch more prominent.
	      setFgColor(toggle_color(track));
	      putstr(heading_height - 2 - level, current_pos, track->isCollapsed() ? "+" : "-");
	      setFgColor(0xff, 0xff, 0xff);
	    }
	    putstr(heading_height - 2 - level, name_pos, name);
	    // Cache this track's own name-field position for
	    // startTrackNameEdit() - only when it's both the one the cursor
	    // is actually on and one that has a name field to begin with
	    // (is_color_eligible; see the reset just above renderHeading()'s
	    // own selected_id computation for the "no field here" case).
	    if (is_color_eligible && track->getInternalId() == selected_id) {
	      track_name_screen_col_ = name_pos;
	      track_name_screen_width_ = text_width;
	    }
	    if (has_mute_solo) {
	      // OFF: faint_color() - see its own comment. ON: unchanged -
	      // the glyph blends into the header instead.
	      auto off_color = faint_color(track);
	      if (is_muted) setFgColor(0x00, 0x00, 0x00);
	      else setFgColor(off_color);
	      putstr(heading_height - 2 - level, name_pos + text_width, "M");
	      if (is_solo) setFgColor(0x00, 0x00, 0x00);
	      else setFgColor(off_color);
	      putstr(heading_height - 2 - level, name_pos + text_width + 1, "S");
	    }
	    // Drawn last - draw_divider() changes the current fg/bg colors as
	    // a side effect, and nothing else in this branch relies on them
	    // afterward.
	    draw_divider(heading_height - 2 - level, name_pos + text_width + (has_mute_solo ? 2 : 0), track, i);

	    // Half brightness for PercussionTrack/DrumMachineTrack's fixed
	    // "(percussion)" label - not a real per-track instrument setting
	    // (see above), so it reads as visibly less prominent than every
	    // other track's own actual instrument name.
	    bool is_fixed_percussion_label = track->getType() == TrackType::DRUM_MACHINE || track->getType() == TrackType::PERCUSSION_CONTROL;
	    if (is_fixed_percussion_label) setFgColor(0x78, 0x78, 0x78);
	    else setFgColor(0xf0, 0xf0, 0xf0);
	    setBgColor(styles.window_bg_color);

	    auto instrument_name_width = std::max(0, actual_width - 1);
	    instrument_name = Utf8::truncateToWidth(instrument_name, instrument_name_width);
	    putstr(heading_height - 2 - level + 1, current_pos, instrument_name);
	  }
	} else if (actual_width <= 1) {
	  // No room for even the "+"/"-" collapse toggle, let alone the
	  // activity dot or name text - a lone collapsed leaf column is 2
	  // wide (see VisibleTrackInfo::getColumnWidth()), so this is only
	  // reached via horizontal scroll truncating the segment down to a
	  // sliver at the screen edge. Nothing to draw but its own trailing
	  // divider, at its one and only column.
	  draw_divider(heading_height - 2 - level, current_pos, track, i);
	} else {
	  std::string name = track->getElementName();
	  auto & track_info = info.getTrackInfo(track->getInternalId());
	  // The toggle (1 cell, front) and the trailing "│" are always
	  // reserved (the actual_width <= 1 branch above covers when even
	  // that can't be met); whatever's left goes to the name, with the
	  // activity dot claiming the last cell of it first - so a
	  // narrow segment drops the dot before it drops the toggle.
	  auto remaining = actual_width - 2;
	  bool show_dot = remaining >= 1;
	  auto element_name_width = std::max(0, remaining - (show_dot ? 1 : 0));

	  setBgColor(segment_color(track));

	  // A track's own row shows up at every level from where its
	  // subtree first starts merging up through getDepth() - 1, where
	  // its whole subtree finally merges into one span (see the
	  // hop-walk above) - drawing the toggle/name/dot at each of those
	  // intermediate, still-partial levels too would show the same
	  // label repeated on several rows. Only the last one, where the
	  // span is actually complete, draws it; the rest stay blank.
	  if (level == track->getDepth() - 1) {
	    name = Utf8::truncateToWidth(name, element_name_width);
	    name = Utf8::padToWidth(name, element_name_width);

	    // Same coloring method as the level-0 title bar's own toggle
	    // (toggle_color() - the track's own darkened base color) - reads
	    // as a heading control, not a status indicator (contrast the dot
	    // below, or the level-0 title bar's own M/S glyphs).
	    setFgColor(toggle_color(track));
	    putstr(heading_height - 2 - level, current_pos, track->isCollapsed() ? "+" : "-");

	    setFgColor(0xff, 0xff, 0xff);
	    putstr(heading_height - 2 - level, current_pos + 1, name);

	    if (show_dot) {
	      // Always draw the dot - faint when idle, full clip/active color
	      // otherwise - rather than only appearing (amid blank padding) once
	      // active, so it reads as a real status indicator, not padding.
	      // At the segment's trailing end (right before the divider),
	      // matching where the level-0 title bar's own M/S glyphs sit,
	      // rather than up front where the collapse toggle now is.
	      if (track_info.isClipping()) {
		setFgColor(0xe0, 0x10, 0x40);
	      } else if (track_info.isActive()) {
		setFgColor(0x10, 0xe0, 0x40);
	      } else {
		// Idle: faint_color() - see its own comment - rather than a
		// fixed grey.
		setFgColor(faint_color(track));
	      }
	      putstr(heading_height - 2 - level, current_pos + 1 + element_name_width, "•");
	    }
	  } else {
	    setFgColor(0x00, 0x00, 0x00);
	    putstr(heading_height - 2 - level, current_pos, string(static_cast<size_t>(actual_width - 1), ' '));
	  }
	  // Drawn last for both branches - see the level 0 branch's own
	  // comment on why. At the segment's own last column regardless of
	  // how the budget above split up the rest of it.
	  draw_divider(heading_height - 2 - level, current_pos + actual_width - 1, track, i);
	}
      }
      
      current_pos += actual_width;
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
  auto & scene = song.getScene(pattern_idx);

  display_row += heading_height;
          
  string padding(static_cast<size_t>(cols), ' ');
    
  setBgColor(styles.window_bg_color);
  putstr(display_row, 0, padding);
    
  auto current_pos = 0;
  for (int i = -1; i < static_cast<int>(track_ids.size()); i++) {
    if (i >= 0 && i < current_scroll_.track) continue;
    if (current_pos >= cols) break;
    
    Color fg, bg, cell_fg, cell_bg;
      
    if (highlight) {
      fg = Color("#80c080");
      bg = Color("#80a080");
    } else if (pattern_row % 4 == 0) {
      fg = styles.window_accent_fg_color;
      bg = styles.window_accent_bg_color;
    } else {
      fg = styles.window_fg_color;
      bg = styles.window_bg_color;
    }

    if (is_neighboring_pattern) {
      Color black;
      bg = bg.blend(0.75f, black);
      fg = fg.blend(0.75f, black);
    }

    // There's always an effective region to highlight, even with no mark
    // set - it degenerates to just the note under the cursor (see
    // getEffectiveSelectionBounds, computed once per render() call).
    bool row_track_in_selection = pattern_idx == info.getPatternIndex() &&
      i >= sel_bounds.track_lo && i <= sel_bounds.track_hi &&
      pattern_row >= sel_bounds.row_lo && pattern_row <= sel_bounds.row_hi;
    // A single-track NOTE_COLUMN/COMMAND-scoped region is scoped to
    // specific columns (see set-mark/kill-region) - highlight per-column
    // inside the loop below instead of the whole track uniformly. TRACK
    // and EVERYTHING (every track, spanning right through the annotation
    // too - see getEffectiveSelectionBounds()'s own comment) both get the
    // uniform whole-track treatment; the annotation area's own separate
    // highlight (below, outside this per-track loop) covers the rest of
    // what EVERYTHING means.
    bool column_scoped_selection = row_track_in_selection &&
      sel_bounds.scope != SelectionScope::TRACK && sel_bounds.scope != SelectionScope::EVERYTHING;
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
      auto track_id = track_ids[static_cast<size_t>(i)];
      auto & notes = scene.getNotes(pattern_row, track_id);
      auto & command = scene.getCommand(pattern_row, track_id);
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
	// Per-column override of the track-level fg/bg for a NOTE_COLUMN- or
	// COMMAND-scoped region (see getEffectiveSelectionBounds()) - set
	// across the *whole* selected row range, not just column_highlighted's
	// single row.
	bool column_selected = column_scoped_selection &&
	  ((sel_bounds.scope == SelectionScope::NOTE_COLUMN && !track_info.isEffectColumn(k) &&
	    track_info.getNoteNumber(k) >= sel_bounds.note_lo && track_info.getNoteNumber(k) <= sel_bounds.note_hi) ||
	   (sel_bounds.scope == SelectionScope::COMMAND && track_info.isEffectColumn(k)));
	Color cur_fg = column_selected ? styles.highlight_fg_color : fg;
	Color cur_bg = column_selected ? styles.highlight_bg_color : bg;

	setFgColor(styles.window_border_color);
	setBgColor(cur_bg);
	auto column_type = track_info.getColumnType(k);
	if (track_info.collapsed_) {
	  // Every column's own content is hidden (see
	  // VisibleTrackInfo::collapsed_) - just a blank placeholder cell,
	  // sized to line up with whatever width renderHeading() laid out
	  // for this same track (getTrackWidth() - 1, not a hardcoded
	  // literal - same reasoning as the SAMPLE/DRUM_MACHINE placeholder
	  // just below), plus the track's own shared trailing "│" (drawn
	  // once this loop is done, below). A "·" marks the cell whenever this
	  // row has any note at all, so a collapsed track doesn't look empty
	  // where it actually has content.
	  auto width = std::max(track_info.getTrackWidth() - 1, 0);
	  putstr(display_row, current_pos, std::string(static_cast<size_t>(width), ' '));
	  bool any_note_defined = false;
	  for (auto & n : notes) {
	    if (n.isDefined()) { any_note_defined = true; break; }
	  }
	  if (width > 0 && any_note_defined) {
	    setFgColor(cur_fg);
	    putstr(display_row, current_pos, "·");
	    setFgColor(styles.window_border_color);
	  }
	  current_pos += width;
	} else if (track && (track->getType() == TrackType::SAMPLE || track->getType() == TrackType::DRUM_MACHINE)) {
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
	  bool defined = k < static_cast<int>(notes.size()) && notes[static_cast<size_t>(k)].isDefined();
	  putstr(display_row, current_pos, std::string(static_cast<size_t>(width), defined ? 'x' : ' '));
	  current_pos += width;
	} else if (column_type == ColumnType::EFFECT) {
	  // Falls back to cur_fg (the region's own dark foreground) when
	  // column_selected, same reasoning as velocity/delay's own cur_fg
	  // fallback below - command_column_color is tuned for contrast
	  // against the normal dark row background, not the bright
	  // effective-region highlight.
	  cell_fg = command.isDefined() && !column_selected ? styles.command_column_color : cur_fg;
	  cell_bg = cur_bg;
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);
	  auto s = to_string(command);
	  putstr(display_row, current_pos, s);
	  if (column_highlighted) {
	    setUnderline(true);
	    putstr(display_row, current_pos + new_cursor.subcol, s[static_cast<size_t>(new_cursor.subcol)]);
	    setUnderline(false);
	  }
	  current_pos += 4;
	} else if (column_type == ColumnType::NOTE) {
	  auto l = track_info.getNoteNumber(k);
	  auto note = l < static_cast<int>(notes.size()) ? notes[static_cast<size_t>(l)] : Note();

	  cell_fg = cur_fg;
	  cell_bg = cur_bg;
	  if (!note.isDefined()) cell_fg = cell_fg.blend(0.5f, cell_bg);
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);
	  auto tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
	  auto s = note.toString(tuning);
	  while (s.size() < 3) s += ' ';
	  putstr(display_row, current_pos, s);
	  current_pos += 3;
	} else if (column_type == ColumnType::VELOCITY || column_type == ColumnType::DELAY) {
	  auto l = track_info.getNoteNumber(k);
	  auto note = l < static_cast<int>(notes.size()) ? notes[static_cast<size_t>(l)] : Note();
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
	  cell_fg = column_selected ? cur_fg : (column_type == ColumnType::VELOCITY ? Color("#bfa426") : Color("#42c1ea"));
	  cell_bg = cur_bg;
	  if (!note.isDefined()) cell_fg = cell_fg.blend(0.5f, cell_bg);
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);

	  putstr(display_row, current_pos, s);
	  if (column_highlighted) {
	    setUnderline(true);
	    putstr(display_row, current_pos + new_cursor.subcol, s[static_cast<size_t>(new_cursor.subcol)]);
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

  // Cache this row's annotation on-screen position whenever it's the
  // cursor/playhead's own row - highlight is true exactly then, in every
  // call site (see render()) - so startAnnotationEdit() can read it
  // instead of re-deriving the same current_pos accumulation above
  // independently, which could drift out of sync with what's actually
  // drawn here.
  if (highlight) {
    annotation_screen_row_ = display_row;
    annotation_screen_col_ = current_pos + 2;
  }

  if (current_pos < cols) {
    auto & annotation = scene.getAnnotation(pattern_row);
    // Cursor parked on this row's annotation slot (see GridPosition::
    // scope's own comment), or this row falls inside an EVERYTHING-scoped
    // selection (getEffectiveSelectionBounds() - one end on the
    // annotation, the other on a real track, escalated to cover the
    // whole row) - either way the annotation area itself needs to read as
    // selected too, not just the tracks.
    bool row_on_annotation_cursor = highlight && current_cursor.isOnAnnotation();
    bool row_in_everything_selection = sel_bounds.scope == SelectionScope::EVERYTHING &&
      pattern_idx == info.getPatternIndex() && pattern_row >= sel_bounds.row_lo && pattern_row <= sel_bounds.row_hi;
    // A genuine multi-row ANNOTATION-scoped selection (mark and point both
    // on the annotation, on different rows - see kill-region/kill-ring-
    // save/yank's own ANNOTATION handling). Guarded on row_lo != row_hi so
    // the everyday degenerate case (no mark, or mark and point on the same
    // row's annotation) keeps its existing single-row look below rather
    // than picking up the full-width selected fill meant for an actual
    // range.
    bool row_in_annotation_selection = sel_bounds.scope == SelectionScope::ANNOTATION &&
      sel_bounds.row_lo != sel_bounds.row_hi &&
      pattern_idx == info.getPatternIndex() && pattern_row >= sel_bounds.row_lo && pattern_row <= sel_bounds.row_hi;
    bool row_selected = row_on_annotation_cursor || row_in_everything_selection || row_in_annotation_selection;
    bool row_fully_filled = row_in_everything_selection || row_in_annotation_selection;

    // Background for the gap after the last track. EVERYTHING means the
    // *whole row* is selected (getEffectiveSelectionBounds()'s own
    // comment), so it fills the full remaining width, same as a selected
    // track column always colors its full width regardless of content;
    // a multi-row ANNOTATION selection gets the same full-width fill,
    // scoped to just the annotation area since nothing on the grid is
    // selected in that scope (see getEffectiveSelectionBounds()). A lone
    // parked cursor with no wider selection only highlights the
    // text/placeholder itself plus one character of margin either side
    // (further down, once its width is known), not the whole row - that
    // full-row treatment is what row_fully_filled is for. Either way this
    // is the *same* light green every other selected cell already uses,
    // not a separate color, so it doesn't read as some other kind of
    // state. Falls back to the playhead tint when this is just the
    // currently-playing/edit-cursor row and nothing here is actually
    // selected, or plain window_bg_color (already painted by the leading
    // padding fill above) otherwise. Plain, unblended green - the same
    // tint the rest of a highlighted row uses - since this is just the
    // gap before the annotation's own content; the red identity only
    // belongs on the text/placeholder span itself (plus its margin),
    // further down.
    if (row_fully_filled) {
      setFgColor(styles.highlight_fg_color);
      setBgColor(styles.highlight_bg_color);
      putstr(display_row, current_pos, string(static_cast<size_t>(cols - current_pos), ' '));
    } else if (highlight) {
      setFgColor(0x80, 0xc0, 0x80);
      setBgColor(0x80, 0xa0, 0x80);
      putstr(display_row, current_pos, string(static_cast<size_t>(cols - current_pos), ' '));
    }

    if (current_pos + 2 < cols) {
      bool has_text = !annotation.empty();
      // Only the cursor's own row gets the "type here" invite when
      // there's nothing written yet - a multi-row EVERYTHING or
      // ANNOTATION selection shouldn't make every other selected row
      // look like it's individually about to be edited too, just selected.
      if (has_text || row_on_annotation_cursor) {
	string text = has_text ? annotation : string("(add annotation)");

	// Whether to extend this span's own background one character
	// either side of the text, rather than coloring just the text
	// itself: everywhere except a fully-filled row (EVERYTHING, or a
	// genuine multi-row ANNOTATION selection), which already filled
	// across the whole remaining width above - the playhead-only case
	// still sits inside that same whole-row fill too, but its own span
	// (a slightly darker shade, so it reads as a distinct thing within
	// the row) gets the same one-character margin as every other case.
	bool want_margin = !row_fully_filled;
	Color annotation_red(0xe0, 0x30, 0x30);

	if (row_selected) {
	  // The same reversed (dark-on-bright) highlight every other
	  // selected cell uses - see the per-column loop above's own
	  // column_selected handling - a selected cell still reads as
	  // "selected" first, not annotation-red - but nudged a little
	  // toward this span's own red identity (both fg and bg), same as
	  // the playhead-only case below, so the annotation's own span
	  // still reads as red content even while selected/reversed.
	  setFgColor(styles.highlight_fg_color.blend(0.2f, annotation_red));
	  setBgColor(styles.highlight_bg_color.blend(0.2f, annotation_red));
	} else if (highlight) {
	  // Playhead row, nothing selected here specifically - the same
	  // green as the rest of the row (not red - the playhead
	  // highlights the *whole* row, annotation included), but nudged a
	  // little toward this span's own red identity (both fg and bg),
	  // as if a translucent red wash sat over just the text/placeholder
	  // itself - unlike the plain gap fill above, which stays pure
	  // green - so the annotation's own span still reads as red
	  // content, not just a darker patch of the same row tint.
	  setFgColor(Color(0x80, 0xc0, 0x80).blend(0.2f, annotation_red));
	  setBgColor(Color(0x60, 0x78, 0x60).blend(0.2f, annotation_red));
	} else {
	  setFgColor(0xe0, 0x30, 0x30);
	  setBgColor(0x70, 0x20, 0x20);
	}

	if (want_margin) {
	  // Just the text plus one character of margin on each side, not
	  // the whole remaining row (that's what EVERYTHING is for) -
	  // without this, the text would otherwise sit directly against
	  // the plain, untinted background on either side.
	  auto margin_lo = current_pos + 1;
	  auto margin_hi = min(cols, current_pos + 2 + static_cast<int>(text.size()) + 1);
	  if (margin_hi > margin_lo) {
	    putstr(display_row, margin_lo, string(static_cast<size_t>(margin_hi - margin_lo), ' '));
	  }
	}
	putstr(display_row, current_pos + 2, text);
      }
    }
  }
}
