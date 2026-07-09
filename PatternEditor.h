#ifndef _PATTERNEDITOR_H_
#define _PATTERNEDITOR_H_

#include "UIElement.h"
#include "Tuning.h"
#include "Cursor.h"
#include "PatternBlockOps.h"

#include <vector>
#include <unordered_map>

class Synth;
class InputEvent;
class StyleProvider;
class Song;
class VisibleTrackInfo;

class PatternEditor : public UIElement {
 public:
  PatternEditor(UIPlane & parent);

  bool render(const StyleProvider & styles, bool refresh = false);
  bool offerInput(const InputEvent & input) override;
  void handleMidiEvent(MidiEvent & ev) override;

protected:
  std::unordered_map<int, VisibleTrackInfo> getTrackInformation(const Song & song) const;
  void renderHeading(const StyleProvider & styles, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info);
  void renderRow(const StyleProvider & styles, int heading_height, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info, int row, bool highlight);

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
  bool selection_active_ = false;
  int selection_start_pattern_ = 0, selection_start_row_ = 0, selection_start_track_ = 0;

  // shadow copies of the above, used only to decide when render() needs a
  // full repaint (see render()'s render_all computation).
  bool current_selection_active_ = false;
  int current_selection_start_pattern_ = 0, current_selection_start_row_ = 0, current_selection_start_track_ = 0;

  PatternBlock clipboard_;
};

#endif
