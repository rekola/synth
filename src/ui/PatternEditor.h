#ifndef _PATTERNEDITOR_H_
#define _PATTERNEDITOR_H_

#include "UIElement.h"
#include "GridPosition.h"
#include "PatternBlockOps.h"
#include "ClipboardEntry.h"
#include "SelectionBounds.h"

#include <vector>
#include <unordered_map>
#include <set>
#include <string>
#include <utility>

class Synth;
class InputEvent;
class StyleProvider;
class Song;
class VisibleTrackInfo;
class Controller;

class PatternEditor : public UIElement {
 public:
  PatternEditor(UIPlane & parent);

  bool render(const StyleProvider & styles, bool refresh = false);
  bool offerInput(const InputEvent & input) override;
  void handleMidiEvent(MidiEvent & ev) override;

  // Mirrors StatusLine::isReaderActive() exactly, same reason it exists:
  // UI::offerInput() must not let a *global* keybinding (Space/toggle-
  // playing, C-x C-c/quit, ...) reach its own dispatchCommand() while this
  // class's own annotation-editing reader (startAnnotationEdit()) is open,
  // the same way it already skips that for StatusLine's M-x reader -
  // otherwise every one of those keys leaks past PatternEditor::offerInput()'s
  // own reader-active forwarding before it ever gets a chance to run.
  bool isReaderActive() { return getPlane().readerActive(); }

  // Plain, source-agnostic cursor/step accessors - PatternEditor has no
  // idea these happen to be used to feed a Launchpad device's own track
  // selection and step-entry advance (see UI::handleLaunchpadButtonEvent
  // and UI::handleLaunchpadPadEvent); it just exposes its own current
  // cursor/step state the same way it always has, and lets the track be
  // moved.
  int getCursorTrackIndex() const { return current_cursor.track; }
  void setCursorTrack(int track_index) { new_cursor.track = track_index; new_cursor.col = new_cursor.subcol = 0; }
  int getEditStepSize() const { return edit_step_size; }

  // Called whenever the UI thread learns of a new playhead position (see
  // UI::handlePlaybackEvent, right after Controller::receivePlaybackSnapshot() -
  // mirrors LaunchpadManager::onRowAdvanced() exactly, see its own
  // comment for the full reasoning) - while a realtime auto-play-while-
  // held recording session is active (see auto_started_playback_),
  // sweeps every row the playhead just passed through and clears each
  // currently-recorded track's notes there, so a live take replaces
  // whatever was previously on that stretch instead of merging with it.
  // A no-op outside such a session.
  void onRowAdvanced(Controller & controller);

  // Called via Controller::setBufferChangeListener()'s UI.cpp fan-out
  // whenever the active buffer changes (switch, kill landing on a
  // different buffer, or a fresh buffer created) - saves the outgoing
  // buffer's own cursor/scroll/selection/live-note/annotation-editing
  // state into buffer_states_ (private, below) and restores the incoming
  // buffer's own saved copy (or a fresh default, for a never-before-
  // visited buffer). Unlike Controller's own internal
  // save/loadActiveBufferState() pair (Controller.h), which run in two
  // separate steps immediately before and after the switch itself, this
  // is an external listener that only learns about a switch after it
  // already happened, so it tracks the outgoing buffer's own identity
  // itself rather than being handed it. Also fires on a plain rename
  // (renameActiveBuffer()), which changes getActiveBufferName() without
  // the active Song actually changing - told apart from a real switch by
  // Song identity, not by name (see last_active_song_, below).
  void handleBufferChanged();

protected:
  // See SelectionBounds.h.
  SelectionBounds getEffectiveSelectionBounds(const Song & song, const std::vector<int> & track_ids) const;

  // The single place selection_active_ is ever written - also mirrors the
  // new value to Controller::setPatternSelectionActive() so
  // moveEditPosition()/setEditPosition() know whether row navigation
  // needs to stay clamped to the current pattern (see that method's own
  // comment). A raw `selection_active_ = ...` assignment anywhere else
  // would silently desync the two.
  void setSelectionActive(bool active);

  // scroll_row is a separate parameter (not always current_scroll_.row)
  // because render() below needs to call this with the *new* scroll
  // position it just computed for this same frame, before that becomes
  // current_scroll_ - see render()'s own comment on why.
  std::unordered_map<int, VisibleTrackInfo> getTrackInformation(const Song & song, int scroll_row) const;
  VisibleTrackInfo getTrackInfoFor(const Song & song, int track_id) const;
  void renderHeading(const StyleProvider & styles, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info);
  void renderRow(const StyleProvider & styles, int heading_height, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info, int row, bool highlight, const SelectionBounds & sel_bounds);

