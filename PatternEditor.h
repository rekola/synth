#ifndef _PATTERNEDITOR_H_
#define _PATTERNEDITOR_H_

#include "UIElement.h"
#include "Tuning.h"
#include "Cursor.h"
#include "PatternBlockOps.h"

#include <vector>
#include <unordered_map>
#include <map>
#include <tuple>

class Synth;
class InputEvent;
class StyleProvider;
class Song;
class VisibleTrackInfo;
class LaunchpadIO;

class PatternEditor : public UIElement {
 public:
  PatternEditor(UIPlane & parent);

  bool render(const StyleProvider & styles, bool refresh = false);
  bool offerInput(const InputEvent & input) override;
  void handleMidiEvent(MidiEvent & ev) override;
  void handleLaunchpadPadEvent(LaunchpadPadEvent & ev) override;

  // Set once at startup (see UI::start) so render() can push LED updates
  // when the song's tuning/key changes or a device newly connects.
  void setLaunchpadIO(LaunchpadIO * io) { launchpad_io_ = io; }

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

  std::unordered_map<int, VisibleTrackInfo> getTrackInformation(const Song & song) const;
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

  // Pattern row index a Launchpad aftertouch/press write landed on when it
  // isn't the current playback row (e.g. step entry already auto-advanced
  // the cursor off of it) - render()'s incremental redraw only repaints
  // the current playback row otherwise, so an off-cursor write would
  // silently never become visible without this. -1 = none pending.
  int launchpad_extra_redraw_row_ = -1;

  std::unordered_map<int, int> active_midi_notes;

  struct ActiveLaunchpadNote {
    int note_column;
    int row, track_id; // where the note was actually written (see handleLaunchpadPadEvent)
    int last_aftertouch_value = -1; // -1 = no aftertouch written yet for this note
  };

  // Keyed by (device_index, pad_x, pad_y); unlike active_midi_notes this
  // uses a free-slot search for column assignment (see
  // handleLaunchpadPadEvent), not "map size", to avoid a latent
  // column-collision bug on non-LIFO pad release order.
  std::map<std::tuple<int, int, int>, ActiveLaunchpadNote> active_launchpad_notes_;

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

  void refreshLaunchpadLeds(Tuning tuning, int key);

  LaunchpadIO * launchpad_io_ = nullptr;
  bool current_launchpad_connected_ = false;
  Tuning current_launchpad_tuning_ = Tuning::TET12;
  int current_launchpad_key_ = -1;
};

#endif
