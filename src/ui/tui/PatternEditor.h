#ifndef _PATTERNEDITOR_H_
#define _PATTERNEDITOR_H_

#include "../UIElement.h"
#include "../GridPosition.h"
#include "../../model/PatternBlockOps.h"
#include "../ClipboardEntry.h"
#include "../SelectionBounds.h"
#include "../PatternSource.h"
#include "../ScenePatternSource.h"
#include "InlineEditor.h"
#include "LevelMeter.h"

#include <chrono>
#include <functional>
#include <memory>
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

  // `focused` (whether this widget is UI::active_element_ - it has no way
  // to know that itself) picks the region's highlight: bright while
  // focused, faint (styles.highlight_unfocused_bg_color) otherwise. The
  // playhead-row tint is transport state, not input focus - the same
  // either way.
  bool render(const StyleProvider & styles, bool refresh, bool focused);
  bool offerInput(const InputEvent & input) override;
  void handleMidiEvent(MidiEvent & ev) override;

  // Mirrors StatusLine::isReaderActive(): while this widget's inline editor
  // is open, UI::offerInput() must not let a global keybinding (Space/
  // toggle-playing, C-x C-c/quit, ...) steal a keystroke meant for it.
  bool isReaderActive() const { return inline_editor_.isOpen(); }

  // Aborts the locator/track-name editor without committing anything -
  // used by StatusLine so opening M-x takes focus away from it rather than
  // opening on top of it. A no-op if nothing is open.
  void cancelReaderEdit();

  // Plain, source-agnostic cursor/step accessors - PatternEditor has no
  // idea these happen to be used to feed a Launchpad device's own track
  // selection and step-entry advance (see UI::handleLaunchpadButtonEvent
  // and UI::handleLaunchpadPadEvent); it just exposes its own current
  // cursor/step state the same way it always has, and lets the track be
  // moved.
  int getCursorTrackIndex() const { return current_cursor.track; }
  // Commits to current_cursor immediately, not just new_cursor - the
  // normal new_cursor -> current_cursor handoff only happens inside
  // render(), which never runs while a different top-level view
  // (ClipGrid) occupies PatternEditor's own screen slot instead (see
  // UI::renderComponents()'s own exactly-one-of-the-two branch). Without
  // this, getCursorTrackIndex() - what every Launchpad device's own
  // fallback_track_index actually reads - would keep reporting the old
  // track for as long as PatternEditor stays offscreen, silently
  // stranding a caller like ClipGrid's own focus-jump callback (same
  // immediate-commit precedent startLocatorEdit() already sets for
  // .scope, elsewhere in this class).
  // Defined in the .cpp, not inline - needs Song's own full definition
  // (getRootTrackIds()) for the Song::setCurrentTrackId() sync described
  // above, and this header only forward-declares Song.
  void setCursorTrack(int track_index);
  int getEditStepSize() const { return edit_step_size; }

  // Arrangement mode (the default) edits the arrangement and placed clips, with
  // the transport as the cursor row; session mode edits clips directly,
  // one scene at a time (ScenePatternSource).
  void setSessionMode(bool session);
  bool isSessionMode() const { return source_ == scene_source_.get(); }
  // The scene (clip) `track_id`'s own session-mode position is in.
  int getSessionScene(int track_id) const { return scene_source_->trackBlock(track_id).value_or(0); }
  // Where each track's launched clip is playing, for session mode's
  // per-track playhead rows.
  void setSessionPlayheads(std::unordered_map<int, ScenePatternSource::Playhead> playheads);


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

  // Which real Clip (by id) this session has created so far, keyed by
  // track_id (Controller::ensureNoteRecordingClip()) - Controller::
  // extendRecordingClipsIfNeeded() (UI::handlePlaybackEvent()) reads/mutates this directly to
  // grow each one's own window as the take continues.
  std::unordered_map<int, std::string> & getAutoRecordClipIds() { return auto_record_clip_ids_; }

  // Every track_id currently receiving live keyboard input - same
  // computation onRowAdvanced() uses for sweepAutoRecordRows(), shared
  // here so Controller::extendRecordingClipsIfNeeded() can grow a track's
  // recording clip only while a note is actually being held on it, not
  // merely while the session is still nominally open.
  std::vector<int> getActiveNoteTrackIds() const;

  // Called via Controller::setBufferChangeListener()'s UI.cpp fan-out
  // whenever the active buffer changes (switch, kill landing on a
  // different buffer, or a fresh buffer created) - saves the outgoing
  // buffer's own cursor/scroll/selection/live-note/locator-editing
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
  // See SelectionBounds.h. The rows never span more than the content the
  // selection's anchor row shows on each selected track
  // (PatternSource::sourceRows()) - a clip's notes, or the background's.
  SelectionBounds getEffectiveSelectionBounds(const Song & song, const std::vector<int> & track_ids) const;
  // Where the selection starts: the mark, or the cursor with no mark - what
  // picks which content a block operation acts on (PatternSource::
  // editGrid()'s own anchor).
  RowAddress selectionAnchor() const;

  // The single place selection_active_ is ever written.
  void setSelectionActive(bool active);

  // scroll_row is a separate parameter (not always current_scroll_.row)
  // because render() below needs to call this with the *new* scroll
  // position it just computed for this same frame, before that becomes
  // current_scroll_ - see render()'s own comment on why.
  std::unordered_map<int, VisibleTrackInfo> getTrackInformation(const Song & song, int scroll_row) const;
  VisibleTrackInfo getTrackInfoFor(const Song & song, int track_id) const;
  // One Tuning per track in [track_lo, track_hi] (track_lo == track_hi for
  // a single-track capture) - kill-region/kill-ring-save/kill-row's own
  // ClipboardEntry::track_tunings capture, see that struct's own comment.
  std::vector<Tuning> tuningsForTrackRange(const Song & song, const std::vector<int> & track_ids, int track_lo, int track_hi) const;
  void renderHeading(const StyleProvider & styles, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info, bool focused);
  void renderRow(const StyleProvider & styles, int heading_height, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info, int row, bool highlight, const SelectionBounds & sel_bounds, bool focused);

  // Opens the locator editor for the cursor's current row - called
  // once offerInput() sees Enter pressed while the cursor is parked on
  // the locator slot (Right arrow past the last track's last column).
  // Positions the reader at locator_screen_row_/locator_screen_col_,
  // cached by renderRow() itself (whenever it draws the cursor's own row)
  // rather than recomputed here, so the two can never disagree about
  // where the locator actually sits on screen. A no-op if the editor
  // is already open.
  void startLocatorEdit();

  // Opens the in-place track-name editor for whatever track the cursor's
  // column currently belongs to. A no-op for a track with no name field
  // to edit at all - an Effect's own title bar carries no name (see
  // renderHeading()'s is_color_eligible) - or while the editor is already
  // open. Positions the reader at track_name_screen_col_/
  // track_name_screen_width_, cached by renderHeading() itself, same
  // reasoning as startLocatorEdit()'s own comment.
  void startTrackNameEdit();

  // `copy-to-clip` - saves the current selection as a new, unnamed clip.
  // Always whole-track scope (SelectionScope::TRACK), on the cursor's own
  // current track, for whatever row range is marked (or just the
  // cursor's own row if nothing is). No placement - the direct keyboard
  // equivalent of hand-editing a new <clip> into the XML. Naming happens
  // later, from the (not yet built) clip viewer, not here. A no-op if the
  // cursor's track index is somehow out of range (shouldn't happen with
  // any real track list, just defensive).
  void copyToClip();

  // Whether the cursor is parked on the current row's locator "slot"
  // (GridPosition::scope == SelectionScope::LOCATOR - reached by Right
  // arrow past the last track's last column) lives on current_cursor/
  // new_cursor themselves, not a separate flag here - see GridPosition.h's
  // own comment on why that's the field to check instead of a one-off
  // bool, and getEffectiveSelectionBounds()/isHighlighted() for the two
  // places it actually matters. Reaching the slot must not start editing
  // on its own; only Enter (see offerInput()) does that
  // (startLocatorEdit()).
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
  // Last frame's PlaybackInfo::getVoiceCount() - lets the VU meter catch
  // the one extra redraw needed right as the last voice finishes (see
  // render()'s own use of it), not just while it's still sounding.
  int current_voice_count_ = 0;
  // A track's VU meter smoothing, by track id, and whether any meter drawn
  // last was still showing a level (the heading keeps redrawing until they
  // have all fallen back to silent).
  struct MeterSmoothing {
    level_meter::Ballistics ballistics;
    std::chrono::steady_clock::time_point last_update;
  };
  std::unordered_map<int, MeterSmoothing> meter_smoothing_;
  bool meters_showing_ = false;

  int edit_step_size = 1, new_edit_step_size = 1;
  bool row_edited = false;
  int current_song_version = 0;

  // What render() last drew the cursor/selection highlight with - not
  // buffer-local state (unlike everything in EditingState below), just a
  // dirty-check cache, so it lives here rather than there: a focus change
  // needs to force a redraw the same way a cursor move already does, or
  // the highlight would stay stuck on/off-screen until some unrelated
  // change happened to repaint this row.
  bool current_focused_ = true;

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
  // last_cleared_row_ exactly (see its comment
  // for the full reasoning): the actual clear-once-per-session logic is
  // centralized on Controller::ensureRowCleared() (this set is just the
  // per-session bookkeeping it's called with - see that method's own
  // comment for why it stays here rather than also moving onto
  // Controller), so it's safe to call from every write site during an
  // active session, and onRowAdvanced() sweeps whatever rows the
  // playhead just passed through.
  std::set<std::pair<int, int>> auto_record_cleared_rows_;
  int last_cleared_row_ = -1;

  // Which real Clip this session has recorded into, per track (see
  // getAutoRecordClipIds()'s own comment) - reset the same moments
  // auto_record_cleared_rows_ above is, so a finished session never
  // leaves a stale entry for a later, unrelated one to stumble over.
  std::unordered_map<int, std::string> auto_record_clip_ids_;

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
  // ever actually differs from the default is LOCATOR, and comparing
  // it against current_cursor.scope is what lets
  // getEffectiveSelectionBounds() tell "the mark and point are on
  // opposite sides of the grid/locator boundary" apart from "both are
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


  // The on-screen (row, col) renderRow()'s own (display-only) locator
  // code draws at for the cursor/playhead's current row - cached there
  // (set whenever its `highlight` parameter is true, which is exactly
  // when it's rendering that row, in every call site - see render()) for
  // startLocatorEdit() to read rather than re-deriving the same
  // current_pos accumulation independently. -1 until the first render.
  int locator_screen_row_ = -1, locator_screen_col_ = -1;

  // The on-screen col/width renderHeading()'s own level-0 branch draws
  // the cursor's current track's name field at - row isn't cached since
  // it's deterministic (a color-eligible track's own title bar is
  // always level 0, i.e. row heading_height - 2 - see
  // startTrackNameEdit()). Set whenever that track is color-eligible and
  // has room to show a name; left at -1/-1 otherwise (including every
  // other track type), same "always recomputed by rendering, never
  // re-derived independently" reasoning as locator_screen_row_/
  // locator_screen_col_ above.
  int track_name_screen_col_ = -1, track_name_screen_width_ = -1;

  // Shared by the locator and track-name editors; only one is ever open.
  InlineEditor inline_editor_{getPlane()};

  // Where rows, cells and edits come from - see PatternSource.h. Points at
  // one of the two sources below, per setSessionMode().
  PatternSource * source_ = nullptr;
  std::unique_ptr<PatternSource> arrangement_source_;
  std::unique_ptr<ScenePatternSource> scene_source_;
  // Tells the source which track the cursor is on (PatternSource::
  // setCursorTrack()), once per change.
  void syncCursorTrack(const Song & song);
  int synced_cursor_track_id_ = -1;
  // The row-number gutter left of the first track: a margin only where
  // each track shows its own row numbers (VisibleTrackInfo::
  // row_number_width_, kTrackRowNumberWidth wide).
  // The row-number gutter: " 1f │" in Arrangement view, as many hex
  // digits as its rows need (row_digits_); a margin in Session view.
  int gutterWidth() const { return isSessionMode() ? 1 : row_digits_ + 3; }
  // Hex digits the arrangement's row numbers take - at least 2, enough
  // for its content and the cursor. Updated once per render().
  int row_digits_ = 2;
  static constexpr int kTrackRowNumberWidth = 3;
  // How close the highlighted row gets to the top or bottom before the
  // whole view scrolls.
  static constexpr int kScrollMargin = 3;
  // Says so on the status line when the cursor can't move - it follows a
  // playing track's playhead. True then.
  bool reportLockedCursor();
  // Set when what's shown changed in a way render()'s own dirty checks
  // don't see (the source switching, per-track playheads moving).
  bool force_full_redraw_ = false;
  std::unordered_map<int, ScenePatternSource::Playhead> session_playheads_;

  // The block at the top of the view - current_scroll_.row counts from
  // its start. Follows the cursor's own block, scrolled just far enough to
  // keep the cursor on screen, until the mouse wheel detaches the view
  // (scrollView()/scrollViewTracks()); the next cursor move reattaches it.
  int view_block_ = 0;
  bool view_detached_ = false;
  // Scroll the view (not the cursor) by rows, or by whole tracks sideways.
  void scrollView(int delta_rows);
  void scrollViewTracks(int delta_tracks);

  // The StyleProvider render() was last called with - stashed there
  // purely so startTrackNameEdit() can force an immediate renderHeading()
  // pass after correcting the scroll position (see that method's own
  // comment), so the name field's cached position is current, without needing a
  // StyleProvider of its own to pass in; commands run from a keybinding/
  // menu item have no such thing handed to them the way render() does.
  // Raw pointer, not a copy: UI::styles_ (what render() is actually
  // always called with) outlives every PatternEditor call by construction,
  // and re-pointing here each render() call is cheaper than copying
  // StyleProvider's several Color fields every frame for a pointer that's
  // read only in this one rare, keyboard-driven path.
  const StyleProvider * last_styles_ = nullptr;

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
    int last_cleared_row = -1;
    bool selection_active = false;
    int selection_start_pattern = 0, selection_start_row = 0, selection_start_track = 0;
    int selection_start_col = 0;
    SelectionScope selection_start_scope = SelectionScope::NOTE_COLUMN;
    SelectionBounds current_sel_bounds;
    int locator_screen_row = -1, locator_screen_col = -1;
    int track_name_screen_col = -1, track_name_screen_width = -1;
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