  // Opens the annotation editor for the cursor's current row - called
  // once offerInput() sees Enter pressed while the cursor is parked on
  // the annotation slot (Right arrow past the last track's last column).
  // Positions the reader at annotation_screen_row_/annotation_screen_col_,
  // cached by renderRow() itself (whenever it draws the cursor's own row)
  // rather than recomputed here, so the two can never disagree about
  // where the annotation actually sits on screen. A no-op if a reader is
  // already active (shouldn't happen - showReader() itself already
  // guards this too - just defensive).
  void startAnnotationEdit();

  // Whether the cursor is parked on the current row's annotation "slot"
  // (GridPosition::scope == SelectionScope::ANNOTATION - reached by Right
  // arrow past the last track's last column) lives on current_cursor/
  // new_cursor themselves, not a separate flag here - see GridPosition.h's
  // own comment on why that's the field to check instead of a one-off
  // bool, and getEffectiveSelectionBounds()/isHighlighted() for the two
  // places it actually matters. Reaching the slot must not start editing
  // on its own; only Enter (see offerInput()) does that
  // (startAnnotationEdit()).
  GridPosition current_cursor, new_cursor;

  int current_score_playing_row = 0;
  int current_score_pattern = 0;  
  int current_score_total_columns = 0;
  // .row/.track is where the grid is scrolled to; .col is only meaningful
  // when .track's own width alone exceeds the screen (see GridPosition.h,
  // PatternScroll.h) - scrolling whole tracks can't keep the cursor's own
  // always-on highlight (its note/velocity/delay group, not just its own
  // column - see VisibleTrackInfo::getNoteColumnRange) in view by itself
  // then, so .col picks which of .track's own columns is the first one
  // actually drawn. Meaningless for any other track, which always renders
  // from its own column 0.
  GridPosition current_scroll_;
  int current_tempo = 0;

  int edit_step_size = 1, new_edit_step_size = 1;
  bool row_edited = false;
  int current_song_version = 0;

  std::unordered_map<int, int> active_midi_notes;

  // Which pattern column/row/track a currently-held computer-keyboard note
  // key landed on (keyed by InputEvent::getId(), the physical key - a key
  // can't be pressed twice without an intervening release, so this is a
  // safe key, the same reasoning active_midi_notes above already relies
  // on for MIDI note numbers). Populated on a fresh note-on press, erased
  // and used to target the right STOP_NOTE on that same key's eventual
  // Kitty-protocol release - see offerInput()'s raw note-entry code.
  struct ActiveKeyboardNote { int note_column, row, track_id; };
  std::unordered_map<int, ActiveKeyboardNote> active_keyboard_notes_;

  // True iff some other currently-held key already occupies (track_id,
  // note_column) - mirrors LaunchpadManager::isColumnLiveHeld (see its
  // own comment). Without this, every non-Shift keystroke lands on the
  // exact same fixed cursor column (new_cursor.col never moves between
  // keystrokes), so a genuine chord - multiple keys held down together,
  // the same physical gesture Launchpad's simultaneous pad presses
  // already support - would have each key's PLAY_NOTE steal the
  // previous one's voice via Player.cpp's stopVoices(column), instead of
  // sounding together.
  bool isKeyColumnLiveHeld(int track_id, int note_column) const {
    for (auto & [ id, note ] : active_keyboard_notes_) {
      if (note.track_id == track_id && note.note_column == note_column) return true;
    }
    return false;
  }

  // Whether this code (not the user manually pressing Space) was the one
  // that started the transport for the realtime-advance-while-held
  // feature - mirrors LaunchpadManager::auto_started_playback_ exactly
  // (see its own comment for the reasoning); only consulted/cleared when
  // active_keyboard_notes_ goes back to empty, so a manually-started
  // session is never stopped just because a held note key was released.
  bool auto_started_playback_ = false;

  // Whole-row-replace bookkeeping for a realtime-recording session -
  // mirrors LaunchpadManager's own auto_record_cleared_rows_/
  // last_cleared_row_/last_cleared_pattern_idx_ exactly (see its comment
  // for the full reasoning): the actual clear-once-per-session logic is
  // centralized on Controller::ensureRowCleared() (this set is just the
  // per-session bookkeeping it's called with - see that method's own
  // comment for why it stays here rather than also moving onto
  // Controller), so it's safe to call from every write site during an
  // active session, and onRowAdvanced() sweeps whatever rows the
  // playhead just passed through.
  std::set<std::pair<int, int>> auto_record_cleared_rows_;
  int last_cleared_row_ = -1;
  int last_cleared_pattern_idx_ = -1;

