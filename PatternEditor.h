#ifndef _PATTERNEDITOR_H_
#define _PATTERNEDITOR_H_

#include "UIElement.h"
#include "Cursor.h"
#include "PatternBlockOps.h"

#include <vector>
#include <unordered_map>
#include <set>
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

protected:
  // Resolved row/track/note-column bounds for a selection-consuming command
  // (kill-region, transpose-region-*, ...). When no mark is active (or the
  // mark is on a different pattern), this degenerates to the single note
  // the cursor is currently on - there's always a region to act on, never
  // "nothing selected".
  struct SelectionBounds {
    int row_lo, row_hi, track_lo, track_hi;
    bool column_scoped; // track_lo == track_hi; note_lo/note_hi are meaningful
    int note_lo, note_hi;
    // true when column_scoped and the note range was widened because the
    // cursor is on the effect column (the effect applies to every note
    // column in the row) - selection-consuming commands that also want to
    // capture/clear/restore the row's Command check this.
    bool includes_command = false;
  };
  SelectionBounds getEffectiveSelectionBounds(const Song & song, const std::vector<int> & track_ids) const;

  // scroll_row is a separate parameter (not always current_scroll_row, the
  // member) because render() below needs to call this with the *new*
  // scroll position it just computed for this same frame, before that
  // becomes current_scroll_row - see render()'s own comment on why.
  std::unordered_map<int, VisibleTrackInfo> getTrackInformation(const Song & song, int scroll_row) const;
  VisibleTrackInfo getTrackInfoFor(const Song & song, int track_id) const;
  void renderHeading(const StyleProvider & styles, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info);
  void renderRow(const StyleProvider & styles, int heading_height, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info, int row, bool highlight, const SelectionBounds & sel_bounds);

  Cursor current_cursor, new_cursor;
  
  int current_score_playing_row = 0;
  int current_score_pattern = 0;  
  int current_score_total_columns = 0;
  int current_scroll_row = 0, current_scroll_track = 0, current_scroll_col = 0;
  int current_tempo = 0;
  int current_keyboard_octave = 4;
  
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
  // for the full reasoning): ensureRowCleared() is idempotent per (row,
  // track_id) so it's safe to call from every write site during an
  // active session, and onRowAdvanced() sweeps whatever rows the
  // playhead just passed through.
  void ensureRowCleared(Song & song, int pattern_idx, int row, int track_id);
  std::set<std::pair<int, int>> auto_record_cleared_rows_;
  int last_cleared_row_ = -1;
  int last_cleared_pattern_idx_ = -1;

  // Emacs-style mark/point selection: the mark is recorded here at C-SPC
  // time, the point is always "wherever the cursor/row currently is" (see
  // getController().getPlaybackInfo() and current_cursor.track), so normal
  // cursor movement extends the selection without any extra bookkeeping.
  // selection_start_note_ narrows this the same way within a single track:
  // a fresh mark starts scoped to just the note column it was set on;
  // moving sideways widens/narrows to the touched note-column range.
  bool selection_active_ = false;
  int selection_start_pattern_ = 0, selection_start_row_ = 0, selection_start_track_ = 0;
  int selection_start_note_ = 0;

  // shadow copies of the above, used only to decide when render() needs a
  // full repaint (see render()'s render_all computation).
  bool current_selection_active_ = false;
  int current_selection_start_pattern_ = 0, current_selection_start_row_ = 0, current_selection_start_track_ = 0;
  int current_selection_start_note_ = 0;

  PatternBlock clipboard_;
  bool clipboard_column_scoped_ = false;
  bool clipboard_includes_command_ = false;
};

#endif
