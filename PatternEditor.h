#ifndef _PATTERNEDITOR_H_
#define _PATTERNEDITOR_H_

#include "UIElement.h"
#include "Cursor.h"
#include "PatternBlockOps.h"

#include <vector>
#include <unordered_map>

class Synth;
class InputEvent;
class StyleProvider;
class Song;
class VisibleTrackInfo;
class LaunchpadManager;

class PatternEditor : public UIElement {
 public:
  PatternEditor(UIPlane & parent);

  bool render(const StyleProvider & styles, bool refresh = false);
  bool offerInput(const InputEvent & input) override;
  void handleMidiEvent(MidiEvent & ev) override;
  void handleLaunchpadPadEvent(LaunchpadPadEvent & ev) override;

  // Set once at startup (see UI::start) so render() can push LED updates
  // and handleLaunchpadPadEvent can resolve notes via the layout/per-device
  // state LaunchpadManager owns. Button *commands* are not handled here at
  // all - see UI::handleLaunchpadButtonEvent and LaunchpadManager::
  // handleCommand: PatternEditor only ever touches Launchpad concepts for
  // actual pattern editing (resolving/recording notes from pad presses).
  void setLaunchpadManager(LaunchpadManager * manager) { launchpad_manager_ = manager; }

  // Plain, source-agnostic cursor accessors - PatternEditor has no idea
  // these happen to be used to keep a Launchpad device's own track
  // selection in sync with what's on screen (see UI::
  // handleLaunchpadButtonEvent); it just exposes "the current track" the
  // same way it always has, and lets it be moved.
  int getCursorTrackIndex() const { return current_cursor.track; }
  void setCursorTrack(int track_index) { new_cursor.track = track_index; new_cursor.col = new_cursor.subcol = 0; }

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

  LaunchpadManager * launchpad_manager_ = nullptr;
};

#endif