  // Emacs-style mark/point selection: the mark is recorded here at C-SPC
  // time, the point is always "wherever the cursor/row currently is" (see
  // getController().getPlaybackInfo() and current_cursor.track), so normal
  // cursor movement extends the selection without any extra bookkeeping.
  // selection_start_col_ narrows this the same way within a single track:
  // the raw column index (not just a voice-slot number) the mark was set
  // on, so getEffectiveSelectionBounds() can tell whether the span ends up
  // touching the effect column at all, not just which note number it
  // started on. selection_start_scope_ mirrors current_cursor.scope at
  // mark time the same way (see GridPosition.h) - the only value that
  // ever actually differs from the default is ANNOTATION, and comparing
  // it against current_cursor.scope is what lets
  // getEffectiveSelectionBounds() tell "the mark and point are on
  // opposite sides of the grid/annotation boundary" apart from "both are
  // on the same side" - see its own comment on SelectionScope::EVERYTHING.
  bool selection_active_ = false;
  int selection_start_pattern_ = 0, selection_start_row_ = 0, selection_start_track_ = 0;
  int selection_start_col_ = 0;
  SelectionScope selection_start_scope_ = SelectionScope::NOTE_COLUMN;

  // Last frame's effective selection (see getEffectiveSelectionBounds()) -
  // compared against this frame's via SelectionBounds::operator== to
  // decide when render() needs a full repaint (render_all), standing in
  // for a hand-rolled diff of every piece of state that feeds into it
  // (the mark fields above, the playhead row, the cursor's own position).
  SelectionBounds current_sel_bounds_;

  // See ClipboardEntry.h - a future kill-ring is just
  // std::vector<ClipboardEntry> plus a rotation index in place of this
  // single entry, not a restructuring of how one entry stores itself.
  ClipboardEntry clipboard_;

  // The on-screen (row, col) renderRow()'s own (display-only) annotation
  // code draws at for the cursor/playhead's current row - cached there
  // (set whenever its `highlight` parameter is true, which is exactly
  // when it's rendering that row, in every call site - see render()) for
  // startAnnotationEdit() to read rather than re-deriving the same
  // current_pos accumulation independently. -1 until the first render.
  int annotation_screen_row_ = -1, annotation_screen_col_ = -1;

  // Which row/pattern an open annotation-editing reader belongs to (-1
  // when none is open) - set by startAnnotationEdit(), read by
  // offerInput()'s reader-active branch on Enter, so the commit always
  // writes into the row editing actually started on, not "whatever row
  // the cursor happens to be on by the time Enter is pressed" (mirrors
  // selection_start_pattern_'s own staleness guard).
  int annotation_edit_row_ = -1, annotation_edit_pattern_ = -1;

 private:
  // Snapshot of every field above (current_score_playing_row/pattern/
  // total_columns deliberately excluded - see the header comment in
  // handleBufferChanged()'s caller-facing declaration above; they're
  // derived from playback_info fresh every render() call, so they need no
  // explicit save/restore of their own) - one struct rather than one
  // std::map per field (contrast Controller.h's own per-buffer scalars)
  // since none of these are read anywhere except through PatternEditor's
  // own methods, so there's no existing wide set of call sites forcing
  // the live-scalar-plus-parallel-map shape Controller needs.
  struct EditingState {
    GridPosition current_cursor, new_cursor, current_scroll;
    int edit_step_size = 1, new_edit_step_size = 1;
    int current_song_version = 0;
    std::unordered_map<int, int> active_midi_notes;
    std::unordered_map<int, ActiveKeyboardNote> active_keyboard_notes;
    bool auto_started_playback = false;
    std::set<std::pair<int, int>> auto_record_cleared_rows;
    int last_cleared_row = -1, last_cleared_pattern_idx = -1;
    bool selection_active = false;
    int selection_start_pattern = 0, selection_start_row = 0, selection_start_track = 0;
    int selection_start_col = 0;
    SelectionScope selection_start_scope = SelectionScope::NOTE_COLUMN;
    SelectionBounds current_sel_bounds;
    int annotation_screen_row = -1, annotation_screen_col = -1;
    int annotation_edit_row = -1, annotation_edit_pattern = -1;
  };

  void saveEditingState(const std::string & name);
  void loadEditingState(const std::string & name);

  // unordered_map, unlike Controller::songs_ (std::map) - nothing here
  // ever needs buffer_states_'s own iteration order (there's no analogue
  // of the Buffers menu reading from it), it's a pure name->snapshot
  // lookup, so there's no reason to pay std::map's ordering cost.
  std::unordered_map<std::string, EditingState> buffer_states_;
  // Identity (not name) of the buffer handleBufferChanged() last saw as
  // active - a rename (renameActiveBuffer()) fires the same listener
  // without the active Song object actually changing, so that has to be
  // told apart from a real switch by Song identity, the same reasoning as
  // UI.cpp's own launchpad_last_song_. last_active_buffer_name_ tracks the
  // *name* to save/drop buffer_states_ entries under, kept in sync with
  // last_active_song_ but distinct from it because a rename does change
  // the name without changing the Song.
  const Song * last_active_song_ = nullptr;
  std::string last_active_buffer_name_;
};

#endif
